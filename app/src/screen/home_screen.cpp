#include "screen/home_screen.hpp"
#include "view/media_card.hpp"
#include "view/media_grid.hpp"
#include "view/shelf.hpp"
#include "view/loading_gate.hpp"
#include "model/media_item.hpp"
#include "model/favorites_store.hpp"
#include "model/continue_watching_store.hpp"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace
{

int colorIndexFor(const std::string& title)
{
    size_t hash = 0;
    for (char c : title)
        hash = hash * 31 + (unsigned char)c;
    return (int)(hash % 6);
}

float parseRating(const std::string& s)
{
    if (s.empty() || !(std::isdigit((unsigned char)s[0])))
        return 0.0f;
    return (float)std::atof(s.c_str());
}

// Real data now: whatever was favorited from a DetailScreen.
std::vector<MediaItem> favoriteItems()
{
    std::vector<MediaItem> out;
    for (auto& f : FavoritesStore::instance().all())
    {
        MediaItem item;
        item.id         = f.id;
        item.type       = f.type;
        item.title      = f.name;
        item.subtitle   = (f.type == "series" ? std::string("Serie") : std::string("Filme")) + (f.year.empty() ? "" : (" - " + f.year));
        item.colorIndex = colorIndexFor(f.name);
        item.rating     = parseRating(f.imdbRating);
        item.isFavorite = true;
        item.poster     = f.poster;
        out.push_back(item);
    }
    return out;
}

// Real data now too: whatever a stream was last opened for (see
// StreamListScreen), most-recently-touched first. There's no embedded
// player, so there's no real progress percentage to show.
std::vector<MediaItem> continueWatchingItems()
{
    std::vector<MediaItem> out;
    for (auto& c : ContinueWatchingStore::instance().all())
    {
        MediaItem item;
        item.id         = c.id;
        item.type       = c.type;
        item.title      = c.name;
        item.subtitle   = c.subtitle;
        item.colorIndex = colorIndexFor(c.name);
        item.isFavorite = FavoritesStore::instance().contains(c.id);
        item.poster     = c.poster;
        out.push_back(item);
    }
    return out;
}

constexpr int FAVORITES_PER_ROW = 10;

} // namespace

void HomeScreen::addSectionHeader(brls::Box* content, const std::string& text)
{
    auto* header = new brls::Label();
    header->setText(text);
    header->setFontSize(26);
    header->setMarginTop(24);
    header->setMarginBottom(12);
    content->addView(header);
}

void HomeScreen::addContinueWatchingRow(brls::Box* content)
{
    ContinueWatchingStore::instance().load();

    auto items = continueWatchingItems();
    if (items.empty())
        return;

    this->addSectionHeader(content, "Continuar Assistindo");

    auto* scroll = new Shelf();
    scroll->setHeight(300);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setHeight(300);

    for (auto& item : items)
        row->addView(new MediaCard(item));

    scroll->setContentView(row);
    content->addView(scroll);
}

void HomeScreen::addFavoritesGrid(brls::Box* content)
{
    FavoritesStore::instance().load();

    this->addSectionHeader(content, "Favoritos");

    auto* grid = new brls::Box();
    grid->setAxis(brls::Axis::COLUMN);
    content->addView(grid);

    fillMediaGrid(grid, favoriteItems(), FAVORITES_PER_ROW, "Nenhum favorito ainda.");
}

HomeScreen::HomeScreen()
{
    this->setAxis(brls::Axis::COLUMN);

    // Nothing is focusable while this shows, so the sidebar's "enter"
    // simply does nothing yet instead of landing on half-built content.
    this->loadingGate = new LoadingGate([this]() { this->buildContent(); });
    this->addView(this->loadingGate);
}

void HomeScreen::buildContent()
{
    this->removeView(this->loadingGate);
    this->loadingGate = nullptr;

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingIndicatorVisible(false);
    // See the comment on Shelf's own setScrollingBehavior call: NATURAL
    // requires holding a direction and only jumps once the target is fully
    // scrolled into view, which reads as navigation refusing to leave the
    // shelf currently on screen. CENTERED reacts to every single press.
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setDimensions(brls::View::AUTO, brls::View::AUTO);
    content->setPadding(30, 50, 30, 50);

    this->addContinueWatchingRow(content);
    this->addFavoritesGrid(content);

    scroll->setContentView(content);
    this->addView(scroll);
}
