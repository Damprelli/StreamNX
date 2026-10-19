#pragma once

#include <borealis.hpp>
#include <functional>

// Enables mouse clicks on a view that already has an action bound to
// BUTTON_A (the overwhelming majority of the app's "buttons", via
// registerAction or registerClickAction): attaches the TapGestureRecognizer
// borealis already ships, which re-fires that same action at the end of a
// tap and gives the view focus/highlight -- the same pattern VideoView
// already uses for the player OSD. Harmless on every platform (only adds a
// gesture recognizer), so call it unconditionally; only the surrounding
// layout/visibility needs to be gated behind #ifdef STREAMNX_PC_UI.
void makeClickable(brls::View* view);

// For views without a prior BUTTON_A action: registers onClick as the
// view's primary action and wires the gesture recognizer in one call.
void makeClickable(brls::View* view, std::function<bool(brls::View*)> onClick);
