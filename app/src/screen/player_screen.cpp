#include "screen/player_screen.hpp"
#include "screen/player_settings_screen.hpp"
#include "screen/stream_list_screen.hpp"
#include "view/player_view.hpp"
#include "view/video_view.hpp"
#include "api/mpv_player.hpp"
#include "model/continue_watching_store.hpp"

#if !defined(BOREALIS_USE_DEKO3D)
#include <borealis/platforms/glfw/glfw_video.hpp>
#endif

PlayerScreen::PlayerScreen(const std::string& url, const std::string& title, const PlaybackContext& ctx)
    : PlayerScreen(url, title, std::make_shared<bool>(true), ctx)
{
}

PlayerScreen::PlayerScreen(
    const std::string& url, const std::string& title, std::shared_ptr<bool> externalAlive, const PlaybackContext& ctx)
    : ctx(ctx)
{
    brls::Logger::info("player: open {}", url);
    // Se passou AliveFlag externo (ex: download_manager), usar esse
    if (externalAlive)
    {
        this->alive = externalAlive;
    }

    this->setAxis(brls::Axis::COLUMN);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    // No background color here either -- same reason as PlayerView: an
    // opaque nanovg fill here gets flushed to the GPU at end-of-frame,
    // after mpv's raw GL render already ran, and paints over the video.

    // Renders the raw video frame every frame, full-screen, no chrome.
    auto* video = new PlayerView();
    this->addView(video);

    // All on-screen controls (title, seek bar, lock, settings, volume,
    // gestures...) live on top of it -- see view/video_view.hpp.
    this->videoView = new VideoView();
    this->videoView->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->videoView->setTitie(title);
    this->addView(this->videoView);

    this->videoView->getSettingEvent()->subscribe(
        []() { brls::Application::pushActivity(new brls::Activity(new PlayerSettingsScreen())); });

    if (!this->ctx.nextVideoId.empty())
    {
        this->videoView->setNextEpisodeAvailable(true);
        this->videoView->getNextEpisodeEvent()->subscribe([this]() { this->openNextEpisode(); });
    }

    auto alive = this->alive;
    MpvPlayer::instance().setCallbacks(
        [this, alive]() {
            if (*alive)
                this->onLoaded();
        },
        [this, alive](std::string message) {
            if (*alive)
                this->onError(message);
        },
        [this, alive]() {
            if (*alive)
                this->onEndOfFile();
        });

    MpvPlayer::instance().loadUrl(url);

    brls::Application::giveFocus(this);

#if !defined(BOREALIS_USE_DEKO3D)
    // Windows' compositor throttles/doesn't present new frames from a
    // window that doesn't have OS input focus (separate from borealis' own
    // internal "active" tracking, already handled in MpvPlayer::draw()) --
    // grab it explicitly on entering the player instead of relying on the
    // user clicking the window themselves. Desktop-only: there's no
    // separate "window focus" concept on Switch.
    auto* videoContext = dynamic_cast<brls::GLFWVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    if (videoContext)
        glfwFocusWindow(videoContext->getGLFWWindow());
#endif
}

PlayerScreen::~PlayerScreen()
{
    this->stopPlayback();
    *this->alive = false;
}

void PlayerScreen::stopPlayback()
{
    if (this->stopped)
        return;
    this->stopped = true;

    this->saveProgress();

    MpvPlayer::instance().clearCallbacks();
    MpvPlayer::instance().stop();
}

void PlayerScreen::saveProgress()
{
    if (this->ctx.continueWatchingId.empty())
        return;

    ContinueWatchingStore::instance().updateProgress(
        this->ctx.continueWatchingId, MpvPlayer::instance().getPosition(), MpvPlayer::instance().getDuration());
}

void PlayerScreen::openNextEpisode()
{
    if (this->ctx.nextVideoId.empty())
        return;

    std::string type = this->ctx.type, nextVideoId = this->ctx.nextVideoId, nextTitle = this->ctx.nextTitle,
                nextSubtitle = this->ctx.nextSubtitle, showId = this->ctx.continueWatchingId,
                posterUrl = this->ctx.posterUrl;

    // Explicit, like onEndOfFile()/onError() -- stop mpv before this screen
    // is popped and destroyed, rather than leaving it to the destructor to
    // race the next screen's own playback getting started.
    this->stopPlayback();

    // Deferred one frame, like VideoView's own btnClose handler: this runs
    // from inside the OSD button's own click handler (nextEpisodeEvent, fired
    // from btnNextEpisode's TapGestureRecognizer), so popping -- and thereby
    // destroying the view that owns that handler -- right now, mid-call,
    // would be reentrant. TransitionAnimation::NONE for the same reason
    // StreamListScreen's own transition into PlayerScreen uses it: a stray
    // press mid-crossfade could otherwise land back on the player that's on
    // its way out.
    brls::sync([type, nextVideoId, nextTitle, nextSubtitle, showId, posterUrl]() {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        brls::Application::pushActivity(
            new brls::Activity(new StreamListScreen(type, nextVideoId, nextTitle, nextSubtitle, showId, posterUrl)),
            brls::TransitionAnimation::NONE);
    });
}

void PlayerScreen::onLoaded()
{
    if (this->ctx.resumePositionSeconds > 0.0)
        MpvPlayer::instance().seekAbsolute(this->ctx.resumePositionSeconds);
    // VideoView's own OSD polls MpvPlayer::getDuration() every frame to
    // know when to hide its loading spinner -- nothing else to do here.
}

void PlayerScreen::onError(const std::string& message)
{
    auto* dialog = new brls::Dialog("Erro ao reproduzir: " + message);
    dialog->addButton("OK", [this]() {
        this->stopPlayback();
        brls::Application::popActivity();
    });
    dialog->open();
}

void PlayerScreen::onEndOfFile()
{
    brls::Application::notify("Reproducao concluida.");
    this->stopPlayback();
    brls::Application::popActivity();
}
