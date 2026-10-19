#pragma once

#include <string>
#include <vector>

#include "model/favorite.hpp"

// Persistent list of favorited titles -- mirrors AddonStore's shape
// (single app-wide instance, JSON file next to the executable).
class FavoritesStore
{
  public:
    static FavoritesStore& instance();

    void load();
    const std::vector<Favorite>& all() const { return items; }

    bool contains(const std::string& id) const;
    void add(const Favorite& item); // no-op if already present
    void remove(const std::string& id);

    // Swaps in the list the profile backend returned (see api/cloud_sync)
    // and saves it -- without echoing it back to the server.
    void replaceAll(std::vector<Favorite> newItems);

  private:
    void save();

    bool loaded = false;
    std::vector<Favorite> items;
};
