// Switch-only implementation of download_manager.hpp's public interface,
// backed by the pipensx project's own BitTorrent engine (vendored verbatim
// in library/pipensx-bt -- peer wire, piece manager, tracker announce, DHT,
// MSE, all its own code, not libtorrent/libtransmission) instead of
// libtransmission. libtorrent needs Boost, which has no Switch portlib;
// pipensx's engine is what a real, shipped Switch homebrew already uses in
// production, so it's the one actually proven on hardware.
//
// One structural difference from the libtransmission version this replaces
// (and from download_manager.cpp's libtorrent version): those both share one
// global session object and poll every active download from a single status
// thread. pipensx's torrent_t has no such shared session -- each one is
// independent and owns its own tracker/DHT/peer state once created, and
// torrent_tick() must always be called from the same thread (see torrent.h).
// So here each startTorrentDownload() spawns its own worker thread that
// resolves the magnet, creates the torrent, and drives its own tick loop
// start to finish -- still updating the same TorrentSession/.metadata-file
// shape download_manager.cpp already established, so getActiveDownloads(),
// getFinishedDownloads() and the rest of the app don't need to know which
// backend is running.
#ifdef USE_PIPENSX_BT

#include "api/download_manager.hpp"
#include "api/magnet_resolver_switch.hpp"

extern "C"
{
#include "torrent.h"
#include "metainfo.h"
#include "dht.h"
#include "switch_crashlog.h"
#include "util.h"
}

#include <borealis.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <borealis/extern/nlohmann/json.hpp>

namespace download_mgr
{

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

std::mutex g_sessionsMutex;
std::atomic<bool> g_shuttingDown { false };

struct TorrentSession
{
    std::string infoHash;
    std::string title;
    std::string posterUrl;
    std::vector<std::string> trackers;
    fs::path cacheDir;
    fs::path metadataFile;
    fs::path torrentFile; // resolved .torrent (magnet_resolver output / metainfo_load input)
    fs::path downloadingFile;

    int64_t totalBytes     = 0;
    int64_t downloadedBytes = 0;
    float percentComplete   = 0.0f;
    bool isStreaming         = false;
    bool hasRealFile         = false; // true once downloadingFile points at the torrent's actual video file

    // Read by getActiveDownloads() on the UI thread while the worker writes
    // them -- only ever touched with g_sessionsMutex held.
    std::string status;
    uint64_t speedBps = 0;
    uint32_t peers    = 0;

    AliveFlag alive;
    ProgressCallback onProgress;
    UrlReadyCallback onUrlReady;
    ErrorCallback onError;
};

std::unordered_map<std::string, std::shared_ptr<TorrentSession>> g_activeSessions;

// Handles of every download worker thread started, joined instead of
// detached: std::thread::detach() fails on the Switch (libnx), and a
// temporary std::thread whose detach() throws is destroyed still joinable,
// i.e. std::terminate -- that was the silent close right after picking a
// torrent. A worker flips `done` as the very last thing it does, so joining
// a done one returns immediately; reapFinishedWorkers() does that for each
// new download, globalCleanup() joins whatever is left. Guarded by
// g_sessionsMutex.
struct WorkerThread
{
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
};
std::vector<WorkerThread> g_workerThreads;

// Caller holds g_sessionsMutex.
void reapFinishedWorkers()
{
    for (auto it = g_workerThreads.begin(); it != g_workerThreads.end();)
    {
        if (it->done->load())
        {
            if (it->thread.joinable())
                it->thread.join();
            it = g_workerThreads.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// Downloads that died with an error (never cancelled ones) -- kept around
// so DownloadsScreen can keep showing them with the reason, since the
// download now runs detached from any screen and an error toast alone is
// easy to miss. Cleared by cancelDownload() (the screen's "Remover") or by
// starting the same infoHash again.
struct FailedDownload
{
    std::string title;
    std::string posterUrl;
    std::string error;
};
std::unordered_map<std::string, FailedDownload> g_failedDownloads;

void setStatus(const std::shared_ptr<TorrentSession>& session, const char* status)
{
    std::lock_guard<std::mutex> lock(g_sessionsMutex);
    session->status = status;
}

// mpv gets a plain absolute path rather than a "file://" URL: the
// downloaded file's path is relative to the app's working directory
// ("cache/downloads/..."), and "file://cache/..." reads as host "cache"
// instead of a path. The absolute form on the Switch is "sdmc:/switch/...",
// which mpv opens as an ordinary file (single slash, not a protocol URL).
std::string playablePath(const fs::path& file)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(file, ec);
    return (ec ? file : absolute).string();
}

// Same relative-to-cwd path image_cache.cpp's own CACHE_DIR already
// establishes -- $HOME/temp_directory_path() resolve to nothing usable in
// the Switch's libnx/newlib environment, but a path relative to the app's
// own working directory (which the homebrew loader sets to the app's own
// folder on the SD card) works unchanged on desktop too.
fs::path getCacheDir()
{
    fs::path cache = "cache/downloads";
    fs::create_directories(cache);
    return cache;
}

std::string sanitizeHash(const std::string& hash)
{
    std::string result = hash;
    for (char& c : result)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    return result;
}

void saveMetadata(const std::shared_ptr<TorrentSession>& session)
{
    try
    {
        json metadata;
        metadata["infoHash"]        = session->infoHash;
        metadata["title"]           = session->title;
        metadata["posterUrl"]       = session->posterUrl;
        metadata["totalBytes"]      = session->totalBytes;
        metadata["downloadedBytes"] = session->downloadedBytes;
        metadata["percentComplete"] = session->percentComplete;
        metadata["isStreaming"]     = session->isStreaming;
        metadata["timestamp"]       = std::chrono::system_clock::now().time_since_epoch().count();
        if (session->hasRealFile)
            metadata["videoFile"] = session->downloadingFile.string();

        std::ofstream file(session->metadataFile);
        file << metadata.dump(2);
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to save metadata: {}", e.what());
    }
}

int64_t getAvailableDiskSpace(const fs::path& path)
{
    try
    {
        return static_cast<int64_t>(fs::space(path).available);
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to check disk space: {}", e.what());
        return -1;
    }
}

// Wipes the whole per-download cache dir (video file + metadata + resolved
// .torrent) -- called when a download is cancelled or dies incomplete.
// Finished downloads (see the worker loop below) are deliberately NOT
// cleaned up this way: they're meant to survive the player closing as a
// "Downloads" library entry, until deleteFinishedDownload() removes them.
void cleanupCacheDir(const std::shared_ptr<TorrentSession>& session)
{
    try
    {
        if (fs::exists(session->cacheDir))
        {
            fs::remove_all(session->cacheDir);
            brls::Logger::info("Cleaned download cache: {}", session->title);
        }
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Error cleaning download cache: {}", e.what());
    }
}

// A magnet only carries the infoHash -- the real file names/sizes are only
// known once metainfo_load() parses the resolved .torrent. Pick the largest
// file (the video, typically) to actually fetch; everything else (sample,
// nfo, etc) is marked SKIP so the engine never requests those pieces.
std::vector<storage_file_config_t> selectLargestFile(const metainfo_t& mi, std::shared_ptr<TorrentSession>& session)
{
    std::vector<storage_file_config_t> configs(mi.num_files);
    std::memset(configs.data(), 0, configs.size() * sizeof(storage_file_config_t));

    uint32_t bestIndex = 0;
    int64_t bestSize   = -1;
    for (uint32_t i = 0; i < mi.num_files; ++i)
    {
        configs[i].mode = STORAGE_FILE_SKIP;
        if (mi.files[i].length > bestSize)
        {
            bestSize  = mi.files[i].length;
            bestIndex = i;
        }
    }
    if (mi.num_files > 0)
    {
        configs[bestIndex].mode = STORAGE_FILE_DISK;
        session->totalBytes     = mi.files[bestIndex].length;
    }
    return configs;
}

// Every download gets its own torrent_t, so (unlike a single shared
// session) each needs its own TCP listen port. Realistically 0-2 downloads
// run at once in this app; cycling through a small range is enough to never
// collide without needing real allocation/bookkeeping.
uint16_t nextListenPort()
{
    static std::atomic<int> counter { 0 };
    constexpr uint16_t kBasePort = 51413;
    return static_cast<uint16_t>(kBasePort + (counter.fetch_add(1) % 8));
}

// Runs start-to-finish on its own thread: resolve magnet -> metainfo_load ->
// torrent_create_ex -> tick loop -> cleanup. torrent_tick() must always run
// on the same thread (torrent.h), which this naturally gives it.
//
// Must never let an exception escape (see downloadWorker() below, the
// actual thread entry point) -- this is a bare std::thread, not something
// running under a message loop that could catch it, and libnx aborts the
// whole process with no dialog/log when an exception escapes a thread's
// root function.
void downloadWorkerBody(std::shared_ptr<TorrentSession> session)
{
    auto reportError = [&session](const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(g_sessionsMutex);
            g_failedDownloads[session->infoHash] = FailedDownload { session->title, session->posterUrl, message };
        }
        if (session->onError && session->alive && *session->alive)
        {
            brls::sync([session, message] {
                if (*session->alive)
                    session->onError(message);
            });
        }
    };

    // Every exit path funnels through here: the thread was detached at
    // creation (startTorrentDownload), so nothing else ever joins it --
    // this is the only place g_activeSessions loses this entry, which is
    // also what tells cancelDownload()/globalCleanup() (both of which only
    // poll the map, never a thread handle) that this download has actually
    // stopped.
    auto abortSession = [&session]() {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);
        cleanupCacheDir(session);
        g_activeSessions.erase(session->infoHash);
    };

    // ---- Step 1: resolve magnet -> .torrent on disk (skip if already
    // resolved by a previous attempt for this same infoHash) ----
    if (!fs::exists(session->torrentFile))
    {
        switch_crashlog_stage("torrent: resolving magnet");
        setStatus(session, "Buscando metadados do torrent...");
        std::atomic<bool> cancelled { false };
        // resolveToFile() blocks for as long as the resolve takes (up to its
        // own internal timeout); this watcher mirrors the caller's AliveFlag
        // into the atomic<bool> it actually polls, so leaving the player
        // still aborts promptly instead of waiting out that timeout.
        std::atomic<bool> watchStop { false };
        std::thread watcher([&] {
            while (!watchStop.load(std::memory_order_relaxed))
            {
                if (g_shuttingDown.load(std::memory_order_relaxed) || (session->alive && !(*session->alive)))
                {
                    cancelled.store(true, std::memory_order_relaxed);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });

        // resolveToFile() can throw (std::system_error when the kernel
        // refuses one of the resolver's own threads, std::bad_alloc, ...).
        // Unwinding past a still-joinable std::thread calls std::terminate,
        // i.e. a silent kill of the whole app -- this stops and joins the
        // watcher on every way out of this scope, so the exception reaches
        // downloadWorker()'s catch and becomes a reported error instead.
        struct WatcherGuard
        {
            std::atomic<bool>& stop;
            std::thread& thread;
            ~WatcherGuard()
            {
                stop.store(true, std::memory_order_relaxed);
                if (thread.joinable())
                    thread.join();
            }
        } watcherGuard { watchStop, watcher };

        std::string resolveError;
        bool resolved = magnet_resolver::resolveToFile(
            session->infoHash, session->trackers, session->torrentFile.string(), cancelled,
            [session](const magnet_resolver::Progress&) {
                // No dedicated "resolving" UI state yet -- the caller's
                // onProgress expects download percentage, which doesn't
                // exist until metainfo is known. Silence is fine here;
                // TorrentProgressScreen's own "Conectando aos peers..."
                // status label already covers this phase.
            },
            resolveError);

        watchStop.store(true, std::memory_order_relaxed);
        if (watcher.joinable())
            watcher.join();

        brls::Logger::info("download_manager: magnet resolve for {} -> {}", session->infoHash, resolved ? "ok" : resolveError);
        if (!resolved)
        {
            if (!(session->alive && !(*session->alive)))
                reportError(resolveError.empty() ? "Falha ao resolver metadados do torrent." : resolveError);
            abortSession();
            return;
        }
    }

    if (session->alive && !(*session->alive))
    {
        abortSession();
        return;
    }

    // ---- Step 2: metainfo_load + torrent_create_ex ----
    switch_crashlog_stage("torrent: metainfo_load + torrent_create_ex");
    setStatus(session, "Preparando arquivos...");
    metainfo_t metainfo {};
    if (!metainfo_load(session->torrentFile.string().c_str(), &metainfo))
    {
        reportError("Torrent resolvido nao pode ser lido.");
        abortSession();
        return;
    }

    std::vector<storage_file_config_t> fileConfigs = selectLargestFile(metainfo, session);
    brls::Logger::info("download_manager: metainfo ok, {} file(s), selected {} bytes", metainfo.num_files, session->totalBytes);

    torrent_options_t options {};
    options.files         = fileConfigs.data();
    options.telemetry_tag = session->infoHash.c_str();

    torrent_t* torrent = torrent_create_ex(&metainfo, nextListenPort(), session->cacheDir.string().c_str(), &options);
    if (!torrent)
    {
        reportError("Nao foi possivel iniciar o motor de torrent.");
        metainfo_free(&metainfo);
        abortSession();
        return;
    }
    torrent_adopt_stashed_peers(torrent);
    brls::Logger::info("download_manager: torrent engine started for {}", session->infoHash);

    // downloadingFile is only known now that the real per-file path exists
    // on disk (storage_open_ex resolved it during torrent_create_ex).
    char pathBuf[1024] = {};
    for (uint32_t i = 0; i < metainfo.num_files; ++i)
        if (fileConfigs[i].mode == STORAGE_FILE_DISK &&
            storage_expected_file_path(&metainfo, session->cacheDir.string().c_str(), i, pathBuf, sizeof(pathBuf)))
        {
            session->downloadingFile = pathBuf;
            session->hasRealFile     = true;
            break;
        }

    // ---- Step 3: tick loop ----
    switch_crashlog_stage("torrent: tick loop");
    setStatus(session, "Conectando aos peers...");
    bool completed          = false;
    uint64_t nextStatusMs   = 0;
    uint64_t nextMetadataMs = now_ms() + 2000;
    // No sleep in this loop, on purpose -- same as pipensx's own download
    // loop. torrent_tick() already blocks up to 10ms in poll() on the peer
    // sockets and is built to be called back-to-back: startup verification
    // only gets a 50ms slice per tick, and uTP (what still gets through on
    // networks that block TCP BitTorrent) relies on utp_check_timeouts()
    // running every few ms. The old 200ms sleep after every tick left the
    // engine idle ~80% of the time: "Verificando pecas..." for ages, uTP
    // connections timing out, peer traffic serviced 5x a second.
    // Only the bookkeeping below (status for the UI, progress callback,
    // .metadata file) is throttled, by wall clock.
    while (session->alive && *session->alive && !g_shuttingDown.load(std::memory_order_relaxed))
    {
        int running = torrent_tick(torrent);

        const char* lastError = torrent_last_error(torrent);
        if (lastError && lastError[0])
        {
            reportError(lastError);
            break;
        }

        if (running == 0)
        {
            completed = true;
            break;
        }

        uint64_t now = now_ms();
        if (now < nextStatusMs)
            continue;
        nextStatusMs = now + 500;

        torrent_stat_t stat {};
        torrent_stat(torrent, &stat);

        {
            std::lock_guard<std::mutex> lock(g_sessionsMutex);
            // completed_bytes/total_bytes include the SKIP'd files (pre-marked
            // done at startup) -- subtract skipped_bytes so progress reflects
            // just the one file actually being downloaded.
            session->downloadedBytes = static_cast<int64_t>(stat.completed_bytes > stat.skipped_bytes ? stat.completed_bytes - stat.skipped_bytes : 0);
            int64_t effectiveTotal   = static_cast<int64_t>(stat.total_bytes > stat.skipped_bytes ? stat.total_bytes - stat.skipped_bytes : session->totalBytes);
            if (effectiveTotal > 0)
                session->totalBytes = effectiveTotal;
            session->percentComplete = session->totalBytes > 0 ? (100.0f * session->downloadedBytes / session->totalBytes) : 0.0f;
            session->speedBps        = stat.speed_bps;
            session->peers           = stat.num_active_peers;
            if (stat.verifying)
            {
                char text[64];
                std::snprintf(text, sizeof(text), "Verificando pecas... %u%%",
                    stat.num_pieces ? (unsigned)(100ull * stat.num_pieces_verified / stat.num_pieces) : 0u);
                session->status = text;
            }
            else if (stat.num_active_peers == 0)
                session->status = "Conectando aos peers...";
            else
                session->status = "Baixando";
        }

        if (session->onProgress)
        {
            float percent          = session->percentComplete;
            int64_t downloadedCopy = session->downloadedBytes;
            int64_t totalCopy      = session->totalBytes;
            brls::sync([session, percent, downloadedCopy, totalCopy] {
                if (*session->alive)
                    session->onProgress(percent, downloadedCopy, totalCopy);
            });
        }

        // Every ~2s: the .metadata file only exists so progress survives an
        // app restart, and rewriting it more often is pure SD card wear.
        if (now >= nextMetadataMs)
        {
            nextMetadataMs = now + 2000;
            saveMetadata(session);
        }
    }

    if (completed && session->hasRealFile)
    {
        session->percentComplete = 100.0f;
        saveMetadata(session);

        if (session->onUrlReady)
        {
            std::string localUrl = playablePath(session->downloadingFile);
            brls::sync([session, localUrl] {
                if (*session->alive)
                    session->onUrlReady(localUrl);
            });
        }
        brls::Logger::info("Download completed: {}", session->title);
    }

    torrent_destroy(torrent);
    metainfo_free(&metainfo);

    // A finished download's files stay on disk (see the class comment on
    // cleanupCacheDir) -- only an incomplete one (cancelled, or the tick
    // loop exited because the player closed early) is wiped.
    std::lock_guard<std::mutex> lock(g_sessionsMutex);
    if (!completed)
        cleanupCacheDir(session);
    g_activeSessions.erase(session->infoHash);
}

// The actual thread entry point (see startTorrentDownload() and
// g_workerThreads). A magnet resolve or
// torrent_create_ex under real hardware memory/handle pressure can throw
// (std::bad_alloc from a peer's metadata buffer, std::system_error if the
// kernel refuses one of the ~13 threads the resolve phase opens, a
// std::filesystem_error from the SD card, ...) -- pipensx's own
// DownloadManager wraps its equivalent task thread the exact same way, with
// the same reasoning: "an exception that escapes here is an instant
// std::terminate and takes the whole app with it". Turning that into a
// reported error instead of a silent full-app crash is the whole point of
// this wrapper.
void downloadWorkerGuarded(std::shared_ptr<TorrentSession> session)
{
    try
    {
        downloadWorkerBody(session);
        return;
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("download_manager: worker for {} threw: {}", session->title, e.what());
        if (session->onError && session->alive && *session->alive)
        {
            std::string message = std::string("Erro interno no download: ") + e.what();
            brls::sync([session, message] {
                if (*session->alive)
                    session->onError(message);
            });
        }
    }
    catch (...)
    {
        brls::Logger::error("download_manager: worker for {} threw a non-std exception", session->title);
        if (session->onError && session->alive && *session->alive)
        {
            brls::sync([session] {
                if (*session->alive)
                    session->onError("Erro interno inesperado no download.");
            });
        }
    }

    // Only reached on an exception -- downloadWorkerBody() already does
    // this same cleanup+erase on every one of its own normal exit paths.
    std::lock_guard<std::mutex> lock(g_sessionsMutex);
    cleanupCacheDir(session);
    g_activeSessions.erase(session->infoHash);
}

void downloadWorker(std::shared_ptr<TorrentSession> session, std::shared_ptr<std::atomic<bool>> done)
{
    downloadWorkerGuarded(std::move(session));
    // Last statement, after every lock above is released -- tells
    // reapFinishedWorkers() this thread is about to return.
    done->store(true);
}

} // namespace

void globalInit()
{
    fs::path baseCacheDir = getCacheDir();
    // Warm-start the DHT routing table across app runs -- without this every
    // resolve/download starts DHT from a cold bootstrap.
    static std::string dhtCachePath = (baseCacheDir / ".dht_cache").string();
    dht_engine_set_cache_path(dhtCachePath.c_str());
    brls::Logger::info("download_manager: pipensx_bt initialized");
}

void globalCleanup()
{
    // Every worker polls g_shuttingDown/its own AliveFlag on a ~200ms
    // cadence -- flip both, give them a moment to notice and erase
    // themselves from g_activeSessions, then join every worker thread
    // before the process tears down curl/borealis/etc out from under it.
    g_shuttingDown.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);
        for (auto& [hash, session] : g_activeSessions)
            if (session->alive)
                *session->alive = false;
    }

    constexpr int kMaxWaitMs = 5000;
    for (int waited = 0; waited < kMaxWaitMs; waited += 100)
    {
        {
            std::lock_guard<std::mutex> lock(g_sessionsMutex);
            if (g_activeSessions.empty())
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::vector<WorkerThread> workers;
    {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);
        if (!g_activeSessions.empty())
            brls::Logger::warning("download_manager: {} worker(s) still running at shutdown, joining", g_activeSessions.size());
        workers.swap(g_workerThreads);
    }
    // Outside the lock: a worker still finishing needs g_sessionsMutex to
    // erase itself. Exiting the process with these still running is what
    // makes the homebrew loader fail with 2001-0106.
    for (auto& worker : workers)
        if (worker.thread.joinable())
            worker.thread.join();

    std::lock_guard<std::mutex> lock(g_sessionsMutex);
    g_activeSessions.clear();

    brls::Logger::info("download_manager: pipensx_bt cleaned up");
}

void startTorrentDownload(const std::string& infoHash, const std::string& title, AliveFlag alive, ProgressCallback onProgress,
    UrlReadyCallback onUrlReady, ErrorCallback onError, const std::vector<std::string>& trackers, const std::string& posterUrl)
{
    switch_crashlog_stage("torrent: startTorrentDownload");
    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    if (g_activeSessions.find(infoHash) != g_activeSessions.end())
    {
        if (onError)
            brls::sync([onError] { onError("Download ja em andamento"); });
        return;
    }

    if (infoHash.empty() || infoHash.length() < 32)
    {
        if (onError)
            brls::sync([onError] { onError("Formato de infoHash invalido"); });
        return;
    }

    // This whole function runs synchronously on the CALLER's thread (e.g.
    // TorrentProgressScreen's constructor calls it directly from the UI
    // thread, right when the user taps a stream) -- unlike downloadWorker(),
    // nothing here was ever wrapped against an exception escaping, so a
    // failure this early (SD card I/O in fs::create_directories, or the
    // kernel refusing to create the worker thread under resource pressure)
    // used to crash the whole app at the exact moment of picking a stream,
    // instead of just failing that one download.
    try
    {
        auto session          = std::make_shared<TorrentSession>();
        session->infoHash     = infoHash;
        session->title        = title;
        session->posterUrl    = posterUrl;
        session->trackers     = trackers;
        session->alive        = alive;
        session->onProgress   = onProgress;
        session->onUrlReady   = onUrlReady;
        session->onError      = onError;

        fs::path baseCacheDir  = getCacheDir();
        std::string safeName   = sanitizeHash(infoHash);
        session->cacheDir      = baseCacheDir / safeName;
        session->metadataFile  = session->cacheDir / ".metadata";
        session->torrentFile   = session->cacheDir / "meta.torrent";
        fs::create_directories(session->cacheDir);

        g_failedDownloads.erase(infoHash);
        g_activeSessions[infoHash] = session;
        // Kept, never detached -- see g_workerThreads.
        reapFinishedWorkers();
        // Reserved before the thread exists, so push_back can't throw
        // (reallocation) with a running, joinable thread in hand.
        g_workerThreads.reserve(g_workerThreads.size() + 1);
        auto done = std::make_shared<std::atomic<bool>>(false);
        g_workerThreads.push_back(WorkerThread { std::thread(downloadWorker, session, done), done });

        brls::Logger::info("Torrent download started: {} ({})", title, infoHash);
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("download_manager: failed to start {}: {}", infoHash, e.what());
        g_activeSessions.erase(infoHash);
        if (onError)
        {
            std::string message = std::string("Nao foi possivel iniciar o download: ") + e.what();
            brls::sync([onError, message] { onError(message); });
        }
    }
    catch (...)
    {
        brls::Logger::error("download_manager: failed to start {}: non-std exception", infoHash);
        g_activeSessions.erase(infoHash);
        if (onError)
            brls::sync([onError] { onError("Nao foi possivel iniciar o download."); });
    }
}

void startHttpDownload(const std::string& url, const std::string& title, AliveFlag alive, ProgressCallback onProgress,
    UrlReadyCallback onUrlReady, ErrorCallback onError)
{
    if (!alive)
        return;

    try
    {
        fs::path baseCacheDir = getCacheDir();
        int64_t available     = getAvailableDiskSpace(baseCacheDir);
        if (available >= 0 && available < 50 * 1024 * 1024)
        {
            if (onError)
                brls::sync([onError] { onError("Insufficient disk space"); });
            return;
        }

        brls::Logger::info("HTTP fallback: streaming directly from {}", url);

        if (onUrlReady && *alive)
            brls::sync([onUrlReady, url] { onUrlReady(url); });
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("HTTP download error: {}", e.what());
        if (onError)
        {
            brls::sync([onError, e] { onError(std::string("HTTP error: ") + e.what()); });
        }
    }
}

std::vector<DownloadInfo> getActiveDownloads()
{
    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    std::vector<DownloadInfo> result;
    for (auto& [hash, session] : g_activeSessions)
    {
        DownloadInfo info;
        info.infoHash         = session->infoHash;
        info.title             = session->title;
        info.percentComplete   = session->percentComplete;
        info.downloadedBytes   = session->downloadedBytes;
        info.totalBytes         = session->totalBytes;
        info.isStreaming        = session->isStreaming;
        info.posterUrl          = session->posterUrl;
        info.status             = session->status;
        info.speedBps           = session->speedBps;
        info.peers              = session->peers;
        result.push_back(info);
    }
    for (auto& [hash, failed] : g_failedDownloads)
    {
        if (g_activeSessions.count(hash))
            continue;
        DownloadInfo info;
        info.infoHash  = hash;
        info.title     = failed.title;
        info.posterUrl = failed.posterUrl;
        info.failed    = true;
        info.error     = failed.error;
        result.push_back(info);
    }
    return result;
}

bool getStorageSpace(int64_t& freeBytes, int64_t& totalBytes)
{
    std::error_code ec;
    fs::space_info info = fs::space(getCacheDir(), ec);
    if (ec)
        return false;
    freeBytes  = static_cast<int64_t>(info.available);
    totalBytes = static_cast<int64_t>(info.capacity);
    return totalBytes > 0;
}

void cancelDownload(const std::string& infoHash)
{
    std::shared_ptr<TorrentSession> session;
    {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);
        g_failedDownloads.erase(infoHash);
        auto it = g_activeSessions.find(infoHash);
        if (it == g_activeSessions.end())
            return;
        session = it->second;
    }

    // Flip the AliveFlag the worker thread is already polling -- it notices
    // within one tick/resolve-watcher iteration (~200ms), cleans up its own
    // cache dir, and erases itself from g_activeSessions. Same "cleanup
    // happens a moment later in the background" contract
    // TorrentProgressScreen's own cancel button already documents; nothing
    // here blocks waiting for that.
    if (session->alive)
        *session->alive = false;

    brls::Logger::info("Download canceled: {}", infoHash);
}

std::vector<FinishedDownload> getFinishedDownloads()
{
    std::vector<FinishedDownload> result;

    try
    {
        fs::path baseCacheDir = getCacheDir();
        if (!fs::exists(baseCacheDir))
            return result;

        for (const auto& entry : fs::directory_iterator(baseCacheDir))
        {
            if (!entry.is_directory())
                continue;

            fs::path metaFile = entry.path() / ".metadata";
            if (!fs::exists(metaFile))
                continue;

            try
            {
                std::ifstream f(metaFile);
                auto meta = json::parse(f);

                float percent          = meta.value("percentComplete", 0.0f);
                std::string videoFile = meta.value("videoFile", "");
                if (percent < 100.0f || videoFile.empty() || !fs::exists(videoFile))
                    continue;

                FinishedDownload d;
                d.infoHash     = meta.value("infoHash", "");
                d.title        = meta.value("title", "");
                d.posterUrl    = meta.value("posterUrl", "");
                d.totalBytes   = meta.value("totalBytes", (int64_t)0);
                d.localFileUrl = playablePath(videoFile);
                d.fileName     = fs::path(videoFile).filename().string();
                result.push_back(d);
            }
            catch (const std::exception& e)
            {
                brls::Logger::error("Failed to read finished download metadata: {}", e.what());
            }
        }
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("getFinishedDownloads failed: {}", e.what());
    }

    return result;
}

void deleteFinishedDownload(const std::string& infoHash)
{
    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    fs::path dir = getCacheDir() / sanitizeHash(infoHash);

    try
    {
        if (fs::exists(dir))
        {
            fs::remove_all(dir);
            brls::Logger::info("Deleted finished download: {}", infoHash);
        }
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to delete finished download: {}", e.what());
    }
}

} // namespace download_mgr

#endif // USE_PIPENSX_BT
