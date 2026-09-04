#pragma once

#include <borealis.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/stremio_api.hpp"

// A movie/series' detail page: backdrop + poster, title, rating/year,
// description, a favorite toggle, and -- for series -- a horizontally
// scrolling "Temporadas" shelf whose selection lists that season's
// episodes below it. Pushed as its own Activity (B pops back), fetched by
// (id, type) alone: tries every enabled addon in turn for a meta match,
// same as a real Stremio client aggregating meta providers.
class DetailScreen : public brls::Box
{
  public:
    DetailScreen(const std::string& id, const std::string& type, const std::string& fallbackTitle);
    ~DetailScreen();

  private:
    void tryAddonsForMeta(size_t addonIndex);
    void onMetaLoaded(const stremio::MetaDetail& detail);
    void showError(const std::string& message);
    void buildContent(const stremio::MetaDetail& detail);
    void showSeasonEpisodes(int season);
    void refreshFavoriteButton();
    void openStreams(const std::string& videoId, const std::string& title, const std::string& subtitle);

    std::string id;
    std::string type;
    std::string fallbackTitle;

    brls::View* loadingGate = nullptr;
    brls::Box* episodesBox  = nullptr;
    brls::Box* favoriteButton = nullptr;
    brls::Label* favoriteLabel = nullptr;

    std::map<int, std::vector<stremio::VideoEntry>> seasons;
    stremio::MetaDetail meta;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
