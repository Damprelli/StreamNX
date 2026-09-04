#pragma once

#include <borealis.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#if defined(BOREALIS_USE_DEKO3D)
#include <deko3d.hpp>
#endif

struct mpv_handle;
struct mpv_render_context;

// A single, app-lifetime libmpv instance -- the same approach StreamNX-main's
// own MPVCore takes (mirrored here at a fraction of its size: no OSD, no
// subtitles/aspect/hwdec settings -- just the two render backends this
// project actually ships: GLFW/OpenGL on desktop, deko3d on Switch,
// matching whichever one borealis itself is rendering with on that
// platform). Playback renders directly into borealis' own framebuffer, so
// a stream plays inside this app's own window/screen instead of handing
// its URL off to an external player or, on desktop, the OS's browser
// handler.
class MpvPlayer
{
  public:
    // One entry from mpv's "track-list" property, filtered to a single
    // type ("audio" or "sub").
    struct TrackInfo
    {
        int64_t id = 0;
        std::string lang;
        std::string title;
        bool selected = false;
    };

    static MpvPlayer& instance();

    // Callbacks fire on the main thread (already marshalled through
    // brls::sync). Only one screen ever owns them at a time; PlayerScreen
    // sets them on construction and clears them on destruction.
    void setCallbacks(std::function<void()> onLoaded, std::function<void(std::string)> onError,
        std::function<void()> onEndOfFile);
    void clearCallbacks();

    void loadUrl(const std::string& url);
    void togglePause();
    void seekRelative(int seconds);
    void stop();
    bool isPaused() const { return paused; }
    bool isValid() const { return mpv != nullptr; }

    // 0-200 (mpv's own "volume" range; 100 = unity gain, up to +6dB boost).
    int getVolume();
    void setVolume(int value);

    // "vo=libmpv" on/off -- VideoView's own port of StreamNX-main's
    // enableVO(false) (matches MPVCore::enableVO), re-affirmed whenever the
    // player screen (re)opens.
    void enableVO(bool enabled);

    // `type` is "audio" or "sub". Subtitle id 0 means "off".
    std::vector<TrackInfo> getTracks(const std::string& type);
    int64_t getCurrentTrack(const std::string& type);
    void setTrack(const std::string& type, int64_t id);

    double getSpeed();
    void setSpeed(double value);

    double getSubDelay();
    void setSubDelay(double value);

    // Seconds. 0 for either while nothing's loaded yet (matches mpv's own
    // property value in that state, so callers don't need a separate
    // "not ready" check).
    double getPosition();
    double getDuration();
    void seekAbsolutePercent(double percent);

    // Renders the current video frame -- called from PlayerView::draw()
    // every frame. Always fills the whole window: this app only ever shows
    // the player full-screen, so there's no inline/mini-player case to
    // size a smaller target for.
    void draw();

  private:
    MpvPlayer();
    ~MpvPlayer();

    void ensureInitialized();
    void pumpEvents();

    static void onWakeup(void* self);
    static void onRenderUpdate(void* self);

    mpv_handle* mpv               = nullptr;
    mpv_render_context* renderCtx = nullptr;
    bool paused                   = false;
    int lastRenderError           = 0;

#if defined(BOREALIS_USE_DEKO3D)
    // mpv writes into the SAME framebuffer image borealis is about to
    // present, so the two fences below are what keep the GPU from reading
    // it (mpv's draw) before borealis is done writing to it, or presenting
    // it (borealis' own composite) before mpv is done drawing -- the
    // deko3d equivalent of a GL_FRAMEBUFFER_BARRIER, required because
    // there's no implicit ordering between two separate command
    // submissions the way there is for two GL calls on the same context.
    DkFence readyFence;
    DkFence doneFence;
#endif

    std::function<void()> onLoaded;
    std::function<void(std::string)> onError;
    std::function<void()> onEndOfFile;
};
