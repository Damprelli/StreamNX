#if defined(ANDROID) || defined(IOS)
#include <SDL2/SDL_main.h>
#endif

#include <borealis.hpp>
#include <cstdlib>
#include <cstring>

#include "screen/main_menu.hpp"
#include "api/http_client.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"

#if defined(__PSV__) && defined(BOREALIS_USE_OPENGL)
// Needed for the OpenGL driver to work
extern "C" unsigned int sceLibcHeapSize = 2 * 1024 * 1024;
#endif

int main(int argc, char* argv[])
{
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

    // Custom widgets used by video_view.xml -- must be registered before
    // that XML is ever inflated (PlayerScreen's first construction).
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("VideoProgressSlider", VideoProgressSlider::create);

    brls::Application::createWindow("StreamNX");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);

    brls::Application::pushActivity(new brls::Activity(new MainMenu()));

    while (brls::Application::mainLoop())
        ;

    http::globalCleanup();

    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
