#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace download_mgr
{

using AliveFlag = std::shared_ptr<bool>;

struct DownloadInfo
{
    std::string infoHash;
    std::string title;
    float percentComplete = 0.0f;
    int64_t downloadedBytes = 0;
    int64_t totalBytes = 0;
    bool isStreaming = false;
};

// Callbacks executadas sempre no main thread (via brls::sync)
using ProgressCallback = std::function<void(float percent, int64_t downloadedBytes, int64_t totalBytes)>;
using UrlReadyCallback = std::function<void(const std::string& localFileUrl)>;
using ErrorCallback = std::function<void(const std::string& errorMessage)>;

// Inicia download de torrent
// - AliveFlag é monitorado; se virar false, pausa/cleanup automático
// - Callbacks retornam sempre no main thread via brls::sync
void startTorrentDownload(
    const std::string& infoHash,
    const std::string& title,
    AliveFlag alive,
    ProgressCallback onProgress = nullptr,
    UrlReadyCallback onUrlReady = nullptr,
    ErrorCallback onError = nullptr
);

// Fallback HTTP quando torrent falha/indisponível
void startHttpDownload(
    const std::string& url,
    const std::string& title,
    AliveFlag alive,
    ProgressCallback onProgress = nullptr,
    UrlReadyCallback onUrlReady = nullptr,
    ErrorCallback onError = nullptr
);

// UI pode consultar dados de downloads ativos
std::vector<DownloadInfo> getActiveDownloads();

// Cancelar um download específico (limpa arquivo)
void cancelDownload(const std::string& infoHash);

// Deve ser chamado em main() antes de qualquer download
void globalInit();

// Deve ser chamado ao encerrar a aplicação
void globalCleanup();

} // namespace download_mgr
