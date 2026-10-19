#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>

// "Perfil": log in to (or create) a StreamNX-Backend profile so favorites,
// "Continuar Assistindo" and addons follow you between the Switch and the
// PC -- or, logged in, see the sync status, sync now, or log out. Name and
// PIN are asked one after the other with the system keyboard (numeric pad
// for the PIN on the Switch); see api/cloud_sync.hpp for what syncing does.
class ProfileScreen : public brls::Box
{
  public:
    ProfileScreen();
    ~ProfileScreen() override;

  private:
    void rebuild();
    void buildLoggedOut(brls::Box* card);
    void buildLoggedIn(brls::Box* card);

    void startLogin();
    void startCreate();
    void setBusy(const std::string& message);
    void finish(const std::string& error, const std::string& successMessage);
    void confirmLogout();
    void refreshStatusLoop();

    brls::Box* body          = nullptr;
    brls::Label* statusLabel = nullptr;
    bool busy                = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
