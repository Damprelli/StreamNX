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

    // Updates just the playback position of an already-touched entry, in
    // place (no reordering -- touch() already moved it to the front when
    // playback started). No-op if `id` isn't present, e.g. a stream that was
    // never really "continue watching" material to begin with. Finishing a
    // stream (95%+ watched) resets its position to 0 instead of leaving it
    // sitting at "almost done", so it reads as fresh to resume/rewatch
    // rather than picking up one second before the credits.
    void updateProgress(const std::string& id, double positionSeconds, double durationSeconds);

    // Swaps in the list the profile backend returned (see api/cloud_sync)
    // and saves it -- without echoing it back to the server.
    void replaceAll(std::vector<ContinueWatching> newItems);

  private:
    void save();

    bool loaded = false;
    std::vector<ContinueWatching> items;
};
