#include "screen/stream_list_screen.hpp"
#include "screen/player_screen.hpp"
#include "view/loading_gate.hpp"
#include "model/addon_store.hpp"
#include "model/continue_watching_store.hpp"
#include "api/stremio_api.hpp"

#include <functional>
#include <regex>

namespace
{

// "https://v3-cinemeta.strem.io/some/path" -> "v3-cinemeta.strem.io" -- the
// protocol has no notion of an addon's display name at the /stream level
// (that's only in its manifest, and fetching every enabled addon's manifest
// again just to label a stream list isn't worth the extra round trips), so
// the host is used as a short, always-available stand-in for "which addon".
std::string hostOf(const std::string& url)
{
    size_t start = url.find("://");
    start        = start == std::string::npos ? 0 : start + 3;
    size_t end   = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Small rounded pill -- used for quality/HDR/size tags pulled out of the
// addon's raw name/title text below.
brls::Box* makePill(const std::string& text, NVGcolor bg, NVGcolor fg)
{
    auto* pill = new brls::Box();
    pill->setDimensions(brls::View::AUTO, brls::View::AUTO);
    pill->setBackgroundColor(bg);
    pill->setCornerRadius(5);
    pill->setPadding(2, 7, 2, 7);
    pill->setMarginRight(6);
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(11);
    label->setTextColor(fg);
    pill->addView(label);
    return pill;
}

// Addons rarely expose structured quality/size fields -- everything is
// buried in the stream's free-text name/title (e.g. "1080p WEB-DL x265
// HDR10 | 2.3 GB"). Pull out the handful of tags worth calling out as
// badges instead of dumping that whole string on the user unstyled.
struct StreamBadges
{
    std::string quality;
    std::string extra; // HDR/HDR10+/DV
    std::string size;
};

StreamBadges parseBadges(const std::string& text)
{
    StreamBadges b;
    std::smatch m;

    static const std::regex qualityRe(R"(4K|2160p|1080p|720p|480p)", std::regex::icase);
    if (std::regex_search(text, m, qualityRe))
        b.quality = m[0];

    static const std::regex extraRe(R"(HDR10\+|HDR10|HDR|DoVi|DV\b)", std::regex::icase);
    if (std::regex_search(text, m, extraRe))
        b.extra = m[0];

    static const std::regex sizeRe(R"(\d+(?:[.,]\d+)?\s?(?:GB|MB))", std::regex::icase);
    if (std::regex_search(text, m, sizeRe))
        b.size = m[0];

    return b;
}

// Compact: one badge/name row plus, only when there's something to say
// beyond the badges (a leftover size or a differently-worded title), a
// single dim detail line -- no more than that, so a long list of results
// stays scannable instead of each entry eating a quarter of the screen.
class StreamRow : public brls::Box
{
  public:
    StreamRow(const stremio::StreamEntry& entry, std::function<void(const stremio::StreamEntry&)> onPlay)
    {
        this->setAxis(brls::Axis::COLUMN);
        this->setFocusable(true);
        this->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->setWidthPercentage(100);
        this->setPadding(9, 14, 9, 14);
        this->setMarginBottom(6);
        this->setCornerRadius(6);
        this->setBackgroundColor(nvgRGB(22, 24, 30));

        std::string combined = entry.name + " " + entry.title;
        StreamBadges badges  = parseBadges(combined);

        auto* topRow = new brls::Box();
        topRow->setAxis(brls::Axis::ROW);
        topRow->setAlignItems(brls::AlignItems::CENTER);

        if (!badges.quality.empty())
            topRow->addView(makePill(badges.quality, nvgRGB(52, 90, 168), nvgRGB(226, 234, 250)));
        if (!badges.extra.empty())
            topRow->addView(makePill(badges.extra, nvgRGB(122, 74, 214), nvgRGB(255, 255, 255)));

        auto* name = new brls::Label();
        name->setText(entry.name.empty() ? "Stream" : entry.name);
        name->setFontSize(14);
        name->setSingleLine(true);
        name->setGrow(1.0f);
        name->setShrink(1.0f);
        topRow->addView(name);

        auto* addonBadge = makePill(entry.addonName, nvgRGB(40, 43, 51), nvgRGB(180, 185, 195));
        addonBadge->setMarginRight(0);
        addonBadge->setMarginLeft(8);
        topRow->addView(addonBadge);

        this->addView(topRow);

        // Whatever's left of the title once the badges above already said
        // their part -- e.g. release group/codec -- shown small, one line,
        // only when it actually adds something.
        std::string detail = badges.size;
        if (!entry.title.empty() && entry.title != entry.name)
        {
            if (!detail.empty())
                detail += "  ·  ";
            detail += entry.title;
        }
        if (!detail.empty())
        {
            auto* detailLabel = new brls::Label();
            detailLabel->setText(detail);
            detailLabel->setFontSize(11);
            detailLabel->setTextColor(nvgRGB(150, 150, 155));
            detailLabel->setSingleLine(true);
            detailLabel->setMarginTop(4);
            this->addView(detailLabel);
        }

        this->registerAction("Assistir", brls::BUTTON_A, [entry, onPlay](brls::View*) {
            onPlay(entry);
            return true;
        });
    }
};

} // namespace

StreamListScreen::StreamListScreen(const std::string& type, const std::string& videoId, const std::string& title,
    const std::string& subtitle, const std::string& showId, const std::string& posterUrl)
    : type(type)
    , videoId(videoId)
    , title(title)
    , subtitle(subtitle)
    , showId(showId)
    , posterUrl(posterUrl)
{
    this->setAxis(brls::Axis::COLUMN);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->setBackgroundColor(nvgRGB(12, 13, 18));
    this->setPadding(30, 50, 30, 50);
    // A safe place to park focus while the stream list loads/rebuilds --
    // without this, focus stays on whatever card in DetailScreen was
    // pressed to get here (pushActivity() never moves it on its own), so
    // input can still reach that now-hidden screen underneath until
    // finish() below hands focus to something in this one. Same fix
    // AddonsScreen/SearchScreen already needed.
    this->setFocusable(true);

    this->registerAction(
        "Voltar", brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        },
        false, false, brls::SOUND_BACK);

    auto* header = new brls::Label();
    header->setText(title);
    header->setFontSize(26);
    this->addView(header);

    if (!subtitle.empty())
    {
        auto* sub = new brls::Label();
        sub->setText(subtitle);
        sub->setFontSize(15);
        sub->setTextColor(nvgRGB(150, 150, 155));
        sub->setMarginTop(4);
        sub->setMarginBottom(20);
        this->addView(sub);
    }
    else
    {
        header->setMarginBottom(20);
    }

    this->loadingGate = new LoadingGate([this]() { this->queryAddon(0); }, 300);
    this->addView(this->loadingGate);

    this->addView(new brls::BottomBar());

    AddonStore::instance().load();

    brls::Application::giveFocus(this);
}

brls::View* StreamListScreen::getDefaultFocus()
{
    if (this->content)
        return this->content->getDefaultFocus();
    return brls::Box::getDefaultFocus();
}

StreamListScreen::~StreamListScreen()
{
    *this->alive = false;
}

void StreamListScreen::queryAddon(size_t addonIndex)
{
    auto& addons = AddonStore::instance().all();

    if (addonIndex >= addons.size())
    {
        this->finish();
        return;
    }

    if (!addons[addonIndex].enabled)
    {
        this->queryAddon(addonIndex + 1);
        return;
    }

    std::string addonUrl = addons[addonIndex].url;
    std::string host     = hostOf(addonUrl);

    stremio::fetchStream(
        addonUrl, this->type, this->videoId, this->alive,
        [this, addonIndex, host](std::vector<stremio::StreamEntry> streams) {
            for (auto& s : streams)
            {
                s.addonName = host;
                this->collected.push_back(s);
            }
            this->queryAddon(addonIndex + 1);
        },
        [this, addonIndex](std::string) { this->queryAddon(addonIndex + 1); });
}

void StreamListScreen::finish()
{
    if (this->loadingGate)
    {
        this->removeView(this->loadingGate);
        this->loadingGate = nullptr;
    }

    // Only a direct http(s) `url` can actually be played -- there's no
    // torrent/debrid backend here (same reasoning StreamNX-main's own
    // stream picker uses: an infoHash with no resolved url just means "no
    // debrid service configured on that addon," not something this app, or
    // even a real Stremio client without its torrent server, can hand a
    // player directly).
    std::vector<stremio::StreamEntry> playable;
    for (auto& entry : this->collected)
        if (!entry.url.empty())
            playable.push_back(entry);

    if (playable.empty())
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setGrow(1.0f);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setFocusable(true);

        auto* label = new brls::Label();
        label->setText(this->collected.empty()
                ? "Nenhum stream encontrado nos addons ativos."
                : "Os streams encontrados sao apenas torrents (sem link direto) --\n"
                  "e preciso um addon com servico de debrid para assistir.");
        label->setFontSize(17);
        label->setTextColor(nvgRGB(180, 180, 180));
        label->setIsWrapping(true);
        label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        box->addView(label);

        this->addView(box);
        this->content = box;
        brls::Application::giveFocus(this);
        return;
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setDimensions(brls::View::AUTO, brls::View::AUTO);

    for (auto& entry : playable)
        list->addView(new StreamRow(entry, [this](const stremio::StreamEntry& e) { this->play(e); }));

    scroll->setContentView(list);
    this->addView(scroll);

    this->content = list;
    brls::Application::giveFocus(this);
}

void StreamListScreen::play(const stremio::StreamEntry& entry)
{
    if (entry.url.empty())
        return;

    // StreamNX-main's own RemoteView::play() explicitly pushes its player
    // with TransitionAnimation::NONE rather than the default fade -- a
    // fade-in animates this screen's own alpha every frame for its
    // duration, and something about that interacting with a view that's
    // continuously redrawing itself via a raw GL call outside nanovg's own
    // pipeline (rather than a normal static screen) may not have been
    // completing/settling the way it does for every other screen.
    brls::Application::pushActivity(
        new brls::Activity(new PlayerScreen(entry.url, this->title)), brls::TransitionAnimation::NONE);

    ContinueWatchingStore::instance().touch(ContinueWatching {
        this->showId.empty() ? this->videoId : this->showId,
        this->type,
        this->title,
        this->posterUrl,
        this->subtitle,
    });
}
