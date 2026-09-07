#include "api/download_manager.hpp"
#include "api/http_client.hpp"

#include <borealis.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <borealis/extern/nlohmann/json.hpp>

#ifdef USE_LIBTORRENT
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
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
// libtorrent session (global, persistent for app lifetime)
std::unique_ptr<lt::session> g_ltSession;

// Metadados de um download em progresso
struct TorrentSession
{
    std::string infoHash;
    std::string title;
    fs::path cacheDir;
    fs::path metadataFile;
    fs::path downloadingFile;

    int64_t totalBytes = 0;
    int64_t downloadedBytes = 0;
    float percentComplete = 0.0f;
    bool isStreaming = false;

    AliveFlag alive;
    ProgressCallback onProgress;
    UrlReadyCallback onUrlReady;
    ErrorCallback onError;

    lt::torrent_handle handle;
};

std::unordered_map<std::string, std::shared_ptr<TorrentSession>> g_activeSessions;
std::thread g_alertThread;
bool g_alertThreadRunning = false;

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
        metadata["totalBytes"] = session->totalBytes;
        metadata["downloadedBytes"] = session->downloadedBytes;
        metadata["percentComplete"] = session->percentComplete;
        metadata["isStreaming"] = session->isStreaming;
        metadata["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

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

// Limpa downloads quando AliveFlag fica false (usuário saiu)
void cleanupIfAliveFlag(const std::shared_ptr<TorrentSession>& session)
{
    if (!session || !session->alive)
        return;

    if (*session->alive)
        return;  // Ainda vivo

    // AliveFlag virou false - usuário saiu
    // Se download ainda incompleto, remove arquivo temporário
    if (session->percentComplete < 100.0f)
    {
        try
        {
            if (fs::exists(session->downloadingFile))
            {
                fs::remove(session->downloadingFile);
                brls::Logger::info("Cleaned incomplete download: {}", session->title);
            }
        }
        catch (const std::exception& e)
        {
            brls::Logger::error("Error cleaning incomplete download: {}", e.what());
        }
    }
}

// Thread que processa alerts do libtorrent
void alertLoopThread()
{
    while (g_alertThreadRunning)
    {
        std::lock_guard<std::mutex> lock(g_sessionsMutex);

        if (!g_ltSession)
            break;

        // Processar todos os alerts pendentes
        auto alerts = g_ltSession->pop_alerts();

        for (auto& alert : alerts)
        {
            if (auto* ta = lt::alert_cast<lt::torrent_added_alert>(alert.get()))
            {
                brls::Logger::info("Torrent added: {}", ta->handle.info_hash().to_string());
            }
            else if (auto* pa = lt::alert_cast<lt::piece_finished_alert>(alert.get()))
            {
                auto hash = pa->handle.info_hash().to_string();
                if (auto it = g_activeSessions.find(hash); it != g_activeSessions.end())
                {
                    auto session = it->second;

                    // Verificar se AliveFlag virou false (usuário saiu)
                    if (!(*session->alive))
                    {
                        cleanupIfAliveFlag(session);
                        continue;
                    }

                    session->downloadedBytes = pa->handle.status().total_done;
                    session->totalBytes = pa->handle.status().total_wanted;
                    session->percentComplete =
                        session->totalBytes > 0 ?
                        (100.0f * session->downloadedBytes / session->totalBytes) : 0.0f;

                    // Notificar progresso
                    if (session->onProgress && *session->alive)
                    {
                        brls::sync([session] {
                            if (*session->alive)
                                session->onProgress(session->percentComplete,
                                    session->downloadedBytes, session->totalBytes);
                        });
                    }

                    // Verificar se atingiu threshold pra começar streaming
                    if (!session->isStreaming && session->totalBytes > 0)
                    {
                        float thresholdPercent = 10.0f;  // 10% ou 50MB
                        int64_t thresholdBytes = 50 * 1024 * 1024;

                        if (session->percentComplete >= thresholdPercent ||
                            session->downloadedBytes >= thresholdBytes)
                        {
                            session->isStreaming = true;
                            std::string localUrl = "file:///" + session->downloadingFile.string();

                            if (session->onUrlReady && *session->alive)
                            {
                                brls::sync([session, localUrl] {
                                    if (*session->alive)
                                        session->onUrlReady(localUrl);
                                });
                            }

                            saveMetadata(session);
                        }
                    }

                    saveMetadata(session);
                }
            }
            else if (auto* dca = lt::alert_cast<lt::torrent_finished_alert>(alert.get()))
            {
                auto hash = dca->handle.info_hash().to_string();
                if (auto it = g_activeSessions.find(hash); it != g_activeSessions.end())
                {
                    auto session = it->second;
                    session->percentComplete = 100.0f;

                    brls::Logger::info("Download completed: {}", session->title);
                    saveMetadata(session);
                }
            }
            else if (auto* ea = lt::alert_cast<lt::torrent_error_alert>(alert.get()))
            {
                auto hash = ea->handle.info_hash().to_string();
                if (auto it = g_activeSessions.find(hash); it != g_activeSessions.end())
                {
                    auto session = it->second;
                    std::string error = ea->error.message();

                    brls::Logger::error("Torrent error: {}", error);

                    if (session->onError && *session->alive)
                    {
                        brls::sync([session, error] {
                            if (*session->alive)
                                session->onError(error);
                        });
                    }
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
        sp.set_int(lt::settings_pack::alert_mask,
            lt::alert::piece_finished_alert::static_category |
            lt::alert::torrent_finished_alert::static_category |
            lt::alert::torrent_error_alert::static_category |
            lt::alert::torrent_added_alert::static_category);

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
    std::lock_guard<std::mutex> lock(g_sessionsMutex);

    // Parar alert thread
    g_alertThreadRunning = false;
    if (g_alertThread.joinable())
        g_alertThread.join();

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
    ErrorCallback onError)
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
        // Formato: magnet:?xt=urn:btih:<infoHash>
        std::string magnetLink = "magnet:?xt=urn:btih:" + infoHash;

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

        result.push_back(info);
    }
#endif

    return result;
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

    // Usar função de cleanup (respeita % completo)
    cleanupIfAliveFlag(session);

    g_activeSessions.erase(it);
    brls::Logger::info("Download canceled: {}", infoHash);
#endif
}

} // namespace download_mgr
