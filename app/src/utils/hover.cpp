#include "utils/hover.hpp"

#include <unordered_map>

namespace hover
{

namespace
{

std::unordered_map<brls::View*, Callback>& registry()
{
    static std::unordered_map<brls::View*, Callback> views;
    return views;
}

brls::View* g_hovered = nullptr;

void setHovered(brls::View* view)
{
    if (view == g_hovered)
        return;

    brls::View* previous = g_hovered;
    g_hovered            = view;

    auto& views = registry();
    if (previous)
    {
        auto it = views.find(previous);
        if (it != views.end())
            it->second(false);
    }
    if (view)
    {
        auto it = views.find(view);
        if (it != views.end())
            it->second(true);
    }
}

void update()
{
    auto stack = brls::Application::getActivitiesStack();
    if (stack.empty())
    {
        setHovered(nullptr);
        return;
    }

    brls::RawMouseState mouse;
    brls::Application::getPlatform()->getInputManager()->updateMouseStates(&mouse);

    // Nearest registered ancestor of whatever is under the cursor -- a
    // card's poster/labels are children of the card, but the card is what
    // reacts.
    brls::View* hit = stack.back()->getContentView()->hitTest(mouse.position);
    auto& views     = registry();
    while (hit && views.find(hit) == views.end())
        hit = hit->getParent();

    setHovered(hit);
}

} // namespace

void install()
{
    brls::Application::getRunLoopEvent()->subscribe([] { update(); });
}

void track(brls::View* view, Callback onChange)
{
    registry()[view] = std::move(onChange);
}

void forget(brls::View* view)
{
    registry().erase(view);
    if (g_hovered == view)
        g_hovered = nullptr;
}

} // namespace hover

HoverBox::~HoverBox()
{
    hover::forget(this);
}

void HoverBox::onHover(hover::Callback onChange)
{
    hover::track(this, std::move(onChange));
}

void HoverBox::setHoverColors(NVGcolor normal, NVGcolor hovered)
{
    this->setBackgroundColor(normal);
    this->onHover([this, normal, hovered](bool isHovered) { this->setBackgroundColor(isHovered ? hovered : normal); });
}
