#pragma once

// Registers the app's custom "streamnx/..." theme keys on top of borealis'
// built-in dark theme -- one palette for every platform; what differs
// between PC and Switch is layout/navigation (see MainMenu), not colors.
// Must run before any XML that references "@theme/streamnx/..." is first
// inflated.
namespace streamnx_theme
{
    void install();

    // Horizontal padding every top-level screen uses on each side.
    constexpr float screenPaddingX = 50.0f;

#ifdef STREAMNX_PC_UI
    // PC shell: navigation lives in a top bar, content gets the full width.
    constexpr float topBarHeight = 64.0f;
#else
    // Switch shell: a fixed icon rail on the left that widens (over the
    // content) to show labels while it has focus.
    constexpr float railCollapsedWidth = 88.0f;
    constexpr float railExpandedWidth  = 260.0f;
#endif

    // Width actually available to a screen's content (window minus the
    // shell's own chrome minus the screen's side padding) -- for screens
    // that lay out a grid themselves.
    float contentAreaWidth();
}
