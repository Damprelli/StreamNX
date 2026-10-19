#include "view/ui_kit.hpp"
#include "utils/mouse.hpp"

namespace ui
{

NVGcolor color(const std::string& key)
{
    return brls::Application::getTheme().getColor("streamnx/" + key);
}

brls::Label* label(const std::string& text, float size, NVGcolor textColor)
{
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(textColor);
    return l;
}

namespace
{
// A Label drawn with the Material Icons font directly. Relying on it being
// a fallback of the regular font isn't enough: Material shares Unicode's
// private-use range with the Switch button-glyph font, which comes first
// in the fallback chain, so codepoints like "add" (U+E145) came out as a
// controller arrow instead.
class IconLabel : public brls::Label
{
  public:
    IconLabel()
    {
        int material = brls::Application::getFont(brls::FONT_MATERIAL_ICONS);
        if (material >= 0)
            this->font = material;
    }
};
} // namespace

brls::Label* icon(const char* glyph, float size, NVGcolor iconColor)
{
    auto* l = new IconLabel();
    l->setText(glyph);
    l->setFontSize(size);
    l->setTextColor(iconColor);
    l->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    l->setVerticalAlign(brls::VerticalAlign::CENTER);
    return l;
}

HoverBox* button(const std::string& text, ButtonStyle style, std::function<void()> onClick, const char* iconGlyph)
{
    NVGcolor normal, hovered;
    switch (style)
    {
        case ButtonStyle::Primary:
            normal  = color("accent");
            hovered = color("accent_hover");
            break;
        case ButtonStyle::Danger:
            normal  = color("danger");
            hovered = color("danger_hover");
            break;
        default:
            normal  = color("surface_raised");
            hovered = color("surface_hover");
            break;
    }

    auto* b = new HoverBox();
    b->setFocusable(true);
    b->setAxis(brls::Axis::ROW);
    b->setDimensions(brls::View::AUTO, brls::View::AUTO);
    b->setAlignItems(brls::AlignItems::CENTER);
    b->setJustifyContent(brls::JustifyContent::CENTER);
    b->setPadding(9, 18, 9, 18);
    b->setCornerRadius(10);
    b->setHoverColors(normal, hovered);

    if (iconGlyph)
    {
        auto* i = icon(iconGlyph, 18, color("text"));
        if (!text.empty())
            i->setMarginRight(8);
        b->addView(i);
    }
    if (!text.empty())
        b->addView(label(text, 15, color("text")));

    makeClickable(b, [onClick](brls::View*) {
        onClick();
        return true;
    });
    return b;
}

brls::Box* backBar()
{
    auto* bar = new brls::Box();
    bar->setAxis(brls::Axis::ROW);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setHeight(60);
    bar->setPadding(0, 24, 0, 24);
    bar->setBackgroundColor(color("surface"));
    bar->addView(button("Voltar", ButtonStyle::Secondary, []() { brls::Application::popActivity(); }, icons::back));
    return bar;
}

void addAboveFooter(brls::Box* parent, brls::View* view)
{
    auto& children = parent->getChildren();
    if (!children.empty() && dynamic_cast<brls::BottomBar*>(children.back()))
        parent->addView(view, children.size() - 1);
    else
        parent->addView(view);
}

brls::Box* emptyState(const char* iconGlyph, const std::string& title, const std::string& hint)
{
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setPadding(60, 20, 60, 20);

    auto* i = icon(iconGlyph, 56, color("text_muted"));
    i->setMarginBottom(16);
    box->addView(i);

    auto* t = label(title, 20, color("text"));
    t->setMarginBottom(8);
    box->addView(t);

    auto* h = label(hint, 15, color("text_muted"));
    h->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    h->setIsWrapping(true);
    h->setMaxWidth(560);
    box->addView(h);
    return box;
}

void makeHoverSurface(HoverBox* box, NVGcolor normal, NVGcolor hovered)
{
    box->setHoverColors(normal, hovered);
}

} // namespace ui
