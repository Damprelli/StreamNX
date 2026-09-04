#pragma once

#include <borealis.hpp>

#include <memory>

#include "model/media_item.hpp"

// A single poster card -- a colored placeholder (+ initial letter) that
// swaps for the real poster art once it's fetched (see the image_cache
// module for how that's kept from being re-downloaded every time). Used by
// Continue Watching, Favoritos, and Buscar's results grid.
class MediaCard : public brls::Box
{
  public:
    explicit MediaCard(const MediaItem& item);
    ~MediaCard();

  private:
    BRLS_BIND(brls::Box, posterBox, "media_card/poster_box");
    BRLS_BIND(brls::Label, initialLabel, "media_card/initial");
    BRLS_BIND(brls::Image, posterImage, "media_card/poster_image");
    BRLS_BIND(brls::Label, ratingLabel, "media_card/rating");
    BRLS_BIND(brls::Label, favoriteLabel, "media_card/favorite");
    BRLS_BIND(brls::View, progressTrack, "media_card/progress_track");
    BRLS_BIND(brls::Rectangle, progressBar, "media_card/progress");
    BRLS_BIND(brls::Label, titleLabel, "media_card/title");
    BRLS_BIND(brls::Label, subtitleLabel, "media_card/subtitle");

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
