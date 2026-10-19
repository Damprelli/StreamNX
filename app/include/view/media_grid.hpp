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

#ifdef STREAMNX_PC_UI
// PC equivalent of fillMediaGrid: plain rows of MediaCard stacked in
// `container`, no per-row Shelf -- free mouse-wheel/drag scrolling comes
// from the vertical ScrollingFrame the caller already wraps `container` in.
// `availableWidth` is the caller's known content width (screen width minus
// its own paddings/sidebar), used to fit as many cards per row as possible.
void fillMediaGridPc(brls::Box* container, const std::vector<MediaItem>& items, float availableWidth, const std::string& emptyMessage);
#endif
