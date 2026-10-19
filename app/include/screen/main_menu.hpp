#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

class NavItem;

// The app's shell: a navigation bar next to a content area that swaps
// screens. Same class, two layouts:
//
//  - PC (mouse only): a top bar with the brand and one tab per screen.
//    Clicking a tab swaps the screen; there is no focus handling at all.
//
//  - Switch (gamepad): a fixed icon rail on the left. While it has focus it
//    widens over the content to show labels; moving UP/DOWN on it switches
//    screens, RIGHT (or A) enters the screen and collapses it again, and
//    LEFT at a screen's left edge or B goes back to it. The rail is a plain
//    Box -- unlike brls::Sidebar (a scrolling frame) it never scrolls, so
//    the focused item can't be pushed off-screen.
class MainMenu : public brls::Box
{
  public:
    MainMenu();

    brls::View* getDefaultFocus() override;
    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override;

    // Switch: entering the content from the rail / back to the rail.
    void focusContent();
    void focusRail();
    brls::View* contentFocusTarget();
    brls::View* railFocusTarget();

  private:
    using ScreenCreator = std::function<brls::View*(void)>;

    struct Entry
    {
        ScreenCreator creator;
        NavItem* item = nullptr;
    };

    void addScreen(const std::string& label, const char* icon, ScreenCreator creator);
    void showScreen(size_t index);
    void completeInitialFocus(int attemptsLeft);
#ifndef STREAMNX_PC_UI
    void setRailExpanded(bool expanded);
#endif

    std::vector<Entry> entries;
    size_t activeIndex       = (size_t)-1;
    brls::Box* nav           = nullptr; // top bar tabs (PC) / rail (Switch)
    brls::Box* contentArea   = nullptr;
    brls::View* activeScreen = nullptr;
    bool railExpanded        = false;
    brls::Label* brandLabel  = nullptr; // Switch: shown only while the rail is expanded
};
