#pragma once

#include <borealis.hpp>

// A plain black canvas that MpvPlayer paints its current video frame onto
// every frame, via a raw OpenGL render call slotted in between the
// background fill and the rest of the (nanovg-drawn) UI -- see
// api/mpv_player.hpp for why that ordering matters.
class PlayerView : public brls::Box
{
  public:
    PlayerView();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;
};
