#pragma once

#include <borealis.hpp>
#include <functional>

// Mouse hover for the PC build. borealis has no notion of "hover" (only
// taps and gamepad focus), so this tracks it itself: once per frame it
// hit-tests the cursor position against the top activity and tells the
// nearest registered view under it that it's hovered (and the previously
// hovered one that it no longer is).
//
// A registered view MUST be unregistered before it's destroyed -- the
// tracker keeps a raw pointer. HoverBox does that in its destructor, so
// prefer it over calling track()/forget() by hand. On Switch everything
// here compiles but stays inert (install() is only called on PC).
namespace hover
{

using Callback = std::function<void(bool hovered)>;

void install();
void track(brls::View* view, Callback onChange);
void forget(brls::View* view);

} // namespace hover

// A Box that can react to the mouse hovering it (PC). Unregisters itself
// from the hover tracker when destroyed.
class HoverBox : public brls::Box
{
  public:
    HoverBox() = default;
    ~HoverBox() override;

    // Calls onChange(true/false) as the cursor enters/leaves this box.
    void onHover(hover::Callback onChange);

    // Common case: swap the background color while hovered.
    void setHoverColors(NVGcolor normal, NVGcolor hovered);
};
