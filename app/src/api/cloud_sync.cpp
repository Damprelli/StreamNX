#include "api/cloud_sync.hpp"
#include "model/addon_store.hpp"
#include "model/continue_watching_store.hpp"
#include "model/favorites_store.hpp"

#include <borealis.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

// Defined next to each store (global namespace) -- declared here so the
// server gets exactly the same shapes the local JSON files use.
void to_json(nlohmann::json& j, const Favorite& f);
void from_json(const nlohmann::json& j, Favorite& f);
void to_json(nlohmann::json& j, const ContinueWatching& c);
void from_json(const nlohmann::json& j, ContinueWatching& c);
void to_json(nlohmann::json& j, const Addon& a);
void from_json(const nlohmann::json& j, Addon& a);

namespace cloud
{

namespace
{

using json = nlohmann::json;

constexpr const char* PROFILE_FILE   = "profile.json";
constexpr const char* DEFAULT_SERVER = "https://streamnxbackend.vercel.app";

// Home waits this long at most for the startup sync before showing the
// local lists -- a cold Vercel function plus the Switch's slow TLS
// handshake usually answer in 1-3s.
constexpr long STARTUP_SYNC_MAX_WAIT_MS = 6000;
// On exit, how long queued changes get to go out before being cancelled
// (they're not lost: the session stays "dirty" and the next start merges).
constexpr int SHUTDOWN_FLUSH_MAX_MS = 3000;

#ifdef __SWITCH__
constexpr const char* DEVICE = "switch";
#else
constexpr const char* DEVICE = "pc";
#endif

struct Session
{
    std::string server;
    std::string token;
    std::string profileId;
    std::string name;
    // Some local change may not have reached the server yet: the next sync
    // merges local into server instead of replacing local with server.
    bool dirty = false;
};

// Written only on the main thread; worker jobs get their own copy.
std::mutex sessionMutex;
Session session;

// Main-thread bookkeeping.
int outstandingPushes   = 0;
bool pushFailed         = false;
bool offline            = false;
bool syncing            = false;
unsigned changeCounter  = 0;
bool startupSyncPending = false;
std::vector<std::pair<http::AliveFlag, std::function<void()>>> startupWaiters;

// Every request goes through this one thread, in order -- a favorite added
// then removed a second later must reach the server in that same order,
// which the shared http pool (several workers) can't promise.
class SerialQueue
{
  public:
    void start()
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->worker.joinable())
            return;
        this->stopping = false;
        this->cancel   = false;
        this->worker   = std::thread([this]() { this->run(); });
    }

    void enqueue(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (this->stopping)
                return;
            this->jobs.push_back(std::move(job));
        }
        this->cv.notify_one();
    }

    // Lets queued jobs finish for up to `maxWaitMs`, then cancels the rest.
    void stop(int maxWaitMs)
    {
        {
            std::unique_lock<std::mutex> lock(this->mutex);
            this->idle.wait_for(lock, std::chrono::milliseconds(maxWaitMs),
                [this]() { return this->jobs.empty() && !this->busy; });
            this->stopping = true;
            this->jobs.clear();
        }
        this->cancel = true;
        this->cv.notify_all();
        if (this->worker.joinable())
            this->worker.join();
    }

    const std::atomic<bool>* cancelFlag() const { return &this->cancel; }

  private:
    void run()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(this->mutex);
                this->cv.wait(lock, [this]() { return this->stopping || !this->jobs.empty(); });
                if (this->stopping)
                    return;
                job = std::move(this->jobs.front());
                this->jobs.pop_front();
                this->busy = true;
            }
            job();
            {
                std::lock_guard<std::mutex> lock(this->mutex);
                this->busy = false;
            }
            this->idle.notify_all();
        }
    }

    std::thread worker;
    std::deque<std::function<void()>> jobs;
    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable idle;
    bool stopping = false;
    bool busy     = false;
    std::atomic<bool> cancel { false };
};

SerialQueue& queue()
{
    static SerialQueue instance;
    return instance;
}

// ---- session file ---------------------------------------------------------------

std::string defaultServer()
{
    // Development: point the PC build at `npm run dev` on the LAN.
    if (const char* env = std::getenv("STREAMNX_SERVER"))
        if (*env)
            return env;
    return DEFAULT_SERVER;
}

std::string trimSlash(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

void saveSession()
{
    json j = {
        { "server", session.server },
        { "token", session.token },
        { "profileId", session.profileId },
        { "name", session.name },
        { "dirty", session.dirty },
    };
    try
    {
        std::ofstream out(PROFILE_FILE);
        if (out.is_open())
            out << j.dump(2);
    }
    catch (const std::exception&)
    {
    }
}

void setSession(Session s)
{
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        session = std::move(s);
    }
    saveSession();
}

void setDirty(bool dirty)
{
    if (session.dirty == dirty)
        return;
    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        session.dirty = dirty;
    }
    saveSession();
}

void loadSession()
{
    Session s;
    try
    {
        std::ifstream in(PROFILE_FILE);
        if (in.is_open())
        {
            json j;
            in >> j;
            s.server    = j.value("server", "");
            s.token     = j.value("token", "");
            s.profileId = j.value("profileId", "");
            s.name      = j.value("name", "");
            s.dirty     = j.value("dirty", false);
        }
    }
    catch (const std::exception&)
    {
        s = Session {};
    }
    if (s.server.empty())
        s.server = defaultServer();
    s.server = trimSlash(s.server);

    std::lock_guard<std::mutex> lock(sessionMutex);
    session = s;
}

// ---- requests -------------------------------------------------------------------------

std::string urlEncode(const std::string& value)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

http::Response call(const Session& s, const std::string& method, const std::string& path,
    const json& body = json())
{
    return http::request(method, s.server + path, body.is_null() ? "" : body.dump(), s.token, queue().cancelFlag());
}

bool isNetworkFailure(const http::Response& r)
{
    return !r.error.empty() || r.status >= 500 || r.status == 0;
}

// The backend's own messages are written to be shown as-is (no accents,
// like the rest of the app's text).
std::string errorOf(const http::Response& r)
{
    if (!r.error.empty())
        return "Sem conexao com o servidor de perfis. Verifique a internet.";
    if (r.status >= 200 && r.status < 300)
        return "";
    try
    {
        json j = json::parse(r.body);
        if (j.contains("error") && j["error"].is_object())
            return j["error"].value("message", "Erro no servidor.");
    }
    catch (const std::exception&)
    {
    }
    return "Erro no servidor (HTTP " + std::to_string(r.status) + ").";
}

json localLists()
{
    FavoritesStore::instance().load();
    ContinueWatchingStore::instance().load();
    AddonStore::instance().load();
    return json {
        { "favorites", FavoritesStore::instance().all() },
        { "continueWatching", ContinueWatchingStore::instance().all() },
        { "addons", AddonStore::instance().all() },
    };
}

// Replaces the local lists with the server's -- parsed in full first, so a
// malformed answer can't leave the stores half-updated.
bool applyState(const std::string& body)
{
    try
    {
        json j       = json::parse(body);
        auto favs    = j.at("favorites").get<std::vector<Favorite>>();
        auto watches = j.at("continueWatching").get<std::vector<ContinueWatching>>();
        auto addons  = j.at("addons").get<std::vector<Addon>>();

        FavoritesStore::instance().replaceAll(std::move(favs));
        ContinueWatchingStore::instance().replaceAll(std::move(watches));
        AddonStore::instance().replaceAll(std::move(addons));

        if (j.contains("profile") && j["profile"].is_object())
        {
            std::string name = j["profile"].value("name", "");
            if (!name.empty() && name != session.name)
            {
                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    session.name = name;
                }
                saveSession();
            }
        }
        return true;
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("cloud: invalid sync response: {}", e.what());
        return false;
    }
}

// The server no longer accepts this token (logged out elsewhere, profile
// deleted): drop the session but keep the local lists -- they're still
// this device's data, and logging back in merges them.
void endSession(const std::string& message)
{
    std::string server = session.server;
    setSession(Session { server });
    outstandingPushes = 0;
    pushFailed        = false;
    brls::Application::notify(message);
}

void releaseStartupWaiters()
{
    if (!startupSyncPending)
        return;
    startupSyncPending = false;
    auto waiters       = std::move(startupWaiters);
    startupWaiters.clear();
    for (auto& [alive, ready] : waiters)
        if (*alive)
            ready();
}

// ---- sync -------------------------------------------------------------------------------

void startSync(std::function<void(const std::string&)> done)
{
    Session s = session;
    if (s.token.empty())
    {
        done("");
        return;
    }

    bool merge        = s.dirty;
    json local        = merge ? localLists() : json();
    unsigned startedAt = changeCounter;
    if (merge)
        local["prefer"] = "local";

    syncing = true;
    queue().enqueue([s, merge, local, startedAt, done]() {
        http::Response r = merge ? call(s, "POST", "/api/me/import", local) : call(s, "GET", "/api/me");

        brls::sync([s, merge, r, startedAt, done]() {
            syncing = false;

            if (s.token != session.token) // logged out/in meanwhile
            {
                done("");
                return;
            }
            if (r.status == 401)
            {
                endSession("Sessao do perfil encerrada. Faca login de novo.");
                done("Sessao encerrada. Faca login de novo.");
                return;
            }

            std::string error = errorOf(r);
            if (!error.empty())
            {
                offline = isNetworkFailure(r);
                done(error);
                return;
            }
            offline = false;

            // A local change made while this request was in flight went out
            // after it (same queue) -- this answer doesn't include it yet, so
            // applying it would briefly undo that change. Ask again instead.
            if (changeCounter != startedAt)
            {
                startSync(done);
                return;
            }

            if (!applyState(r.body))
            {
                done("Resposta invalida do servidor de perfis.");
                return;
            }
            if (merge)
            {
                pushFailed = false;
                if (outstandingPushes == 0)
                    setDirty(false);
            }
            done("");
        });
    });
}

// Queues one change for the server. `send` runs on the worker.
void push(std::function<http::Response(const Session&)> send)
{
    if (session.token.empty())
        return;

    Session s = session;
    ++outstandingPushes;
    ++changeCounter;
    setDirty(true); // cleared once everything queued so far has gone out

    queue().enqueue([s, send]() {
        http::Response r = send(s);
        brls::sync([s, r]() {
            if (s.token != session.token)
                return;
            outstandingPushes = std::max(0, outstandingPushes - 1);

            if (r.status == 401)
            {
                endSession("Sessao do perfil encerrada. Faca login de novo.");
                return;
            }
            // A 4xx other than 401 means the server rejected this change on
            // purpose (already there, invalid) -- resending wouldn't help.
            if (isNetworkFailure(r))
            {
                pushFailed = true;
                offline    = true;
            }
            else
            {
                offline = false;
            }

            if (outstandingPushes == 0 && !pushFailed)
                setDirty(false);
        });
    });
}

void authenticate(const std::string& path, json body, http::AliveFlag alive,
    std::function<void(const std::string&)> done)
{
    std::string server = session.server;
    json local         = localLists();
    body["device"]     = DEVICE;

    queue().enqueue([server, path, body, local, alive, done]() {
        const std::atomic<bool>* cancel = queue().cancelFlag();
        http::Response r = http::request("POST", server + path, body.dump(), "", cancel);
        std::string error = errorOf(r);

        std::string token, profileId, name;
        if (error.empty())
        {
            try
            {
                json j    = json::parse(r.body);
                token     = j.value("token", "");
                profileId = j.at("profile").value("id", "");
                name      = j.at("profile").value("name", "");
            }
            catch (const std::exception&)
            {
            }
            if (token.empty())
                error = "Resposta invalida do servidor de perfis.";
        }

        // Merge whatever this device already had into the profile.
        http::Response imported;
        if (error.empty())
            imported = http::request("POST", server + "/api/me/import", local.dump(), token, cancel);

        brls::sync([server, token, profileId, name, error, imported, alive, done]() {
            if (!error.empty())
            {
                if (*alive)
                    done(error);
                return;
            }

            outstandingPushes = 0;
            pushFailed        = false;
            offline           = false;
            setSession(Session { server, token, profileId, name, false });

            if (!errorOf(imported).empty() || !applyState(imported.body))
                setDirty(true); // logged in fine; the merge is retried on the next sync

            if (*alive)
                done("");
        });
    });
}

} // namespace

// ---- public ---------------------------------------------------------------------------

void globalInit()
{
    loadSession();
    queue().start();

    if (!isLoggedIn())
        return;

    brls::Logger::info("cloud: profile {} on {}", session.name, session.server);
    startupSyncPending = true;
    startSync([](const std::string& error) {
        if (!error.empty())
            brls::Logger::warning("cloud: startup sync failed: {}", error);
        releaseStartupWaiters();
    });
    brls::delay(STARTUP_SYNC_MAX_WAIT_MS, []() { releaseStartupWaiters(); });
}

void globalCleanup()
{
    queue().stop(SHUTDOWN_FLUSH_MAX_MS);
}

bool isLoggedIn()
{
    return !session.token.empty();
}

std::string profileName()
{
    return session.name;
}

std::string serverUrl()
{
    return session.server;
}

std::string statusText()
{
    if (!isLoggedIn())
        return "Nenhum perfil conectado. Os dados ficam so neste aparelho.";
    if (syncing)
        return "Sincronizando...";
    if (offline)
        return "Sem conexao com o servidor. As mudancas serao enviadas na proxima sincronizacao.";
    if (session.dirty)
        return "Enviando alteracoes...";
    return "Sincronizado com o servidor.";
}

void login(const std::string& name, const std::string& pin, http::AliveFlag alive,
    std::function<void(const std::string& error)> done)
{
    authenticate("/api/auth/login", json { { "name", name }, { "pin", pin } }, alive, done);
}

void createProfile(const std::string& name, const std::string& pin, http::AliveFlag alive,
    std::function<void(const std::string& error)> done)
{
    authenticate("/api/profiles", json { { "name", name }, { "pin", pin } }, alive, done);
}

void logout()
{
    if (!isLoggedIn())
        return;

    Session s = session;
    // Anything that couldn't be sent earlier goes out first, merged.
    json local = s.dirty ? localLists() : json();
    if (s.dirty)
        local["prefer"] = "local";

    queue().enqueue([s, local]() {
        if (!local.is_null())
            call(s, "POST", "/api/me/import", local);
        call(s, "POST", "/api/auth/logout");
    });

    outstandingPushes = 0;
    pushFailed        = false;
    offline           = false;
    setSession(Session { s.server });

    FavoritesStore::instance().replaceAll({});
    ContinueWatchingStore::instance().replaceAll({});
    AddonStore::instance().replaceAll({});
}

void syncNow(http::AliveFlag alive, std::function<void(const std::string& error)> done)
{
    startSync([alive, done](const std::string& error) {
        if (*alive)
            done(error);
    });
}

void whenStartupSyncDone(http::AliveFlag alive, std::function<void()> ready)
{
    if (!startupSyncPending)
    {
        ready();
        return;
    }
    startupWaiters.emplace_back(alive, ready);
}

// ---- store hooks --------------------------------------------------------------------------

void favoriteAdded(const Favorite& item)
{
    json body = item;
    push([body](const Session& s) { return call(s, "POST", "/api/me/favorites", body); });
}

void favoriteRemoved(const std::string& id)
{
    push([id](const Session& s) { return call(s, "DELETE", "/api/me/favorites/" + urlEncode(id)); });
}

void continueTouched(const ContinueWatching& item)
{
    json body = item;
    push([body](const Session& s) { return call(s, "POST", "/api/me/continue-watching", body); });
}

void continueProgress(const ContinueWatching& item)
{
    json full     = item;
    json progress = { { "positionSeconds", item.positionSeconds }, { "durationSeconds", item.durationSeconds } };
    std::string path = "/api/me/continue-watching/" + urlEncode(item.id);
    push([full, progress, path](const Session& s) {
        http::Response r = call(s, "PATCH", path, progress);
        // Not on the server yet (its "touch" never made it there): send the
        // whole entry, position included.
        if (r.error.empty() && r.status == 404)
            r = call(s, "POST", "/api/me/continue-watching", full);
        return r;
    });
}

void continueRemoved(const std::string& id)
{
    push([id](const Session& s) { return call(s, "DELETE", "/api/me/continue-watching/" + urlEncode(id)); });
}

void addonAdded(const Addon& addon)
{
    json body = addon;
    push([body](const Session& s) { return call(s, "POST", "/api/me/addons", body); });
}

void addonChanged(const Addon& addon)
{
    json body = addon;
    push([body](const Session& s) { return call(s, "PATCH", "/api/me/addons", body); });
}

void addonRemoved(const std::string& url)
{
    push([url](const Session& s) { return call(s, "DELETE", "/api/me/addons?url=" + urlEncode(url)); });
}

} // namespace cloud
