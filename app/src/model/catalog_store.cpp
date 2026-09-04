#include "model/catalog_store.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace
{
constexpr const char* CATALOGS_FILE = "catalogs.json";
}

void to_json(nlohmann::json& j, const Catalog& c)
{
    j = nlohmann::json {
        { "key", c.key },
        { "title", c.title },
        { "url", c.url },
        { "enabled", c.enabled },
    };
}

void from_json(const nlohmann::json& j, Catalog& c)
{
    c.key     = j.value("key", "");
    c.title   = j.value("title", "");
    c.url     = j.value("url", "");
    c.enabled = j.value("enabled", true);
}

CatalogStore& CatalogStore::instance()
{
    static CatalogStore store;
    return store;
}

void CatalogStore::load()
{
    if (this->loaded)
        return;
    this->loaded = true;

    std::ifstream in(CATALOGS_FILE);
    if (in.is_open())
    {
        try
        {
            nlohmann::json j;
            in >> j;
            this->items = j.get<std::vector<Catalog>>();
            return;
        }
        catch (const std::exception&)
        {
            this->items.clear();
        }
    }

    // First run (or the file was deleted): seed Cinemeta's known
    // genre/year/rating catalog variants -- its manifest only advertises
    // "top", the rest exist but aren't discoverable any other way.
    this->items = {
        { "cinemeta:popular-movies", "Filmes Populares", "https://v3-cinemeta.strem.io/catalog/movie/top.json", true },
        { "cinemeta:popular-series", "Series Populares", "https://v3-cinemeta.strem.io/catalog/series/top.json", true },
        { "cinemeta:new-movies", "Filmes Novos",
            "https://v3-cinemeta.strem.io/catalog/movie/year/genre=2026.json", true },
        { "cinemeta:new-series", "Series Novas",
            "https://v3-cinemeta.strem.io/catalog/series/year/genre=2026.json", true },
        { "cinemeta:top-movies", "Filmes Mais Bem Avaliados",
            "https://v3-cinemeta.strem.io/catalog/movie/imdbRating.json", true },
        { "cinemeta:top-series", "Series Mais Bem Avaliadas",
            "https://v3-cinemeta.strem.io/catalog/series/imdbRating.json", true },
        { "cinemeta:animation-movies", "Filmes de Animacao",
            "https://v3-cinemeta.strem.io/catalog/movie/top/genre=Animation.json", true },
        { "cinemeta:animation-series", "Series de Animacao",
            "https://v3-cinemeta.strem.io/catalog/series/top/genre=Animation.json", true },
        { "cinemeta:documentary-movies", "Documentarios",
            "https://v3-cinemeta.strem.io/catalog/movie/top/genre=Documentary.json", true },
        { "cinemeta:documentary-series", "Documentarios (Series)",
            "https://v3-cinemeta.strem.io/catalog/series/top/genre=Documentary.json", true },
    };
    this->save();
}

void CatalogStore::save()
{
    try
    {
        std::ofstream out(CATALOGS_FILE);
        if (out.is_open())
        {
            nlohmann::json j = this->items;
            out << j.dump(2);
        }
    }
    catch (const std::exception&)
    {
    }
}

void CatalogStore::toggle(size_t index)
{
    if (index >= this->items.size())
        return;
    this->items[index].enabled = !this->items[index].enabled;
    this->save();
}
