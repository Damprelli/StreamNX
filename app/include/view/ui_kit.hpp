#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

#include "utils/hover.hpp"

// Small shared building blocks so every screen looks the same: theme
// colors, labels, Material icon glyphs and buttons. Buttons are HoverBoxes:
// on PC they react to the mouse (hover color, click); on Switch they're
// ordinary focusable views (A / touch).
namespace ui
{

// "streamnx/<key>" from the theme (see streamnx_theme::install).
NVGcolor color(const std::string& key);

brls::Label* label(const std::string& text, float size, NVGcolor color);

// Material Icons glyphs -- the font is loaded as a fallback of the regular
// font on every platform, so a Label renders these directly.
namespace icons
{
    constexpr const char* home       = "";
    constexpr const char* search     = "";
    constexpr const char* download   = "";
    constexpr const char* extension  = "";
    constexpr const char* play       = "";
    constexpr const char* trash      = "";
    constexpr const char* close      = "";
    constexpr const char* back       = "";
    constexpr const char* add        = "";
    constexpr const char* battery    = "";
    constexpr const char* charging   = "";
    constexpr const char* storage    = "";
    constexpr const char* schedule   = "";
    constexpr const char* check      = "";
    constexpr const char* error      = "";
    constexpr const char* movie      = "";
    constexpr const char* person     = "";
    constexpr const char* sync       = "";
    constexpr const char* logout     = "";
}

brls::Label* icon(const char* glyph, float size, NVGcolor color);

enum class ButtonStyle
{
    Primary,   // accent background
    Secondary, // surface background
    Danger,    // red background
};

// A rounded, clickable button: optional icon + text. Clickable/hoverable
// on PC, focusable (A) and tappable on Switch.
HoverBox* button(const std::string& text, ButtonStyle style, std::function<void()> onClick, const char* iconGlyph = nullptr);

// PC: top strip for screens opened on top of the shell (Detail, Streams)
// -- a "Voltar" button, since there's no gamepad B there.
brls::Box* backBar();

// Adds `view` to `parent` just above its BottomBar footer, if it has one
// (Switch) -- for content built after the screen's constructor already
// added the footer, which otherwise ends up below... the footer.
void addAboveFooter(brls::Box* parent, brls::View* view);

// Centered placeholder for an empty list: big icon, title, one-line hint.
brls::Box* emptyState(const char* iconGlyph, const std::string& title, const std::string& hint);

// A box that turns lighter under the mouse (PC) -- for clickable rows and
// cards that aren't buttons.
void makeHoverSurface(HoverBox* box, NVGcolor normal, NVGcolor hovered);

} // namespace ui
