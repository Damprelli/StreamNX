#pragma once

#include <borealis.hpp>

#include <chrono>
#include <functional>
#include <memory>

// Shows a spinner + "Carregando..." for a short simulated load, then swaps
// itself out and calls `onReady` exactly once -- there's nothing focusable
// while a LoadingGate is showing, so a screen can't be navigated into (and
// broken by interacting with half-built content) before its data exists.
//
// No networking here (there's nothing to actually fetch yet): the delay
// just stands in for one, on the same timer/frame-driven mechanism a real
// fetch's completion callback would use, so swapping in a real request
// later is a one-line change in whoever owns this gate.
class LoadingGate : public brls::Box
{
  public:
    explicit LoadingGate(std::function<void()> onReady, int delayMs = 550);
    ~LoadingGate();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

  private:
    std::function<void()> onReady;
    int delayMs;
    bool fired = false;
    std::chrono::steady_clock::time_point start;

    // Shared with the deferred callback so it can tell, a frame later,
    // whether this gate (and whatever owns it) has since been destroyed --
    // switching screens before the simulated load finishes must not call
    // back into a freed screen.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
