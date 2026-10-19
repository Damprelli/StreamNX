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

    // Shown as-is by DownloadsScreen: the current phase ("Buscando
    // metadados...", "Baixando", ...), live swarm numbers, and -- for a
    // download that died -- the reason, so it stays listed (with a
    // "Remover" button) instead of silently vanishing.
    std::string posterUrl;
    std::string status;
    uint64_t speedBps = 0;
    uint32_t peers    = 0;
    bool failed       = false;
    std::string error;
};

// One torrent that finished downloading (100%) and is still sitting on
// disk -- unlike an in-progress download, this is NOT torn down when the
// AliveFlag that started it goes false, so it survives past the player
// closing (and past app restarts) until deleteFinishedDownload() removes it.
struct FinishedDownload
{
    std::string infoHash;
    std::string title;
    std::string posterUrl;
    std::string localFileUrl; // ready to hand straight to PlayerScreen
    std::string fileName;     // the downloaded video file's own name (e.g. "Movie.2024.1080p.mkv")
    int64_t totalBytes = 0;
};

// Callbacks executadas sempre no main thread (via brls::sync)
using ProgressCallback = std::function<void(float percent, int64_t downloadedBytes, int64_t totalBytes)>;
using UrlReadyCallback = std::function<void(const std::string& localFileUrl)>;
using ErrorCallback = std::function<void(const std::string& errorMessage)>;

// Inicia download de torrent
// - AliveFlag é monitorado; se virar false ANTES de completar, cancela e
//   limpa o cache. Uma vez 100% completo, o arquivo fica em disco
//   independentemente do AliveFlag -- vira uma entrada em
//   getFinishedDownloads(), assistível/deletável depois sem precisar de
//   rede/peers de novo.
// - Callbacks retornam sempre no main thread via brls::sync
// - `trackers`: URLs de anúncio vindas do addon (StreamEntry::trackers) --
//   sem elas, achar peers depende só de DHT, que é bem mais lento (ou falha
//   atrás de NAT restritivo)
// - `posterUrl`: opcional, só pra exibir na tela de Downloads
void startTorrentDownload(
    const std::string& infoHash,
    const std::string& title,
    AliveFlag alive,
    ProgressCallback onProgress = nullptr,
    UrlReadyCallback onUrlReady = nullptr,
    ErrorCallback onError = nullptr,
    const std::vector<std::string>& trackers = {},
    const std::string& posterUrl = ""
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

// Downloads concluídos que ainda estão em disco -- ver FinishedDownload.
// Reflete o estado real do disco (lê os .metadata salvos), então funciona
// mesmo logo após o app iniciar, antes de qualquer download novo começar.
std::vector<FinishedDownload> getFinishedDownloads();

// Apaga os arquivos de um download concluído. Sem efeito se `infoHash` não
// for um download concluído (um em progresso usa cancelDownload() em vez
// disso).
void deleteFinishedDownload(const std::string& infoHash);

// Espaço do armazenamento onde os downloads ficam (cartão SD no Switch).
// Retorna false se não foi possível consultar.
bool getStorageSpace(int64_t& freeBytes, int64_t& totalBytes);

// Cancelar um download específico (limpa arquivo). Também remove da
// lista um download que falhou (DownloadInfo::failed).
void cancelDownload(const std::string& infoHash);

// Deve ser chamado em main() antes de qualquer download
void globalInit();

// Deve ser chamado ao encerrar a aplicação
void globalCleanup();

} // namespace download_mgr
