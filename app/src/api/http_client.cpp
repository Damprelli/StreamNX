#include "api/http_client.hpp"

#include <borealis.hpp>
#include <borealis/core/assets.hpp>
#include <curl/curl.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// A screen like SearchScreen can fire off a dozen-plus concurrent catalog
// fetches at once (one per enabled addon/catalog). Spawning a raw
// std::thread per request (the old approach) worked fine on desktop but
// crashed the app outright on Switch -- libnx caps how many threads/how
// much stack an app gets, and unbounded thread creation blew past it. A
// small fixed pool of worker threads keeps concurrency bounded on every
// platform instead of scaling with however many requests happen to be in
// flight, mirroring StreamNX-main's own ThreadPool sizing for this reason.
class WorkerPool
{
  public:
    void start(size_t count)
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (!this->workers.empty())
            return;
        this->stopping = false;
        for (size_t i = 0; i < count; i++)
            this->workers.emplace_back([this]() { this->run(); });
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->stopping = true;
        }
        this->cv.notify_all();
        for (auto& t : this->workers)
            if (t.joinable())
                t.join();
        this->workers.clear();
    }

    void enqueue(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->jobs.push_back(std::move(job));
        }
        this->cv.notify_one();
    }

  private:
    void run()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(this->mutex);
                this->cv.wait(lock, [this]() { return this->stopping || !this->jobs.empty(); });
                if (this->stopping && this->jobs.empty())
                    return;
                job = std::move(this->jobs.front());
                this->jobs.pop_front();
            }
            job();
        }
    }

    std::vector<std::thread> workers;
    std::deque<std::function<void()>> jobs;
    std::mutex mutex;
    std::condition_variable cv;
    bool stopping = false;
};

WorkerPool& pool()
{
    static WorkerPool instance;
    return instance;
}

} // namespace

namespace http
{

void globalInit()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
#if defined(__SWITCH__)
    // Keep this well under libnx's thread/stack budget -- it's shared with
    // the whole app (UI, mpv, etc), not just networking.
    pool().start(4);
#else
    pool().start(8);
#endif
}

void globalCleanup()
{
    pool().stop();
    curl_global_cleanup();
}

void getAsync(const std::string& url, AliveFlag alive, std::function<void(const std::string&)> onSuccess,
    std::function<void(const std::string&)> onError)
{
    pool().enqueue([url, alive, onSuccess, onError]() {
        std::string body;
        std::string errorMessage;

        CURL* curl = curl_easy_init();
        if (curl)
        {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "StreamNX/1.0");
            // This curl build only trusts a CA bundle it's pointed at (no
            // OS certificate store fallback) -- without this, HTTPS fails
            // outside a dev shell that happens to have the toolchain's own
            // cert path configured, i.e. everywhere a real user runs this.
            // A plain "resources/..." path only resolves on desktop (where
            // resources/ is copied next to the exe and CWD points there);
            // on Switch the same file is embedded in romfs and only
            // reachable as "romfs:/..." -- BRLS_RESOURCES is borealis's own
            // per-platform prefix for exactly this (see assets.hpp), so
            // every addon's HTTPS request was failing cert loading and
            // showing "Indisponivel" until this used it too.
            static const std::string caBundlePath = std::string(BRLS_RESOURCES) + "ca-bundle.crt";
            curl_easy_setopt(curl, CURLOPT_CAINFO, caBundlePath.c_str());

            CURLcode res = curl_easy_perform(curl);

            if (res != CURLE_OK)
            {
                errorMessage = curl_easy_strerror(res);
            }
            else
            {
                long httpCode = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
                if (httpCode >= 400)
                    errorMessage = "HTTP " + std::to_string(httpCode);
            }

            curl_easy_cleanup(curl);
        }
        else
        {
            errorMessage = "Nao foi possivel iniciar a requisicao";
        }

        brls::sync([alive, body, errorMessage, onSuccess, onError]() {
            if (!*alive)
                return;
            if (!errorMessage.empty())
                onError(errorMessage);
            else
                onSuccess(body);
        });
    });
}

} // namespace http
