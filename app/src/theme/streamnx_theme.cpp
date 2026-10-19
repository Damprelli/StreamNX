#include "theme/streamnx_theme.hpp"

#include <borealis.hpp>

void streamnx_theme::install()
{
    brls::Theme& theme = brls::Theme::getDarkTheme();

    // Near-black neutral background, raised surfaces in two steps, one
    // saturated accent for "active/primary" -- same on PC and Switch.
    theme.addColor("streamnx/bg", nvgRGB(12, 13, 18));
    theme.addColor("streamnx/surface", nvgRGB(24, 26, 34));
    theme.addColor("streamnx/surface_hover", nvgRGB(36, 39, 50));
    theme.addColor("streamnx/surface_raised", nvgRGB(30, 33, 43));
    theme.addColor("streamnx/accent", nvgRGB(99, 116, 255));
    theme.addColor("streamnx/accent_hover", nvgRGB(125, 140, 255));
    theme.addColor("streamnx/text", nvgRGB(240, 241, 245));
    theme.addColor("streamnx/text_muted", nvgRGB(150, 156, 170));
    theme.addColor("streamnx/border", nvgRGB(40, 44, 56));
    theme.addColor("streamnx/danger", nvgRGB(232, 74, 102));
    theme.addColor("streamnx/danger_hover", nvgRGB(245, 100, 125));
    theme.addColor("streamnx/success", nvgRGB(72, 199, 142));
    theme.addColor("streamnx/warning", nvgRGB(245, 166, 35));
}

float streamnx_theme::contentAreaWidth()
{
#ifdef STREAMNX_PC_UI
    return brls::Application::contentWidth - 2 * screenPaddingX;
#else
    return brls::Application::contentWidth - railCollapsedWidth - 2 * screenPaddingX;
#endif
}
