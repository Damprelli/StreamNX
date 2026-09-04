#pragma once

#include <string>

// Plain data for one poster card (Continue Watching, Favoritos, search
// results). The poster is always a colored placeholder picked by
// colorIndex -- no image assets, no network image loading for the grid.
//
// `id`/`type` are the addon-protocol identifiers (e.g. "tt1234567" /
// "movie"); when set, selecting the card opens DetailScreen to fetch the
// real thing. Left empty for purely cosmetic mock items (Continue
// Watching has no backing catalog to look up), in which case selecting
// the card just shows its title. Appended at the end (rather than kept
// alongside title/subtitle) so every existing positional
// `{title, subtitle, colorIndex, rating, progress, isFavorite}`
// initializer still lines up.
struct MediaItem
{
    std::string title;
    std::string subtitle;   // e.g. "Filme - 2010", or "S1E5 - O Vortice"
    int colorIndex  = 0;    // picks a placeholder poster color
    float rating    = 0.0f; // 0 = hide the rating badge
    float progress  = 0.0f; // 0..1, 0 = hide the progress bar
    bool isFavorite = false;

    std::string id;   // e.g. "tt1234567" -- empty for mock-only items
    std::string type; // "movie" | "series"
    std::string poster; // poster art URL -- empty falls back to the colored placeholder
};
