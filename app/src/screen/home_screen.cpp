#include "screen/home_screen.hpp"
#include "screen/player_screen.hpp"
#include "screen/downloads_screen.hpp"
#include "api/download_manager.hpp"
#include "view/media_card.hpp"
#include "view/media_grid.hpp"
#include "view/shelf.hpp"
#include "view/loading_gate.hpp"
#include "model/media_item.hpp"
#include "model/favorites_store.hpp"
#include "model/continue_watching_store.hpp"
#include "model/playback_context.hpp"
#include "api/stremio_api.hpp"
#include "api/cloud_sync.hpp"
#include "theme/streamnx_theme.hpp"

#include <cctype>
#include <cstdlib>
#include <functional>
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

// Resumes exactly the stream a Continue Watching entry was last playing,
// instead of the usual card action (open DetailScreen) -- nullptr (falls
// back to the default) for an entry saved before this existed, which has
// neither a streamUrl nor a torrentInfoHash to resume with.
std::function<bool(brls::View*)> makeResumeAction(const ContinueWatching& c)
{
    if (c.streamUrl.empty() && c.torrentInfoHash.empty())
        return nullptr;

    return [c](brls::View*) {
        PlaybackContext ctx;
        ctx.continueWatchingId    = c.id;
        ctx.type                  = c.type;
        ctx.posterUrl             = c.poster;
        ctx.subtitle              = c.subtitle;
        ctx.resumePositionSeconds = c.positionSeconds;

        if (c.isTorrent)
        {
            // Same rule as picking a torrent in StreamListScreen: play it
            // only once it's fully on disk; otherwise (re)start the
            // download and show it in Downloads.
            for (auto& d : download_mgr::getFinishedDownloads())
                if (d.infoHash == c.torrentInfoHash)
                {
                    brls::Application::pushActivity(
                        new brls::Activity(new PlayerScreen(d.localFileUrl, c.name, ctx)), brls::TransitionAnimation::NONE);
                    return true;
                }

            bool active = false;
            for (auto& d : download_mgr::getActiveDownloads())
                active = active || (d.infoHash == c.torrentInfoHash && !d.failed);
            if (!active)
            {
                std::string title = c.subtitle.empty() ? c.name : c.name + " - " + c.subtitle;
                download_mgr::startTorrentDownload(
                    c.torrentInfoHash,
                    title,
                    std::make_shared<bool>(true),
                    nullptr,
                    [title](const std::string&) { brls::Application::notify("Download concluido: " + title); },
                    [](const std::string& error) { brls::Application::notify("Falha no download: " + error); },
                    c.torrentTrackers,
                    c.poster);
            }
            brls::Application::pushActivity(new brls::Activity(new DownloadsScreen(true)), brls::TransitionAnimation::NONE);
        }
        else
        {
            brls::Application::pushActivity(
                new brls::Activity(new PlayerScreen(c.streamUrl, c.name, ctx)), brls::TransitionAnimation::NONE);
        }
        return true;
    };
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

    auto& entries = ContinueWatchingStore::instance().all();
    if (entries.empty())
        return;

    this->addSectionHeader(content, "Continuar Assistindo");

    auto* scroll = new Shelf();
    scroll->setHeight(300);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setHeight(300);

    for (auto& c : entries)
    {
        MediaItem item;
        item.id         = c.id;
        item.type       = c.type;
        item.title      = c.name;
        item.subtitle   = c.subtitle;
        item.colorIndex = colorIndexFor(c.name);
        item.isFavorite = FavoritesStore::instance().contains(c.id);
        item.poster     = c.poster;
        item.progress   = c.durationSeconds > 0 ? (float)(c.positionSeconds / c.durationSeconds) : 0.0f;

        row->addView(new MediaCard(item, makeResumeAction(c)));
    }

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

#ifdef STREAMNX_PC_UI
    // Free-scrolling grid instead of the D-pad-shaped shelf-per-block --
    // content's own horizontal padding (30/50/30/50, see buildContent) is
    // the only thing eating into the screen's width besides the sidebar.
    float availableWidth = streamnx_theme::contentAreaWidth();
    fillMediaGridPc(grid, favoriteItems(), availableWidth, "Nenhum favorito ainda.");
#else
    fillMediaGrid(grid, favoriteItems(), FAVORITES_PER_ROW, "Nenhum favorito ainda.");
#endif
}

HomeScreen::HomeScreen()
{
    this->setAxis(brls::Axis::COLUMN);

    // Nothing is focusable while this shows, so the sidebar's "enter"
    // simply does nothing yet instead of landing on half-built content.
    // Logged into a profile, the lists shown here come from the server --
    // keep the spinner up until the startup sync is in (or gave up, see
    // cloud_sync), instead of flashing this device's older copy first.
    this->loadingGate = new LoadingGate([this]() {
        cloud::whenStartupSyncDone(this->alive, [this]() { this->buildContent(); });
    });
    this->addView(this->loadingGate);
}

HomeScreen::~HomeScreen()
{
    *this->alive = false;
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
