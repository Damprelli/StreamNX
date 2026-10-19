#include "utils/mouse.hpp"

void makeClickable(brls::View* view)
{
    view->addGestureRecognizer(new brls::TapGestureRecognizer(view));
}

void makeClickable(brls::View* view, std::function<bool(brls::View*)> onClick)
{
    view->registerClickAction(onClick);
    makeClickable(view);
}
