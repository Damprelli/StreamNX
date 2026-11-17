#pragma once

#include <map>
#include <string>
#include <vector>

#include "api/cloud_sync.hpp"
#include "model/addon.hpp"

// A user-managed list of addons for one purpose (subtitles, catalogs...) --
// same add/toggle/remove rules as AddonStore, persisted to its own JSON file,
// but kept apart from AddonStore's stream providers the way Stremio files
// addons under separate categories. Synced with the profile as its own list
// (`cloudList`); the settings stay on this device.
class AddonListStore
{
  public:
    AddonListStore(const char* file, const char* duplicateMessage, cloud::AddonList cloudList);

    void load(); // reads from disk once; safe to call repeatedly
    const std::vector<Addon>& all() const { return items; }

    // Same validation as AddonStore::add(). Returns "" on success,
    // otherwise a user-facing error message to show as-is.
    std::string add(const std::string& rawUrl);
    void remove(size_t index);
    void toggle(size_t index);

    // Swaps in the list the profile backend returned (see api/cloud_sync)
    // and saves it, keeping the settings -- without echoing it back.
    void replaceAll(std::vector<Addon> newItems);

  protected:
    // Small per-list preferences (e.g. the subtitles language), saved in
    // the same file as the list.
    std::string setting(const std::string& key, const std::string& fallback) const;
    void setSetting(const std::string& key, const std::string& value);

  private:
    void save();

    const char* file;
    const char* duplicateMessage;
    cloud::AddonList cloudList;
    bool loaded = false;
    std::vector<Addon> items;
    std::map<std::string, std::string> settings;
};
