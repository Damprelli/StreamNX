#include "view/media_grid.hpp"
#include "view/media_card.hpp"
#include "view/shelf.hpp"

#include <algorithm>

namespace
{
constexpr float SHELF_HEIGHT = 300;
}

void fillMediaGrid(brls::Box* container, const std::vector<MediaItem>& items, int perRow, const std::string& emptyMessage)
{
    container->clearViews();

    if (items.empty())
    {
        auto* empty = new brls::Label();
        empty->setText(emptyMessage);
        empty->setTextColor(nvgRGB(150, 150, 150));
        container->addView(empty);
        return;
    }

    // Each chunk of `perRow` items becomes its own horizontally-scrolling
    // shelf (Shelf, an HScrollingFrame) instead of a plain overflowing Box
    // row -- a plain row has no way to bring items past the visible width
    // into view (borealis doesn't wrap or auto-scroll a Box), which left
    // later items in a wide row permanently unreachable. Shelves stacked in
    // the outer vertical ScrollingFrame is the same shape "Continuar
    // Assistindo" already uses.
    for (size_t i = 0; i < items.size(); i += (size_t)perRow)
    {
        auto* shelf = new Shelf();
        shelf->setHeight(SHELF_HEIGHT);
        shelf->setScrollingIndicatorVisible(false);
        shelf->setMarginBottom(16);
        // NATURAL (the default) only scrolls while a direction is held,
        // and only jumps focus once the target scrolls fully into view --
        // a single tap past the visible width does nothing yet, which reads
        // as navigation being capped at the window's width. CENTERED moves
        // focus (and scrolls to it) on every single press.
        shelf->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setHeight(SHELF_HEIGHT);

        for (size_t j = i; j < items.size() && j < i + (size_t)perRow; j++)
            row->addView(new MediaCard(items[j]));

        shelf->setContentView(row);
        container->addView(shelf);
    }
}

#ifdef STREAMNX_PC_UI
namespace
{
// MediaCard's width (160) + marginRight (16), from media_card.xml.
constexpr float CARD_STEP = 176.0f;
}

void fillMediaGridPc(brls::Box* container, const std::vector<MediaItem>& items, float availableWidth, const std::string& emptyMessage)
{
    container->clearViews();

    if (items.empty())
    {
        auto* empty = new brls::Label();
        empty->setText(emptyMessage);
        empty->setTextColor(nvgRGB(150, 150, 150));
        container->addView(empty);
        return;
    }

    int perRow = std::max(1, (int)(availableWidth / CARD_STEP));

    brls::Box* row = nullptr;
    for (size_t i = 0; i < items.size(); i++)
    {
        if (i % (size_t)perRow == 0)
        {
            row = new brls::Box();
            row->setAxis(brls::Axis::ROW);
            row->setMarginBottom(20);
            container->addView(row);
        }
        row->addView(new MediaCard(items[i]));
    }
}
#endif
