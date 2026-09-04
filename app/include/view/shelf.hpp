#pragma once

#include <borealis.hpp>

// An HScrollingFrame meant to be stacked vertically among sibling shelves
// (e.g. "Continuar Assistindo" above "Favoritos"). Plain borealis
// HScrollingFrame blocks UP/DOWN from ever leaving it -- its own
// getParentNavigationDecision() returns nullptr outright for those two
// directions, on the assumption a horizontal strip is never nested inside
// a vertical stack. That left shelf-to-shelf UP/DOWN navigation dead on
// arrival. Overridden here so UP/DOWN falls back to the generic Box
// behaviour, which correctly bubbles the request out to whatever real
// vertical container holds this shelf.
class Shelf : public brls::HScrollingFrame
{
  public:
    brls::View* getParentNavigationDecision(brls::View* from, brls::View* newFocus, brls::FocusDirection direction) override
    {
        if (direction == brls::FocusDirection::UP || direction == brls::FocusDirection::DOWN)
            return brls::Box::getParentNavigationDecision(from, newFocus, direction);

        return brls::HScrollingFrame::getParentNavigationDecision(from, newFocus, direction);
    }
};
