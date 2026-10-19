#pragma once

#include <functional>
#include <string>

#include "api/http_client.hpp"
#include "model/addon.hpp"
#include "model/continue_watching.hpp"
#include "model/favorite.hpp"

// Profile sync with StreamNX-Backend: favorites, "Continuar Assistindo" and
// addons follow the logged-in profile across the Switch, the PC and the web.
//
// The local JSON files stay the source the screens read from -- this only
// keeps them in step with the server:
//  - on startup (and "Sincronizar agora"), the server's lists replace the
//    local ones;
//  - every local change (the store hooks below) is sent in the background,
//    in order, through one serial queue;
//  - if a change can't be sent (offline), the session is marked "dirty" and
//    the next sync merges the local lists into the server's instead of
//    overwriting them, so nothing done offline is lost.
//
// The session (server, token, profile name) lives in profile.json next to
// the other data files. Logged out, every hook is a no-op and the app works
// exactly as before, purely local.
namespace cloud
{

void globalInit();    // after http::globalInit(): loads profile.json, starts the startup sync
void globalCleanup(); // before http::globalCleanup(): flushes what's queued (briefly), stops the worker

bool isLoggedIn();
std::string profileName();
std::string serverUrl();

// One line for the Profile screen ("Sincronizado", "Sem conexao...", ...).
std::string statusText();

// `done` runs on the main thread with "" on success or a message to show
// as-is. On success the device's existing local lists are merged into the
// profile, so the first login on a console/PC with data loses nothing.
void login(const std::string& name, const std::string& pin, http::AliveFlag alive,
    std::function<void(const std::string& error)> done);
void createProfile(const std::string& name, const std::string& pin, http::AliveFlag alive,
    std::function<void(const std::string& error)> done);

// Sends whatever is still pending, ends the session on the server and
// clears the profile's lists from this device (they're safe on the server)
// -- so whoever logs in next doesn't get them merged into their profile.
void logout();

void syncNow(http::AliveFlag alive, std::function<void(const std::string& error)> done);

// Calls `ready` (main thread) once the startup sync has finished, failed or
// taken too long -- Home waits on this so it isn't built from stale lists.
void whenStartupSyncDone(http::AliveFlag alive, std::function<void()> ready);

// Store hooks: called right after the local change is saved.
void favoriteAdded(const Favorite& item);
void favoriteRemoved(const std::string& id);
void continueTouched(const ContinueWatching& item);
void continueProgress(const ContinueWatching& item);
void continueRemoved(const std::string& id);
void addonAdded(const Addon& addon);
void addonChanged(const Addon& addon);
void addonRemoved(const std::string& url);

} // namespace cloud
