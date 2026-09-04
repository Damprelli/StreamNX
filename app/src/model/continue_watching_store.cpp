#include "model/continue_watching_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace
{
constexpr const char* CONTINUE_WATCHING_FILE = "continue_watching.json";
constexpr size_t MAX_ITEMS                   = 20;
}

void to_json(nlohmann::json& j, const ContinueWatching& c)
{
    j = nlohmann::json {
        { "id", c.id },
        { "type", c.type },
        { "name", c.name },
        { "poster", c.poster },
        { "subtitle", c.subtitle },
    };
}

void from_json(const nlohmann::json& j, ContinueWatching& c)
{
    c.id       = j.value("id", "");
    c.type     = j.value("type", "");
    c.name     = j.value("name", "");
    c.poster   = j.value("poster", "");
    c.subtitle = j.value("subtitle", "");
}

ContinueWatchingStore& ContinueWatchingStore::instance()
{
    static ContinueWatchingStore store;
    return store;
}

void ContinueWatchingStore::load()
{
    if (this->loaded)
        return;
    this->loaded = true;

    try
    {
        std::ifstream in(CONTINUE_WATCHING_FILE);
        if (in.is_open())
        {
            nlohmann::json j;
            in >> j;
            this->items = j.get<std::vector<ContinueWatching>>();
        }
    }
    catch (const std::exception&)
    {
        this->items.clear();
    }
}

void ContinueWatchingStore::save()
{
    try
    {
        std::ofstream out(CONTINUE_WATCHING_FILE);
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

void ContinueWatchingStore::touch(const ContinueWatching& item)
{
    if (item.id.empty())
        return;

    this->items.erase(
        std::remove_if(this->items.begin(), this->items.end(), [&](const ContinueWatching& c) { return c.id == item.id; }),
        this->items.end());

    this->items.insert(this->items.begin(), item);

    if (this->items.size() > MAX_ITEMS)
        this->items.resize(MAX_ITEMS);

    this->save();
}

void ContinueWatchingStore::remove(const std::string& id)
{
    this->items.erase(
        std::remove_if(this->items.begin(), this->items.end(), [&](const ContinueWatching& c) { return c.id == id; }),
        this->items.end());
    this->save();
}
