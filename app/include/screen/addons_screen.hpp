#pragma once

#include <borealis.hpp>

// "Addons": lets the user paste an addon manifest URL to add it, then
// enable/disable or remove what's configured. Same logic as StreamNX-main's
// addon manager (a flat list of manifest URLs, toggled/removed
// individually, persisted to disk) -- built with plain borealis views in
// this project's own visual style, not a copy of that screen's layout.
// Shows a (brief) LoadingGate first -- reading the config file is
// effectively instant, but this keeps the same "don't show an interactive
// list until it's actually loaded" pattern the other screens use, and is
// where a real config source would plug in.
class AddonsScreen : public brls::Box
{
  public:
    AddonsScreen();

  private:
    void buildContent();
    void openAddDialog();
    void rebuildList();
    void rebuildCatalogList();

    brls::View* loadingGate     = nullptr;
    brls::Box* listBox          = nullptr;
    brls::Box* catalogListBox   = nullptr;
};
