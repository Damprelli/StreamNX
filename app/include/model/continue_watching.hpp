#pragma once

#include <string>
#include <vector>

// One entry in "Continuar Assistindo" -- most-recent first. Beyond the
// display fields, carries enough to actually resume: a playback position,
// and either the direct stream url or (for a torrent) the info needed to
// restart the download via TorrentProgressScreen -- a torrent's real
// playback url is a local, session-only proxy address handed out by
// download_manager, not something that can be reopened after the app
// restarts or the torrent session ends.
struct ContinueWatching
{
    std::string id;   // the show/movie's own id (e.g. "tt1234567")
    std::string type; // "movie" | "series"
    std::string name;
    std::string poster;
    std::string subtitle; // e.g. "Temporada 1 - Episodio 2", or the year for a movie

    double positionSeconds = 0.0;
    double durationSeconds = 0.0;

    bool isTorrent = false;
    std::string streamUrl;       // set when !isTorrent
    std::string torrentInfoHash; // set when isTorrent
    std::string torrentName;     // entry.title/name, passed to download_mgr::startTorrentDownload
    std::vector<std::string> torrentTrackers;
};
