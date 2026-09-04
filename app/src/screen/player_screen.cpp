#include "screen/player_screen.hpp"
#include "screen/player_settings_screen.hpp"
#include "view/player_view.hpp"
#include "view/video_view.hpp"
#include "api/mpv_player.hpp"

#if !defined(BOREALIS_USE_DEKO3D)
#include <borealis/platforms/glfw/glfw_video.hpp>
#endif

PlayerScreen::PlayerScreen(const std::string& url, const std::string& title)
{
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

    MpvPlayer::instance().clearCallbacks();
    MpvPlayer::instance().stop();
}

void PlayerScreen::onLoaded()
{
    // VideoView's own OSD polls MpvPlayer::getDuration() every frame to
    // know when to hide its loading spinner -- nothing to do here.
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
