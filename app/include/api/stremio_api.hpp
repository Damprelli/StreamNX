#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// A small client for the addon protocol StreamNX-main's addons speak:
// GET {addonUrl}/manifest.json, GET {addonUrl}/catalog/{type}/{id}.json,
// GET {addonUrl}/meta/{type}/{id}.json. `addonUrl` is the base URL as
// stored by AddonStore (no trailing "/manifest.json").
namespace stremio
{

// The official Stremio metadata addon. Unlike user-added addons (which are
// stream providers managed through AddonStore/the Addons screen), this is a
// fixed dependency of the catalog/search/detail features -- baked in and
// always queried, the same way StreamNX-main treats it, rather than a
// removable/disable-able entry a user could accidentally turn off and lose
// their whole catalog.
inline const std::string CINEMETA_URL = "https://v3-cinemeta.strem.io";

using AliveFlag = std::shared_ptr<bool>;

struct CatalogRef
{
    std::string type;
    std::string id;
    std::string name;
    // From the catalog's "extra" (or legacy "extraSupported"/
    // "extraRequired"): whether it answers "search=...", and whether it
    // only answers with an extra there's no way to fill in -- e.g. a
    // search-only catalog, which has nothing to list when browsing.
    bool searchable    = false;
    bool requiresExtra = false;
    // A required extra that does list its options (Cinemeta's "New" needs
    // a year) is filled with the first one -- e.g. "genre=2026" -- to pass
    // as fetchCatalog's `extra` when browsing.
    std::string defaultExtra;
};

struct ManifestInfo
{
    std::string name;
    // behaviorHints.configurationRequired: the bare URL doesn't work --
    // the user has to set the addon up on its /configure page and add the
    // URL that page gives back instead.
    bool configurationRequired = false;
    std::vector<CatalogRef> catalogs;
};

// One item in a catalog listing.
struct MetaSummary
{
    std::string id;
    std::string type;
    std::string name;
    std::string poster;
    std::string year;
    std::string imdbRating;
};

// One episode, as listed on a series' meta detail.
struct VideoEntry
{
    std::string id;
    std::string name;
    int season  = 0;
    int episode = 0;
};

struct MetaDetail
{
    std::string id;
    std::string type;
    std::string name;
    std::string poster;
    std::string background;
    std::string description;
    std::string releaseInfo;
    std::string imdbRating;
    std::vector<VideoEntry> videos; // populated for series
};

// One entry from an addon's /stream/{type}/{id}.json -- informational only
// (no player exists in this app): title/name/quality plus, filled in by the
// caller (StreamEntry itself has no notion of "which addon"), the addon it
// came from.
struct StreamEntry
{
    std::string name;        // usually the addon/release-group tag ("Torrentio", "1080p")
    std::string title;       // usually filename/quality/codec details
    std::string url;         // direct playable URL, when the addon provides one
    std::string infoHash;    // torrent info hash, when the addon provides a torrent instead
    std::vector<std::string> trackers; // announce URLs from "sources" -- without these, finding
                                        // peers for the infoHash above relies on DHT alone, which
                                        // can take much longer (or fail behind restrictive NAT)
    std::string addonName;   // filled by the caller, not present in the JSON
};

// One entry from a subtitles addon's /subtitles/{type}/{id}.json.
struct SubtitleEntry
{
    std::string id;
    std::string url;  // the subtitle file itself (srt/vtt/ass...), loaded straight into mpv
    std::string lang; // as the addon reports it -- "pob", "eng", "Portuguese (BR)"... (see SubtitleAddonStore)
    std::string langCode; // some addons send a code here alongside a full name in `lang` ("pt-br")
    std::string addonName; // filled by the caller, not present in the JSON
};

void fetchManifest(const std::string& addonUrl, AliveFlag alive, std::function<void(ManifestInfo)> onSuccess,
    std::function<void(std::string)> onError);

// `extra`, when not empty, is appended as one more path segment before
// ".json" (e.g. "genre=Animation" or "year/genre=2026") -- the protocol's
// way of asking a catalog for a filtered slice (genre, year, sort order...).
void fetchCatalog(const std::string& addonUrl, const std::string& type, const std::string& catalogId,
    AliveFlag alive, std::function<void(std::vector<MetaSummary>)> onSuccess,
    std::function<void(std::string)> onError, const std::string& extra = "");

void fetchMeta(const std::string& addonUrl, const std::string& type, const std::string& id, AliveFlag alive,
    std::function<void(MetaDetail)> onSuccess, std::function<void(std::string)> onError);

// GET {addonUrl}/stream/{type}/{id}.json -- for series, `id` is the video id
// from MetaDetail::videos (e.g. "tt1234567:1:2" for S01E02), not the show's
// own id.
void fetchStream(const std::string& addonUrl, const std::string& type, const std::string& id, AliveFlag alive,
    std::function<void(std::vector<StreamEntry>)> onSuccess, std::function<void(std::string)> onError);

// GET {addonUrl}/subtitles/{type}/{id}.json -- same `id` rules as
// fetchStream (the episode's video id for series).
void fetchSubtitles(const std::string& addonUrl, const std::string& type, const std::string& id, AliveFlag alive,
    std::function<void(std::vector<SubtitleEntry>)> onSuccess, std::function<void(std::string)> onError);

} // namespace stremio
