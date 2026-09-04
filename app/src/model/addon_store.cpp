#include "model/addon_store.hpp"
#include "api/stremio_api.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace
{

constexpr const char* ADDONS_FILE = "addons.json";

bool isCinemeta(const std::string& url)
{
    return url == stremio::CINEMETA_URL;
}

std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n\"'");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n\"'");
    return s.substr(start, end - start + 1);
}

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

void to_json(nlohmann::json& j, const Addon& a)
{
    j = nlohmann::json { { "url", a.url }, { "enabled", a.enabled } };
}

void from_json(const nlohmann::json& j, Addon& a)
{
    a.url     = j.value("url", "");
    a.enabled = j.value("enabled", true);
}

AddonStore& AddonStore::instance()
{
    static AddonStore store;
    return store;
}

void AddonStore::load()
{
    if (this->loaded)
        return;
    this->loaded = true;

    std::ifstream in(ADDONS_FILE);
    bool fileExists = in.is_open();

    if (fileExists)
    {
        try
        {
            nlohmann::json j;
            in >> j;
            this->items = j.get<std::vector<Addon>>();

            // Cinemeta used to be seeded in here as a regular, removable
            // addon -- it's now a fixed dependency queried directly (see
            // stremio::CINEMETA_URL), never part of this user-editable
            // list. Strip any leftover entry from an install that predates
            // that change, so it doesn't show up twice (once here, once
            // fixed) or, worse, sit here disabled/removed by a user who
            // shouldn't have been able to touch it in the first place.
            this->items.erase(
                std::remove_if(this->items.begin(), this->items.end(), [](const Addon& a) { return isCinemeta(a.url); }),
                this->items.end());
        }
        catch (const std::exception&)
        {
            this->items.clear();
        }
    }
}

void AddonStore::save()
{
    try
    {
        std::ofstream out(ADDONS_FILE);
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

std::string AddonStore::add(const std::string& rawUrl)
{
    std::string url = trim(rawUrl);

    if (url.empty())
        return "Digite a URL do manifesto do addon.";

    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        return "A URL precisa comecar com http:// ou https://";

    if (endsWith(url, "manifest.json"))
        url = url.substr(0, url.size() - std::string("manifest.json").size());

    while (!url.empty() && url.back() == '/')
        url.pop_back();

    if (url.empty())
        return "URL de addon invalida.";

    if (isCinemeta(url))
        return "Cinemeta ja esta sempre ativo, nao precisa adicionar.";

    for (auto& a : this->items)
        if (a.url == url)
            return "Esse addon ja foi adicionado.";

    this->items.push_back(Addon { url, true });
    this->save();
    return "";
}

void AddonStore::remove(size_t index)
{
    if (index >= this->items.size())
        return;
    this->items.erase(this->items.begin() + index);
    this->save();
}

void AddonStore::toggle(size_t index)
{
    if (index >= this->items.size())
        return;
    this->items[index].enabled = !this->items[index].enabled;
    this->save();
}
