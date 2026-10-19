#pragma once

#include <borealis.hpp>

#include <memory>
#include <string>

#include "model/playback_context.hpp"

class VideoView;

// Full-screen playback: a full-screen PlayerView (raw MpvPlayer::draw(), no
// chrome) with a VideoView on top for all the on-screen controls -- ported
// from StreamNX-main's own RemotePlayer/VideoView pairing, same button
// scheme and auto-hiding overlay, adapted to this app's simpler MpvPlayer
// (see view/video_view.hpp for what got trimmed and why).
class PlayerScreen : public brls::Box
{
  public:
    PlayerScreen(const std::string& url, const std::string& title, const PlaybackContext& ctx = {});
    // Alternativo: aceita AliveFlag externo (usado por download_manager)
    PlayerScreen(const std::string& url, const std::string& title, std::shared_ptr<bool> externalAlive,
        const PlaybackContext& ctx = {});
    ~PlayerScreen();

  private:
    void onLoaded();
    void onError(const std::string& message);
    void onEndOfFile();
    void stopPlayback();
    // Records the current playback position into the Continue Watching
    // entry this stream started from, if any -- called from every exit path
    // via stopPlayback() (close, error, end of file), so wherever the user
    // stops matters, not just a clean finish.
    void saveProgress();
    void openNextEpisode();

    PlaybackContext ctx;
    VideoView* videoView = nullptr;
    bool stopped         = false;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
