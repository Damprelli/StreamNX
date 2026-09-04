#pragma once

#include <string>

// One entry in "Continuar Assistindo" -- there's no embedded player in this
// app (streams open in an external one), so there's no real playback
// position to track; this instead records the last title a stream was
// opened for, most-recent first, as a "pick up where you left off" list.
struct ContinueWatching
{
    std::string id;   // the show/movie's own id (e.g. "tt1234567")
    std::string type; // "movie" | "series"
    std::string name;
    std::string poster;
    std::string subtitle; // e.g. "Temporada 1 - Episodio 2", or the year for a movie
};
