#pragma once

#include <string>

// One extra Cinemeta catalog beyond what its manifest advertises (genre,
// year, or rating-sorted slices) -- StreamNX-main lists these the same
// way: as their own toggleable entries, not something buried in code.
// `url` is the full, ready-to-fetch catalog endpoint.
struct Catalog
{
    std::string key;
    std::string title;
    std::string url;
    bool enabled = true;
};
