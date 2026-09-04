#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

#include "model/media_item.hpp"

// Fills `container` (a Box, axis COLUMN) with horizontally-scrolling
// shelves of MediaCard, one shelf per `perRow` items. Replaces whatever the
// container held before; shows `emptyMessage` instead if `items` is empty.
// Shared by the Home (Favoritos) and Search (results) screens.
void fillMediaGrid(brls::Box* container, const std::vector<MediaItem>& items, int perRow, const std::string& emptyMessage);
