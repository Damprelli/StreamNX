#pragma once

#include <borealis.hpp>

#include <memory>
#include <string>
#include <vector>

#include "api/stremio_api.hpp"
#include "model/playback_context.hpp"

// Shown while a torrent stream is being resolved: libtorrent needs to find
// peers and fetch the metadata before anything can play, then download
// enough of the real video file (see download_manager's streaming
// threshold) before playback can start -- with no networking indicator at
// all, that gap looked from the user's side exactly like the app hanging.
// This screen gives it a visible status line + progress bar, and hands off
// to PlayerScreen the moment download_manager signals the file is ready.
class TorrentProgressScreen : public brls::Box
{
  public:
    TorrentProgressScreen(const stremio::StreamEntry& entry, const std::string& title, const PlaybackContext& ctx = {});
    ~TorrentProgressScreen();

    brls::View* getDefaultFocus() override;

  private:
    void setStatus(const std::string& text);
    void setProgress(float percent, int64_t downloadedBytes, int64_t totalBytes);
    void startHttpFallback(const std::string& url);

    std::string title;
    PlaybackContext ctx;

    // Copied out of `entry` (a ctor-only reference) so the async
    // download_manager callbacks below -- which fire well after the
    // constructor returns -- can still record what this torrent actually
    // was into Continue Watching once it starts playing.
    std::string infoHash;
    std::string torrentName;
    std::vector<std::string> trackers;

    brls::Label* statusLabel     = nullptr;
    brls::Label* detailLabel     = nullptr;
    brls::Box* progressBarFill   = nullptr;
    brls::Box* progressBarTrack  = nullptr;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
