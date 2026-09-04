#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

// The app's shell: a side menu (Sidebar) that exclusively owns navigation
// while open, next to a content area that swaps screens. Only one thing is
// ever navigable at a time:
//   - Menu open: focus is on the Sidebar; RIGHT (or A on an item) enters
//     the highlighted screen and closes the menu.
//   - Menu closed: focus is confined to the active screen; navigating past
//     its left edge does NOT leak back into the (hidden) menu -- only Y or
//     the screen's own B re-opens it.
class MainMenu : public brls::Box
{
  public:
    MainMenu();

    // Always land on "Home" -- overridden because the generic Box
    // resolution (last-focused child) isn't reliable before the very first
    // layout pass.
    brls::View* getDefaultFocus() override;

    // Detects focus actually entering the active screen (vs. still moving
    // around the Sidebar) so the menu can close itself at that point.
    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override;

  private:
    using ScreenCreator = std::function<brls::View*(void)>;

    void addScreen(const std::string& label, ScreenCreator creator);
    void openSidebar();
    void closeSidebar();
    void completeInitialFocus(int attemptsLeft);

    brls::Sidebar* sidebar   = nullptr;
    brls::Box* row           = nullptr;
    brls::View* activeScreen = nullptr;
    bool sidebarOpen         = true;
};
