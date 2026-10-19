#if defined(ANDROID) || defined(IOS)
#include <SDL2/SDL_main.h>
#endif

#include <borealis.hpp>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

#include "screen/main_menu.hpp"
#include "api/http_client.hpp"
#include "api/cloud_sync.hpp"
#include "api/download_manager.hpp"
#include "api/updater.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "theme/streamnx_theme.hpp"
#include "utils/hover.hpp"

#ifdef USE_PIPENSX_BT
// Homebrew has no OS crash dialog/debugger to fall back on -- an uncaught
// signal or C++ exception just silently kills the process. These make that
// land a line in cache/crash.log instead (see library/pipensx-bt's own
// switch_crashlog.c/util.c, vendored from pipensx, which already does this
// for its own production builds).
extern "C"
{
#include "switch_crashlog.h"
#include "util.h"
}
#include <exception>
#include <filesystem>
#include <thread>
#include <unwind.h>
#include <string>
#include <typeinfo>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __SWITCH__
namespace
{

// Every data file this app writes is a path relative to the working
// directory (addons.json, favorites.json, cache/..., the downloads, the
// crash log) -- and on the Switch that directory is wherever the .nro
// happens to sit, or whatever a forwarder sets, so nobody could say where
// the files actually were. Pin it to one fixed folder instead, carrying
// over the user's saved lists from the old location the first time.
constexpr const char* kDataDir = "sdmc:/switch/StreamNX";

std::string pinWorkingDirectory()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path previous = fs::current_path(ec);
    if (ec)
        previous.clear();

    fs::create_directories(kDataDir, ec);
    if (chdir(kDataDir) != 0)
        return previous.string();

    if (!previous.empty() && fs::path(previous) != fs::path(kDataDir))
    {
        for (const char* name : { "addons.json", "catalogs.json", "favorites.json", "continue_watching.json" })
        {
            fs::path from = previous / name;
            if (!fs::exists(name, ec) && fs::exists(from, ec))
                fs::copy_file(from, name, ec);
        }
    }
    return previous.string();
}

} // namespace
#endif

#ifdef USE_PIPENSX_BT
namespace
{

std::thread::id g_mainThreadId;

struct Backtrace
{
    uintptr_t addresses[32];
    int count = 0;
};

_Unwind_Reason_Code collectFrame(_Unwind_Context* context, void* arg)
{
    auto* trace = static_cast<Backtrace*>(arg);
    uintptr_t ip = _Unwind_GetIP(context);
    if (ip)
        trace->addresses[trace->count++] = ip;
    return trace->count < 32 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

// One crash line, written everywhere it can be found afterwards: the main
// log (flushed on the spot -- the process is about to _Exit, and anything
// still buffered is lost) and last_crash.txt next to it, a small file that's
// replaced on every crash and is easy to spot on the SD card.
void crashLine(const std::string& text)
{
    static FILE* lastCrash = fopen("last_crash.txt", "w");
    log_msg("%s\n", text.c_str());
    log_flush();
    if (lastCrash)
    {
        fprintf(lastCrash, "%s\n", text.c_str());
        fflush(lastCrash);
    }
}

// Return addresses of the thread that's dying, plus this function's own
// runtime address: the NRO is loaded at a random base, so an address only
// maps back to a source line (addr2line against the matching StreamNX.elf)
// once that base is known -- base = logged ref - logBacktrace's address in
// the ELF.
void logBacktrace()
{
    char line[128];
    std::snprintf(line, sizeof(line), "[crash] thread=%s ref=%p",
        std::this_thread::get_id() == g_mainThreadId ? "main" : "worker", (void*)&logBacktrace);
    crashLine(line);

    Backtrace trace;
    _Unwind_Backtrace(collectFrame, &trace);
    for (int i = 0; i < trace.count; ++i)
    {
        std::snprintf(line, sizeof(line), "[crash]   #%d %p", i, (void*)trace.addresses[i]);
        crashLine(line);
    }
}

} // namespace
#endif

#if defined(__PSV__) && defined(BOREALIS_USE_OPENGL)
// Needed for the OpenGL driver to work
extern "C" unsigned int sceLibcHeapSize = 2 * 1024 * 1024;
#endif

int main(int argc, char* argv[])
{
#ifdef __SWITCH__
    std::string previousWorkingDirectory = pinWorkingDirectory();
#endif

#ifdef USE_PIPENSX_BT
    g_mainThreadId = std::this_thread::get_id();
    switch_crashlog_install();
    mkdir("cache", 0755); // fopen() below won't create a missing parent dir
    log_init("cache/crash.log");
    std::set_terminate([] {
        switch_crashlog_stage("uncaught C++ exception");
        // "std::terminate called" on its own names nothing -- rethrowing
        // the in-flight exception is the only way to get its type/message
        // into the log, and it's safe here since we _Exit either way.
        if (std::exception_ptr current = std::current_exception())
        {
            try
            {
                std::rethrow_exception(current);
            }
            catch (const std::exception& e)
            {
                crashLine(std::string("[crash] std::terminate: ") + typeid(e).name() + ": " + e.what());
            }
            catch (...)
            {
                crashLine("[crash] std::terminate: non-std exception");
            }
        }
        else
        {
            crashLine("[crash] std::terminate called with no live exception");
        }
        logBacktrace();
        log_flush();
        std::_Exit(134);
    });

    // Every brls::Logger line (download_manager, mpv errors, screen
    // markers, ...) into that same file too -- on the Switch those only
    // ever went to stdout, i.e. nowhere, so a silent close left crash.log
    // with no trail of what the app was doing. Flushed per line on purpose:
    // the file is otherwise flushed ~1x/sec, and a process that dies (or
    // _exit()s from the signal handler) loses whatever was still buffered,
    // which is exactly the last lines that matter.
    brls::Logger::getLogEvent()->subscribe([](brls::Logger::TimePoint, brls::LogLevel level, std::string text) {
        static const char* names[] = { "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE" };
        int index = (int)level;
        log_msg("[%s] %s\n", (index >= 0 && index < 5) ? names[index] : "?", text.c_str());
        log_flush();
    });

    // A normal exit (mainLoop returning, + button, exit() from anywhere)
    // closes the app exactly like a crash does from the user's side --
    // this line is what tells the two apart in crash.log.
    std::atexit([] {
        log_msg("[exit] process exiting normally\n");
        log_flush();
    });
    brls::Logger::info("main: start");
#endif

#ifdef __SWITCH__
    {
        std::error_code ec;
        brls::Logger::info("main: data dir {} (launched from {})", std::filesystem::current_path(ec).string(),
            previousWorkingDirectory.empty() ? "?" : previousWorkingDirectory);
    }
#endif

    if (argc > 0)
        updater::setExecutablePath(argv[0]);

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "-d") == 0)
        {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        }
        else if (std::strcmp(argv[i], "-v") == 0)
        {
            brls::Application::enableDebuggingView(true);
        }
    }

    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    // Must happen before any background thread touches curl (addon
    // fetching), and exactly once for the process's lifetime.
    http::globalInit();

    // Initialize download manager (torrent support)
    download_mgr::globalInit();

    // Profile sync (profile.json): starts pulling the logged-in profile's
    // lists right away, so they're usually in before Home is built.
    cloud::globalInit();

    // Custom widgets used by video_view.xml -- must be registered before
    // that XML is ever inflated (PlayerScreen's first construction).
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("VideoProgressSlider", VideoProgressSlider::create);

    // Must run before any XML referencing "@theme/streamnx/..." is inflated.
    streamnx_theme::install();

#ifdef STREAMNX_PC_UI
    // PC is mouse-driven: buttons, tabs and cards light up under the cursor.
    hover::install();
#endif

    brls::Application::createWindow("StreamNX");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);

    brls::Application::pushActivity(new brls::Activity(new MainMenu()));

    // Asks StreamNX-Backend for a newer release; a dialog only if there is one.
    updater::checkOnStartup();

#ifdef STREAMNX_TV_PREVIEW
    // Switch-layout preview on PC (-DSTREAMNX_TV_UI=ON) only: replay a
    // comma-separated button script from STREAMNX_PREVIEW_KEYS (e.g.
    // "LEFT,DOWN,A"), one press every 500ms after a 4s warm-up -- lets the
    // TV layout be exercised and screenshotted without the window needing
    // keyboard focus.
    if (const char* script = std::getenv("STREAMNX_PREVIEW_KEYS"))
    {
        std::string keys = script;
        std::thread([keys]() {
            std::this_thread::sleep_for(std::chrono::seconds(4));
            size_t start = 0;
            while (start <= keys.size())
            {
                size_t end      = keys.find(',', start);
                std::string key = keys.substr(start, end == std::string::npos ? std::string::npos : end - start);
                start           = end == std::string::npos ? keys.size() + 1 : end + 1;

                brls::ControllerButton button;
                if (key == "LEFT") button = brls::BUTTON_NAV_LEFT;
                else if (key == "RIGHT") button = brls::BUTTON_NAV_RIGHT;
                else if (key == "UP") button = brls::BUTTON_NAV_UP;
                else if (key == "DOWN") button = brls::BUTTON_NAV_DOWN;
                else if (key == "A") button = brls::BUTTON_A;
                else if (key == "B") button = brls::BUTTON_B;
                else if (key == "X") button = brls::BUTTON_X;
                else if (key == "Y") button = brls::BUTTON_Y;
                else continue;

                brls::sync([button]() { brls::Application::onControllerButtonPressed(button, false); });
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }).detach();
    }
#endif

#ifdef USE_PIPENSX_BT
    switch_crashlog_stage("main loop");
#endif

    while (brls::Application::mainLoop())
        ;

    brls::Logger::info("main: mainLoop exit");

    download_mgr::globalCleanup();
    updater::globalCleanup();
    // Gives changes made just before quitting (e.g. the position saved when
    // leaving the player) a moment to reach the server -- needs curl alive.
    cloud::globalCleanup();
    http::globalCleanup();

    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
