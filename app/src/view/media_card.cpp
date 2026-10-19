#include "view/media_card.hpp"
#include "screen/detail_screen.hpp"
#include "api/image_cache.hpp"
#include "utils/mouse.hpp"
#include "utils/hover.hpp"

#include <cctype>

namespace
{

// A small fixed palette so cards without real poster art still look
// distinct from one another -- picked by MediaItem::colorIndex.
const NVGcolor PALETTE[] = {
    nvgRGB(0x3A, 0x6B, 0x8F),
    nvgRGB(0x8F, 0x4A, 0x6B),
    nvgRGB(0x4A, 0x8F, 0x5C),
    nvgRGB(0x8F, 0x7A, 0x3A),
    nvgRGB(0x5C, 0x4A, 0x8F),
    nvgRGB(0x8F, 0x5C, 0x3A),
};
constexpr size_t PALETTE_SIZE = sizeof(PALETTE) / sizeof(PALETTE[0]);

std::string firstLetter(const std::string& title)
{
    if (title.empty())
        return "?";
    return std::string(1, (char)std::toupper((unsigned char)title[0]));
}

} // namespace

MediaCard::MediaCard(const MediaItem& item, std::function<bool(brls::View*)> onOpen)
{
    this->inflateFromXMLRes("xml/views/media_card.xml");

    this->posterBox->setBackgroundColor(PALETTE[item.colorIndex % PALETTE_SIZE]);
    this->initialLabel->setText(firstLetter(item.title));

    this->titleLabel->setText(item.title);

    if (item.subtitle.empty())
        this->subtitleLabel->setVisibility(brls::Visibility::GONE);
    else
        this->subtitleLabel->setText(item.subtitle);

    if (item.rating > 0.0f)
        this->ratingLabel->setText(std::to_string(item.rating).substr(0, 3));
    else
        this->ratingLabel->setVisibility(brls::Visibility::INVISIBLE);

    this->favoriteLabel->setVisibility(item.isFavorite ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);

    if (!item.poster.empty())
    {
        brls::Image* posterImage  = this->posterImage;
        brls::Label* initialLabel = this->initialLabel;
        imgcache::load(this->posterImage, item.poster, this->alive, [posterImage, initialLabel]() {
            posterImage->setVisibility(brls::Visibility::VISIBLE);
            initialLabel->setVisibility(brls::Visibility::GONE);
        });
    }

    if (item.progress > 0.0f)
    {
        this->progressTrack->setVisibility(brls::Visibility::VISIBLE);
        this->progressBar->setWidthPercentage(item.progress * 100.0f);
    }
    else
    {
        this->progressTrack->setVisibility(brls::Visibility::GONE);
    }

    if (onOpen)
    {
        this->registerAction("Abrir", brls::BUTTON_A, onOpen);
    }
    else
    {
        std::string title = item.title;
        std::string id     = item.id;
        std::string type   = item.type;
        this->registerAction("Abrir", brls::BUTTON_A, [id, type, title](brls::View*) {
            if (id.empty() || type.empty())
                brls::Application::notify(title);
            else
                brls::Application::pushActivity(new brls::Activity(new DetailScreen(id, type, title)));
            return true;
        });
    }
    makeClickable(this);

    // PC: lift the card and outline the poster under the mouse.
    brls::Box* posterBox = this->posterBox;
    hover::track(this, [this, posterBox](bool hovered) {
        posterBox->setBorderColor(brls::Application::getTheme().getColor("streamnx/accent"));
        posterBox->setBorderThickness(hovered ? 3.0f : 0.0f);
        this->setTranslationY(hovered ? -4.0f : 0.0f);
    });
}

MediaCard::~MediaCard()
{
    hover::forget(this);
    *this->alive = false;
}
