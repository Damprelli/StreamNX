#pragma once

#include <borealis.hpp>

#include <string>
#include <unordered_map>

#include "api/download_manager.hpp"

class ActiveDownloadRow;

// Everything torrent-related that's on (or coming to) this device:
//  - "Baixando": downloads still running (live progress/speed/peers, with a
//    Cancelar button) and ones that failed (reason + Remover).
//  - "Concluidos": torrents that finished and are still sitting on disk
//    (see download_manager's getFinishedDownloads()) -- file info, Assistir
//    and Apagar.
// Picking a torrent stream starts its download in the background and opens
// this screen (standalone = true, its own Activity); it's also a tab of the
// main menu (standalone = false). Separate from Continuar Assistindo: that
// one is "what was last played"; this is "what's saved on this device".
class DownloadsScreen : public brls::Box
{
  public:
    explicit DownloadsScreen(bool standalone = false);

    brls::View* getDefaultFocus() override;

  private:
    void buildContent();
    void refreshList();
    void poll();
    void updateStatusBar();
    void confirmDelete(const std::string& infoHash, const std::string& title);
    void confirmCancel(const std::string& infoHash, const std::string& title, bool failed);

    // Identifies which rows the list currently shows -- poll() rebuilds the
    // whole list only when this changes (a download starts, finishes, fails
    // or is removed), and otherwise just updates the active rows in place,
    // so focus isn't yanked around every second.
    std::string listSignature(const std::vector<download_mgr::DownloadInfo>& active,
        const std::vector<download_mgr::FinishedDownload>& finished) const;

    bool standalone         = false;
    brls::View* loadingGate = nullptr;
    brls::Box* listBox      = nullptr;
    std::string shownSignature;
    std::unordered_map<std::string, ActiveDownloadRow*> activeRows;
    brls::RepeatingTimer pollTimer;

    // Header status bar: clock, battery and free space where downloads go.
    brls::Label* clockLabel   = nullptr;
    brls::Label* batteryIcon  = nullptr;
    brls::Label* batteryLabel = nullptr;
    brls::Label* storageLabel = nullptr;
    brls::Box* storageFill    = nullptr;
};
