#include "view/video_view.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "utils/gesture.hpp"
#include "api/mpv_player.hpp"

#include <fmt/format.h>
#include <cmath>
#include <limits>

const int VIDEO_SEEK_NODELAY = 0;

#define CHECK_OSD(shake)                                                             \
    if (this->isOsdLock)                                                             \
    {                                                                                \
        if (this->isOsdShown)                                                        \
        {                                                                            \
            brls::Application::giveFocus(this->osdLockBox);                          \
            if (shake)                                                              \
                this->osdLockBox->shakeHighlight(brls::FocusDirection::RIGHT);       \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            this->showOSD(true);                                                    \
        }                                                                            \
        return true;                                                                \
    }

namespace
{

std::string sec2Time(double seconds)
{
    if (seconds < 0 || seconds != seconds) // NaN guard
        seconds = 0;
    int total = (int)seconds;
    int h     = total / 3600;
    int m     = (total % 3600) / 60;
    int s     = total % 60;
    char buf[16];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else
        snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

int getSeekRange(int current)
{
    current = abs(current);
    if (current < 60)
        return 5;
    if (current < 300)
        return 10;
    if (current < 600)
        return 20;
    if (current < 1200)
        return 60;
    return current / 15;
}

} // namespace

VideoView::VideoView()
{
    this->inflateFromXMLRes("xml/view/video_view.xml");
    this->setHideHighlightBorder(true);
    this->setHideHighlightBackground(true);
    this->setHideClickAnimation(true);

    MpvPlayer::instance().enableVO(true);

    this->input = brls::Application::getPlatform()->getInputManager();

    this->registerAction(
        "Voltar", brls::BUTTON_B,
        [this](brls::View* view) {
            if (isOsdLock)
            {
                this->toggleOSD();
                return true;
            }
            if (this->isOsdShown)
            {
                this->hideOSD();
                return true;
            }
            return close();
        },
        true);

    this->registerAction(
        "", brls::BUTTON_LB,
        [this](brls::View* view) -> bool {
            CHECK_OSD(true);
            this->seekingRange -= getSeekRange((int)this->seekingRange);
            this->requestSeeking((int)seekingRange);
            return true;
        },
        false, true);

    this->registerAction(
        "", brls::BUTTON_RB,
        [this](brls::View* view) -> bool {
            CHECK_OSD(true);
            this->seekingRange += getSeekRange((int)this->seekingRange);
            this->requestSeeking((int)seekingRange);
            return true;
        },
        false, true);

    this->registerAction(
        "Info", brls::BUTTON_Y,
        [this](brls::View* view) -> bool {
            if (!this->seekingRange)
                this->toggleOSD();
            return true;
        },
        true);

    this->btnSetting->registerClickAction([this](brls::View* view) {
        this->settingEvent.fire();
        return true;
    });
    this->btnSetting->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnSetting));

    this->registerAction(
        "Bloquear", brls::BUTTON_X,
        [this](brls::View* view) {
            this->toggleOSDLock();
            return true;
        },
        true);

    this->registerAction(
        "Opcoes", brls::BUTTON_START,
        [this](brls::View* view) {
            this->settingEvent.fire();
            return true;
        },
        true);

    this->btnVolume->registerClickAction([this](brls::View* view) { return this->toggleVolume(view); });
    this->btnVolume->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVolume));

    this->btnToggle->registerClickAction([](brls::View* view) {
        MpvPlayer::instance().togglePause();
        return true;
    });
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnToggle));

    this->osdLockBox->registerClickAction([this](brls::View* view) { return this->toggleOSDLock(); });
    this->osdLockBox->addGestureRecognizer(new brls::TapGestureRecognizer(this->osdLockBox));

    this->btnClose->registerClickAction([](brls::View* view) {
        brls::sync([]() { close(); });
        return true;
    });
    this->btnClose->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnClose));

    this->registerAction(
        "Pausar/Retomar", brls::BUTTON_A,
        [this](brls::View* view) {
            CHECK_OSD(true);
            MpvPlayer::instance().togglePause();
            return true;
        },
        false);

    this->btnVideoSpeed->registerClickAction([this](brls::View* view) { return this->toggleSpeed(); });
    this->btnVideoSpeed->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVideoSpeed));

    // Touch/gamepad-drag gestures: single tap toggles the OSD, double tap
    // toggles play/pause, a long press speeds up to 2x while held,
    // horizontal drag seeks, vertical drag (left half) adjusts backlight
    // brightness where supported and (right half) volume -- same mapping
    // as StreamNX-main's own VideoView.
    this->addGestureRecognizer(new OsdGestureRecognizer([this](OsdGestureStatus status) {
        auto& mpv = MpvPlayer::instance();
        if (status.osdGestureType == OsdGestureType::TAP)
        {
            this->toggleOSD();
            return;
        }

        switch (status.osdGestureType)
        {
            case OsdGestureType::DOUBLE_TAP_END:
                if (isOsdLock)
                {
                    this->toggleOSD();
                    break;
                }
                mpv.togglePause();
                break;
            case OsdGestureType::LONG_PRESS_START:
            {
                if (isOsdLock)
                    break;
                MpvPlayer::instance().setSpeed(2.0);
                this->speedHintLabel->setText("Avancando 2.0x");
                this->speedHintBox->setVisibility(brls::Visibility::VISIBLE);
                break;
            }
            case OsdGestureType::LONG_PRESS_CANCEL:
            case OsdGestureType::LONG_PRESS_END:
                if (isOsdLock)
                {
                    this->toggleOSD();
                    break;
                }
                mpv.setSpeed(1.0);
                this->speedHintBox->setVisibility(brls::Visibility::GONE);
                break;
            case OsdGestureType::HORIZONTAL_PAN_START:
                if (isOsdLock)
                    break;
                infoIcon->setImageFromSVGRes("icon/ico-seeking.svg");
                osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
                break;
            case OsdGestureType::HORIZONTAL_PAN_UPDATE:
                if (isOsdLock)
                    break;
                this->requestSeeking((int)(fmin(120.0, mpv.getDuration()) * status.deltaX));
                break;
            case OsdGestureType::HORIZONTAL_PAN_CANCEL:
                if (isOsdLock)
                    break;
                this->requestSeeking(0, VIDEO_SEEK_NODELAY);
                break;
            case OsdGestureType::HORIZONTAL_PAN_END:
                if (isOsdLock)
                {
                    this->toggleOSD();
                    break;
                }
                this->requestSeeking((int)(fmin(120.0, mpv.getDuration()) * status.deltaX), VIDEO_SEEK_NODELAY);
                break;
            case OsdGestureType::LEFT_VERTICAL_PAN_START:
                if (isOsdLock)
                    break;
                if (brls::Application::getPlatform()->canSetBacklightBrightness())
                {
                    this->brightnessInit = brls::Application::getPlatform()->getBacklightBrightness();
                    infoIcon->setImageFromSVGRes("icon/ico-sun-fill.svg");
                    osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
                }
                break;
            case OsdGestureType::RIGHT_VERTICAL_PAN_START:
                if (isOsdLock)
                    break;
                this->volumeInit = mpv.getVolume();
                infoIcon->setImageFromSVGRes("icon/ico-volume.svg");
                osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
                break;
            case OsdGestureType::LEFT_VERTICAL_PAN_UPDATE:
                if (isOsdLock)
                    break;
                if (brls::Application::getPlatform()->canSetBacklightBrightness())
                    this->requestBrightness(this->brightnessInit + status.deltaY);
                break;
            case OsdGestureType::RIGHT_VERTICAL_PAN_UPDATE:
                if (isOsdLock)
                    break;
                this->requestVolume((int)(this->volumeInit + status.deltaY * 100));
                break;
            case OsdGestureType::LEFT_VERTICAL_PAN_CANCEL:
            case OsdGestureType::LEFT_VERTICAL_PAN_END:
                if (isOsdLock)
                {
                    this->toggleOSD();
                    break;
                }
                osdInfoBox->setVisibility(brls::Visibility::GONE);
                break;
            case OsdGestureType::RIGHT_VERTICAL_PAN_CANCEL:
            case OsdGestureType::RIGHT_VERTICAL_PAN_END:
                if (isOsdLock)
                {
                    this->toggleOSD();
                    break;
                }
                osdInfoBox->setVisibility(brls::Visibility::GONE);
                break;
            default:
                break;
        }
    }));

    osdSlider->getProgressSetEvent().subscribe([this](float progress) {
        this->showOSD(true);
        MpvPlayer::instance().seekAbsolutePercent(progress * 100);
    });
    osdSlider->getProgressEvent().subscribe([this](float progress) { this->showOSD(false); });

    this->showLoading();
}

VideoView::~VideoView()
{
    disableDimming(false);
}

void VideoView::setTitie(const std::string& title)
{
    this->titleLabel->setText(title);
}

void VideoView::requestSeeking(int seek, int delay)
{
    auto& mpv      = MpvPlayer::instance();
    double duration = mpv.getDuration();
    if (duration <= 0)
    {
        this->seekingRange = 0;
        return;
    }
    double progress = (mpv.getPosition() + seek) / duration;

    if (progress < 0)
    {
        progress = 0;
        seek     = (int)(mpv.getPosition() * -1);
    }
    else if (progress > 1)
    {
        progress = 1;
        seek     = (int)duration;
    }

    showOSD(false);
    if (osdInfoBox->getVisibility() != brls::Visibility::VISIBLE)
    {
        osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
        infoIcon->setImageFromSVGRes("icon/ico-seeking.svg");
    }
    infoLabel->setText(fmt::format("{:+d} s", seek));
    osdSlider->setProgress((float)progress);
    leftStatusLabel->setText(sec2Time(duration * progress));

    brls::cancelDelay(this->seekingIter);
    if (delay <= VIDEO_SEEK_NODELAY)
    {
        osdInfoBox->setVisibility(brls::Visibility::GONE);
        this->seekingRange = 0;
        if (seek == 0)
            return;
        MpvPlayer::instance().seekRelative(seek);
    }
    else if (delay > 0)
    {
        ASYNC_RETAIN
        this->seekingIter = brls::delay(delay, [ASYNC_TOKEN, seek]() {
            ASYNC_RELEASE
            osdInfoBox->setVisibility(brls::Visibility::GONE);
            this->seekingRange = 0;
            if (seek == 0)
                return;
            MpvPlayer::instance().seekRelative(seek);
        });
    }
}

void VideoView::requestVolume(int value, int delay)
{
    if (value < 0)
        value = 0;
    if (value > 200)
        value = 200;
    MpvPlayer::instance().setVolume(value);
    infoLabel->setText(fmt::format("{:+d} %", value));

    if (delay == 0)
        return;
    if (this->volumeIter == 0)
    {
        osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
        infoIcon->setImageFromSVGRes("icon/ico-volume.svg");
    }
    else
    {
        brls::cancelDelay(this->volumeIter);
    }
    ASYNC_RETAIN
    volumeIter = brls::delay(delay, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        osdInfoBox->setVisibility(brls::Visibility::GONE);
        this->volumeIter = 0;
    });
}

void VideoView::requestBrightness(float value)
{
    if (value < 0)
        value = 0.0f;
    if (value > 1)
        value = 1.0f;
    brls::Application::getPlatform()->setBacklightBrightness(value);
    infoLabel->setText(fmt::format("{} %", (int)(value * 100)));
    infoIcon->setImageFromSVGRes("icon/ico-sun-fill.svg");
    osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
}

bool VideoView::toggleVolume(brls::View* view)
{
    this->showOSD(false);
    auto theme     = brls::Application::getTheme();
    auto container = new brls::Box();
    container->setHideClickAnimation(true);
    container->registerAction("Voltar", brls::BUTTON_B, [this](brls::View* view) {
        this->showOSD(true);
        view->dismiss();
        return true;
    });
    container->addGestureRecognizer(new brls::TapGestureRecognizer(container, [this, container]() {
        this->showOSD(true);
        container->dismiss();
        return true;
    }));

    auto sliderBox = new brls::Box();
    sliderBox->setAlignItems(brls::AlignItems::CENTER);
    sliderBox->setHeight(40);
    sliderBox->setCornerRadius(4);
    sliderBox->setBackgroundColor(nvgRGB(60, 60, 60));
    float sliderX = view->getX() - 120;
    if (sliderX < 0)
        sliderX = 20;
    if (sliderX > brls::Application::ORIGINAL_WINDOW_WIDTH - 332)
        sliderX = brls::Application::ORIGINAL_WINDOW_WIDTH - 332;
    sliderBox->setTranslationX(sliderX);
    sliderBox->setTranslationY(view->getY() - 70);

    auto slider = new brls::Slider();
    slider->setMargins(8, 16, 8, 16);
    slider->setWidth(300);
    slider->setHeight(20);
    slider->setProgress(MpvPlayer::instance().getVolume() / 200.0f);
    slider->getProgressEvent()->subscribe([](float progress) { MpvPlayer::instance().setVolume((int)(progress * 200)); });
    sliderBox->addView(slider);
    container->addView(sliderBox);

    auto frame = new brls::AppletFrame(container);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    frame->setFooterVisibility(brls::Visibility::GONE);
    frame->setBackgroundColor(theme.getColor("brls/backdrop"));
    brls::Application::pushActivity(new brls::Activity(frame));
    return true;
}

bool VideoView::toggleSpeed()
{
    brls::Dropdown* dropdown = new brls::Dropdown(
        "Velocidade", { "2.0x", "1.75x", "1.5x", "1.25x", "1.0x", "0.75x", "0.5x" },
        [](int selected) { MpvPlayer::instance().setSpeed((200 - selected * 25) / 100.0); },
        (int)(200 - MpvPlayer::instance().getSpeed() * 100) / 25);
    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}

bool VideoView::close()
{
    if (brls::Application::getActivitiesStack().size() > 1)
        return brls::Application::popActivity(brls::TransitionAnimation::NONE);

    auto dialog = new brls::Dialog("Sair do aplicativo?");
    dialog->addButton("Cancelar", []() {});
    dialog->addButton("Sair", []() { brls::Application::quit(); });
    dialog->open();
    return false;
}

void VideoView::disableDimming(bool disable)
{
    brls::Application::getPlatform()->disableScreenDimming(disable, "Reproduzindo video", "StreamNX");
}

void VideoView::toggleOSD()
{
    if (this->isOsdShown)
        this->hideOSD();
    else
        this->showOSD(true);
}

void VideoView::showOSD(bool autoHide)
{
    if (autoHide)
        this->osdLastShowTime = brls::getCPUTimeUsec() + VideoView::OSD_SHOW_TIME;
    else
        this->osdLastShowTime = std::numeric_limits<brls::Time>::max();
}

void VideoView::hideOSD()
{
    this->osdLastShowTime = 0;
}

void VideoView::showHint(const std::string& value)
{
    this->hintLabel->setText(value);
    this->hintBox->setVisibility(brls::Visibility::VISIBLE);
    this->hintLastShowTime = brls::getCPUTimeUsec() + VideoView::OSD_SHOW_TIME;
    this->showOSD();
}

bool VideoView::toggleOSDLock()
{
    this->isOsdLock = !this->isOsdLock;
    if (this->isOsdLock)
    {
        this->osdLockIcon->setImageFromSVGRes("icon/player-lock.svg");
        osdTopBox->setVisibility(brls::Visibility::GONE);
        osdBottomBox->setVisibility(brls::Visibility::GONE);
    }
    else
    {
        this->osdLockIcon->setImageFromSVGRes("icon/player-unlock.svg");
    }
    return true;
}

void VideoView::showLoading()
{
    this->osdCenterBox->setVisibility(brls::Visibility::VISIBLE);
    disableDimming(false);
}

void VideoView::hideLoading(bool dimming)
{
    this->osdCenterBox->setVisibility(brls::Visibility::GONE);
    disableDimming(dimming);
}

void VideoView::onChildFocusGained(View* directChild, View* focusedView)
{
    Box::onChildFocusGained(directChild, focusedView);
    if (isOsdLock)
    {
        brls::Application::giveFocus(this->osdLockBox);
        return;
    }
    if (this->isOsdShown)
    {
        lastFocusedView = focusedView;
        return;
    }
    brls::Application::giveFocus(this);
}

void VideoView::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx)
{
    // The actual video frame is drawn by a separate, full-screen PlayerView
    // sibling below this one in the tree (MpvPlayer::draw() fills the whole
    // window and doesn't take a target rect the way StreamNX-main's
    // MPVCore::draw(rect, alpha) does) -- this view is purely the OSD chrome
    // drawn on top of it.
    auto& mpv = MpvPlayer::instance();
    if (!mpv.isValid())
        return;

    double duration = mpv.getDuration();
    double position = mpv.getPosition();

    // Thin always-visible progress line at the very bottom of the screen.
    float progress = duration > 0 ? (float)(position / duration) : 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;
    nvgFillColor(vg, a(bottomBarColor));
    nvgBeginPath(vg);
    nvgRect(vg, x, y + h - 2, w * progress, 2);
    nvgFill(vg);

    brls::Time current = brls::getCPUTimeUsec();
    if (current < this->osdLastShowTime)
    {
        if (!this->isOsdShown)
            this->isOsdShown = true;

        if (!isOsdLock)
        {
            osdTopBox->setVisibility(brls::Visibility::VISIBLE);
            osdBottomBox->setVisibility(brls::Visibility::VISIBLE);
            if (this->seekingRange == 0)
            {
                rightStatusLabel->setText(sec2Time(duration));
                leftStatusLabel->setText(sec2Time(position));
                osdSlider->setProgress(progress);
            }
            toggleIcon->setImageFromSVGRes(mpv.isPaused() ? "icon/ico-play.svg" : "icon/ico-pause.svg");
            osdBottomBox->frame(ctx);
            osdTopBox->frame(ctx);
        }

        osdLockBox->setVisibility(brls::Visibility::VISIBLE);
        osdLockBox->frame(ctx);
    }
    else if (this->isOsdShown)
    {
        this->isOsdShown = false;
        if (isChildFocused())
            brls::Application::giveFocus(this);
        osdTopBox->setVisibility(brls::Visibility::INVISIBLE);
        osdBottomBox->setVisibility(brls::Visibility::INVISIBLE);
        osdLockBox->setVisibility(brls::Visibility::INVISIBLE);
    }

    if (current > this->hintLastShowTime)
    {
        this->hintBox->setVisibility(brls::Visibility::GONE);
        this->hintLastShowTime = 0;
    }

    if (speedHintBox->getVisibility() == brls::Visibility::VISIBLE)
    {
        speedHintBox->frame(ctx);
        brls::Rect frame = speedHintLabel->getFrame();

        int ta1  = (int)(((current >> 10) % 800) * 0.3);
        float tx = frame.getMinX() - 50;
        float ty = frame.getMinY() + 4.5f;

        for (int i = 0; i < 3; i++)
        {
            int offx = (int)tx + i * 15;
            int ta2  = (ta1 + i * 40) % 240;
            if (ta2 > 120)
                ta2 = 240 - ta2;

            nvgBeginPath(vg);
            nvgMoveTo(vg, offx, ty);
            nvgLineTo(vg, offx, ty + 12);
            nvgLineTo(vg, offx + 12, ty + 6);
            nvgFillColor(vg, a(nvgRGBA(255, 255, 255, ta2 + 80)));
            nvgClosePath(vg);
            nvgFill(vg);
        }
    }

    // Loading spinner shows until mpv reports a real duration for the first
    // time (matches StreamNX-main's LOADING_START/LOADING_END events, which
    // this simpler player doesn't have -- polled here instead).
    if (!this->everLoaded && duration > 0)
    {
        this->everLoaded = true;
        // Force the first call through regardless of pause state (nothing
        // to compare `wasPaused` against yet).
        this->wasPaused = !mpv.isPaused();
    }
    if (this->everLoaded)
    {
        // hideLoading's argument is passed straight through to
        // disableDimming() -- true *disables* dimming (screen stays awake),
        // which is what should hold for as long as something is actually
        // playing. `false` here was inverted: it left the system's normal
        // idle/sleep timer running the entire time a video played, which is
        // exactly the "screen goes dark mid-movie" symptom. Only relax it
        // (allow the Switch to sleep normally again) while paused, same as
        // StreamNX-main's own MPV_PAUSE/MPV_RESUME handling.
        bool paused = mpv.isPaused();
        if (paused != this->wasPaused)
        {
            hideLoading(!paused);
            this->wasPaused = paused;
        }
    }
    osdCenterBox->frame(ctx);

    osdInfoBox->frame(ctx);
}
