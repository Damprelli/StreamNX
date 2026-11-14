#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>
#include <vector>

#include "model/media_item.hpp"

// "Buscar Filmes/Series": with no query typed, shows every movie/series
// catalog Cinemeta and the enabled addons declare, merged together, 100 at
// a time. Press X to type a query -- that fires a real search request
// (catalog/{type}/{id}/search={query}.json) against Cinemeta and every
// catalog addon catalog that supports it, rather than just filtering titles
// already fetched for browsing (which almost never includes an arbitrary
// title someone actually searches for).
class SearchScreen : public brls::Box
{
  public:
    SearchScreen();
    ~SearchScreen();

  private:
    void buildContent();
    void loadCatalog();
    void openSearchDialog();
    void performSearch(const std::string& searchQuery);
    // `resetPaging` starts back at the first 100 results -- true for a
    // fresh catalog load or a new search query, false for "Carregar mais"
    // extending how much of the already-filtered list is shown.
    void rebuildResults(bool resetPaging = true);
    void loadMore();

    brls::View* loadingGate    = nullptr;
    brls::Label* resultsHeader = nullptr;
    brls::Box* resultsBox      = nullptr;
    std::string query;
    std::vector<MediaItem> allItems;    // browse mode (query empty)
    std::vector<MediaItem> searchResults; // search mode (query non-empty)
    size_t visibleCount   = 0;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
