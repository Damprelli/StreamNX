#pragma once

#include <map>
#include <string>
#include <vector>

#include "model/addon.hpp"

// A local, user-managed list of addons for one purpose (subtitles,
// catalogs...) -- same add/toggle/remove rules as AddonStore, persisted to
// its own JSON file, but kept apart from AddonStore's stream providers the
// way Stremio files addons under separate categories. Local only: the
// profile backend only knows AddonStore's list, so these aren't synced.
class AddonListStore
{
  public:
    AddonListStore(const char* file, const char* duplicateMessage);

    void load(); // reads from disk once; safe to call repeatedly
    const std::vector<Addon>& all() const { return items; }

    // Same validation as AddonStore::add(). Returns "" on success,
    // otherwise a user-facing error message to show as-is.
    std::string add(const std::string& rawUrl);
    void remove(size_t index);
    void toggle(size_t index);

  protected:
    // Small per-list preferences (e.g. the subtitles language), saved in
    // the same file as the list.
    std::string setting(const std::string& key, const std::string& fallback) const;
    void setSetting(const std::string& key, const std::string& value);

  private:
    void save();

    const char* file;
    const char* duplicateMessage;
    bool loaded = false;
    std::vector<Addon> items;
    std::map<std::string, std::string> settings;
};
