#pragma once

#include <string>

// A favorited title -- just enough to show it on the Home screen and to
// re-fetch full details (DetailScreen) later: the addon protocol id/type
// pair, plus the bits already on hand from when it was favorited so the
// card doesn't need a network round-trip to render.
struct Favorite
{
    std::string id;   // e.g. "tt1234567"
    std::string type; // "movie" | "series"
    std::string name;
    std::string year;
    std::string imdbRating;
    std::string poster;
};
