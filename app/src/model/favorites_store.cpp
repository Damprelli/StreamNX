#include "model/favorites_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace
{
constexpr const char* FAVORITES_FILE = "favorites.json";
}

void to_json(nlohmann::json& j, const Favorite& f)
{
    j = nlohmann::json {
        { "id", f.id },
        { "type", f.type },
        { "name", f.name },
        { "year", f.year },
        { "imdbRating", f.imdbRating },
        { "poster", f.poster },
    };
}

void from_json(const nlohmann::json& j, Favorite& f)
{
    f.id         = j.value("id", "");
    f.type       = j.value("type", "");
    f.name       = j.value("name", "");
    f.year       = j.value("year", "");
    f.imdbRating = j.value("imdbRating", "");
    f.poster     = j.value("poster", "");
}

FavoritesStore& FavoritesStore::instance()
{
    static FavoritesStore store;
    return store;
}

void FavoritesStore::load()
{
    if (this->loaded)
        return;
    this->loaded = true;

    try
    {
        std::ifstream in(FAVORITES_FILE);
        if (in.is_open())
        {
            nlohmann::json j;
            in >> j;
            this->items = j.get<std::vector<Favorite>>();
        }
    }
    catch (const std::exception&)
    {
        this->items.clear();
    }
}

void FavoritesStore::save()
{
    try
    {
        std::ofstream out(FAVORITES_FILE);
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

bool FavoritesStore::contains(const std::string& id) const
{
    for (auto& f : this->items)
        if (f.id == id)
            return true;
    return false;
}

void FavoritesStore::add(const Favorite& item)
{
    if (this->contains(item.id))
        return;
    this->items.push_back(item);
    this->save();
}

void FavoritesStore::remove(const std::string& id)
{
    this->items.erase(
        std::remove_if(this->items.begin(), this->items.end(), [&](const Favorite& f) { return f.id == id; }),
        this->items.end());
    this->save();
}
