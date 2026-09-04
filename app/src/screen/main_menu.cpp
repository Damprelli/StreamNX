#include "screen/main_menu.hpp"
#include "screen/home_screen.hpp"
#include "screen/search_screen.hpp"
#include "screen/addons_screen.hpp"

namespace
{

// The Box holding [Sidebar, content]. Overridden so LEFT never bubbles
// content -> sidebar: crossing that boundary is only ever allowed through
// MainMenu's own explicit open/close logic (Y, B, or entering a screen),
// never by a stray press at the left edge of whatever screen is focused.
class ShellRow : public brls::Box
{
  public:
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override
    {
        if (direction == brls::FocusDirection::LEFT)
            return nullptr;
        return brls::Box::getNextFocus(direction, currentView);
    }
};

} // namespace

void MainMenu::addScreen(const std::string& label, ScreenCreator creator)
{
    this->sidebar->addItem(label, [this, creator](brls::View* view) {
        // Sidebar fires this for every item on every focus change inside it
        // (see Sidebar/SidebarItem) -- only react when it's this item that
        // just became focused, exactly like TabFrame::addTab does.
        if (!view->isFocused())
            return;

        if (this->activeScreen)
        {
            this->row->removeView(this->activeScreen);
            this->activeScreen = nullptr;
        }

        brls::View* newContent = creator();
        if (!newContent)
            return;

        newContent->setGrow(1.0f);
        this->row->addView(newContent);
        this->activeScreen = newContent;

        // B always re-opens the menu (closing happens automatically, via
        // onChildFocusGained, the moment focus actually enters a screen).
        // Hidden from the footer: Y ("Abrir Menu", registered once on the
        // shell) already advertises this, and screens with several of
        // their own row-level hints (Addons) were wrapping/truncating the
        // footer with both shown.
        newContent->registerAction(
            "Menu", brls::BUTTON_B, [this](brls::View*) {
                this->openSidebar();
                return true;
            },
            true, false, brls::SOUND_BACK);
    });
}

void MainMenu::openSidebar()
{
    this->sidebarOpen = true;
    this->sidebar->setVisibility(brls::Visibility::VISIBLE);
    brls::Application::giveFocus(this->sidebar);
}

void MainMenu::closeSidebar()
{
    this->sidebarOpen = false;
    this->sidebar->setVisibility(brls::Visibility::GONE);
}

void MainMenu::onChildFocusGained(brls::View* directChild, brls::View* focusedView)
{
    brls::Box::onChildFocusGained(directChild, focusedView);

    if (!this->sidebarOpen || !this->activeScreen)
        return;

    // Focus landed inside the active screen (as opposed to still moving
    // around the Sidebar) -- that's "entering" it: close the menu so
    // navigation is confined to the screen alone from here on.
    for (brls::View* v = focusedView; v != nullptr; v = v->getParent())
    {
        if (v == this->activeScreen)
        {
            this->closeSidebar();
            return;
        }
    }
}

MainMenu::MainMenu()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->setBackgroundColor(nvgRGB(16, 18, 24));

    this->row = new ShellRow();
    this->row->setAxis(brls::Axis::ROW);
    this->row->setGrow(1.0f);

    this->sidebar = new brls::Sidebar();
    this->sidebar->setWidth(brls::getStyle()["brls/tab_frame/sidebar_width"]);
    this->sidebar->setHeight(brls::View::AUTO);
    this->row->addView(this->sidebar);

    this->addView(this->row);

    // One footer for the whole shell: lists whatever the focused
    // screen/menu item registered (A "Abrir", B/Y menu hints, + Exit).
    this->addView(new brls::BottomBar());

    this->addScreen("⌂ Home", []() -> brls::View* { return new HomeScreen(); });
    this->addScreen("• Buscar Filmes/Series", []() -> brls::View* { return new SearchScreen(); });
    this->addScreen("• Addons", []() -> brls::View* { return new AddonsScreen(); });

    // Y opens the side menu (same as B from inside a screen) -- always
    // reachable since it's registered on the shell itself.
    this->registerAction("Abrir Menu", brls::BUTTON_Y, [this](brls::View*) {
        this->openSidebar();
        return true;
    });

    // pushActivity() resolves its own default focus right after this
    // constructor returns, and that first pass can land past "Home" (the
    // sidebar's own focus bookkeeping isn't settled before the first
    // layout) -- force it back once that's done, then land directly on
    // Home's content with the menu already closed.
    brls::sync([this]() {
        brls::Application::giveFocus(this->sidebar->getItem(0));
        this->completeInitialFocus(180);
    });
}

void MainMenu::completeInitialFocus(int attemptsLeft)
{
    // Home is still showing its own LoadingGate at this point (it takes a
    // few hundred ms; this runs one frame after construction), so its
    // first focusable card doesn't exist yet -- retry next frame instead
    // of closing the menu and leaving focus stranded on the sidebar item
    // we just hid, which is what silently happened before: giveFocus(null)
    // is a no-op, so the "cursor" stayed on the now-invisible menu forever.
    brls::View* focus = this->activeScreen ? this->activeScreen->getDefaultFocus() : nullptr;

    if (!focus && attemptsLeft > 0)
    {
        brls::sync([this, attemptsLeft]() { this->completeInitialFocus(attemptsLeft - 1); });
        return;
    }

    this->closeSidebar();
    if (focus)
        brls::Application::giveFocus(focus);
}

brls::View* MainMenu::getDefaultFocus()
{
    return this->sidebar->getItem(0);
}
