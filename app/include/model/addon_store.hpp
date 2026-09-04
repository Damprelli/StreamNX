#pragma once

#include <string>
#include <vector>

#include "model/addon.hpp"

// Persistent list of configured addons -- add by manifest URL, toggle
// enabled/disabled, remove. A single app-wide instance (like Favourites in
// StreamNX-main), backed by a small JSON file next to the executable so
// addons survive restarts.
class AddonStore
{
  public:
    static AddonStore& instance();

    void load(); // reads from disk once; safe to call repeatedly
    const std::vector<Addon>& all() const { return items; }

    // Normalizes and validates rawUrl (must start with http(s)://, strips a
    // trailing "manifest.json" and slashes), then appends it unless it's a
    // duplicate. Returns "" on success, otherwise a user-facing error
    // message to show as-is.
    std::string add(const std::string& rawUrl);
    void remove(size_t index);
    void toggle(size_t index);

  private:
    void save();

    bool loaded = false;
    std::vector<Addon> items;
};
