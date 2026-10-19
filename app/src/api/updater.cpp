#include "api/updater.hpp"
#include "api/cloud_sync.hpp"
#include "api/http_client.hpp"
#include "view/ui_kit.hpp"

#include <borealis.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#ifdef __SWITCH__
#include <switch.h>
#endif

// Set by CMake from VERSION_MAJOR/MINOR/ALTER.
#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace updater
{

#ifdef _WIN32
bool runInstaller(const std::string& path); // at the end, away from <windows.h>'s macros
#endif

namespace
{

using json = nlohmann::json;

#if defined(__SWITCH__)
constexpr const char* PLATFORM = "switch";
#elif defined(_WIN32)
constexpr const char* PLATFORM = "windows";
#else
constexpr const char* PLATFORM = nullptr;
#endif

constexpr const char* UPDATE_FILE = "update.json";

// Lets Home (and the profile sync) settle before a dialog can pop up.
constexpr int STARTUP_DELAY_MS = 3000;
// The offer waits while something else is on top (player, another dialog).
constexpr int BUSY_RETRY_MS = 5000;
// The release notes can be long; the dialog only has room for a summary.
constexpr size_t MAX_NOTES_CHARS = 500;

struct Offer
{
    std::string latest;
    std::string notes;
    std::string downloadUrl;
    std::string fileName;
    long long size = 0;
    bool required  = false;
};

std::string exePath;
http::AliveFlag alive = std::make_shared<bool>(true);

std::thread worker;
std::atomic<bool> cancelDownload { false };
std::atomic<long long> received { 0 };
std::atomic<long long> total { 0 };

std::string skippedVersion()
{
    try
    {
        std::ifstream in(UPDATE_FILE);
        if (in.is_open())
        {
            json j;
            in >> j;
            return j.value("skippedVersion", "");
        }
    }
    catch (const std::exception&)
    {
    }
    return "";
}

void skipVersion(const std::string& version)
{
    std::ofstream out(UPDATE_FILE);
    out << json { { "skippedVersion", version } }.dump(2);
}

std::string megabytes(long long bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024.0));
    return text;
}

// Markdown from the GitHub release, trimmed to fit the dialog -- cut on a
// UTF-8 character boundary so an accent isn't split in half.
std::string summarizeNotes(std::string notes)
{
    std::string clean;
    for (char c : notes)
        if (c != '\r')
            clean += c;
    while (!clean.empty() && (clean.back() == '\n' || clean.back() == ' '))
        clean.pop_back();
    if (clean.size() <= MAX_NOTES_CHARS)
        return clean;
    size_t cut = MAX_NOTES_CHARS;
    while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0) == 0x80)
        cut--;
    return clean.substr(0, cut) + "...";
}

brls::Box* dialogBody()
{
    brls::Style style = brls::Application::getStyle();
    auto* box         = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setPadding(style["brls/dialog/paddingTopBottom"] / 2, style["brls/dialog/paddingLeftRight"] / 2,
        style["brls/dialog/paddingTopBottom"] / 2, style["brls/dialog/paddingLeftRight"] / 2);
    return box;
}

brls::Label* wrapped(const std::string& text, float size, const char* colorKey)
{
    auto* label = ui::label(text, size, ui::color(colorKey));
    label->setIsWrapping(true);
    label->setMarginTop(12);
    return label;
}

// Shows what the download thread has received so far, until the dialog closes.
void pollProgress(brls::Label* progress, std::shared_ptr<bool> dialogAlive)
{
    if (!*dialogAlive)
        return;
    long long done = received, size = total;
    if (size > 0)
        progress->setText(std::to_string(done * 100 / size) + "%  (" + megabytes(done) + " de " + megabytes(size) + ")");
    else if (done > 0)
        progress->setText(megabytes(done));
    brls::delay(250, [progress, dialogAlive]() { pollProgress(progress, dialogAlive); });
}

void offer(const Offer& update);
void installDownloaded(const Offer& update, const std::string& path);

void finishDownload(const Offer& update, const std::string& path, const std::string& error)
{
    if (worker.joinable())
        worker.join();

    std::error_code ec;
    if (error.empty() && update.size > 0 && (long long)std::filesystem::file_size(path, ec) != update.size)
    {
        std::filesystem::remove(path, ec);
        brls::Application::notify("O download veio incompleto. Tente de novo.");
    }
    else if (!error.empty())
    {
        std::filesystem::remove(path, ec);
        if (!cancelDownload)
            brls::Application::notify("Falha ao baixar a atualizacao: " + error);
    }
    else
    {
        installDownloaded(update, path);
        return;
    }

    // An update the app can't go on without is offered again.
    if (update.required)
        offer(update);
}

void download(const Offer& update)
{
#ifdef __SWITCH__
    // Next to the running .nro, so the swap is a rename on the same drive.
    std::string path = exePath + ".new";
#else
    std::error_code ec;
    std::string path = (std::filesystem::temp_directory_path(ec) / update.fileName).string();
#endif

    cancelDownload = false;
    received       = 0;
    total          = update.size;

    auto* body = dialogBody();
    body->addView(ui::label("Baixando a versao " + update.latest, 22, ui::color("text")));
    auto* progress = wrapped("Conectando...", 17, "text_muted");
    body->addView(progress);

    auto* dialog = new brls::Dialog(body);
    dialog->setCancelable(false);
    auto dialogAlive = std::make_shared<bool>(true);
    dialog->addButton("Cancelar", [dialogAlive]() {
        *dialogAlive   = false;
        cancelDownload = true;
    });
    dialog->open();

    pollProgress(progress, dialogAlive);

    worker = std::thread([update, path, dialog, dialogAlive]() {
        std::string error = http::downloadToFile(update.downloadUrl, path, &cancelDownload, &received, &total);
        brls::sync([update, path, error, dialog, dialogAlive]() {
            if (*dialogAlive)
            {
                *dialogAlive = false;
                dialog->close([update, path, error]() { finishDownload(update, path, error); });
            }
            else
                finishDownload(update, path, error);
        });
    });
}

void offer(const Offer& update)
{
    auto* body = dialogBody();
    body->addView(ui::label("Nova versao disponivel: " + update.latest, 22, ui::color("text")));

    std::string info = std::string("Voce esta na versao ") + APP_VERSION + ".";
    if (update.size > 0)
        info += " O download tem " + megabytes(update.size) + ".";
    body->addView(wrapped(info, 16, "text_muted"));

    if (update.required)
        body->addView(wrapped("Esta atualizacao e obrigatoria para continuar usando o app.", 16, "text"));

    std::string notes = summarizeNotes(update.notes);
    if (!notes.empty())
        body->addView(wrapped(notes, 15, "text"));

    auto* dialog = new brls::Dialog(body);
    if (update.required)
    {
        dialog->setCancelable(false);
    }
    else
    {
        dialog->addButton("Agora nao", []() {});
        dialog->addButton("Pular versao", [version = update.latest]() { skipVersion(version); });
    }
    dialog->addButton("Atualizar", [update]() { download(update); });
    dialog->open();
}

// Waits for the app to be back at its main screen -- a dialog over the
// player (or over another dialog) would be in the way.
void offerWhenIdle(const Offer& update)
{
    if (brls::Application::getActivitiesStack().size() > 1)
    {
        brls::delay(BUSY_RETRY_MS, [update]() { offerWhenIdle(update); });
        return;
    }
    offer(update);
}

void check()
{
    std::string url = cloud::serverUrl() + "/api/update?platform=" + PLATFORM + "&version=" + APP_VERSION;
    http::getAsync(
        url, alive,
        [](const std::string& body) {
            Offer update;
            try
            {
                json j = json::parse(body);
                if (!j.value("updateAvailable", false))
                    return;
                update.latest      = j.value("latest", "");
                update.notes       = j.value("notes", "");
                update.downloadUrl = j.value("downloadUrl", "");
                update.fileName    = j.value("fileName", "");
                update.size        = j.value("size", 0LL);
                update.required    = j.value("required", false);
            }
            catch (const std::exception& e)
            {
                brls::Logger::warning("updater: invalid response: {}", e.what());
                return;
            }
            if (update.latest.empty() || update.downloadUrl.empty() || update.fileName.empty())
                return;
            if (!update.required && update.latest == skippedVersion())
                return;

            brls::Logger::info("updater: {} -> {} available", APP_VERSION, update.latest);
            offerWhenIdle(update);
        },
        // Offline or the server is down: just try again next time.
        [](const std::string& error) { brls::Logger::warning("updater: check failed: {}", error); });
}

#ifdef __SWITCH__

void installDownloaded(const Offer&, const std::string& path)
{
    // The romfs (fonts, images, the CA bundle) is read from the running
    // .nro, which keeps that file open -- it has to let go before the file
    // can be swapped. Everything on screen is already loaded by now.
    romfsExit();

    std::string old = exePath + ".old";
    std::remove(old.c_str());
    bool swapped = std::rename(exePath.c_str(), old.c_str()) == 0;
    if (swapped && std::rename(path.c_str(), exePath.c_str()) != 0)
    {
        std::rename(old.c_str(), exePath.c_str());
        swapped = false;
    }
    std::remove(old.c_str());
    romfsInit();

    if (!swapped)
    {
        std::remove(path.c_str());
        brls::Application::notify("Nao foi possivel substituir o app em " + exePath);
        return;
    }

    if (envHasNextLoad())
    {
        // hbloader starts the new .nro as soon as this one exits.
        envSetNextLoad(exePath.c_str(), exePath.c_str());
        brls::Application::quit();
        return;
    }

    auto* dialog = new brls::Dialog("Atualizado! Feche e abra o StreamNX de novo para usar a nova versao.");
    dialog->setCancelable(false);
    dialog->addButton("Fechar o app", []() { brls::Application::quit(); });
    dialog->open();
}

#elif defined(_WIN32)

void installDownloaded(const Offer&, const std::string& path)
{
    // The installer closes nothing by itself here: this app quits right
    // away, and StreamNX.iss reopens it when a silent install finishes.
    if (!runInstaller(path))
    {
        brls::Application::notify("Nao foi possivel abrir o instalador da atualizacao.");
        return;
    }
    brls::Application::quit();
}

#else

void installDownloaded(const Offer&, const std::string&) {}

#endif

} // namespace

void setExecutablePath(const std::string& path)
{
    exePath = path;
#ifdef __SWITCH__
    // Some launchers don't pass argv[0]; this is where the app lives.
    if (exePath.size() < 4 || exePath.compare(exePath.size() - 4, 4, ".nro") != 0)
        exePath = "sdmc:/switch/StreamNX/StreamNX.nro";
#endif
}

void checkOnStartup()
{
    if (!PLATFORM)
        return;
    brls::delay(STARTUP_DELAY_MS, []() { check(); });
}

void globalCleanup()
{
    *alive         = false;
    cancelDownload = true;
    if (worker.joinable())
        worker.join();
}

} // namespace updater

#if defined(_WIN32) && !defined(__SWITCH__)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

bool updater::runInstaller(const std::string& path)
{
    // /SILENT still shows Inno's progress window, so the user sees the
    // update happening; /CLOSEAPPLICATIONS covers this app not having
    // finished quitting yet when it starts copying.
    HINSTANCE result = ShellExecuteW(nullptr, L"open", std::filesystem::path(path).wstring().c_str(),
        L"/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS", nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

#endif
