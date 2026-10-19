#include "screen/player_settings_screen.hpp"
#include "api/mpv_player.hpp"
#include "utils/mouse.hpp"
#include "view/ui_kit.hpp"

#include <cstdio>

namespace
{

constexpr float ROW_WIDTH = 520;

// One row: a small section-colored accent, a label, and a value flanked by
// chevrons (so it reads as "this is adjustable", not just static text) --
// LEFT/RIGHT changes it.
class SettingRow : public brls::Box
{
  public:
    SettingRow(const std::string& label, NVGcolor accent, brls::Label** outValueLabel,
        std::function<void(int)> onAdjust, const std::string& leftHint = "-", const std::string& rightHint = "+")
    {
        this->setAxis(brls::Axis::ROW);
        this->setFocusable(true);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->setWidth(ROW_WIDTH);
        this->setPadding(16, 22, 16, 22);
        this->setMarginBottom(10);
        this->setCornerRadius(10);
        this->setBackgroundColor(nvgRGB(24, 26, 33));

        auto* accentBar = new brls::Rectangle();
        accentBar->setColor(accent);
        accentBar->setDimensions(4, 22);
        accentBar->setCornerRadius(2);
        accentBar->setMarginRight(16);
        this->addView(accentBar);

        auto* titleLabel = new brls::Label();
        titleLabel->setText(label);
        titleLabel->setFontSize(17);
        titleLabel->setGrow(1.0f);
        this->addView(titleLabel);

        auto* valueRow = new brls::Box();
        valueRow->setAxis(brls::Axis::ROW);
        valueRow->setAlignItems(brls::AlignItems::CENTER);

        auto* leftChevron = makeChevron("<");
        leftChevron->setMarginRight(4);
        valueRow->addView(leftChevron);

        auto* valueLabel = new brls::Label();
        valueLabel->setFontSize(17);
        valueLabel->setTextColor(accent);
        valueLabel->setWidth(110);
        valueLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        valueRow->addView(valueLabel);
        *outValueLabel = valueLabel;

        auto* rightChevron = makeChevron(">");
        rightChevron->setMarginLeft(4);
        valueRow->addView(rightChevron);

        this->addView(valueRow);

        this->registerAction(
            leftHint, brls::BUTTON_LEFT, [onAdjust](brls::View*) { onAdjust(-1); return true; }, false, true);
        this->registerAction(
            rightHint, brls::BUTTON_RIGHT, [onAdjust](brls::View*) { onAdjust(1); return true; }, false, true);

#ifdef STREAMNX_PC_UI
        // LEFT/RIGHT have no click equivalent -- the chevrons themselves
        // become their own clickable targets instead (a plain Label has no
        // BUTTON_A action for makeClickable(view) to reuse, so each gets its
        // own click action directly).
        makeClickable(leftChevron, [onAdjust](brls::View*) { onAdjust(-1); return true; });
        makeClickable(rightChevron, [onAdjust](brls::View*) { onAdjust(1); return true; });
#endif
    }

  private:
    // A small focusable hit target around a "<"/">" label -- bare labels
    // have no padding to click on comfortably.
    static brls::Box* makeChevron(const std::string& symbol)
    {
        auto* box = new HoverBox();
        box->setHoverColors(nvgRGBA(0, 0, 0, 0), ui::color("surface_hover"));
        box->setFocusable(true);
        box->setDimensions(brls::View::AUTO, brls::View::AUTO);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setPadding(4, 10, 4, 10);
        box->setCornerRadius(6);

        auto* label = new brls::Label();
        label->setText(symbol);
        label->setFontSize(20);
        label->setTextColor(nvgRGB(110, 120, 140));
        box->addView(label);

        return box;
    }
};

brls::Label* makeSectionLabel(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(13);
    label->setTextColor(nvgRGB(130, 135, 145));
    label->setMarginBottom(8);
    label->setMarginTop(18);
    label->setWidth(ROW_WIDTH);
    return label;
}

std::string formatSpeed(double speed)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2gx", speed);
    return buf;
}

std::string formatDelay(double seconds)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fs", seconds);
    return buf;
}

std::string trackLabel(const MpvPlayer::TrackInfo& t)
{
    if (!t.title.empty())
        return t.title;
    if (!t.lang.empty())
        return t.lang;
    return "Faixa " + std::to_string(t.id);
}

const NVGcolor ACCENT_AUDIO = nvgRGB(90, 170, 250);
const NVGcolor ACCENT_SUB   = nvgRGB(250, 170, 90);
const NVGcolor ACCENT_SPEED = nvgRGB(120, 220, 150);
const NVGcolor ACCENT_DELAY = nvgRGB(220, 120, 200);

} // namespace

PlayerSettingsScreen::PlayerSettingsScreen()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(40, 50, 40, 50);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->setBackgroundColor(nvgRGB(10, 11, 15));

    auto* title = new brls::Label();
    title->setText("Opcoes de reproducao");
    title->setFontSize(28);
    title->setWidth(ROW_WIDTH);
    this->addView(title);

    auto* hint = new brls::Label();
#ifdef STREAMNX_PC_UI
    hint->setText("Clique nas setas para ajustar  ·  Esc para voltar");
#else
    hint->setText("Use < > para ajustar, B para voltar");
#endif
    hint->setFontSize(14);
    hint->setTextColor(nvgRGB(130, 135, 145));
    hint->setMarginTop(6);
    hint->setWidth(ROW_WIDTH);
    this->addView(hint);

    this->addView(makeSectionLabel("FAIXAS"));

    auto* audioRow = new SettingRow(
        "Audio", ACCENT_AUDIO, &this->audioValue, [this](int dir) { (void)dir; this->cycleTrack("audio"); },
        "Anterior", "Proximo");
    auto* subRow = new SettingRow(
        "Legenda", ACCENT_SUB, &this->subValue, [this](int dir) { (void)dir; this->cycleTrack("sub"); }, "Anterior",
        "Proximo");
    this->addView(audioRow);
    this->addView(subRow);

    this->addView(makeSectionLabel("REPRODUCAO"));

    auto* speedRow = new SettingRow(
        "Velocidade", ACCENT_SPEED, &this->speedValue, [this](int dir) { this->adjustSpeed(dir * 0.25); });
    auto* subDelayRow = new SettingRow(
        "Sincronia da legenda", ACCENT_DELAY, &this->subDelayValue, [this](int dir) { this->adjustSubDelay(dir * 0.5); });
    this->addView(speedRow);
    this->addView(subDelayRow);

#ifdef STREAMNX_PC_UI
    auto* closeButton = ui::button("Fechar", ui::ButtonStyle::Secondary, []() { brls::Application::popActivity(); },
        ui::icons::close);
    closeButton->setMarginTop(26);
    this->addView(closeButton);
#else
    this->addView(new brls::BottomBar());
#endif

    this->registerAction(
        "Voltar", brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        },
        false, false, brls::SOUND_BACK);

    this->refreshLabels();
    brls::Application::giveFocus(audioRow);
}

void PlayerSettingsScreen::refreshLabels()
{
    auto& mpv = MpvPlayer::instance();

    int64_t currentAudio = mpv.getCurrentTrack("audio");
    this->audioValue->setText("Padrao");
    for (auto& t : mpv.getTracks("audio"))
        if (t.id == currentAudio)
            this->audioValue->setText(trackLabel(t));

    int64_t currentSub = mpv.getCurrentTrack("sub");
    this->subValue->setText(currentSub <= 0 ? "Desativada" : "Faixa " + std::to_string(currentSub));
    for (auto& t : mpv.getTracks("sub"))
        if (t.id == currentSub)
            this->subValue->setText(trackLabel(t));

    this->speedValue->setText(formatSpeed(mpv.getSpeed()));
    this->subDelayValue->setText(formatDelay(mpv.getSubDelay()));
}

void PlayerSettingsScreen::cycleTrack(const std::string& type)
{
    auto& mpv       = MpvPlayer::instance();
    auto tracks     = mpv.getTracks(type);
    int64_t current = mpv.getCurrentTrack(type);

    // Subtitles additionally cycle through "off" (id 0), which never
    // appears in the track list itself.
    std::vector<int64_t> ids;
    if (type == "sub")
        ids.push_back(0);
    for (auto& t : tracks)
        ids.push_back(t.id);

    if (ids.empty())
        return;

    size_t idx = 0;
    for (size_t i = 0; i < ids.size(); i++)
        if (ids[i] == current)
            idx = i;

    idx = (idx + 1) % ids.size();
    mpv.setTrack(type, ids[idx]);

    // mpv applies the track switch asynchronously; the label refresh runs
    // on the next tick so it reads back the value that's actually current.
    brls::sync([this]() { this->refreshLabels(); });
}

void PlayerSettingsScreen::adjustSpeed(double delta)
{
    auto& mpv    = MpvPlayer::instance();
    double speed = mpv.getSpeed() + delta;
    if (speed < 0.25)
        speed = 0.25;
    if (speed > 3.0)
        speed = 3.0;
    mpv.setSpeed(speed);
    this->speedValue->setText(formatSpeed(speed));
}

void PlayerSettingsScreen::adjustSubDelay(double delta)
{
    auto& mpv    = MpvPlayer::instance();
    double delay = mpv.getSubDelay() + delta;
    mpv.setSubDelay(delay);
    this->subDelayValue->setText(formatDelay(delay));
}
