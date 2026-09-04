#pragma once

#include <string>

// A configured addon: just a manifest base URL (no trailing
// "/manifest.json" or slash) plus whether it's currently enabled. Mirrors
// how StreamNX-main models addons -- no networking here, just the URL and
// its on/off state.
struct Addon
{
    std::string url;
    bool enabled = true;
};
