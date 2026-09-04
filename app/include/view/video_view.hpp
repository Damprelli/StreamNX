#pragma once

#include <borealis.hpp>
#include <functional>

class VideoProgressSlider;
class SVGImage;

// Full OSD-driven player control surface, ported from StreamNX-main's own
// VideoView (same button scheme, same auto-hiding on-screen overlay, same
// touch/gamepad gestures) and adapted to this project's MpvPlayer. Trimmed
// out relative to the original: casting, danmaku, per-episode/quality
// selection and TV mode -- none of those have a data source in this app
// (episodes are picked in DetailScreen before the player ever opens; there's
// no quality-variant or danmaku feed to select from).
class VideoView : public brls::Box
{
  public:
    VideoView();
    ~VideoView() override;

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;

    View* getDefaultFocus() override { return this->isOsdShown ? this->btnToggle : this; }

    void onChildFocusGained(View* directChild, View* focusedView) override;

    View* getNextFocus(brls::FocusDirection direction, View* currentView) override { return this; }

    void setTitie(const std::string& title);

    brls::VoidEvent* getSettingEvent() { return &this->settingEvent; }

    void showOSD(bool autoHide = true);

    static bool close();

  private:
    BRLS_BIND(brls::Label, titleLabel, "video/osd/title");
    BRLS_BIND(brls::Box, btnSetting, "video/osd/setting");
    BRLS_BIND(brls::Box, btnToggle, "video/osd/toggle");
    BRLS_BIND(brls::Box, btnVideoSpeed, "video/speed/box");
    BRLS_BIND(brls::Box, btnVolume, "video/osd/volume");
    BRLS_BIND(brls::Box, btnClose, "video/close/box");
    BRLS_BIND(brls::Box, osdLockBox, "video/osd/lock/box");
    BRLS_BIND(brls::Box, iconBox, "video/osd/icon/box");
    BRLS_BIND(SVGImage, osdLockIcon, "video/osd/lock/icon");
    BRLS_BIND(SVGImage, toggleIcon, "video/osd/toggle/icon");
    BRLS_BIND(SVGImage, volumeIcon, "video/osd/volume/icon");
    BRLS_BIND(SVGImage, osdSettingIcon, "video/osd/setting/icon");
    BRLS_BIND(brls::Box, osdTopBox, "video/osd/top/box");
    BRLS_BIND(brls::Box, osdBottomBox, "video/osd/bottom/box");
    BRLS_BIND(brls::Box, osdCenterBox, "video/osd/center/box");
    BRLS_BIND(brls::ProgressSpinner, osdSpinner, "video/osd/loading");
    BRLS_BIND(brls::Box, osdInfoBox, "video/osd/info/box");
    BRLS_BIND(brls::Label, infoLabel, "video/osd/info/label");
    BRLS_BIND(SVGImage, infoIcon, "video/osd/info/icon");
    BRLS_BIND(VideoProgressSlider, osdSlider, "video/osd/bottom/progress");
    BRLS_BIND(brls::Label, leftStatusLabel, "video/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "video/right/status");
    BRLS_BIND(brls::Label, videoSpeedLabel, "video/speed/label");
    BRLS_BIND(brls::Label, speedHintLabel, "video/speed/hint/label");
    BRLS_BIND(brls::Box, speedHintBox, "video/speed/hint/box");
    BRLS_BIND(brls::Label, hintLabel, "video/osd/hint/label");
    BRLS_BIND(brls::Box, hintBox, "video/osd/hint/box");

    void showLoading();
    void hideLoading(bool dimming = true);
    void toggleOSD();
    void hideOSD();
    bool toggleOSDLock();
    bool toggleSpeed();
    bool toggleVolume(brls::View* view);
    void showHint(const std::string& value);

    void requestSeeking(int seek, int delay = 400);
    void requestVolume(int value, int delay = 400);
    void requestBrightness(float value);
    static void disableDimming(bool disable);

    brls::VoidEvent settingEvent;
    View* lastFocusedView = nullptr;

    bool isOsdShown         = false;
    bool isOsdLock          = false;
    brls::Time osdLastShowTime  = 0;
    brls::Time hintLastShowTime = 0;
    const brls::Time OSD_SHOW_TIME = 5000000; // 5s

    int64_t seekingRange = 0;
    size_t seekingIter   = 0;

    bool wasPaused = false;
    bool everLoaded = false;

    brls::Rect oldRect = brls::Rect(-1, -1, -1, -1);
    brls::InputManager* input = nullptr;

    NVGcolor bottomBarColor = nvgRGB(200, 60, 60);

    size_t volumeIter = 0;
    int volumeInit     = 0;
    float brightnessInit = 0;
};
