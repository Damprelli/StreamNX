#include "screen/main_menu.hpp"
#include "screen/home_screen.hpp"
#include "screen/search_screen.hpp"
#include "screen/addons_screen.hpp"
#include "screen/downloads_screen.hpp"
#include "screen/profile_screen.hpp"
#include "theme/streamnx_theme.hpp"
#include "utils/mouse.hpp"
#include "view/ui_kit.hpp"

#include <cstdlib>

// One entry of the navigation: icon + label, an "active" marker for the
// screen being shown. A tab in the PC top bar, a row of the Switch rail.
class NavItem : public HoverBox
{
  public:
    NavItem(const char* glyph, const std::string& text)
    {
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setFocusable(true);

#ifdef STREAMNX_PC_UI
        this->setHeight(streamnx_theme::topBarHeight);
        this->setPadding(0, 18, 0, 18);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->iconLabel = ui::icon(glyph, 20, ui::color("text_muted"));
        this->iconLabel->setMarginRight(8);
        this->textLabel = ui::label(text, 16, ui::color("text_muted"));

        // Underline under the active tab.
        this->indicator = new brls::Box();
        this->indicator->setPositionType(brls::PositionType::ABSOLUTE);
        this->indicator->setPositionBottom(0);
        this->indicator->setPositionLeft(14);
        this->indicator->setPositionRight(14);
        this->indicator->setHeight(3);
        this->indicator->setCornerRadius(2);
        this->indicator->setBackgroundColor(ui::color("accent"));

        this->onHover([this](bool hovered) {
            this->hovered = hovered;
            this->refresh();
        });
#else
        this->setHeight(56);
        this->setMarginBottom(6);
        this->setCornerRadius(12);
        this->setHighlightCornerRadius(12);
        this->setPadding(0, 0, 0, 22);
        this->iconLabel = ui::icon(glyph, 28, ui::color("text_muted"));
        this->iconLabel->setWidth(44);
        this->textLabel = ui::label(text, 18, ui::color("text_muted"));
        this->textLabel->setMarginLeft(16);
        this->textLabel->setSingleLine(true);

        // Pill on the left edge of the active screen's row.
        this->indicator = new brls::Box();
        this->indicator->setPositionType(brls::PositionType::ABSOLUTE);
        this->indicator->setPositionLeft(4);
        this->indicator->setPositionTop(14);
        this->indicator->setWidth(4);
        this->indicator->setHeight(28);
        this->indicator->setCornerRadius(2);
        this->indicator->setBackgroundColor(ui::color("accent"));
#endif
        this->addView(this->iconLabel);
        this->addView(this->textLabel);
        this->addView(this->indicator);
        this->refresh();
    }

    void setActive(bool active)
    {
        this->active = active;
        this->refresh();
    }

    void setExpanded(bool expanded)
    {
        this->textLabel->setVisibility(expanded ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    // Switch: highlighting a rail row shows its screen right away.
    void onFocusGained() override
    {
        HoverBox::onFocusGained();
        if (this->onFocus)
            this->onFocus();
    }

    std::function<void()> onFocus;

  private:
    void refresh()
    {
        bool lit       = this->active || this->hovered;
        NVGcolor color = this->active ? ui::color("text") : lit ? ui::color("text") : ui::color("text_muted");
        this->iconLabel->setTextColor(this->active ? ui::color("accent") : color);
        this->textLabel->setTextColor(color);
        this->indicator->setVisibility(this->active ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
#ifdef STREAMNX_PC_UI
        this->setBackgroundColor(this->hovered ? ui::color("surface_hover") : nvgRGBA(0, 0, 0, 0));
#else
        this->setBackgroundColor(this->active ? ui::color("surface_raised") : nvgRGBA(0, 0, 0, 0));
#endif
    }

    brls::Label* iconLabel = nullptr;
    brls::Label* textLabel = nullptr;
    brls::Box* indicator   = nullptr;
    bool active            = false;
    bool hovered           = false;
};

namespace
{

#ifndef STREAMNX_PC_UI
// Row holding [content, rail]. Crossing between the two only happens here,
// explicitly: RIGHT from the rail enters the active screen, LEFT at a
// screen's left edge returns to the rail. Everything else stays inside
// whichever of the two has focus.
class ShellBody : public brls::Box
{
  public:
    ShellBody(MainMenu* menu, brls::Box* content, brls::Box* rail)
        : menu(menu)
        , content(content)
        , rail(rail)
    {
    }

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override
    {
        // Returned (not given directly) so borealis' own navigation moves
        // focus, with the normal sound -- nullptr means "stay put".
        if (currentView == this->rail && direction == brls::FocusDirection::RIGHT)
            return this->menu->contentFocusTarget();
        if (currentView == this->content && direction == brls::FocusDirection::LEFT)
            return this->menu->railFocusTarget();
        return nullptr;
    }

  private:
    MainMenu* menu;
    brls::Box* content;
    brls::Box* rail;
};
#endif

#ifndef STREAMNX_PC_UI
bool isInside(brls::View* view, brls::View* ancestor)
{
    for (brls::View* v = view; v != nullptr; v = v->getParent())
        if (v == ancestor)
            return true;
    return false;
}
#endif

} // namespace

MainMenu::MainMenu()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->setBackgroundColor(ui::color("bg"));

    this->contentArea = new brls::Box();
    this->contentArea->setAxis(brls::Axis::COLUMN);
    this->contentArea->setGrow(1.0f);

#ifdef STREAMNX_PC_UI
    // ---- PC: top bar + full-width content ----
    auto* topBar = new brls::Box();
    topBar->setAxis(brls::Axis::ROW);
    topBar->setAlignItems(brls::AlignItems::CENTER);
    topBar->setHeight(streamnx_theme::topBarHeight);
    topBar->setPadding(0, 32, 0, 32);
    topBar->setBackgroundColor(ui::color("surface"));

    auto* brandIcon = new brls::Image();
    brandIcon->setImageFromRes("img/logo_small.png");
    brandIcon->setDimensions(34, 34);
    brandIcon->setMarginRight(10);
    topBar->addView(brandIcon);
    auto* brand = ui::label("StreamNX", 21, ui::color("text"));
    brand->setMarginRight(36);
    topBar->addView(brand);

    this->nav = new brls::Box();
    this->nav->setAxis(brls::Axis::ROW);
    this->nav->setHeight(streamnx_theme::topBarHeight);
    topBar->addView(this->nav);

    this->addView(topBar);
    this->addView(this->contentArea);
#else
    // ---- Switch: icon rail over the content's left edge ----
    this->contentArea->setMarginLeft(streamnx_theme::railCollapsedWidth);

    this->nav = new brls::Box();
    this->nav->setAxis(brls::Axis::COLUMN);
    this->nav->setPositionType(brls::PositionType::ABSOLUTE);
    this->nav->setPositionLeft(0);
    this->nav->setPositionTop(0);
    this->nav->setPositionBottom(0);
    this->nav->setPadding(26, 12, 26, 12);
    this->nav->setBackgroundColor(ui::color("surface"));

    auto* brandRow = new brls::Box();
    brandRow->setAxis(brls::Axis::ROW);
    brandRow->setAlignItems(brls::AlignItems::CENTER);
    brandRow->setPadding(0, 0, 0, 22);
    brandRow->setMarginBottom(28);
    brandRow->setHeight(40);
    auto* brandIcon = new brls::Image();
    brandIcon->setImageFromRes("img/logo_small.png");
    brandIcon->setDimensions(40, 40);
    brandIcon->setMarginLeft(2);
    brandRow->addView(brandIcon);
    this->brandLabel = ui::label("StreamNX", 20, ui::color("text"));
    this->brandLabel->setMarginLeft(16);
    brandRow->addView(this->brandLabel);
    this->nav->addView(brandRow);

    // Content first, rail second: the rail draws (and receives taps) on top
    // of the content when it widens.
    auto* body = new ShellBody(this, this->contentArea, this->nav);
    body->setAxis(brls::Axis::ROW);
    body->setGrow(1.0f);
    body->addView(this->contentArea);
    body->addView(this->nav);
    this->addView(body);

    // One footer for the whole shell: lists whatever the focused view
    // registered (A, B, + Sair...).
    this->addView(new brls::BottomBar());
#endif

    this->addScreen("Inicio", ui::icons::home, []() -> brls::View* { return new HomeScreen(); });
    this->addScreen("Buscar", ui::icons::search, []() -> brls::View* { return new SearchScreen(); });
    this->addScreen("Downloads", ui::icons::download, []() -> brls::View* { return new DownloadsScreen(); });
    this->addScreen("Addons", ui::icons::extension, []() -> brls::View* { return new AddonsScreen(); });
    this->addScreen("Perfil", ui::icons::person, []() -> brls::View* { return new ProfileScreen(); });

#ifndef STREAMNX_PC_UI
    this->setRailExpanded(false);
#endif
    // Development aid: STREAMNX_START_SCREEN=<index> opens on another tab
    // (0 Inicio, 1 Buscar, 2 Downloads, 3 Addons, 4 Perfil). Unset = Inicio.
    size_t startScreen = 0;
    if (const char* start = std::getenv("STREAMNX_START_SCREEN"))
        startScreen = (size_t)std::strtoul(start, nullptr, 10);
    this->showScreen(startScreen < this->entries.size() ? startScreen : 0);

#ifndef STREAMNX_PC_UI
    // Land straight on Home's content (rail collapsed) once it has built
    // something focusable -- see completeInitialFocus().
    brls::sync([this]() { this->completeInitialFocus(180); });
#endif
}

void MainMenu::addScreen(const std::string& label, const char* icon, ScreenCreator creator)
{
    size_t index = this->entries.size();
    auto* item   = new NavItem(icon, label);

#ifdef STREAMNX_PC_UI
    makeClickable(item, [this, index](brls::View*) {
        this->showScreen(index);
        return true;
    });
#else
    item->onFocus = [this, index]() { this->showScreen(index); };
    // A on a rail row = enter that screen; touch works the same way.
    makeClickable(item, [this, index](brls::View*) {
        this->showScreen(index);
        this->focusContent();
        return true;
    });
#endif

    this->nav->addView(item);
    this->entries.push_back(Entry { creator, item });
}

void MainMenu::showScreen(size_t index)
{
    if (index >= this->entries.size() || index == this->activeIndex)
        return;

    if (this->activeScreen)
    {
        this->contentArea->removeView(this->activeScreen);
        this->activeScreen = nullptr;
    }
    if (this->activeIndex < this->entries.size())
        this->entries[this->activeIndex].item->setActive(false);

    this->activeIndex = index;
    this->entries[index].item->setActive(true);

    brls::View* screen = this->entries[index].creator();
    if (!screen)
        return;
    screen->setGrow(1.0f);
    this->contentArea->addView(screen);
    this->activeScreen = screen;

#ifndef STREAMNX_PC_UI
    // B from anywhere inside a screen goes back to the rail. Hidden from
    // the footer -- it's the universal "back" there anyway.
    screen->registerAction(
        "Menu", brls::BUTTON_B, [this](brls::View*) {
            this->focusRail();
            return true;
        },
        true, false, brls::SOUND_BACK);
#endif
}

brls::View* MainMenu::contentFocusTarget()
{
    if (!this->activeScreen)
        return nullptr;

    // Several screens are focusable themselves, only so they can park focus
    // while rebuilding their own lists -- and brls::Box::getDefaultFocus()
    // returns a focusable box itself before looking at its children. Landing
    // there leaves focus on an invisible, empty-handed screen ("focus outside
    // the screen"), so look past it for its first real focusable child.
    brls::View* focus = this->activeScreen->getDefaultFocus();
    if (focus == this->activeScreen)
    {
        focus = nullptr;
        if (auto* box = dynamic_cast<brls::Box*>(this->activeScreen))
            for (brls::View* child : box->getChildren())
                if ((focus = child->getDefaultFocus()))
                    break;
    }
    return focus;
}

brls::View* MainMenu::railFocusTarget()
{
    return this->activeIndex < this->entries.size() ? this->entries[this->activeIndex].item : nullptr;
}

void MainMenu::focusContent()
{
    if (brls::View* focus = this->contentFocusTarget())
        brls::Application::giveFocus(focus);
}

void MainMenu::focusRail()
{
    if (brls::View* item = this->railFocusTarget())
        brls::Application::giveFocus(item);
}

void MainMenu::onChildFocusGained(brls::View* directChild, brls::View* focusedView)
{
    brls::Box::onChildFocusGained(directChild, focusedView);
#ifndef STREAMNX_PC_UI
    this->setRailExpanded(isInside(focusedView, this->nav));
#endif
}

#ifndef STREAMNX_PC_UI
void MainMenu::setRailExpanded(bool expanded)
{
    this->railExpanded = expanded;
    this->nav->setWidth(expanded ? streamnx_theme::railExpandedWidth : streamnx_theme::railCollapsedWidth);
    this->nav->setShadowType(expanded ? brls::ShadowType::GENERIC : brls::ShadowType::NONE);
    this->nav->setShadowVisibility(expanded);
    this->brandLabel->setVisibility(expanded ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    for (auto& entry : this->entries)
        entry.item->setExpanded(expanded);
}
#endif

void MainMenu::completeInitialFocus(int attemptsLeft)
{
    // Home is still behind its own LoadingGate right after construction --
    // its first focusable card doesn't exist yet. Retry each frame until it
    // does; until then focus stays on the rail (expanded), never lost.
    brls::View* focus = this->contentFocusTarget();
    if (!focus && attemptsLeft > 0)
    {
        brls::sync([this, attemptsLeft]() { this->completeInitialFocus(attemptsLeft - 1); });
        return;
    }
    if (focus)
        brls::Application::giveFocus(focus);
    else
        this->focusRail();
}

brls::View* MainMenu::getDefaultFocus()
{
    if (this->activeIndex < this->entries.size())
        return this->entries[this->activeIndex].item;
    return brls::Box::getDefaultFocus();
}
