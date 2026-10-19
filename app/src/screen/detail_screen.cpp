#include "screen/detail_screen.hpp"
#include "screen/stream_list_screen.hpp"
#include "view/loading_gate.hpp"
#include "view/shelf.hpp"
#include "api/image_cache.hpp"
#include "model/addon_store.hpp"
#include "model/favorite.hpp"
#include "model/favorites_store.hpp"
#include "utils/mouse.hpp"
#include "view/ui_kit.hpp"

#include <cctype>

namespace
{

const NVGcolor PALETTE[] = {
    nvgRGB(0x3A, 0x6B, 0x8F),
    nvgRGB(0x8F, 0x4A, 0x6B),
    nvgRGB(0x4A, 0x8F, 0x5C),
    nvgRGB(0x8F, 0x7A, 0x3A),
    nvgRGB(0x5C, 0x4A, 0x8F),
    nvgRGB(0x8F, 0x5C, 0x3A),
};
constexpr size_t PALETTE_SIZE = sizeof(PALETTE) / sizeof(PALETTE[0]);

NVGcolor colorForTitle(const std::string& title)
{
    size_t hash = 0;
    for (char c : title)
        hash = hash * 31 + (unsigned char)c;
    return PALETTE[hash % PALETTE_SIZE];
}

// A small rounded badge -- used for the rating/year/type pills under the
// title. Padding is Box-only in this fork, so the label sits inside one.
brls::Box* pill(const std::string& text, NVGcolor bg, NVGcolor fg)
{
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::ROW);
    box->setDimensions(brls::View::AUTO, brls::View::AUTO);
    box->setBackgroundColor(bg);
    box->setCornerRadius(6);
    box->setPadding(6, 12, 6, 12);
    box->setMarginRight(10);

    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(14);
    label->setTextColor(fg);
    box->addView(label);

    return box;
}

// One season in the shelf: focusable pill with the season number and
// episode count; A lists that season's episodes below the shelf.
class SeasonPill : public HoverBox
{
  public:
    SeasonPill(int season, size_t episodeCount, std::function<void()> onSelect)
    {
        this->setAxis(brls::Axis::COLUMN);
        this->setFocusable(true);
        this->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->setPadding(16, 22, 16, 22);
        this->setMarginRight(14);
        this->setCornerRadius(10);
        this->setHoverColors(ui::color("surface_raised"), ui::color("surface_hover"));

        auto* title = new brls::Label();
        title->setText(season == 0 ? "Especiais" : "Temporada " + std::to_string(season));
        title->setFontSize(17);
        this->addView(title);

        auto* subtitle = new brls::Label();
        subtitle->setText(std::to_string(episodeCount) + (episodeCount == 1 ? " episodio" : " episodios"));
        subtitle->setFontSize(13);
        subtitle->setTextColor(nvgRGB(150, 150, 150));
        subtitle->setMarginTop(4);
        this->addView(subtitle);

        this->registerAction("Ver episodios", brls::BUTTON_A, [onSelect](brls::View*) {
            onSelect();
            return true;
        });
        makeClickable(this);
    }
};

// One episode in the horizontally scrolling episode strip; A opens its
// stream list.
class EpisodeCard : public HoverBox
{
  public:
    EpisodeCard(const stremio::VideoEntry& video, std::function<void()> onSelect)
    {
        this->setAxis(brls::Axis::COLUMN);
        this->setFocusable(true);
        this->setWidth(220);
        this->setHeight(90);
        this->setPadding(14, 18, 14, 18);
        this->setMarginRight(14);
        this->setCornerRadius(10);
        this->setHoverColors(ui::color("surface"), ui::color("surface_raised"));

        auto* number = new brls::Label();
        number->setText("Episodio " + std::to_string(video.episode));
        number->setFontSize(13);
        number->setTextColor(nvgRGB(150, 150, 155));
        this->addView(number);

        auto* name = new brls::Label();
        name->setText(video.name.empty() ? video.id : video.name);
        name->setFontSize(15);
        name->setIsWrapping(true);
        name->setMarginTop(4);
        this->addView(name);

        this->registerAction("Ver streams", brls::BUTTON_A, [onSelect](brls::View*) {
            onSelect();
            return true;
        });
        makeClickable(this);
    }
};

} // namespace

DetailScreen::DetailScreen(const std::string& id, const std::string& type, const std::string& fallbackTitle)
    : id(id)
    , type(type)
    , fallbackTitle(fallbackTitle)
{
    this->setAxis(brls::Axis::COLUMN);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->setBackgroundColor(ui::color("bg"));

    this->registerAction(
        "Voltar", brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        },
        false, false, brls::SOUND_BACK);

#ifdef STREAMNX_PC_UI
    this->addView(ui::backBar());
#endif

    this->loadingGate = new LoadingGate([this]() { this->tryAddonsForMeta(0); }, 300);
    this->addView(this->loadingGate);

#ifndef STREAMNX_PC_UI
    this->addView(new brls::BottomBar());
#endif

    AddonStore::instance().load();
}

DetailScreen::~DetailScreen()
{
    *this->alive = false;
}

void DetailScreen::tryAddonsForMeta(size_t addonIndex)
{
    // Cinemeta is a fixed dependency (see stremio::CINEMETA_URL), not one of
    // AddonStore's user-managed entries -- always tried first, same as
    // StreamNX-main, regardless of what the user has added/enabled.
    std::vector<std::string> urls { stremio::CINEMETA_URL };
    for (auto& addon : AddonStore::instance().all())
        if (addon.enabled)
            urls.push_back(addon.url);

    size_t i = addonIndex;
    if (i >= urls.size())
    {
        this->showError("Nenhum addon conseguiu carregar este titulo.");
        return;
    }

    stremio::fetchMeta(
        urls[i], this->type, this->id, this->alive,
        [this](stremio::MetaDetail detail) { this->onMetaLoaded(detail); },
        [this, i](std::string) { this->tryAddonsForMeta(i + 1); });
}

void DetailScreen::onMetaLoaded(const stremio::MetaDetail& detail)
{
    this->meta = detail;
    if (this->meta.name.empty())
        this->meta.name = this->fallbackTitle;

    this->seasons.clear();
    for (auto& v : detail.videos)
        this->seasons[v.season].push_back(v);

    this->buildContent(this->meta);
}

void DetailScreen::showError(const std::string& message)
{
    if (this->loadingGate)
    {
        this->removeView(this->loadingGate);
        this->loadingGate = nullptr;
    }

    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setGrow(1.0f);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    // Nothing else on this screen is focusable -- without this, giveFocus()
    // below (and pushActivity()'s own earlier attempt) resolves to nullptr
    // and silently no-ops, stranding focus on whatever the previous screen
    // had focused (B would then never reach this screen's own action).
    box->setFocusable(true);

    auto* label = new brls::Label();
    label->setText(message);
    label->setFontSize(18);
    label->setTextColor(nvgRGB(180, 180, 180));
    box->addView(label);

    ui::addAboveFooter(this, box);
    brls::Application::giveFocus(this->getDefaultFocus());
}

void DetailScreen::buildContent(const stremio::MetaDetail& detail)
{
    if (this->loadingGate)
    {
        this->removeView(this->loadingGate);
        this->loadingGate = nullptr;
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setDimensions(brls::View::AUTO, brls::View::AUTO);

    // Backdrop hero, fading into the page's own background at the bottom.
    auto* backdropBox = new brls::Box();
    backdropBox->setAxis(brls::Axis::ROW);
    backdropBox->setHeight(320);
    backdropBox->setBackgroundColor(colorForTitle(detail.name));

    auto* backdropImage = new brls::Image();
    backdropImage->setGrow(1.0f);
    backdropImage->setScalingType(brls::ImageScalingType::FILL);
    backdropBox->addView(backdropImage);

    auto* scrim = new brls::Box();
    scrim->setPositionType(brls::PositionType::ABSOLUTE);
    scrim->setPositionBottom(0);
    scrim->setWidthPercentage(100);
    scrim->setHeight(160);
    scrim->setBackground(brls::ViewBackground::VERTICAL_LINEAR);
    backdropBox->addView(scrim);

    content->addView(backdropBox);

    std::string backdropUrl = detail.background.empty() ? detail.poster : detail.background;
    imgcache::load(backdropImage, backdropUrl, this->alive);

    // Poster + title + meta + favorite + description.
    auto* bodyBox = new brls::Box();
    bodyBox->setAxis(brls::Axis::COLUMN);
    bodyBox->setPadding(0, 50, 40, 50);

    auto* headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setMarginTop(-70);

    auto* posterBox = new brls::Box();
    posterBox->setWidth(180);
    posterBox->setHeight(260);
    posterBox->setCornerRadius(10);
    posterBox->setBackgroundColor(colorForTitle(detail.name));
    posterBox->setAxis(brls::Axis::COLUMN);
    posterBox->setAlignItems(brls::AlignItems::CENTER);
    posterBox->setJustifyContent(brls::JustifyContent::CENTER);

    auto* posterImage = new brls::Image();
    posterImage->setDimensions(180, 260);
    posterImage->setScalingType(brls::ImageScalingType::FILL);
    posterImage->setCornerRadius(10);
    posterBox->addView(posterImage);
    imgcache::load(posterImage, detail.poster, this->alive);

    headerRow->addView(posterBox);

    auto* infoCol = new brls::Box();
    infoCol->setAxis(brls::Axis::COLUMN);
    infoCol->setGrow(1.0f);
    infoCol->setMarginLeft(30);
    infoCol->setMarginTop(75); // aligns the title with the poster's lower half, clear of the backdrop

    auto* title = new brls::Label();
    title->setText(detail.name);
    title->setFontSize(30);
    title->setIsWrapping(true);
    infoCol->addView(title);

    auto* metaRow = new brls::Box();
    metaRow->setAxis(brls::Axis::ROW);
    metaRow->setMarginTop(12);
    if (!detail.imdbRating.empty())
        metaRow->addView(pill("★ " + detail.imdbRating, nvgRGB(40, 36, 20), nvgRGB(255, 213, 74)));
    if (!detail.releaseInfo.empty())
        metaRow->addView(pill(detail.releaseInfo, nvgRGB(30, 33, 41), nvgRGB(210, 210, 215)));
    metaRow->addView(pill(detail.type == "series" ? "Serie" : "Filme", nvgRGB(30, 33, 41), nvgRGB(210, 210, 215)));
    infoCol->addView(metaRow);

    this->favoriteButton = new HoverBox();
    this->favoriteButton->setFocusable(true);
    this->favoriteButton->setAxis(brls::Axis::ROW);
    this->favoriteButton->setAlignItems(brls::AlignItems::CENTER);
    this->favoriteButton->setDimensions(brls::View::AUTO, brls::View::AUTO);
    this->favoriteButton->setPadding(10, 20, 10, 20);
    this->favoriteButton->setCornerRadius(10);
    this->favoriteButton->setHighlightCornerRadius(10);
    this->favoriteButton->setMarginTop(18);

    this->favoriteLabel = new brls::Label();
    this->favoriteLabel->setFontSize(16);
    this->favoriteButton->addView(this->favoriteLabel);

    std::string fid = detail.id, ftype = detail.type, fname = detail.name, fyear = detail.releaseInfo,
                frating = detail.imdbRating, fposter = detail.poster;
    this->favoriteButton->registerAction("Favoritar", brls::BUTTON_A, [this, fid, ftype, fname, fyear, frating, fposter](brls::View*) {
        if (FavoritesStore::instance().contains(fid))
        {
            FavoritesStore::instance().remove(fid);
            brls::Application::notify("Removido dos favoritos");
        }
        else
        {
            FavoritesStore::instance().add(Favorite { fid, ftype, fname, fyear, frating, fposter });
            brls::Application::notify("Adicionado aos favoritos");
        }
        this->refreshFavoriteButton();
        return true;
    });
    makeClickable(this->favoriteButton);

    auto* actionsRow = new brls::Box();
    actionsRow->setAxis(brls::Axis::ROW);
    actionsRow->setMarginTop(18);
    this->favoriteButton->setMarginTop(0);
    actionsRow->addView(this->favoriteButton);

    // Series list streams per-episode (below, in the episode strip) --
    // there's no single "the show" video id to ask an addon for. Movies
    // have exactly one playable video, so their streams button lives here.
    if (detail.type != "series")
    {
        std::string movieId = detail.id, movieTitle = detail.name;
        auto* streamsButton = ui::button("Ver Streams", ui::ButtonStyle::Primary,
            [this, movieId, movieTitle]() { this->openStreams(movieId, movieTitle, ""); }, ui::icons::play);
        streamsButton->setPadding(10, 22, 10, 22);
        streamsButton->setMarginLeft(14);

        actionsRow->addView(streamsButton);
    }

    infoCol->addView(actionsRow);
    this->refreshFavoriteButton();

    if (!detail.description.empty())
    {
        auto* description = new brls::Label();
        description->setText(detail.description);
        description->setFontSize(15);
        description->setTextColor(nvgRGB(190, 190, 195));
        description->setIsWrapping(true);
        description->setMarginTop(18);
        infoCol->addView(description);
    }

    headerRow->addView(infoCol);
    bodyBox->addView(headerRow);

    if (detail.type == "series" && !this->seasons.empty())
    {
        auto* seasonsHeader = new brls::Label();
        seasonsHeader->setText("Temporadas");
        seasonsHeader->setFontSize(22);
        seasonsHeader->setMarginTop(36);
        seasonsHeader->setMarginBottom(14);
        bodyBox->addView(seasonsHeader);

        auto* shelf = new Shelf();
        shelf->setHeight(100);
        shelf->setScrollingIndicatorVisible(false);
        shelf->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setHeight(100);

        for (auto& entry : this->seasons)
        {
            int season             = entry.first;
            size_t episodeCount     = entry.second.size();
            row->addView(new SeasonPill(season, episodeCount, [this, season]() { this->showSeasonEpisodes(season); }));
        }

        shelf->setContentView(row);
        bodyBox->addView(shelf);

        this->episodesBox = new brls::Box();
        this->episodesBox->setAxis(brls::Axis::COLUMN);
        this->episodesBox->setMarginTop(16);
        bodyBox->addView(this->episodesBox);

        this->showSeasonEpisodes(this->seasons.begin()->first);
    }

    content->addView(bodyBox);

    scroll->setContentView(content);
    ui::addAboveFooter(this, scroll);

    // pushActivity() resolved its default focus the instant this screen was
    // pushed, when only the LoadingGate existed (nothing focusable) -- that
    // attempt silently did nothing, leaving focus stranded on whatever was
    // focused on the screen *behind* this one (which is why it looked like
    // Search was still half-interactive, and why B didn't seem to do
    // anything: B was reaching Search's old focus, never this screen's own
    // action). Grab focus for real now that something exists to focus.
    brls::Application::giveFocus(this->getDefaultFocus());
}

void DetailScreen::showSeasonEpisodes(int season)
{
    if (!this->episodesBox)
        return;

    this->episodesBox->clearViews();

    auto it = this->seasons.find(season);
    if (it == this->seasons.end())
        return;

    auto* shelf = new Shelf();
    shelf->setHeight(90);
    shelf->setScrollingIndicatorVisible(false);
    shelf->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setHeight(90);

    std::string showName = this->meta.name;
    for (auto& video : it->second)
    {
        std::string videoId    = video.id;
        std::string title      = showName + (video.name.empty() ? "" : (" - " + video.name));
        std::string subtitle   = "Temporada " + std::to_string(season) + " - Episodio " + std::to_string(video.episode);

        std::string nextId, nextTitle, nextSubtitle;
        bool hasNext = this->findNextEpisode(season, video.episode, nextId, nextTitle, nextSubtitle);

        row->addView(new EpisodeCard(video, [this, videoId, title, subtitle, hasNext, nextId, nextTitle, nextSubtitle]() {
            this->openStreams(videoId, title, subtitle, hasNext ? nextId : "", nextTitle, nextSubtitle);
        }));
    }

    shelf->setContentView(row);
    this->episodesBox->addView(shelf);
}

bool DetailScreen::findNextEpisode(
    int season, int episode, std::string& outId, std::string& outTitle, std::string& outSubtitle)
{
    auto pickLowestEpisode = [](const std::vector<stremio::VideoEntry>& videos, int minEpisode) -> const stremio::VideoEntry* {
        const stremio::VideoEntry* best = nullptr;
        for (auto& v : videos)
            if (v.episode > minEpisode && (!best || v.episode < best->episode))
                best = &v;
        return best;
    };

    auto it = this->seasons.find(season);
    if (it != this->seasons.end())
    {
        if (const stremio::VideoEntry* next = pickLowestEpisode(it->second, episode))
        {
            outId       = next->id;
            outTitle    = this->meta.name + (next->name.empty() ? "" : (" - " + next->name));
            outSubtitle = "Temporada " + std::to_string(season) + " - Episodio " + std::to_string(next->episode);
            return true;
        }
    }

    // std::map keeps its keys sorted, so upper_bound(season) is exactly
    // "the next season after this one", if there is one.
    auto nextSeasonIt = this->seasons.upper_bound(season);
    if (nextSeasonIt != this->seasons.end())
    {
        if (const stremio::VideoEntry* next = pickLowestEpisode(nextSeasonIt->second, /* minEpisode */ -1))
        {
            outId       = next->id;
            outTitle    = this->meta.name + (next->name.empty() ? "" : (" - " + next->name));
            outSubtitle = "Temporada " + std::to_string(nextSeasonIt->first) + " - Episodio " + std::to_string(next->episode);
            return true;
        }
    }

    return false;
}

void DetailScreen::openStreams(const std::string& videoId, const std::string& title, const std::string& subtitle,
    const std::string& nextVideoId, const std::string& nextTitle, const std::string& nextSubtitle)
{
    // NONE, not the default fade -- during the crossfade the previous
    // screen's own cards are still focusable underneath StreamListScreen
    // (which starts with nothing focused of its own until its network
    // fetch finishes), so a stray press mid-transition could land back on
    // this screen instead. Same reasoning as PlayerScreen's own push.
    brls::Application::pushActivity(
        new brls::Activity(
            new StreamListScreen(this->meta.type, videoId, title, subtitle, this->meta.id, this->meta.poster,
                nextVideoId, nextTitle, nextSubtitle)),
        brls::TransitionAnimation::NONE);
}

void DetailScreen::refreshFavoriteButton()
{
    if (!this->favoriteButton || !this->favoriteLabel)
        return;

    bool favorited = FavoritesStore::instance().contains(this->meta.id.empty() ? this->id : this->meta.id);

    this->favoriteLabel->setText(favorited ? "♥ Favoritado" : "♥ Favoritar");
    this->favoriteLabel->setTextColor(ui::color("text"));
    if (favorited)
        this->favoriteButton->setHoverColors(ui::color("danger"), ui::color("danger_hover"));
    else
        this->favoriteButton->setHoverColors(ui::color("surface_raised"), ui::color("surface_hover"));
}
