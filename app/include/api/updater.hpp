#pragma once

#include <string>

// Self-update: on startup, asks StreamNX-Backend (GET /api/update, the same
// server as the profiles -- cloud::serverUrl()) whether a newer release
// exists for this platform, and if so offers it in a dialog. Accepting
// downloads the file straight from GitHub Releases and installs it:
//
//  - Switch: the new .nro replaces the running one (the app is already in
//    memory, so that's safe) and the app relaunches itself through hbloader;
//  - Windows: the downloaded Setup.exe runs silently over the current
//    install and relaunches the app (see StreamNX.iss), while this one quits.
//
// Other platforms don't check at all. "Pular esta versao" is remembered in
// update.json next to the other data files.
namespace updater
{

// The version this build is ("2.0.1"), as sent to /api/update.
std::string currentVersion();

// main(): argv[0] -- on the Switch, the .nro that has to be replaced.
void setExecutablePath(const std::string& path);

// After the first activity is pushed: checks in the background a moment
// later; the dialog only shows up if there's something new.
void checkOnStartup();

// Before http::globalCleanup(): cancels a download still running.
void globalCleanup();

} // namespace updater
