#include "view/player_view.hpp"
#include "api/mpv_player.hpp"

PlayerView::PlayerView()
{
    this->setGrow(1.0f);
    // Deliberately no background color here. nanovg batches every fill it's
    // asked to draw and only actually submits them to the GPU once, in
    // nvgEndFrame() -- which runs after the *entire* view tree, including
    // this draw() call, has already executed. mpv's render below is a raw,
    // immediate OpenGL call, not a nanovg one: an opaque nanovg background
    // set here would get flushed to the screen *after* mpv's frame and
    // paint over it, every single frame -- which is exactly what was
    // happening (confirmed by reading back the framebuffer right after
    // mpv's render call: the video was really there, just painted over a
    // moment later).
}

void PlayerView::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    MpvPlayer::instance().draw();
}
