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
};

struct ManifestInfo
{
    std::string name;
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
    std::string addonName;   // filled by the caller, not present in the JSON
};

void fetchManifest(const std::string& addonUrl, AliveFlag alive, std::function<void(ManifestInfo)> onSuccess,
    std::function<void(std::string)> onError);

// `extra`, when not empty, is appended as one more path segment before
// ".json" (e.g. "genre=Animation" or "year/genre=2026") -- the protocol's
// way of asking a catalog for a filtered slice (genre, year, sort order...).
void fetchCatalog(const std::string& addonUrl, const std::string& type, const std::string& catalogId,
    AliveFlag alive, std::function<void(std::vector<MetaSummary>)> onSuccess,
    std::function<void(std::string)> onError, const std::string& extra = "");

// Same response shape as fetchCatalog, but `url` is already the complete
// catalog endpoint -- for CatalogStore's entries, which aren't relative to
// any one addon's base URL.
void fetchCatalogUrl(const std::string& url, AliveFlag alive, std::function<void(std::vector<MetaSummary>)> onSuccess,
    std::function<void(std::string)> onError);

void fetchMeta(const std::string& addonUrl, const std::string& type, const std::string& id, AliveFlag alive,
    std::function<void(MetaDetail)> onSuccess, std::function<void(std::string)> onError);

// GET {addonUrl}/stream/{type}/{id}.json -- for series, `id` is the video id
// from MetaDetail::videos (e.g. "tt1234567:1:2" for S01E02), not the show's
// own id.
void fetchStream(const std::string& addonUrl, const std::string& type, const std::string& id, AliveFlag alive,
    std::function<void(std::vector<StreamEntry>)> onSuccess, std::function<void(std::string)> onError);

} // namespace stremio
