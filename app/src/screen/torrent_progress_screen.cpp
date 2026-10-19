#include "screen/torrent_progress_screen.hpp"
#include "screen/player_screen.hpp"
#include "model/continue_watching_store.hpp"
#include "api/download_manager.hpp"
#include "utils/mouse.hpp"
#include "theme/streamnx_theme.hpp"

#include <cstdio>

namespace
{

std::string formatMB(int64_t bytes)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

} // namespace

TorrentProgressScreen::TorrentProgressScreen(const stremio::StreamEntry& entry, const std::string& title, const PlaybackContext& ctx)
    : title(title)
    , ctx(ctx)
    , infoHash(entry.infoHash)
    , torrentName(entry.title.empty() ? entry.name : entry.title)
    , trackers(entry.trackers)
{
    this->setAxis(brls::Axis::COLUMN);
    this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
    this->setBackgroundColor(nvgRGB(12, 13, 18));
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(30, 50, 30, 50);
    this->setFocusable(true);

    this->registerAction(
        "Cancelar", brls::BUTTON_B, [](brls::View*) {
            // Leaves it to the download manager's own background loop to
            // actually tear the torrent down (it already polls the
            // AliveFlag this screen's destructor flips below) instead of
            // calling cancelDownload() here directly -- that call runs
            // tr_torrentRemove()/remove_torrent() synchronously, on the UI
            // thread, and can block waiting on the torrent library's own
            // internal thread while it's mid-operation (e.g. talking to
            // peers). Popping immediately keeps B responsive no matter
            // what state the download is in; the corresponding cleanup
            // still happens, just a moment later in the background.
            brls::Application::popActivity();
            return true;
        },
        false, false, brls::SOUND_BACK);

    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setDimensions(600, brls::View::AUTO);
    box->setAlignItems(brls::AlignItems::CENTER);

    auto* header = new brls::Label();
    header->setText(title.empty() ? "Preparando torrent..." : title);
    header->setFontSize(22);
    header->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    header->setIsWrapping(true);
    header->setMarginBottom(20);
    box->addView(header);

    this->statusLabel = new brls::Label();
    this->statusLabel->setText("Conectando aos peers...");
    this->statusLabel->setFontSize(16);
    this->statusLabel->setTextColor(nvgRGB(200, 200, 205));
    this->statusLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->statusLabel->setMarginBottom(16);
    box->addView(this->statusLabel);

    this->progressBarTrack = new brls::Box();
    this->progressBarTrack->setDimensions(500, 10);
    this->progressBarTrack->setBackgroundColor(nvgRGB(35, 38, 46));
    this->progressBarTrack->setCornerRadius(5);
    this->progressBarTrack->setMarginBottom(10);
    box->addView(this->progressBarTrack);

    this->progressBarFill = new brls::Box();
    this->progressBarFill->setDimensions(0, 10);
    this->progressBarFill->setBackgroundColor(nvgRGB(52, 152, 219));
    this->progressBarFill->setCornerRadius(5);
    this->progressBarTrack->addView(this->progressBarFill);

    this->detailLabel = new brls::Label();
    this->detailLabel->setText("0.0 MB / 0.0 MB");
    this->detailLabel->setFontSize(13);
    this->detailLabel->setTextColor(nvgRGB(150, 150, 155));
    this->detailLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    box->addView(this->detailLabel);

#ifdef STREAMNX_PC_UI
    // No footer hint to advertise BUTTON_B on PC, and this screen has no
    // BottomBar of its own even on other platforms -- a real button.
    auto* cancelButton = new brls::Box();
    cancelButton->setFocusable(true);
    cancelButton->setDimensions(brls::View::AUTO, brls::View::AUTO);
    cancelButton->setAlignItems(brls::AlignItems::CENTER);
    cancelButton->setJustifyContent(brls::JustifyContent::CENTER);
    cancelButton->setPadding(10, 24, 10, 24);
    cancelButton->setCornerRadius(8);
    cancelButton->setBackgroundColor(brls::Application::getTheme().getColor("streamnx/surface"));
    cancelButton->setMarginTop(24);

    auto* cancelLabel = new brls::Label();
    cancelLabel->setText("Cancelar");
    cancelLabel->setFontSize(16);
    cancelButton->addView(cancelLabel);

    makeClickable(cancelButton, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    box->addView(cancelButton);
#endif

    this->addView(box);

    brls::Application::giveFocus(this);

#ifdef USE_TORRENT
    auto playerAlive = std::make_shared<bool>(true);
    auto screenAlive = this->alive;

    download_mgr::startTorrentDownload(
        entry.infoHash,
        entry.title.empty() ? entry.name : entry.title,
        playerAlive,
        [this, screenAlive](float percent, int64_t downloadedBytes, int64_t totalBytes) {
            if (!*screenAlive)
                return;
            brls::sync([this, screenAlive, percent, downloadedBytes, totalBytes] {
                if (*screenAlive)
                    this->setProgress(percent, downloadedBytes, totalBytes);
            });
        },
        [this, screenAlive, playerAlive](const std::string& localUrl) {
            if (!*screenAlive)
                return;
            // Copy everything this lambda needs out of `this` before touching
            // the activity stack -- popActivity() below destroys this screen,
            // so `this->...` can't be read anymore once it's called.
            std::string title = this->title;
            PlaybackContext ctx = this->ctx;
            std::string infoHash = this->infoHash, torrentName = this->torrentName;
            std::vector<std::string> trackers = this->trackers;
            brls::sync([screenAlive, playerAlive, localUrl, title, ctx, infoHash, torrentName, trackers] {
                if (!*screenAlive)
                    return;

                // Pop this progress screen first -- pushing PlayerScreen and
                // then popping would instead pop PlayerScreen itself right
                // back off (it's now the top of the stack), destroying it on
                // the spot and, since it owns `playerAlive`, immediately
                // signalling download_manager that the player was closed.
                brls::Application::popActivity();

                brls::Application::pushActivity(
                    new brls::Activity(new PlayerScreen(localUrl, title, playerAlive, ctx)),
                    brls::TransitionAnimation::NONE);

                if (!ctx.continueWatchingId.empty())
                {
                    ContinueWatchingStore::instance().touch(ContinueWatching {
                        ctx.continueWatchingId,
                        ctx.type,
                        title,
                        ctx.posterUrl,
                        ctx.subtitle,
                        ctx.resumePositionSeconds,
                        0.0,
                        /* isTorrent */ true,
                        /* streamUrl */ "",
                        infoHash,
                        torrentName,
                        trackers,
                    });
                }
            });
        },
        [this, screenAlive, entry](const std::string& error) {
            if (!*screenAlive)
                return;
            brls::sync([this, screenAlive, entry, error] {
                if (!*screenAlive)
                    return;

                brls::Logger::error("Torrent download error: {}", error);

                if (!entry.url.empty())
                {
                    this->setStatus("Torrent falhou, tentando HTTP direto...");
                    this->startHttpFallback(entry.url);
                }
                else
                {
                    this->setStatus("Erro: " + error);
                    brls::Application::notify("Falha ao baixar torrent");
                }
            });
        },
        entry.trackers,
        this->ctx.posterUrl);
#else
    this->setStatus("Torrents não suportados nesta compilação");
    brls::Application::notify("Torrents não suportados nesta compilação");
#endif
}

TorrentProgressScreen::~TorrentProgressScreen()
{
    *this->alive = false;
}

brls::View* TorrentProgressScreen::getDefaultFocus()
{
    return this;
}

void TorrentProgressScreen::setStatus(const std::string& text)
{
    if (this->statusLabel)
        this->statusLabel->setText(text);
}

void TorrentProgressScreen::setProgress(float percent, int64_t downloadedBytes, int64_t totalBytes)
{
    if (this->statusLabel)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Baixando: %.1f%%", percent);
        this->statusLabel->setText(buf);
    }

    if (this->detailLabel)
        this->detailLabel->setText(formatMB(downloadedBytes) + " / " + formatMB(totalBytes));

    if (this->progressBarFill)
    {
        float clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
        this->progressBarFill->setDimensions(500 * (clamped / 100.0f), 10);
    }
}

void TorrentProgressScreen::startHttpFallback(const std::string& url)
{
#ifdef USE_TORRENT
    auto playerAlive = std::make_shared<bool>(true);
    auto screenAlive = this->alive;

    download_mgr::startHttpDownload(
        url,
        this->title,
        playerAlive,
        nullptr,
        [this, screenAlive, playerAlive](const std::string& httpUrl) {
            if (!*screenAlive)
                return;
            std::string title = this->title;
            PlaybackContext ctx = this->ctx;
            brls::sync([screenAlive, playerAlive, httpUrl, title, ctx] {
                if (!*screenAlive)
                    return;

                brls::Application::popActivity();

                brls::Application::pushActivity(
                    new brls::Activity(new PlayerScreen(httpUrl, title, playerAlive, ctx)),
                    brls::TransitionAnimation::NONE);

                if (!ctx.continueWatchingId.empty())
                {
                    ContinueWatchingStore::instance().touch(ContinueWatching {
                        ctx.continueWatchingId,
                        ctx.type,
                        title,
                        ctx.posterUrl,
                        ctx.subtitle,
                        ctx.resumePositionSeconds,
                        0.0,
                        /* isTorrent */ false,
                        httpUrl,
                    });
                }
            });
        },
        [this, screenAlive](const std::string& httpError) {
            if (!*screenAlive)
                return;
            brls::sync([this, screenAlive, httpError] {
                if (!*screenAlive)
                    return;
                this->setStatus("Erro: torrent e HTTP falharam");
                brls::Application::notify("Erro: torrent e HTTP falharam");
            });
        });
#endif
}
