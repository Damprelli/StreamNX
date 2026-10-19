#include "screen/stream_list_screen.hpp"
#include "screen/player_screen.hpp"
#include "screen/downloads_screen.hpp"
#include "api/download_manager.hpp"
#include "view/loading_gate.hpp"
#include "model/addon_store.hpp"
#include "model/continue_watching_store.hpp"
#include "model/playback_context.hpp"
#include "api/stremio_api.hpp"
#include "utils/mouse.hpp"
#include "view/ui_kit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

#ifdef __SWITCH__
// True when `token` appears in `text` as a whole word -- "dv" must match
// "4k DV | HDR" but not "DVDRip", "hdr" must not match inside "hdrip".
bool hasWord(const std::string& text, const std::string& token)
{
    auto isWordChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
    for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + 1))
    {
        bool startOk = pos == 0 || !isWordChar(text[pos - 1]);
        size_t end   = pos + token.size();
        bool endOk   = end >= text.size() || !isWordChar(text[end]);
        if (startOk && endOk)
            return true;
    }
    return false;
}

// Addons (Torrentio and co.) advertise each stream's format in its
// name/title: "4k DV | HDR", "2160p ... HDR10 x265 10bit", "1080p AV1"...
// The Switch can't play those -- the Tegra's video decoder path used here
// tops out at 1080p 8-bit, it has no AV1 decoder at all, and a 4K HDR/Dolby
// Vision file crashes the renderer (deko3d) outright. Anything that says so
// is hidden rather than offered and left to fail; a stream that says nothing
// about its format is kept.
bool isSwitchPlayable(const stremio::StreamEntry& entry)
{
    std::string text = entry.name + " " + entry.title;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    static const char* kUnsupportedWords[] = {
        "2160p", "4k", "uhd", "1440p",                          // above 1080p
        "hdr", "hdr10", "hdr10plus", "dv", "dovi", "hlg",       // HDR / Dolby Vision
        "10bit", "hi10", "hi10p",                              // 10-bit
        "av1",                                                  // no decoder
    };
    for (const char* word : kUnsupportedWords)
        if (hasWord(text, word))
            return false;

    for (const char* phrase : { "dolby vision", "10-bit", "10 bit" })
        if (text.find(phrase) != std::string::npos)
            return false;

    return true;
}
#endif

// Addons like Torrentio pack their name/title with emoji (seeds, size,
// source icons...) that this app's font has no glyphs for -- they render as
// unreadable tofu boxes instead of silently falling back to anything.
//
// Every emoji introduced after Unicode 6 (the huge "modern" set: people,
// objects, flags...) sits in the Supplementary Multilingual Plane and
// encodes as a 4-byte UTF-8 sequence (lead byte 0xF0-0xF4) -- ordinary text
// (accented Latin, Cyrillic, CJK) never needs more than 3 bytes, so those
// are always safe to drop. The older, pre-emoji "dingbats"/misc-symbols
// block (U+2600-U+27BF -- gear, umbrella, scissors...) and standalone
// variation selectors (U+FE00-U+FE0F, the invisible modifier that forces an
// emoji-style render of the character before it) both live in the Basic
// Multilingual Plane as 3-byte sequences instead, so those get decoded and
// checked by codepoint rather than blanket-dropped like real 3-byte text.
std::string stripUnrenderableSymbols(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    size_t i = 0;
    while (i < text.size())
    {
        unsigned char c = static_cast<unsigned char>(text[i]);

        if (c >= 0xF0 && c <= 0xF4)
        {
            i += 4; // 4-byte sequence: modern emoji (SMP), always droppable
            continue;
        }

        if ((c & 0xF0) == 0xE0 && i + 2 < text.size())
        {
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            uint32_t cp = (static_cast<uint32_t>(c & 0x0F) << 12) |
                          (static_cast<uint32_t>(c1 & 0x3F) << 6) |
                          static_cast<uint32_t>(c2 & 0x3F);
            if ((cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0xFE00 && cp <= 0xFE0F))
            {
                i += 3;
                continue;
            }
        }

        out += text[i];
        ++i;
    }

    return out;
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
    bool unsupportedCodecOnSwitch = false; // AV1: no hwaccel on the Switch's Tegra X1, software decode can't keep up
};

// Torrentio (and others) already separate their own name/title into
// meaningful lines -- e.g. the filename on one line, "100 seeds  5.3 GB
// ThePirateBay" on the next. Splitting on that instead of collapsing
// everything onto one line keeps each piece of information readable on its
// own row instead of running together into a single hard-to-parse string.
std::vector<std::string> splitCleanLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;

    auto flush = [&]() {
        std::string cleaned = stripUnrenderableSymbols(current);
        // Collapse the runs of whitespace the stripped-out emoji leave
        // behind (e.g. "100 <removed> 5.3 GB" -> "100  5.3 GB") down to a
        // single space, and trim the ends.
        std::string collapsed;
        bool lastWasSpace = true;
        for (char c : cleaned)
        {
            bool isSpace = (c == ' ' || c == '\t' || c == '\r');
            if (isSpace && lastWasSpace)
                continue;
            collapsed += isSpace ? ' ' : c;
            lastWasSpace = isSpace;
        }
        while (!collapsed.empty() && collapsed.back() == ' ')
            collapsed.pop_back();

        if (!collapsed.empty())
            lines.push_back(collapsed);
        current.clear();
    };

    for (char c : text)
    {
        if (c == '\n')
            flush();
        else
            current += c;
    }
    flush();

    return lines;
}

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

#ifdef __SWITCH__
    // The ffmpeg build shipped for Switch (see scripts/switch/ffmpeg) only
    // has a hardware decode path (the nvtegra patches) for h264/hevc/vp8/
    // vp9/mpeg1-4/vc1 -- AV1 is software-only (dav1d), and the Tegra X1's
    // Cortex-A57 cores can't keep up with it in real time, so these streams
    // reliably stall/fail instead of playing. Flagged only on Switch: every
    // other platform decodes AV1 fine.
    static const std::regex av1Re(R"(\bAV1\b)", std::regex::icase);
    if (std::regex_search(text, av1Re))
        b.unsupportedCodecOnSwitch = true;
#endif

    return b;
}

// Three stacked lines -- badges+source on top, the release name on its own
// line, then size/extra detail below -- instead of cramming everything into
// one row. A torrent stream also gets its own magenta "Torrent" tag next to
// the quality badges, so it's never confused with a direct HTTP link (which
// plays instantly) at a glance -- torrents need a download/buffer step first.
class StreamRow : public HoverBox
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
        this->setHoverColors(ui::color("surface"), ui::color("surface_raised"));

        std::vector<std::string> nameLines  = splitCleanLines(entry.name);
        std::vector<std::string> titleLines = splitCleanLines(entry.title);
        std::string cleanName = nameLines.empty() ? "" : nameLines.front();

        StreamBadges badges = parseBadges(entry.name + " " + entry.title);
        bool isTorrent       = !entry.infoHash.empty();

        // Line 1: type + quality/HDR badges, addon source pinned to the right.
        auto* topRow = new brls::Box();
        topRow->setAxis(brls::Axis::ROW);
        topRow->setAlignItems(brls::AlignItems::CENTER);

        topRow->addView(isTorrent
                ? makePill("Torrent", nvgRGB(148, 58, 122), nvgRGB(255, 226, 247))
                : makePill("Direto", nvgRGB(45, 125, 85), nvgRGB(214, 250, 230)));
        if (!badges.quality.empty())
            topRow->addView(makePill(badges.quality, nvgRGB(52, 90, 168), nvgRGB(226, 234, 250)));
        if (!badges.extra.empty())
            topRow->addView(makePill(badges.extra, nvgRGB(122, 74, 214), nvgRGB(255, 255, 255)));
        if (badges.unsupportedCodecOnSwitch)
            topRow->addView(makePill("AV1 - pode nao rodar", nvgRGB(120, 40, 20), nvgRGB(255, 205, 178)));

        auto* spacer = new brls::Box();
        spacer->setGrow(1.0f);
        topRow->addView(spacer);

        auto* addonBadge = makePill(entry.addonName, nvgRGB(40, 43, 51), nvgRGB(180, 185, 195));
        addonBadge->setMarginRight(0);
        topRow->addView(addonBadge);

        this->addView(topRow);

        // Line 2: the release name, on its own so it's never sharing space
        // (and getting truncated) with the badges or the addon tag above.
        auto* name = new brls::Label();
        name->setText(cleanName.empty() ? "Stream" : cleanName);
        name->setFontSize(14);
        name->setSingleLine(true);
        name->setMarginTop(6);
        this->addView(name);

        // Remaining lines: any extra line the addon's own name/title already
        // broke out on its own (filename, then something like "100 seeds
        // 5.3 GB ThePirateBay" on the next) -- each kept on its own row
        // exactly as the addon split it, instead of joining them back into
        // one string. Skips whichever line became the name above (already
        // shown), and any exact repeat (some addons echo the same line in
        // both fields).
        std::vector<std::string> seen = { cleanName };
        for (auto* group : { &nameLines, &titleLines })
        {
            for (auto& line : *group)
            {
                if (std::find(seen.begin(), seen.end(), line) != seen.end())
                    continue;
                seen.push_back(line);

                auto* detailLabel = new brls::Label();
                detailLabel->setText(line);
                detailLabel->setFontSize(11);
                detailLabel->setTextColor(nvgRGB(150, 150, 155));
                detailLabel->setSingleLine(true);
                detailLabel->setMarginTop(4);
                this->addView(detailLabel);
            }
        }

        this->registerAction("Assistir", brls::BUTTON_A, [entry, onPlay](brls::View*) {
            onPlay(entry);
            return true;
        });
        makeClickable(this);
    }
};

} // namespace

StreamListScreen::StreamListScreen(const std::string& type, const std::string& videoId, const std::string& title,
    const std::string& subtitle, const std::string& showId, const std::string& posterUrl,
    const std::string& nextVideoId, const std::string& nextTitle, const std::string& nextSubtitle)
    : type(type)
    , videoId(videoId)
    , title(title)
    , subtitle(subtitle)
    , showId(showId)
    , posterUrl(posterUrl)
    , nextVideoId(nextVideoId)
    , nextTitle(nextTitle)
    , nextSubtitle(nextSubtitle)
{
    this->setAxis(brls::Axis::COLUMN);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->setBackgroundColor(ui::color("bg"));
    this->setPadding(30, 50, 30, 50);
#ifdef STREAMNX_PC_UI
    // Back bar spans the full width: negative margins cancel the screen's
    // own padding for just this strip.
    auto* backBar = ui::backBar();
    backBar->setMargins(-30, -50, 20, -50);
    this->addView(backBar);
#endif
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

#ifndef STREAMNX_PC_UI
    this->addView(new brls::BottomBar());
#endif

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

    // Filter playable streams: accept both HTTP URLs and torrents (via infoHash)
    // HTTP URLs play directly; torrents use download_manager for streaming
    std::vector<stremio::StreamEntry> playable;
    size_t hiddenByFormat = 0;
    for (auto& entry : this->collected)
    {
        if (entry.url.empty() && entry.infoHash.empty())
            continue;
#ifdef __SWITCH__
        if (!isSwitchPlayable(entry))
        {
            ++hiddenByFormat;
            continue;
        }
#endif
        playable.push_back(entry);
    }

    std::string hiddenNote;
    if (hiddenByFormat > 0)
        hiddenNote = std::to_string(hiddenByFormat) + (hiddenByFormat == 1 ? " stream oculto" : " streams ocultos") +
                     " por formato incompativel com o Switch (4K, HDR, Dolby Vision, 10-bit ou AV1).";

    if (playable.empty())
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setGrow(1.0f);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setFocusable(true);

        auto* label = new brls::Label();
        label->setText(hiddenNote.empty() ? "Nenhum stream encontrado nos addons ativos."
                                          : "Nenhum stream compativel encontrado. " + hiddenNote);
        label->setFontSize(17);
        label->setTextColor(nvgRGB(180, 180, 180));
        label->setIsWrapping(true);
        label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        box->addView(label);

        ui::addAboveFooter(this, box);
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

    // Direct HTTP links (the kind that used to be the only ones shown, and
    // play instantly) listed before torrents (which need a download/buffer
    // step first) -- grouped under their own headers instead of interleaved,
    // so the two very different "tap and it plays" vs. "tap and wait" flows
    // don't look like one homogeneous list.
    auto addSection = [list](const std::string& label) {
        auto* header = new brls::Label();
        header->setText(label);
        header->setFontSize(13);
        header->setTextColor(nvgRGB(140, 140, 148));
        header->setMarginTop(list->getChildren().empty() ? 0 : 16);
        header->setMarginBottom(8);
        list->addView(header);
    };

    if (!hiddenNote.empty())
    {
        auto* note = new brls::Label();
        note->setText(hiddenNote);
        note->setFontSize(13);
        note->setTextColor(nvgRGB(140, 140, 148));
        note->setIsWrapping(true);
        note->setMarginBottom(12);
        list->addView(note);
    }

    std::vector<stremio::StreamEntry> direct, torrents;
    for (auto& entry : playable)
        (entry.infoHash.empty() ? direct : torrents).push_back(entry);

    auto onPlay = [this](const stremio::StreamEntry& e) { this->play(e); };

    // A single addon (Torrentio especially) can report well over a hundred
    // results. Each row draws several fill/text elements, and this app's
    // renderer -- deko3d on Switch -- pulls its per-frame draw-command
    // memory from a small fixed pool shared across the whole app; a
    // screen's worth of rows can exhaust it before the round-trip to fix
    // the pool size ships. Capping here is cheap insurance against that,
    // and a list this long was never realistically scrollable anyway.
    constexpr size_t kMaxRowsPerSection = 60;
    if (direct.size() > kMaxRowsPerSection)
        direct.resize(kMaxRowsPerSection);
    if (torrents.size() > kMaxRowsPerSection)
        torrents.resize(kMaxRowsPerSection);

    if (!direct.empty())
    {
        addSection("Links diretos");
        for (auto& entry : direct)
            list->addView(new StreamRow(entry, onPlay));
    }
    if (!torrents.empty())
    {
        addSection("Torrents");
        for (auto& entry : torrents)
            list->addView(new StreamRow(entry, onPlay));
    }

    scroll->setContentView(list);
    ui::addAboveFooter(this, scroll);

    this->content = list;
    brls::Application::giveFocus(this);
}

void StreamListScreen::play(const stremio::StreamEntry& entry)
{
    // Anything with an infoHash is listed under the "Torrent" tag (see the
    // direct/torrents split in the list builder), so it's downloaded the
    // same way even if the addon also sent a url -- picking a row tagged
    // Torrent always means "baixar", never "tocar direto".
    if (!entry.infoHash.empty())
    {
        this->playTorrent(entry);
    }
    else if (!entry.url.empty())
    {
        this->playUrl(entry.url);
    }
    else
    {
        brls::Logger::warning("Stream entry has neither URL nor infoHash");
    }
}

void StreamListScreen::playUrl(const std::string& url)
{
    PlaybackContext ctx;
    ctx.continueWatchingId = this->showId.empty() ? this->videoId : this->showId;
    ctx.type               = this->type;
    ctx.posterUrl          = this->posterUrl;
    ctx.subtitle           = this->subtitle;
    ctx.nextVideoId        = this->nextVideoId;
    ctx.nextTitle          = this->nextTitle;
    ctx.nextSubtitle       = this->nextSubtitle;

    // StreamNX-main's own RemoteView::play() explicitly pushes its player
    // with TransitionAnimation::NONE rather than the default fade -- a
    // fade-in animates this screen's own alpha every frame for its
    // duration, and something about that interacting with a view that's
    // continuously redrawing itself via a raw GL call outside nanovg's own
    // pipeline (rather than a normal static screen) may not have been
    // completing/settling the way it does for every other screen.
    brls::Application::pushActivity(
        new brls::Activity(new PlayerScreen(url, this->title, ctx)), brls::TransitionAnimation::NONE);

    ContinueWatchingStore::instance().touch(ContinueWatching {
        ctx.continueWatchingId,
        ctx.type,
        this->title,
        ctx.posterUrl,
        ctx.subtitle,
        /* positionSeconds */ 0.0,
        /* durationSeconds */ 0.0,
        /* isTorrent */ false,
        /* streamUrl */ url,
    });
}

void StreamListScreen::playTorrent(const stremio::StreamEntry& entry)
{
#ifdef USE_TORRENT
    brls::Logger::info("ui: torrent selected {} ({})", entry.infoHash, entry.title.empty() ? entry.name : entry.title);
    // Torrents aren't played while they download anymore: picking one starts
    // the download in the background -- owned by download_manager, not by
    // any screen, so it keeps going while the user browses elsewhere -- and
    // opens Downloads, where it shows live progress and, once finished, the
    // file's info with Assistir/Apagar.
    bool alreadyTracked = false;
    for (auto& d : download_mgr::getFinishedDownloads())
        if (d.infoHash == entry.infoHash)
        {
            alreadyTracked = true;
            brls::Application::notify("Esse torrent ja foi baixado");
        }
    for (auto& d : download_mgr::getActiveDownloads())
        if (d.infoHash == entry.infoHash && !d.failed)
        {
            alreadyTracked = true;
            brls::Application::notify("Esse torrent ja esta sendo baixado");
        }

    if (!alreadyTracked)
    {
        std::string downloadTitle = this->title;
        if (!this->subtitle.empty())
            downloadTitle += " - " + this->subtitle;

        // Never flipped to false by anything but cancelDownload() -- the
        // download outlives every screen, including this one.
        auto alive = std::make_shared<bool>(true);

        download_mgr::startTorrentDownload(
            entry.infoHash,
            downloadTitle,
            alive,
            nullptr,
            [downloadTitle](const std::string&) { brls::Application::notify("Download concluido: " + downloadTitle); },
            [](const std::string& error) { brls::Application::notify("Falha no download: " + error); },
            entry.trackers,
            this->posterUrl);
    }

    brls::Application::pushActivity(new brls::Activity(new DownloadsScreen(true)), brls::TransitionAnimation::NONE);
#else
    brls::Logger::error("Torrent download not available (libtorrent not compiled)");
    brls::Application::notify("Torrents não suportados nesta compilação");
#endif
}
