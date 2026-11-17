#include "model/addon_list_store.hpp"
#include "model/addon_store.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

void to_json(nlohmann::json& j, const Addon& a);
void from_json(const nlohmann::json& j, Addon& a);

AddonListStore::AddonListStore(const char* file, const char* duplicateMessage, cloud::AddonList cloudList)
    : file(file)
    , duplicateMessage(duplicateMessage)
    , cloudList(cloudList)
{
}

void AddonListStore::load()
{
    if (this->loaded)
        return;
    this->loaded = true;

    std::ifstream in(this->file);
    if (!in.is_open())
        return;

    try
    {
        nlohmann::json j;
        in >> j;
        this->items    = j.value("addons", std::vector<Addon> {});
        this->settings = j.value("settings", std::map<std::string, std::string> {});
    }
    catch (const std::exception&)
    {
        this->items.clear();
    }
}

void AddonListStore::save()
{
    try
    {
        std::ofstream out(this->file);
        if (out.is_open())
        {
            nlohmann::json j = { { "addons", this->items }, { "settings", this->settings } };
            out << j.dump(2);
        }
    }
    catch (const std::exception&)
    {
    }
}

std::string AddonListStore::add(const std::string& rawUrl)
{
    std::string url;
    std::string error = AddonStore::normalizeUrl(rawUrl, url);
    if (!error.empty())
        return error;

    for (auto& a : this->items)
        if (a.url == url)
            return this->duplicateMessage;

    this->items.push_back(Addon { url, true });
    this->save();
    cloud::addonAdded(this->cloudList, this->items.back());
    return "";
}

void AddonListStore::remove(size_t index)
{
    if (index >= this->items.size())
        return;
    std::string url = this->items[index].url;
    this->items.erase(this->items.begin() + index);
    this->save();
    cloud::addonRemoved(this->cloudList, url);
}

void AddonListStore::toggle(size_t index)
{
    if (index >= this->items.size())
        return;
    this->items[index].enabled = !this->items[index].enabled;
    this->save();
    cloud::addonChanged(this->cloudList, this->items[index]);
}

void AddonListStore::replaceAll(std::vector<Addon> newItems)
{
    this->load(); // the settings live in the same file -- read them before saving over it
    this->items = std::move(newItems);
    this->save();
}

std::string AddonListStore::setting(const std::string& key, const std::string& fallback) const
{
    auto it = this->settings.find(key);
    return it == this->settings.end() ? fallback : it->second;
}

void AddonListStore::setSetting(const std::string& key, const std::string& value)
{
    this->settings[key] = value;
    this->save();
}
