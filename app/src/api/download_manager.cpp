// This file is the libtorrent-backed implementation (desktop only -- see
// CMakeLists.txt). The Switch build instead links download_manager_switch.cpp,
// a pipensx_bt-backed implementation of the exact same public interface
// (download_manager.hpp) -- both files define the same functions, so
// guarding this one out under USE_PIPENSX_BT keeps the two from ever being
// compiled into the same binary and colliding at link time.
#ifndef USE_PIPENSX_BT

#include "api/download_manager.hpp"
#include "api/http_client.hpp"

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
#include <borealis/extern/nlohmann/json.hpp>

#ifdef USE_LIBTORRENT
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/alert.hpp>

namespace lt = libtorrent;
#endif

namespace download_mgr
{

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

std::mutex g_sessionsMutex;

#ifdef USE_LIBTORRENT
// lt::sha1_hash::to_string() returns the raw 20 binary bytes of the hash,
// not a hex string -- that's neither valid UTF-8 (breaks fmt/log calls) nor
// the same format the infoHash strings from stream addons use as keys into
// g_activeSessions, so alert handling (piece_finished/torrent_finished/error)
// could never find the session it was looking for.
std::string toHex(const lt::sha1_hash& hash)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (unsigned char byte : hash)
    {
        out.push_back(digits[(byte >> 4) & 0xF]);
        out.push_back(digits[byte & 0xF]);
    }
    return out;
}
#endif

#ifdef USE_LIBTORRENT
// libtorrent session (global, persistent for app lifetime)
std::unique_ptr<lt::session> g_ltSession;
#endif

// Metadados de um download em progresso
struct TorrentSession
{
    std::string infoHash;
    std::string title;
    std::string posterUrl;
    fs::path cacheDir;
    fs::path metadataFile;
    fs::path downloadingFile;

    int64_t totalBytes = 0;
    int64_t downloadedBytes = 0;
    float percentComplete = 0.0f;
    bool isStreaming = false;
    bool hasRealFile = false; // true once downloadingFile points at the torrent's actual video file

    // Live numbers for the Downloads screen, refreshed by the alert thread.
    uint64_t speedBps = 0;
    uint32_t peers    = 0;
    std::chrono::steady_clock::time_point lastSave;

    AliveFlag alive;
    ProgressCallback onProgress;
    UrlReadyCallback onUrlReady;
    ErrorCallback onError;

#ifdef USE_LIBTORRENT
    lt::torrent_handle handle;
#endif
};

std::unordered_map<std::string, std::shared_ptr<TorrentSession>> g_activeSessions;
std::thread g_alertThread;
std::atomic<bool> g_alertThreadRunning { false };

// Base directory para cache de downloads
fs::path getCacheDir()
{
    fs::path cache;

#ifdef _WIN32
    // Windows: AppData\Local\StreamNX\cache\downloads
    const char* appdata = std::getenv("LOCALAPPDATA");
    if (appdata)
    {
        cache = fs::path(appdata) / "StreamNX" / "cache" / "downloads";
    }
    else
    {
        cache = fs::temp_directory_path() / "StreamNX" / "cache" / "downloads";
    }
#else
    // Linux/Switch: ~/.cache/streamnx/downloads
    const char* home = std::getenv("HOME");
    if (home)
    {
        cache = fs::path(home) / ".cache" / "streamnx" / "downloads";
    }
    else
    {
        cache = fs::temp_directory_path() / "streamnx" / "downloads";
    }
#endif

    fs::create_directories(cache);
    return cache;
}

// Gera hash seguro para usar como nome de diretório
std::string sanitizeHash(const std::string& hash)
{
    std::string result = hash;
    // Substituir caracteres não-alfanuméricos por underscore
    for (char& c : result)
    {
        if (!std::isalnum(c))
            c = '_';
    }
    return result;
}

// Salva metadados do download em JSON
void saveMetadata(const std::shared_ptr<TorrentSession>& session)
{
    try
    {
        json metadata;
        metadata["infoHash"] = session->infoHash;
        metadata["title"] = session->title;
        metadata["posterUrl"] = session->posterUrl;
        metadata["totalBytes"] = session->totalBytes;
        metadata["downloadedBytes"] = session->downloadedBytes;
        metadata["percentComplete"] = session->percentComplete;
        metadata["isStreaming"] = session->isStreaming;
        metadata["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        // Only meaningful once hasRealFile is true (metadata_received) --
        // getFinishedDownloads() reads this back to know what to actually
        // play, without needing a live libtorrent handle (which only exists
        // while the download is active in g_activeSessions).
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

// Carrega metadados existentes (pra resumir downloads)
std::shared_ptr<TorrentSession> loadMetadata(const fs::path& metadataFile, const fs::path& cacheDir)
{
    try
    {
        std::ifstream file(metadataFile);
        if (!file)
            return nullptr;

        auto metadata = json::parse(file);
        auto session = std::make_shared<TorrentSession>();

        session->infoHash = metadata.value("infoHash", "");
        session->title = metadata.value("title", "");
        session->posterUrl = metadata.value("posterUrl", "");
        session->totalBytes = metadata.value("totalBytes", 0);
        session->downloadedBytes = metadata.value("downloadedBytes", 0);
        session->percentComplete = metadata.value("percentComplete", 0.0f);
        session->isStreaming = metadata.value("isStreaming", false);
        session->cacheDir = cacheDir;
        session->metadataFile = metadataFile;
        session->downloadingFile = cacheDir / ".downloading";

        brls::Logger::info("Loaded metadata: {} ({:.1f}%)", session->title, session->percentComplete);
        return session;
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to load metadata: {}", e.what());
        return nullptr;
    }
}

// Verifica espaço em disco disponível
int64_t getAvailableDiskSpace(const fs::path& path)
{
    try
    {
        fs::space_info si = fs::space(path);
        return si.available;
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to check disk space: {}", e.what());
        return -1;
    }
}

// Limpa downloads antigos/incompletos se espaço insuficiente
void cleanupOldDownloads(const fs::path& baseCacheDir, int64_t requiredSpace = 100 * 1024 * 1024)
{
    try
    {
        int64_t available = getAvailableDiskSpace(baseCacheDir);
        if (available < 0 || available > requiredSpace)
            return;  // Espaço suficiente

        brls::Logger::info("Disk space low, cleaning old downloads");

        std::vector<std::pair<fs::path, std::time_t>> downloads;

        // Listar todos os diretórios de download
        for (const auto& entry : fs::directory_iterator(baseCacheDir))
        {
            if (entry.is_directory())
            {
                fs::path metaFile = entry.path() / ".metadata";
                if (fs::exists(metaFile))
                {
                    auto lastWrite = fs::last_write_time(metaFile);
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        lastWrite - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                    auto time = std::chrono::system_clock::to_time_t(sctp);
                    downloads.push_back({entry.path(), time});
                }
            }
        }

        // Ordenar por timestamp (mais antigos primeiro)
        std::sort(downloads.begin(), downloads.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        // Remover downloads incompletos mais antigos
        for (const auto& [dir, timestamp] : downloads)
        {
            // Só remove se o download não foi completado (% < 100)
            fs::path metaFile = dir / ".metadata";
            try
            {
                std::ifstream f(metaFile);
                auto meta = json::parse(f);
                float percent = meta.value("percentComplete", 0.0f);

                if (percent < 100.0f)
                {
                    brls::Logger::info("Removing incomplete download: {}", dir.filename().string());
                    fs::remove_all(dir);

                    available = getAvailableDiskSpace(baseCacheDir);
                    if (available > requiredSpace)
                        break;
                }
            }
            catch (const std::exception& e)
            {
                brls::Logger::error("Error cleaning download: {}", e.what());
            }
        }
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Cleanup failed: {}", e.what());
    }
}

// Sempre apaga o cache inteiro do download (arquivo + metadata) assim que o
// usuário sai do player -- concluído ou não. Como playback só começa com o
// torrent 100% completo (ver torrent_finished_alert), reassistir sempre
// implica rebaixar do zero, mas evita que cada filme assistido fique
// ocupando alguns GB pra sempre sem nenhuma limpeza automática.
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

#ifdef USE_LIBTORRENT
// Um magnet link só carrega o infoHash -- libtorrent só descobre os nomes/
// tamanhos reais dos arquivos do torrent depois que o metadata chega via DHT/
// peers (metadata_received_alert). Até lá não existe nenhum arquivo real no
// disco para tocar. Assim que o metadata chega, escolhe o maior arquivo do
// torrent (o vídeo, tipicamente) como o que será de fato reproduzido, e
// despriorizada os demais (samples, nfo, etc.) para não desperdiçar banda.
void resolveRealFile(const std::shared_ptr<TorrentSession>& session)
{
    auto tf = session->handle.torrent_file();
    if (!tf)
        return;

    const lt::file_storage& files = tf->files();
    int bestIndex = -1;
    int64_t bestSize = -1;
    for (int i = 0; i < files.num_files(); ++i)
    {
        int64_t size = files.file_size(lt::file_index_t(i));
        if (size > bestSize)
        {
            bestSize = size;
            bestIndex = i;
        }
    }

    if (bestIndex < 0)
        return;

    std::vector<lt::download_priority_t> priorities(files.num_files(), lt::dont_download);
    priorities[bestIndex] = lt::top_priority;
    session->handle.prioritize_files(priorities);

    // Playback only starts once the whole file is done (torrent_finished_alert,
    // see below) -- sequential order doesn't change *whether* it plays, but
    // downloading front-to-back still finishes with a fully playable prefix
    // sooner than rarest-first would, in case this gets revisited later.
    session->handle.set_flags(lt::torrent_flags::sequential_download);

    // Multi-file torrents nest the real file under a subfolder named after
    // the release itself (files.file_path() includes it) -- stacked on top
    // of this app's own cache path, the combined length routinely blows past
    // Windows' 260-char MAX_PATH, and CreateFileW then fails with "invalid
    // argument" instead of anything that looks like a path-length error.
    // Renaming the file (safe here: this runs on metadata_received, before
    // any actual piece has been written anywhere) to a short, fixed name
    // right at the torrent's root sidesteps the subfolder entirely.
    std::string ext = fs::path(files.file_path(lt::file_index_t(bestIndex))).extension().string();
    std::string shortName = "video" + ext;
    session->handle.rename_file(lt::file_index_t(bestIndex), shortName);

    session->downloadingFile = session->cacheDir / shortName;
    session->hasRealFile = true;

    brls::Logger::info("Torrent real file resolved: {}", session->downloadingFile.string());
}

// Thread que processa alerts do libtorrent
void alertLoopThread()
{
    auto nextStatsRefresh = std::chrono::steady_clock::now();

    while (g_alertThreadRunning)
    {
        // The lock covers only this block -- it used to span the whole
        // iteration, sleep included, so the mutex was held ~100% of the time
        // and was only free for an instant between iterations. std::mutex
        // isn't fair: the UI thread (DownloadsScreen polling
        // getActiveDownloads() every second) could wait on it indefinitely,
        // freezing the whole window while a torrent was downloading.
        {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);

        if (!g_ltSession)
            break;

        // Processar todos os alerts pendentes
        std::vector<lt::alert*> alerts;
        g_ltSession->pop_alerts(&alerts);

        for (auto& alert : alerts)
        {
            if (auto* ta = lt::alert_cast<lt::torrent_added_alert>(alert))
            {
                brls::Logger::info("Torrent added: {}", toHex(ta->handle.info_hash()));
            }
            else if (auto* ma = lt::alert_cast<lt::metadata_received_alert>(alert))
            {
                auto hash = toHex(ma->handle.info_hash());
                if (auto it = g_activeSessions.find(hash); it != g_activeSessions.end())
                    resolveRealFile(it->second);
            }
            else if (auto* pa = lt::alert_cast<lt::piece_finished_alert>(alert))
            {
                auto hash = toHex(pa->handle.info_hash());
                if (auto it = g_activeSessions.find(hash); it != g_activeSessions.end())
                {
                    auto session = it->second;

                    // AliveFlag já false (usuário saiu) -- a varredura no fim
                    // deste loop cuida do cleanup completo, incluindo o caso
                    // (comum) de o torrent já ter terminado antes disso, quando
                    // não há mais piece_finished_alert nenhum pra reagir a isso.
                    if (!(*session->alive))
                        continue;

                    lt::torrent_status status = pa->handle.status();
                    session->downloadedBytes = status.total_wanted_done;
                    session->totalBytes      = status.total_wanted;
                    session->percentComplete =
                        session->totalBytes > 0 ?
                        (100.0f * session->downloadedBytes / session->totalBytes) : 0.0f;

                    // Notificar progresso
                    if (session->onProgress && *session->alive)
                    {
                        brls::sync([=] {
                            if (*session->alive)
                                session->onProgress(session->percentComplete,
                                    session->downloadedBytes, session->totalBytes);
                        });
                    }

                    // Every ~2s, not every piece: the file only exists so
                    // progress survives a restart.
                    auto now = std::chrono::steady_clock::now();
                    if (now - session->lastSave >= std::chrono::seconds(2))
                    {
                        session->lastSave = now;
                        saveMetadata(session);
                    }
                }
            }
            else if (auto* dca = lt::alert_cast<lt::torrent_finished_alert>(alert))
            {
                auto hash = toHex(dca->handle.info_hash());
                if (auto it = g_activeSessions.find(hash); it != g_activeSessions.end())
                {
                    auto session = it->second;
                    session->percentComplete = 100.0f;

                    brls::Logger::info("Download completed: {}", session->title);
                    saveMetadata(session);

                    // Streaming during download hit constant decode errors --
                    // sequential_download is only a preference, and mpv reading
                    // ahead of pieces that were still mid-flight produced
                    // corrupt-cluster resyncs and A/V desync throughout
                    // playback. Waiting for torrent_finished_alert (every
                    // wanted piece done, not just the edges checked before)
                    // trades "watch while downloading" for a file that's
                    // actually complete when playback starts.
                    if (!session->isStreaming && session->hasRealFile && *session->alive)
                    {
                        session->isStreaming = true;
                        std::string localUrl = "file:///" + session->downloadingFile.string();

                        if (session->onUrlReady)
                        {
                            brls::sync([=] {
                                if (*session->alive)
                                    session->onUrlReady(localUrl);
                            });
                        }
                    }
                }
            }
            else if (auto* ea = lt::alert_cast<lt::torrent_error_alert>(alert))
            {
                auto hash = toHex(ea->handle.info_hash());
                if (auto it = g_activeSessions.find(hash); it != g_activeSessions.end())
                {
                    auto session = it->second;
                    std::string error = ea->error.message();

                    brls::Logger::error("Torrent error: {}", error);

                    if (session->onError && *session->alive)
                    {
                        brls::sync([=] {
                            if (*session->alive)
                                session->onError(error);
                        });
                    }
                }
            }
        }

        // Sweep every active session for one whose AliveFlag went false --
        // catches both a download cancelled mid-flight and one that already
        // finished (no more alerts fire for it, so the handlers above never
        // see it again once playback ends).
        for (auto it = g_activeSessions.begin(); it != g_activeSessions.end(); )
        {
            auto& session = it->second;
            if (session->alive && !(*session->alive))
            {
                if (g_ltSession && session->handle.is_valid())
                {
                    try
                    {
                        g_ltSession->remove_torrent(session->handle);
                    }
                    catch (const std::exception& e)
                    {
                        brls::Logger::error("Error removing torrent: {}", e.what());
                    }
                }

                if (session->percentComplete >= 100.0f && session->hasRealFile)
                    saveMetadata(session); // final state, in case the last piece_finished_alert raced this
                else
                    cleanupCacheDir(session);

                it = g_activeSessions.erase(it);
            }
            else
            {
                ++it;
            }
        }
        } // g_sessionsMutex

        // Live speed/peers/progress for the Downloads screen, ~2x a second.
        // torrent_handle::status() is a synchronous round trip to
        // libtorrent's own thread, so it's called with g_sessionsMutex
        // released and only the results are stored under it.
        auto now = std::chrono::steady_clock::now();
        if (now >= nextStatsRefresh)
        {
            nextStatsRefresh = now + std::chrono::milliseconds(500);

            std::vector<std::shared_ptr<TorrentSession>> sessions;
            {
                std::lock_guard<std::mutex> lock(g_sessionsMutex);
                for (auto& [hash, session] : g_activeSessions)
                    if (session->handle.is_valid())
                        sessions.push_back(session);
            }
            for (auto& session : sessions)
            {
                try
                {
                    lt::torrent_status status = session->handle.status();
                    std::lock_guard<std::mutex> lock(g_sessionsMutex);
                    session->speedBps = static_cast<uint64_t>(std::max(0, status.download_payload_rate));
                    session->peers    = static_cast<uint32_t>(std::max(0, status.num_peers));
                    if (status.total_wanted > 0)
                    {
                        session->downloadedBytes = status.total_wanted_done;
                        session->totalBytes      = status.total_wanted;
                        session->percentComplete = 100.0f * session->downloadedBytes / session->totalBytes;
                    }
                }
                catch (const std::exception&)
                {
                    // Handle went invalid between the snapshot and now
                    // (torrent removed) -- nothing to report for it.
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

#endif // USE_LIBTORRENT

} // namespace anônimo

void globalInit()
{
#ifdef USE_LIBTORRENT
    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    try
    {
        lt::settings_pack sp;
        sp.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);

        // Limitar threads: 4 em desktop (Switch seria 2-3)
        sp.set_int(lt::settings_pack::aio_threads, 4);
        sp.set_int(lt::settings_pack::checking_mem_usage, 256);  // 256 MB pra verificação de peças

        g_ltSession = std::make_unique<lt::session>(sp);

        // Iniciar thread de processamento de alerts
        g_alertThreadRunning = true;
        g_alertThread = std::thread(alertLoopThread);

        // Carregar downloads anteriores pra resume
        fs::path baseCacheDir = getCacheDir();
        for (const auto& entry : fs::directory_iterator(baseCacheDir))
        {
            if (entry.is_directory())
            {
                fs::path metaFile = entry.path() / ".metadata";
                if (fs::exists(metaFile))
                {
                    auto session = loadMetadata(metaFile, entry.path());
                    if (session && session->percentComplete < 100.0f)
                    {
                        // TODO: Re-adicionar torrent à sessão pra resume
                        // Por enquanto, apenas carrega metadados
                        g_activeSessions[session->infoHash] = session;
                        brls::Logger::info("Resumable download found: {} ({:.1f}%)",
                            session->title, session->percentComplete);
                    }
                }
            }
        }

        brls::Logger::info("download_manager: libtorrent initialized");
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to initialize libtorrent: {}", e.what());
    }
#else
    brls::Logger::info("download_manager: libtorrent not available (USE_LIBTORRENT not defined)");
#endif
}

void globalCleanup()
{
#ifdef USE_LIBTORRENT
    // Stop and join the alert thread BEFORE taking g_sessionsMutex: the
    // thread needs that same mutex to finish its current iteration, so
    // joining it while holding the lock deadlocked app shutdown whenever a
    // download was active.
    g_alertThreadRunning = false;
    if (g_alertThread.joinable())
        g_alertThread.join();

    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    // Limpar sessões ativas
    for (auto& [hash, session] : g_activeSessions)
    {
        if (session->handle.is_valid())
        {
            try
            {
                g_ltSession->remove_torrent(session->handle);
            }
            catch (const std::exception& e)
            {
                brls::Logger::error("Error removing torrent: {}", e.what());
            }
        }
    }

    g_activeSessions.clear();
    g_ltSession.reset();

    brls::Logger::info("download_manager: libtorrent cleaned up");
#endif
}

void startTorrentDownload(
    const std::string& infoHash,
    const std::string& title,
    AliveFlag alive,
    ProgressCallback onProgress,
    UrlReadyCallback onUrlReady,
    ErrorCallback onError,
    const std::vector<std::string>& trackers,
    const std::string& posterUrl)
{
#ifdef USE_LIBTORRENT
    if (!g_ltSession)
    {
        if (onError)
        {
            brls::sync([onError] {
                onError("Download manager not initialized");
            });
        }
        return;
    }

    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    // Verificar se já existe download ativo com este hash
    if (g_activeSessions.find(infoHash) != g_activeSessions.end())
    {
        if (onError)
        {
            brls::sync([onError] {
                onError("Download already in progress");
            });
        }
        return;
    }

    try
    {
        auto session = std::make_shared<TorrentSession>();
        session->infoHash = infoHash;
        session->title = title;
        session->posterUrl = posterUrl;
        session->alive = alive;
        session->onProgress = onProgress;
        session->onUrlReady = onUrlReady;
        session->onError = onError;

        // Configurar diretórios de cache
        fs::path baseCacheDir = getCacheDir();
        std::string safeName = sanitizeHash(infoHash);
        session->cacheDir = baseCacheDir / safeName;
        session->metadataFile = session->cacheDir / ".metadata";
        session->downloadingFile = session->cacheDir / ".downloading";

        fs::create_directories(session->cacheDir);

        // Validar infoHash (precisa ser formato válido)
        if (infoHash.empty() || infoHash.length() < 32)
        {
            throw std::runtime_error("Invalid infoHash format");
        }

        // Criar magnet link a partir do infoHash
        // Formato: magnet:?xt=urn:btih:<infoHash>&tr=<tracker1>&tr=<tracker2>...
        // Sem trackers, achar peers depende só de DHT (bem mais lento, ou
        // pode falhar atrás de NAT restritivo) -- ver StreamEntry::trackers.
        std::string magnetLink = "magnet:?xt=urn:btih:" + infoHash;
        for (const auto& tracker : trackers)
        {
            std::string encoded;
            for (char c : tracker)
            {
                if (c == '&' || c == '=' || c == ' ' || c == '#')
                {
                    char buf[4];
                    std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
                    encoded += buf;
                }
                else
                {
                    encoded += c;
                }
            }
            magnetLink += "&tr=" + encoded;
        }

        brls::Logger::info("Adding torrent to session: {} ({})", title, magnetLink);

        // Adicionar torrent à sessão do libtorrent
        lt::add_torrent_params params;
        params.url = magnetLink;
        params.save_path = session->downloadingFile.parent_path().string();

        // Resume: se já tem arquivo parcial, aproveitar
        if (fs::exists(session->downloadingFile))
        {
            params.resume_data = std::vector<char>();  // Deixar libtorrent carregar
            brls::Logger::info("Resuming download: {}", title);
        }

        try
        {
            session->handle = g_ltSession->add_torrent(params);
            brls::Logger::info("Torrent added to session: {}", title);
        }
        catch (const std::exception& e)
        {
            brls::Logger::error("Failed to add torrent: {}", e.what());
            throw;
        }

        brls::Logger::info("Torrent download started: {} ({})", title, infoHash);

        g_activeSessions[infoHash] = session;
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("Failed to start torrent download: {}", e.what());
        if (onError)
        {
            brls::sync([onError, e] {
                onError(std::string("Error: ") + e.what());
            });
        }
    }

#else
    if (onError)
    {
        brls::sync([onError] {
            onError("libtorrent not available - torrent downloads disabled");
        });
    }
#endif
}

void startHttpDownload(
    const std::string& url,
    const std::string& title,
    AliveFlag alive,
    ProgressCallback onProgress,
    UrlReadyCallback onUrlReady,
    ErrorCallback onError)
{
    if (!alive)
        return;

    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    try
    {
        // Usar hash da URL como identifique (similar a torrents)
        std::hash<std::string> hasher;
        std::string urlHash = std::to_string(hasher(url));

        // Verificar espaço em disco antes
        fs::path baseCacheDir = getCacheDir();
        int64_t available = getAvailableDiskSpace(baseCacheDir);
        if (available < 100 * 1024 * 1024)  // < 100MB
        {
            cleanupOldDownloads(baseCacheDir);
            available = getAvailableDiskSpace(baseCacheDir);
            if (available < 50 * 1024 * 1024)
            {
                if (onError)
                {
                    brls::sync([onError] {
                        onError("Insufficient disk space");
                    });
                }
                return;
            }
        }

        // HTTP download seria uma operação mais complexa
        // Por enquanto, apenas retorna URL direto (fallback simples)
        brls::Logger::info("HTTP fallback: streaming directly from {}", url);

        if (onUrlReady && *alive)
        {
            brls::sync([onUrlReady, url] {
                onUrlReady(url);
            });
        }
    }
    catch (const std::exception& e)
    {
        brls::Logger::error("HTTP download error: {}", e.what());
        if (onError)
        {
            brls::sync([onError, e] {
                onError(std::string("HTTP error: ") + e.what());
            });
        }
    }
}

std::vector<DownloadInfo> getActiveDownloads()
{
    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    std::vector<DownloadInfo> result;

#ifdef USE_LIBTORRENT
    for (const auto& [hash, session] : g_activeSessions)
    {
        DownloadInfo info;
        info.infoHash = session->infoHash;
        info.title = session->title;
        info.percentComplete = session->percentComplete;
        info.downloadedBytes = session->downloadedBytes;
        info.totalBytes = session->totalBytes;
        info.isStreaming = session->isStreaming;
        info.posterUrl   = session->posterUrl;
        info.speedBps    = session->speedBps;
        info.peers       = session->peers;
        if (!session->hasRealFile)
            info.status = "Buscando metadados do torrent...";
        else if (session->peers == 0)
            info.status = "Conectando aos peers...";
        else
            info.status = "Baixando";

        result.push_back(info);
    }
#endif

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
    std::lock_guard<std::mutex> lock(g_sessionsMutex);

#ifdef USE_LIBTORRENT
    auto it = g_activeSessions.find(infoHash);
    if (it == g_activeSessions.end())
        return;

    auto session = it->second;

    // Remover da sessão de torrent
    if (g_ltSession && session->handle.is_valid())
    {
        try
        {
            g_ltSession->remove_torrent(session->handle);
        }
        catch (const std::exception& e)
        {
            brls::Logger::error("Error canceling torrent: {}", e.what());
        }
    }

    cleanupCacheDir(session);

    g_activeSessions.erase(it);
    brls::Logger::info("Download canceled: {}", infoHash);
#endif
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

                float percent = meta.value("percentComplete", 0.0f);
                std::string videoFile = meta.value("videoFile", "");
                // No "videoFile" means metadata never made it past
                // metadata_received (or this is an old-format .metadata from
                // before that field existed) -- nothing real to play.
                if (percent < 100.0f || videoFile.empty() || !fs::exists(videoFile))
                    continue;

                FinishedDownload d;
                d.infoHash     = meta.value("infoHash", "");
                d.title        = meta.value("title", "");
                d.posterUrl    = meta.value("posterUrl", "");
                d.totalBytes   = meta.value("totalBytes", (int64_t)0);
                d.localFileUrl = "file:///" + videoFile;
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

    // Recomputed the same way startTorrentDownload() builds it, never taken
    // from stored data -- keeps this from ever being pointed outside the
    // downloads cache dir.
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

#endif // !USE_PIPENSX_BT
