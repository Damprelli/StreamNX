#pragma once

#include <borealis.hpp>

#include "utils/hover.hpp"

// "Addons": lets the user paste an addon manifest URL to add it, then
// enable/disable or remove what's configured. Same logic as StreamNX-main's
// addon manager (a flat list of manifest URLs, toggled/removed
// individually, persisted to disk) -- built with plain borealis views in
// this project's own visual style, not a copy of that screen's layout.
// Shows a (brief) LoadingGate first -- reading the config file is
// effectively instant, but this keeps the same "don't show an interactive
// list until it's actually loaded" pattern the other screens use, and is
// where a real config source would plug in.
//
// Three tabs, like Stremio's own addon categories: "Streams" (AddonStore),
// "Catalogos" (CatalogAddonStore, on top of the always-on Cinemeta) and
// "Legendas" (SubtitleAddonStore and its preferred language). X /
// "Adicionar" adds to whichever tab is showing; ZL/ZR switch tabs.
class AddonListStore;

class AddonsScreen : public brls::Box
{
  public:
    AddonsScreen();

  private:
    enum Tab
    {
        TAB_STREAMS,
        TAB_CATALOGS,
        TAB_SUBTITLES,
        TAB_COUNT,
    };

    void buildContent();
    void openAddDialog();
    void showTab(int tab);
    void rebuildList();
    // The "Catalogos"/"Legendas" addon lists -- same rows and actions as
    // rebuildList(), over an AddonListStore instead of AddonStore.
    void rebuildAddonList(AddonListStore& store, brls::Box* box, const std::string& kind);
    void rebuildCatalogAddonList();
    void rebuildSubtitleList();

    brls::View* loadingGate     = nullptr;
    brls::Box* listBox          = nullptr;

    int currentTab                    = TAB_STREAMS;
    brls::Box* tabs[TAB_COUNT]        = {};
    HoverBox* tabButtons[TAB_COUNT]   = {};
    brls::Box* catalogAddonListBox    = nullptr;
    brls::Box* subtitleListBox        = nullptr;
    brls::Label* languageValue        = nullptr;
};
