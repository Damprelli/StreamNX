#include "utils/pin_input.hpp"

#include <borealis.hpp>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace pin_input
{

void open(const std::string& header, const std::string& subText, std::function<void(std::string)> onDone)
{
#ifdef __SWITCH__
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return;

    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetType(&config, SwkbdType_NumPad);
    swkbdConfigSetPasswordFlag(&config, 1);
    swkbdConfigSetHeaderText(&config, header.c_str());
    swkbdConfigSetSubText(&config, subText.c_str());
    swkbdConfigSetStringLenMin(&config, 4);
    swkbdConfigSetStringLenMax(&config, 8);
    swkbdConfigSetBlurBackground(&config, true);

    char buffer[16] = {};
    Result rc       = swkbdShow(&config, buffer, sizeof(buffer));
    swkbdClose(&config);

    if (R_SUCCEEDED(rc) && buffer[0] != '\0')
        onDone(buffer);
#else
    brls::Application::getImeManager()->openForText(
        [onDone](std::string text) {
            if (!text.empty())
                onDone(text);
        },
        header, subText, 8, "");
#endif
}

} // namespace pin_input
