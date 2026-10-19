#include "screen/downloads_screen.hpp"
#include "screen/player_screen.hpp"
#include "view/loading_gate.hpp"
#include "api/download_manager.hpp"
#include "api/image_cache.hpp"
#include "utils/mouse.hpp"
#include "view/ui_kit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>

namespace
{

constexpr float kProgressBarWidth = 360.0f;
constexpr float kStorageBarWidth  = 90.0f;

std::string formatSize(int64_t bytes)
{
    double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    char buf[32];
    if (gb >= 1.0)
        std::snprintf(buf, sizeof(buf), "%.2f GB", gb);
    else
        std::snprintf(buf, sizeof(buf), "%.0f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

std::string formatSpeed(uint64_t bytesPerSecond)
{
    char buf[32];
    if (bytesPerSecond >= 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f MB/s", bytesPerSecond / (1024.0 * 1024.0));
    else
        std::snprintf(buf, sizeof(buf), "%.0f KB/s", bytesPerSecond / 1024.0);
    return buf;
}

// "Movie.2024.1080p.mkv" -> "MKV"
std::string fileFormat(const std::string& fileName)
{
    std::string ext = std::filesystem::path(fileName).extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return ext;
}

brls::Label* makeLabel(const std::string& text, float fontSize, NVGcolor color)
{
    return ui::label(text, fontSize, color);
}

brls::Box* makeButton(const std::string& text, ui::ButtonStyle style, const char* icon, std::function<void()> onClick)
{
    auto* button = ui::button(text, style, std::move(onClick), icon);
    button->setMarginLeft(10);
    return button;
}

// A row card: raised surface that lights up under the mouse on PC.
brls::Box* makeRowShell()
{
    auto* row = new HoverBox();
    row->setAxis(brls::Axis::ROW);
    row->setFocusable(true);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setDimensions(brls::View::AUTO, brls::View::AUTO);
    row->setPadding(14, 20, 14, 20);
    row->setMarginBottom(10);
    row->setCornerRadius(12);
    row->setHighlightCornerRadius(12);
    row->setHoverColors(ui::color("surface"), ui::color("surface_raised"));
    return row;
}

void addPoster(brls::Box* row, const std::string& posterUrl, const std::shared_ptr<bool>& alive)
{
    if (posterUrl.empty())
        return;
    auto* poster = new brls::Image();
    poster->setDimensions(48, 70);
    poster->setCornerRadius(8);
    poster->setScalingType(brls::ImageScalingType::FILL);
    poster->setMarginRight(16);
    imgcache::load(poster, posterUrl, alive);
    row->addView(poster);
}

brls::Label* makeSectionHeader(const std::string& text)
{
    auto* header = makeLabel(text, 14, ui::color("text_muted"));
    header->setMarginTop(8);
    header->setMarginBottom(10);
    return header;
}

// One finished download: poster, title and the downloaded file's own info
// (name, format, size), with "Assistir" (plays the file straight from disk)
// and "Apagar". The two buttons are siblings of, not nested inside, the
// row's own click target -- see AddonRow's own comment on why (borealis'
// tap gesture bubbles to every clickable ancestor with no way to "consume"
// it, so nesting two different actions double-fires both).
class FinishedDownloadRow : public brls::Box
{
  public:
    FinishedDownloadRow(const download_mgr::FinishedDownload& d, std::function<void()> onWatch, std::function<void()> onDelete)
    {
        brls::Box* shell = makeRowShell();
        this->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->addView(shell);

        addPoster(shell, d.posterUrl, this->alive);

        auto* infoCol = new brls::Box();
        infoCol->setAxis(brls::Axis::COLUMN);
        infoCol->setGrow(1.0f);
        infoCol->setShrink(1.0f);

        auto* titleLabel = makeLabel(d.title.empty() ? "Download" : d.title, 17, ui::color("text"));
        titleLabel->setSingleLine(true);
        infoCol->addView(titleLabel);

        if (!d.fileName.empty())
        {
            auto* fileLabel = makeLabel(d.fileName, 13, ui::color("text_muted"));
            fileLabel->setSingleLine(true);
            fileLabel->setMarginTop(4);
            infoCol->addView(fileLabel);
        }

        std::string details = "Concluido · " + formatSize(d.totalBytes);
        std::string format  = fileFormat(d.fileName);
        if (!format.empty())
            details += " · " + format;
        auto* detailLabel = makeLabel(details, 13, ui::color("success"));
        detailLabel->setMarginTop(4);
        infoCol->addView(detailLabel);

        shell->addView(infoCol);
        shell->addView(makeButton("Assistir", ui::ButtonStyle::Primary, ui::icons::play, onWatch));
        shell->addView(makeButton("Apagar", ui::ButtonStyle::Danger, ui::icons::trash, onDelete));

        // Gamepad path -- separate from the two buttons above (no
        // TapGestureRecognizer on the row itself, so no conflict with them).
        shell->registerAction("Assistir", brls::BUTTON_A, [onWatch](brls::View*) {
            onWatch();
            return true;
        });
        shell->registerAction("Apagar", brls::BUTTON_RB, [onDelete](brls::View*) {
            onDelete();
            return true;
        });
    }

  private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

} // namespace

// A download still in progress (or one that failed): poster, title, phase,
// progress bar and live numbers, updated in place by DownloadsScreen::poll()
// via update() -- plus "Cancelar" (or "Remover", once it has failed).
class ActiveDownloadRow : public brls::Box
{
  public:
    ActiveDownloadRow(const download_mgr::DownloadInfo& info, std::function<void()> onCancel)
    {
        brls::Box* shell = makeRowShell();
        this->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->addView(shell);

        addPoster(shell, info.posterUrl, this->alive);

        auto* infoCol = new brls::Box();
        infoCol->setAxis(brls::Axis::COLUMN);
        infoCol->setGrow(1.0f);
        infoCol->setShrink(1.0f);

        auto* titleLabel = makeLabel(info.title.empty() ? "Download" : info.title, 17, ui::color("text"));
        titleLabel->setSingleLine(true);
        infoCol->addView(titleLabel);

        this->statusLabel = makeLabel("", 13, ui::color("text"));
        this->statusLabel->setMarginTop(4);
        this->statusLabel->setIsWrapping(true);
        infoCol->addView(this->statusLabel);

        if (!info.failed)
        {
            auto* track = new brls::Box();
            track->setDimensions(kProgressBarWidth, 8);
            track->setBackgroundColor(ui::color("surface_hover"));
            track->setCornerRadius(4);
            track->setMarginTop(8);
            this->progressFill = new brls::Box();
            this->progressFill->setDimensions(0, 8);
            this->progressFill->setBackgroundColor(ui::color("accent"));
            this->progressFill->setCornerRadius(4);
            track->addView(this->progressFill);
            infoCol->addView(track);

            this->detailLabel = makeLabel("", 13, ui::color("text_muted"));
            this->detailLabel->setMarginTop(6);
            infoCol->addView(this->detailLabel);

            this->etaLabel = makeLabel("", 13, ui::color("accent_hover"));
            this->etaLabel->setMarginTop(4);
            infoCol->addView(this->etaLabel);
        }

        shell->addView(infoCol);

        const char* actionText = info.failed ? "Remover" : "Cancelar";
        shell->addView(makeButton(actionText, ui::ButtonStyle::Danger, info.failed ? ui::icons::trash : ui::icons::close, onCancel));
        shell->registerAction(actionText, brls::BUTTON_RB, [onCancel](brls::View*) {
            onCancel();
            return true;
        });

        this->update(info);
    }

    void update(const download_mgr::DownloadInfo& info)
    {
        if (info.failed)
        {
            this->statusLabel->setText("Falhou: " + (info.error.empty() ? std::string("erro desconhecido") : info.error));
            this->statusLabel->setTextColor(ui::color("danger"));
            return;
        }

        char percent[16];
        std::snprintf(percent, sizeof(percent), "%.1f%%", info.percentComplete);
        std::string status = info.status.empty() ? "Iniciando..." : info.status;
        if (info.totalBytes > 0)
            status += " · " + std::string(percent);
        this->statusLabel->setText(status);

        float clamped = std::max(0.0f, std::min(100.0f, info.percentComplete));
        this->progressFill->setWidth(kProgressBarWidth * (clamped / 100.0f));

        // Peer speed swings a lot second to second; an exponential moving
        // average (update() runs ~1x/s) keeps the estimate from jumping
        // between "3 min" and "2 h" on every refresh.
        if (info.speedBps > 0)
            this->smoothedSpeed = this->smoothedSpeed > 0 ? 0.8 * this->smoothedSpeed + 0.2 * info.speedBps
                                                          : (double)info.speedBps;
        else
            this->smoothedSpeed *= 0.8;

        std::string details;
        if (info.totalBytes > 0)
            details = formatSize(info.downloadedBytes) + " / " + formatSize(info.totalBytes);
        else
            details = "Tamanho ainda desconhecido";
        details += " · " + formatSpeed(info.speedBps) + " · " + std::to_string(info.peers) + " peers";
        this->detailLabel->setText(details);

        this->etaLabel->setText(this->estimate(info));
    }

  private:
    // "Faltam ~12 min · termina ~14:52", or why there's no estimate yet.
    std::string estimate(const download_mgr::DownloadInfo& info) const
    {
        int64_t remaining = info.totalBytes - info.downloadedBytes;
        if (info.totalBytes <= 0)
            return "Tempo restante: aguardando metadados";
        if (remaining <= 0)
            return "Finalizando...";
        if (this->smoothedSpeed < 1024) // < 1 KB/s -- effectively stalled
            return "Tempo restante: calculando...";

        int64_t seconds = (int64_t)(remaining / this->smoothedSpeed);
        char text[96];
        if (seconds < 60)
            std::snprintf(text, sizeof(text), "Falta menos de 1 min");
        else if (seconds < 3600)
            std::snprintf(text, sizeof(text), "Faltam ~%lld min", (long long)((seconds + 59) / 60));
        else if (seconds < 100 * 3600)
            std::snprintf(text, sizeof(text), "Faltam ~%lldh %02lldmin", (long long)(seconds / 3600),
                (long long)((seconds % 3600) / 60));
        else
            return "Tempo restante: mais de 4 dias";

        std::time_t finish = std::time(nullptr) + (std::time_t)seconds;
        std::tm finishTm   = *std::localtime(&finish);
        char clock[32];
        if (seconds < 24 * 3600)
            std::strftime(clock, sizeof(clock), " · termina ~%H:%M", &finishTm);
        else
            std::strftime(clock, sizeof(clock), " · termina ~%d/%m %H:%M", &finishTm);
        return std::string(text) + clock;
    }

    brls::Label* statusLabel = nullptr;
    brls::Label* detailLabel = nullptr;
    brls::Label* etaLabel    = nullptr;
    brls::Box* progressFill  = nullptr;
    double smoothedSpeed     = 0.0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

DownloadsScreen::DownloadsScreen(bool standalone)
    : standalone(standalone)
{
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(30, 50, 30, 50);
    // Same reason as AddonsScreen/SearchScreen: a safe place to park focus
    // while refreshList() tears down whichever row's own action (watch,
    // just-confirmed delete) triggered the rebuild.
    this->setFocusable(true);

    if (standalone)
    {
        // Pushed as its own Activity (right after picking a torrent stream)
        // rather than living inside the main menu shell -- needs its own
        // size/background and its own way back.
        this->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
        this->setBackgroundColor(ui::color("bg"));
        this->registerAction(
            "Voltar", brls::BUTTON_B, [](brls::View*) {
                brls::Application::popActivity();
                return true;
            },
            false, false, brls::SOUND_BACK);
    }

    // Fixed-height header row: [Voltar (PC, standalone)] title ... status.
    // Status = clock, battery and free space where downloads are saved --
    // the things worth glancing at while a multi-GB download runs on a
    // handheld. A spacer (not grow on the title) pushes it right: a growing
    // Label inside a ROW gets measured at its min-content width by yoga and
    // wraps ("Downloa/ds"), which also stretched the whole header.
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setHeight(52);
    header->setMarginBottom(18);

#ifdef STREAMNX_PC_UI
    if (standalone)
    {
        auto* back = ui::button("", ui::ButtonStyle::Secondary, []() { brls::Application::popActivity(); }, ui::icons::back);
        back->setMarginRight(16);
        header->addView(back);
    }
#endif

    auto* title = ui::label("Downloads", 28, ui::color("text"));
    title->setSingleLine(true);
    header->addView(title);

    auto* spacer = new brls::Box();
    spacer->setGrow(1.0f);
    header->addView(spacer);

    auto addStatusIcon = [header](const char* glyph) {
        auto* i = ui::icon(glyph, 18, ui::color("text_muted"));
        i->setMarginRight(6);
        header->addView(i);
        return i;
    };

    addStatusIcon(ui::icons::schedule);
    this->clockLabel = ui::label("", 16, ui::color("text"));
    this->clockLabel->setMarginRight(22);
    header->addView(this->clockLabel);

    this->batteryIcon  = addStatusIcon(ui::icons::battery);
    this->batteryLabel = ui::label("", 15, ui::color("text"));
    this->batteryLabel->setMarginRight(22);
    header->addView(this->batteryLabel);

    addStatusIcon(ui::icons::storage);
    this->storageLabel = ui::label("", 15, ui::color("text"));
    this->storageLabel->setMarginRight(10);
    header->addView(this->storageLabel);
    auto* storageTrack = new brls::Box();
    storageTrack->setDimensions(kStorageBarWidth, 6);
    storageTrack->setBackgroundColor(ui::color("surface_raised"));
    storageTrack->setCornerRadius(3);
    this->storageFill = new brls::Box();
    this->storageFill->setDimensions(0, 6);
    this->storageFill->setCornerRadius(3);
    storageTrack->addView(this->storageFill);
    header->addView(storageTrack);

    this->updateStatusBar();

    this->addView(header);

    this->loadingGate = new LoadingGate([this]() { this->buildContent(); }, 200);
    this->addView(this->loadingGate);
}

brls::View* DownloadsScreen::getDefaultFocus()
{
    if (this->listBox)
        if (brls::View* first = this->listBox->getDefaultFocus())
            return first;
    return this;
}

void DownloadsScreen::buildContent()
{
    this->removeView(this->loadingGate);
    this->loadingGate = nullptr;

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    this->listBox = new brls::Box();
    this->listBox->setAxis(brls::Axis::COLUMN);
    this->listBox->setDimensions(brls::View::AUTO, brls::View::AUTO);

    scroll->setContentView(this->listBox);
    this->addView(scroll);

    this->refreshList();

    // Opened right after picking a torrent: land focus on the download
    // that was just started instead of on the bare screen.
    if (this->standalone)
        brls::sync([this]() { brls::Application::giveFocus(this->getDefaultFocus()); });

    this->pollTimer.setCallback([this]() { this->poll(); });
    this->pollTimer.start(1000);
}

std::string DownloadsScreen::listSignature(const std::vector<download_mgr::DownloadInfo>& active,
    const std::vector<download_mgr::FinishedDownload>& finished) const
{
    std::string signature;
    for (auto& a : active)
        signature += (a.failed ? "F:" : "A:") + a.infoHash + ";";
    for (auto& f : finished)
        signature += "C:" + f.infoHash + ";";
    return signature;
}

void DownloadsScreen::updateStatusBar()
{
    std::time_t now = std::time(nullptr);
    std::tm local   = *std::localtime(&now);
    char clock[16];
    std::strftime(clock, sizeof(clock), "%H:%M", &local);
    this->clockLabel->setText(clock);

    brls::Platform* platform = brls::Application::getPlatform();
    bool hasBattery          = platform && platform->canShowBatteryLevel();
    // A desktop without a battery: no icon, no text, no gap.
    auto batteryVisibility = hasBattery ? brls::Visibility::VISIBLE : brls::Visibility::GONE;
    this->batteryIcon->setVisibility(batteryVisibility);
    this->batteryLabel->setVisibility(batteryVisibility);
    if (hasBattery)
    {
        int level     = platform->getBatteryLevel();
        bool charging = platform->isBatteryCharging();
        NVGcolor tint = charging ? ui::color("success") : level <= 15 ? ui::color("danger") : ui::color("text");
        this->batteryIcon->setText(charging ? ui::icons::charging : ui::icons::battery);
        this->batteryIcon->setTextColor(charging || level <= 15 ? tint : ui::color("text_muted"));
        this->batteryLabel->setText(std::to_string(level) + "%" + (charging ? " carregando" : ""));
        this->batteryLabel->setTextColor(tint);
    }

    int64_t freeBytes = 0, totalBytes = 0;
    if (download_mgr::getStorageSpace(freeBytes, totalBytes))
    {
#ifdef __SWITCH__
        const char* where = "SD";
#else
        const char* where = "Disco";
#endif
        this->storageLabel->setText(std::string(where) + " " + formatSize(freeBytes) + " livres de " + formatSize(totalBytes));
        float used = 1.0f - (float)freeBytes / (float)totalBytes;
        used       = std::max(0.0f, std::min(1.0f, used));
        this->storageFill->setWidth(kStorageBarWidth * used);
        // Red once under 10% free: a big download is about to run out of room.
        this->storageFill->setBackgroundColor(used > 0.9f ? ui::color("danger") : ui::color("accent"));
    }
    else
    {
        this->storageLabel->setText("Armazenamento indisponivel");
    }
}

void DownloadsScreen::poll()
{
    this->updateStatusBar();

    if (!this->listBox)
        return;

    auto active   = download_mgr::getActiveDownloads();
    auto finished = download_mgr::getFinishedDownloads();

    if (this->listSignature(active, finished) != this->shownSignature)
    {
        this->refreshList();
        return;
    }

    for (auto& info : active)
    {
        auto it = this->activeRows.find(info.infoHash);
        if (it != this->activeRows.end())
            it->second->update(info);
    }
}

void DownloadsScreen::refreshList()
{
    // Same reasoning as AddonsScreen::rebuildList(): a row's own action
    // (watch/delete) is what triggers this rebuild, destroying every row
    // including the one whose callback is still running -- park focus on
    // the screen itself first.
    bool focusWasInList = false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v != nullptr; v = v->getParent())
        if (v == this->listBox)
        {
            focusWasInList = true;
            break;
        }
    if (focusWasInList)
        brls::Application::giveFocus(this);

    this->listBox->clearViews();
    this->activeRows.clear();

    auto active   = download_mgr::getActiveDownloads();
    auto finished = download_mgr::getFinishedDownloads();
    this->shownSignature = this->listSignature(active, finished);
    brls::Logger::info("ui: downloads list rebuilt ({} active/failed, {} finished)", active.size(), finished.size());

    if (active.empty() && finished.empty())
    {
        this->listBox->addView(ui::emptyState(ui::icons::download, "Nenhum download ainda",
            "Escolha um stream marcado como Torrent para baixar -- ele aparece aqui com o progresso."));
    }

    if (!active.empty())
    {
        this->listBox->addView(makeSectionHeader("BAIXANDO"));
        for (auto& info : active)
        {
            std::string infoHash = info.infoHash;
            std::string title    = info.title;
            bool failed          = info.failed;

            auto* row = new ActiveDownloadRow(info, [this, infoHash, title, failed]() {
                this->confirmCancel(infoHash, title, failed);
            });
            this->activeRows[infoHash] = row;
            this->listBox->addView(row);
        }
    }

    if (!finished.empty())
    {
        this->listBox->addView(makeSectionHeader("CONCLUIDOS"));
        for (auto& d : finished)
        {
            std::string infoHash = d.infoHash;
            std::string title    = d.title;
            std::string url      = d.localFileUrl;

            this->listBox->addView(new FinishedDownloadRow(
                d,
                [title, url]() {
                    brls::Logger::info("ui: watch downloaded file {}", url);
                    brls::Application::pushActivity(
                        new brls::Activity(new PlayerScreen(url, title)), brls::TransitionAnimation::NONE);
                },
                [this, infoHash, title]() { this->confirmDelete(infoHash, title); }));
        }
    }

    if (focusWasInList)
        brls::sync([this]() { brls::Application::giveFocus(this->getDefaultFocus()); });
}

void DownloadsScreen::confirmDelete(const std::string& infoHash, const std::string& title)
{
    auto* dialog = new brls::Dialog("Apagar \"" + title + "\"? Isso remove o arquivo baixado do dispositivo.");
    dialog->addButton("Cancelar", []() {});
    dialog->addButton("Apagar", [this, infoHash]() {
        download_mgr::deleteFinishedDownload(infoHash);
        this->refreshList();
    });
    dialog->open();
}

void DownloadsScreen::confirmCancel(const std::string& infoHash, const std::string& title, bool failed)
{
    if (failed)
    {
        // Nothing left running or on disk -- just drop it from the list.
        download_mgr::cancelDownload(infoHash);
        this->refreshList();
        return;
    }

    auto* dialog = new brls::Dialog("Cancelar o download de \"" + title + "\"? O que ja foi baixado sera apagado.");
    dialog->addButton("Continuar baixando", []() {});
    dialog->addButton("Cancelar download", [infoHash]() {
        // The worker notices within ~200ms, wipes its cache dir and drops
        // out of getActiveDownloads() -- the next poll() rebuilds the list.
        download_mgr::cancelDownload(infoHash);
    });
    dialog->open();
}
