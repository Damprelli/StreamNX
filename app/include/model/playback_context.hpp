#pragma once

#include <string>

// Everything PlayerScreen needs beyond the URL/title themselves: which
// Continue Watching entry to keep updated with the current playback
// position while this stream plays (and to resume from, when playback
// started because the user picked that entry back up), and what to open if
// the user picks "Proximo Episodio" from the OSD. Passed down unchanged from
// wherever playback actually starts (StreamListScreen, TorrentProgressScreen)
// -- default-constructed means "a one-off stream with nothing to track",
// which is what every call site had before this existed.
struct PlaybackContext
{
    std::string continueWatchingId; // the show's own id (movies fall back to their own id)
    std::string type;               // "movie" | "series"
    std::string posterUrl;
    std::string subtitle; // e.g. "Temporada 1 - Episodio 2", recorded into Continue Watching

    double resumePositionSeconds = 0.0;

    // Empty nextVideoId means there's no next episode to offer (a movie, or
    // the last episode DetailScreen knows about).
    std::string nextVideoId;
    std::string nextTitle;
    std::string nextSubtitle;
};
