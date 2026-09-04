#include "view/loading_gate.hpp"

LoadingGate::LoadingGate(std::function<void()> onReady, int delayMs)
    : onReady(std::move(onReady))
    , delayMs(delayMs)
{
    this->setAxis(brls::Axis::COLUMN);
    this->setGrow(1.0f);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::CENTER);

    auto* spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::LARGE);
    this->addView(spinner);

    auto* label = new brls::Label();
    label->setText("Carregando...");
    label->setFontSize(16);
    label->setTextColor(nvgRGB(150, 150, 150));
    label->setMarginTop(16);
    this->addView(label);

    this->start = std::chrono::steady_clock::now();
}

LoadingGate::~LoadingGate()
{
    *this->alive = false;
}

void LoadingGate::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    if (!this->fired)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - this->start)
                           .count();

        if (elapsed >= this->delayMs)
        {
            this->fired = true;

            // Deferred to the next tick (not called inline from draw()) so
            // whoever owns this gate can safely remove/replace it without
            // mutating the view tree mid-traversal.
            auto callback  = this->onReady;
            auto aliveFlag = this->alive;
            brls::sync([callback, aliveFlag]() {
                if (*aliveFlag)
                    callback();
            });
        }
    }

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}
