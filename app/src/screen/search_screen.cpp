#include "screen/search_screen.hpp"
#include "view/media_grid.hpp"
#include "view/loading_gate.hpp"
#include "model/addon_store.hpp"
#include "model/catalog_store.hpp"
#include "model/favorites_store.hpp"
#include "api/stremio_api.hpp"

#include <cctype>
#include <cstdlib>
#include <set>

namespace
{

// Just enough percent-encoding for a query embedded in a catalog "extra"
// path segment (catalog/{type}/top/search={query}.json).
std::string urlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

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

MediaItem toMediaItem(const stremio::MetaSummary& m)
{
    MediaItem item;
    item.id         = m.id;
    item.type       = m.type;
    item.title      = m.name;
    item.subtitle   = (m.type == "series" ? std::string("Serie") : std::string("Filme")) + (m.year.empty() ? "" : (" - " + m.year));
    item.colorIndex = colorIndexFor(m.name);
    item.rating     = parseRating(m.imdbRating);
    item.isFavorite = FavoritesStore::instance().contains(m.id);
    item.poster     = m.poster;
    return item;
}

constexpr int RESULTS_PER_ROW = 8;
constexpr size_t PAGE_SIZE    = 100;

} // namespace

SearchScreen::SearchScreen()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(30, 50, 30, 50);
    // A safe place to park focus while rebuildResults() tears down the
    // list -- without this, giveFocus(this) falls through Box's own
    // getDefaultFocus() straight to lastFocusedView (the very card being
    // destroyed) instead of stopping at `this`, silently defeating the
    // "park focus first" fix below. Same reasoning as AddonsScreen.
    this->setFocusable(true);

    auto* title = new brls::Label();
    title->setText("Buscar Filmes e Series");
    title->setFontSize(26);
    title->setMarginBottom(20);
    this->addView(title);

    // The catalog (and the X-to-search hint) only appear once "loaded" --
    // see buildContent(). Nothing else here is focusable meanwhile.
    this->loadingGate = new LoadingGate([this]() { this->buildContent(); });
    this->addView(this->loadingGate);
}

SearchScreen::~SearchScreen()
{
    *this->alive = false;
}

void SearchScreen::buildContent()
{
    this->removeView(this->loadingGate);
    this->loadingGate = nullptr;

    this->resultsHeader = new brls::Label();
    this->resultsHeader->setText("Carregando catalogo...");
    this->resultsHeader->setFontSize(16);
    this->resultsHeader->setMarginBottom(16);
    this->resultsHeader->setTextColor(nvgRGB(150, 150, 150));
    this->addView(this->resultsHeader);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingIndicatorVisible(false);
    // CENTERED reacts to every single press; the default (NATURAL) only
    // scrolls while a direction is held and only moves focus once the
    // target is fully in view, which reads as navigation being capped at
    // whatever fits in the window.
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    this->resultsBox = new brls::Box();
    this->resultsBox->setAxis(brls::Axis::COLUMN);
    this->resultsBox->setDimensions(brls::View::AUTO, brls::View::AUTO);

    scroll->setContentView(this->resultsBox);
    this->addView(scroll);

    // No inline search field -- X opens the platform's on-screen keyboard
    // (shows up as a footer hint automatically); this screen otherwise just
    // shows the loaded catalog. Registered only now: pressing X before the
    // catalog exists would open the dialog for a list that isn't there yet.
    this->registerAction("Buscar", brls::BUTTON_X, [this](brls::View*) {
        this->openSearchDialog();
        return true;
    });

    this->loadCatalog();
}

void SearchScreen::loadCatalog()
{
    AddonStore::instance().load();
    CatalogStore::instance().load();

    std::vector<std::string> enabledUrls;
    for (auto& addon : AddonStore::instance().all())
        if (addon.enabled)
            enabledUrls.push_back(addon.url);

    std::vector<std::string> enabledCatalogUrls;
    for (auto& catalog : CatalogStore::instance().all())
        if (catalog.enabled)
            enabledCatalogUrls.push_back(catalog.url);

    this->hasEnabledAddons = !enabledUrls.empty() || !enabledCatalogUrls.empty();

    if (enabledUrls.empty() && enabledCatalogUrls.empty())
    {
        this->allItems.clear();
        this->rebuildResults();
        return;
    }

    // "pending" starts at 1 (dropped last, right after the loop) and gains
    // one more for every manifest fetch, one more again for every
    // movie/series catalog a manifest turns out to list, and one for each
    // catalog fetched directly from CatalogStore below -- that total only
    // stabilizes once every request has answered, so the extra starting
    // reservation is what stops the first one from racing the count down
    // to zero before the rest get a chance to add theirs.
    auto pending = std::make_shared<int>(1);
    auto results = std::make_shared<std::vector<MediaItem>>();
    auto seenIds = std::make_shared<std::set<std::string>>();
    auto alive   = this->alive;

    auto onOneCatalogDone = [this, pending, results]() {
        if (--(*pending) > 0)
            return;

        this->allItems = *results;
        this->rebuildResults();
    };

    // A manifest-declared "top" and one of CatalogStore's entries often
    // resolve to the exact same URL for Cinemeta -- de-duping by id (shared
    // across both sources) keeps that from showing every popular title
    // twice.
    auto addUnique = [results, seenIds](const std::vector<stremio::MetaSummary>& metas) {
        for (auto& m : metas)
            if (seenIds->insert(m.id).second)
                results->push_back(toMediaItem(m));
    };

    for (auto& url : enabledUrls)
    {
        (*pending)++;
        stremio::fetchManifest(
            url, alive,
            [url, alive, pending, addUnique, onOneCatalogDone](stremio::ManifestInfo info) {
                std::vector<stremio::CatalogRef> picks;
                for (auto& c : info.catalogs)
                    if (c.type == "movie" || c.type == "series")
                        picks.push_back(c);

                for (auto& pick : picks)
                    (*pending)++;

                for (auto& pick : picks)
                {
                    stremio::fetchCatalog(
                        url, pick.type, pick.id, alive,
                        [addUnique, onOneCatalogDone](std::vector<stremio::MetaSummary> metas) {
                            addUnique(metas);
                            onOneCatalogDone();
                        },
                        [onOneCatalogDone](std::string) { onOneCatalogDone(); });
                }

                onOneCatalogDone();
            },
            [onOneCatalogDone](std::string) { onOneCatalogDone(); });
    }

    for (auto& catalogUrl : enabledCatalogUrls)
    {
        (*pending)++;
        stremio::fetchCatalogUrl(
            catalogUrl, alive,
            [addUnique, onOneCatalogDone](std::vector<stremio::MetaSummary> metas) {
                addUnique(metas);
                onOneCatalogDone();
            },
            [onOneCatalogDone](std::string) { onOneCatalogDone(); });
    }

    onOneCatalogDone();
}

void SearchScreen::openSearchDialog()
{
    auto alive = this->alive;
    brls::Application::getImeManager()->openForText(
        [this, alive](std::string text) {
            // The IME dialog's own "submit" handling calls this from inside
            // Application::popActivity()'s completion callback -- i.e.
            // while the dialog is still mid-teardown and its pop
            // transition/focus-restore hasn't fully settled. Rebuilding
            // resultsBox synchronously right here (which used to happen)
            // raced that teardown and crashed the app. Deferring one frame
            // via brls::sync lets the pop finish completely first; `alive`
            // guards against this screen having been navigated away from
            // in the meantime.
            brls::sync([this, alive, text]() {
                if (!*alive)
                    return;
                this->query = text;
                if (this->query.empty())
                    this->rebuildResults();
                else
                    this->performSearch(this->query);
            });
        },
        "Buscar Filmes e Series", "Digite o nome de um filme ou serie", 64, this->query);
}

void SearchScreen::performSearch(const std::string& searchQuery)
{
    AddonStore::instance().load();

    // Cinemeta is a fixed dependency (see stremio::CINEMETA_URL), not one of
    // AddonStore's user-managed entries -- always searched, same as
    // StreamNX-main, regardless of what the user has added/enabled.
    std::vector<std::string> enabledUrls { stremio::CINEMETA_URL };
    for (auto& addon : AddonStore::instance().all())
        if (addon.enabled)
            enabledUrls.push_back(addon.url);

    this->resultsHeader->setText("Buscando \"" + searchQuery + "\"...");

    auto pending = std::make_shared<int>(1);
    auto results = std::make_shared<std::vector<MediaItem>>();
    auto seenIds = std::make_shared<std::set<std::string>>();
    auto alive   = this->alive;
    std::string extra = "search=" + urlEncode(searchQuery);

    auto addUnique = [results, seenIds](const std::vector<stremio::MetaSummary>& metas) {
        for (auto& m : metas)
            if (seenIds->insert(m.id).second)
                results->push_back(toMediaItem(m));
    };

    // A newer search may start (another X press) while this one's requests
    // are still in flight -- only apply results if `query` still matches
    // what this search was for, so a slow, stale response can't clobber a
    // more recent one that already finished.
    auto onOneDone = [this, pending, results, searchQuery]() {
        if (--(*pending) > 0)
            return;

        if (this->query != searchQuery)
            return;

        this->searchResults = *results;
        this->rebuildResults();
    };

    for (auto& url : enabledUrls)
    {
        for (const char* type : { "movie", "series" })
        {
            (*pending)++;
            stremio::fetchCatalog(
                url, type, "top", alive,
                [addUnique, onOneDone](std::vector<stremio::MetaSummary> metas) {
                    addUnique(metas);
                    onOneDone();
                },
                [onOneDone](std::string) { onOneDone(); }, extra);
        }
    }

    onOneDone();
}

void SearchScreen::rebuildResults(bool resetPaging)
{
    if (resetPaging)
        this->visibleCount = PAGE_SIZE;

    bool isSearch = !this->query.empty();
    const std::vector<MediaItem>& source = isSearch ? this->searchResults : this->allItems;

    if (!source.empty())
        this->resultsHeader->setText(isSearch ? ("Resultados para \"" + query + "\"") : "Todos os titulos");
    else if (!this->hasEnabledAddons)
        this->resultsHeader->setText("Nenhum addon ou catalogo ativo -- ative um na tela Addons.");
    else if (isSearch)
        this->resultsHeader->setText("Nenhum resultado para \"" + query + "\"");
    else
        this->resultsHeader->setText("Nenhum titulo encontrado nos addons ativos");

    bool hasMore = source.size() > this->visibleCount;
    std::vector<MediaItem> page(source.begin(), source.begin() + (hasMore ? this->visibleCount : source.size()));

    // rebuildResults() runs both from a fresh catalog load/search (nothing
    // in resultsBox is focused yet) and, via loadMore() below, from a
    // "Carregar mais" row's own A action -- clearing resultsBox tears that
    // row down mid-callback, and clearing it out from under whatever card
    // is currently focused (restored here after the search IME dialog
    // closes) is exactly what used to crash the app: destroying the
    // focused view without first moving focus off it left
    // Application::currentFocus dangling. Same fix AddonsScreen's
    // rebuildList() already needed: park focus on the screen itself first.
    bool focusWasInList = false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v != nullptr; v = v->getParent())
        if (v == this->resultsBox)
        {
            focusWasInList = true;
            break;
        }
    if (focusWasInList)
        brls::Application::giveFocus(this);

    fillMediaGrid(this->resultsBox, page, RESULTS_PER_ROW, "Nenhum resultado encontrado.");

    if (hasMore)
    {
        size_t remaining = source.size() - this->visibleCount;
        auto* loadMore    = new brls::Box();
        loadMore->setFocusable(true);
        loadMore->setDimensions(brls::View::AUTO, brls::View::AUTO);
        loadMore->setAlignItems(brls::AlignItems::CENTER);
        loadMore->setJustifyContent(brls::JustifyContent::CENTER);
        loadMore->setPadding(14, 24, 14, 24);
        loadMore->setMarginTop(4);
        loadMore->setCornerRadius(8);
        loadMore->setBackgroundColor(nvgRGB(30, 33, 41));

        auto* label = new brls::Label();
        label->setText("Carregar mais (" + std::to_string(remaining) + " restantes)");
        label->setFontSize(15);
        loadMore->addView(label);

        loadMore->registerAction("Carregar mais", brls::BUTTON_A, [this](brls::View*) {
            this->loadMore();
            return true;
        });

        this->resultsBox->addView(loadMore);
    }

    if (focusWasInList)
        brls::sync([this]() { brls::Application::giveFocus(this->resultsBox); });
}

void SearchScreen::loadMore()
{
    this->visibleCount += PAGE_SIZE;
    this->rebuildResults(false);
}
