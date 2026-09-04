#pragma once

#include <borealis.hpp>

// The app's Home screen: a "Continuar Assistindo" carousel above a
// "Favoritos" grid. Built entirely from borealis' own views, filled with
// sample data -- no networking, no persistence. Shows a LoadingGate first,
// standing in for the fetch a real backend would need.
class HomeScreen : public brls::Box
{
  public:
    HomeScreen();

  private:
    void addSectionHeader(brls::Box* content, const std::string& text);
    void addContinueWatchingRow(brls::Box* content);
    void addFavoritesGrid(brls::Box* content);
    void buildContent();

    brls::View* loadingGate = nullptr;
};
