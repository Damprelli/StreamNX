#include "api/stremio_api.hpp"
#include "api/http_client.hpp"

#include <nlohmann/json.hpp>

namespace stremio
{

namespace
{

std::string yearOf(const nlohmann::json& j)
{
    if (j.contains("releaseInfo") && j["releaseInfo"].is_string())
        return j["releaseInfo"].get<std::string>();

    if (j.contains("year"))
    {
        if (j["year"].is_string())
            return j["year"].get<std::string>();
        if (j["year"].is_number())
            return std::to_string(j["year"].get<int>());
    }

    return "";
}

MetaSummary parseMetaSummary(const nlohmann::json& j)
{
    MetaSummary m;
    // Cinemeta's own catalog responses key the id as "imdb_id" instead of
    // the "id" the rest of the protocol (and Cinemeta's own /meta and other
    // addons' catalogs) uses -- accept either.
    m.id         = j.value("id", j.value("imdb_id", ""));
    m.type       = j.value("type", "");
    m.name       = j.value("name", j.value("title", ""));
    m.poster     = j.value("poster", "");
    m.year       = yearOf(j);
    m.imdbRating = j.value("imdbRating", "");
    return m;
}

void fetchMetasFromUrl(
    const std::string& url, AliveFlag alive, std::function<void(std::vector<MetaSummary>)> onSuccess,
    std::function<void(std::string)> onError)
{
    http::getAsync(
        url, alive,
        [onSuccess, onError](const std::string& body) {
            try
            {
                auto j = nlohmann::json::parse(body);

                std::vector<MetaSummary> metas;
                if (j.contains("metas") && j["metas"].is_array())
                    for (auto& m : j["metas"])
                        metas.push_back(parseMetaSummary(m));

                onSuccess(metas);
            }
            catch (const std::exception& e)
            {
                onError(std::string("Catalogo invalido: ") + e.what());
            }
        },
        onError);
}

} // namespace

void fetchManifest(const std::string& addonUrl, AliveFlag alive, std::function<void(ManifestInfo)> onSuccess,
    std::function<void(std::string)> onError)
{
    http::getAsync(
        addonUrl + "/manifest.json", alive,
        [onSuccess, onError](const std::string& body) {
            try
            {
                auto j = nlohmann::json::parse(body);

                ManifestInfo info;
                info.name = j.value("name", "");

                if (j.contains("catalogs") && j["catalogs"].is_array())
                {
                    for (auto& c : j["catalogs"])
                    {
                        CatalogRef ref;
                        ref.type = c.value("type", "");
                        ref.id   = c.value("id", "");
                        ref.name = c.value("name", ref.id);
                        info.catalogs.push_back(ref);
                    }
                }

                onSuccess(info);
            }
            catch (const std::exception& e)
            {
                onError(std::string("Manifesto invalido: ") + e.what());
            }
        },
        onError);
}

void fetchCatalog(const std::string& addonUrl, const std::string& type, const std::string& catalogId,
    AliveFlag alive, std::function<void(std::vector<MetaSummary>)> onSuccess,
    std::function<void(std::string)> onError, const std::string& extra)
{
    std::string url = addonUrl + "/catalog/" + type + "/" + catalogId + (extra.empty() ? "" : ("/" + extra)) + ".json";
    fetchMetasFromUrl(url, alive, onSuccess, onError);
}

void fetchCatalogUrl(const std::string& url, AliveFlag alive, std::function<void(std::vector<MetaSummary>)> onSuccess,
    std::function<void(std::string)> onError)
{
    fetchMetasFromUrl(url, alive, onSuccess, onError);
}

void fetchMeta(const std::string& addonUrl, const std::string& type, const std::string& id, AliveFlag alive,
    std::function<void(MetaDetail)> onSuccess, std::function<void(std::string)> onError)
{
    std::string url = addonUrl + "/meta/" + type + "/" + id + ".json";

    http::getAsync(
        url, alive,
        [onSuccess, onError](const std::string& body) {
            try
            {
                auto root  = nlohmann::json::parse(body);
                auto metaJ = root.contains("meta") ? root["meta"] : root;

                MetaDetail d;
                d.id          = metaJ.value("id", "");
                d.type        = metaJ.value("type", "");
                d.name        = metaJ.value("name", "");
                d.poster      = metaJ.value("poster", "");
                d.background  = metaJ.value("background", "");
                d.description = metaJ.value("description", metaJ.value("overview", ""));
                d.releaseInfo = yearOf(metaJ);
                d.imdbRating  = metaJ.value("imdbRating", "");

                if (metaJ.contains("videos") && metaJ["videos"].is_array())
                {
                    for (auto& v : metaJ["videos"])
                    {
                        VideoEntry ve;
                        ve.id      = v.value("id", "");
                        ve.name    = v.value("name", ve.id);
                        ve.season  = v.value("season", 0);
                        ve.episode = v.value("episode", 0);
                        d.videos.push_back(ve);
                    }
                }

                onSuccess(d);
            }
            catch (const std::exception& e)
            {
                onError(std::string("Detalhes invalidos: ") + e.what());
            }
        },
        onError);
}

void fetchStream(const std::string& addonUrl, const std::string& type, const std::string& id, AliveFlag alive,
    std::function<void(std::vector<StreamEntry>)> onSuccess, std::function<void(std::string)> onError)
{
    std::string url = addonUrl + "/stream/" + type + "/" + id + ".json";

    http::getAsync(
        url, alive,
        [onSuccess, onError](const std::string& body) {
            try
            {
                auto j = nlohmann::json::parse(body);

                std::vector<StreamEntry> streams;
                if (j.contains("streams") && j["streams"].is_array())
                {
                    for (auto& s : j["streams"])
                    {
                        StreamEntry e;
                        e.name     = s.value("name", "");
                        e.title    = s.value("title", s.value("description", ""));
                        e.url      = s.value("url", "");
                        e.infoHash = s.value("infoHash", "");
                        streams.push_back(e);
                    }
                }

                onSuccess(streams);
            }
            catch (const std::exception& e)
            {
                onError(std::string("Streams invalidos: ") + e.what());
            }
        },
        onError);
}

} // namespace stremio
