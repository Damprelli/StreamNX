#pragma once

#include <string>
#include <vector>

#include "model/continue_watching.hpp"

// Persistent "Continuar Assistindo" list -- mirrors FavoritesStore's shape.
// Most-recently-touched entry first; capped so it can't grow forever.
class ContinueWatchingStore
{
  public:
    static ContinueWatchingStore& instance();

    void load();
    const std::vector<ContinueWatching>& all() const { return items; }

    // Adds `item` at the front, or moves it there and refreshes its fields
    // if that id was already present -- call whenever a stream is opened.
    void touch(const ContinueWatching& item);
    void remove(const std::string& id);

  private:
    void save();

    bool loaded = false;
    std::vector<ContinueWatching> items;
};
