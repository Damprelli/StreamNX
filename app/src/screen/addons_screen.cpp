#include "screen/addons_screen.hpp"
#include "model/addon_store.hpp"
#include "model/catalog_store.hpp"
#include "view/loading_gate.hpp"
#include "api/stremio_api.hpp"

#include <memory>

namespace
{

std::string hostLabel(const std::string& url)
{
    std::string s   = url;
    size_t schemeAt = s.find("://");
    if (schemeAt != std::string::npos)
        s = s.substr(schemeAt + 3);
    if (s.rfind("www.", 0) == 0)
        s = s.substr(4);
    size_t slashAt = s.find('/');
    if (slashAt != std::string::npos)
        s = s.substr(0, slashAt);
    return s.empty() ? url : s;
}

// One configured addon: host name + enabled tag on top, full URL below.
// A on the row toggles enabled/disabled; RB removes it.
class AddonRow : public brls::Box
{
  public:
    AddonRow(const std::string& url, bool enabled, std::function<void()> onToggle, std::function<void()> onRemove)
    {
        this->buildRow(url, enabled, onToggle, onRemove);
    }

    ~AddonRow()
    {
        *this->rowAlive = false;
    }

  private:
    std::shared_ptr<bool> rowAlive = std::make_shared<bool>(true);

    void buildRow(const std::string& url, bool enabled, std::function<void()> onToggle, std::function<void()> onRemove)
    {
        this->setAxis(brls::Axis::COLUMN);
        this->setFocusable(true);
        this->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->setPadding(14, 20, 14, 20);
        this->setMarginBottom(10);
        this->setCornerRadius(8);
        this->setBackgroundColor(nvgRGB(26, 29, 36));

        auto* topRow = new brls::Box();
        topRow->setAxis(brls::Axis::ROW);
        topRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

        auto* host = new brls::Label();
        host->setText(hostLabel(url));
        host->setFontSize(19);
        topRow->addView(host);

        auto* status = new brls::Label();
        status->setText(enabled ? "Ativo" : "Desativado");
        status->setFontSize(14);
        status->setTextColor(enabled ? nvgRGB(110, 220, 130) : nvgRGB(150, 150, 150));
        topRow->addView(status);

        this->addView(topRow);

        auto* urlLabel = new brls::Label();
        urlLabel->setText(url);
        urlLabel->setFontSize(13);
        urlLabel->setTextColor(nvgRGB(140, 140, 150));
        urlLabel->setMarginTop(4);
        this->addView(urlLabel);

        // Whether the URL is enabled/disabled says nothing about whether it
        // actually answers -- probe it the same way a real client would
        // before trying to use it, via the one request every addon must
        // support (its manifest).
        auto* netStatus = new brls::Label();
        netStatus->setText("Verificando conexao...");
        netStatus->setFontSize(13);
        netStatus->setTextColor(nvgRGB(180, 160, 90));
        netStatus->setMarginTop(6);
        this->addView(netStatus);

        stremio::fetchManifest(
            url, this->rowAlive,
            [netStatus](stremio::ManifestInfo) {
                netStatus->setText("✓ Online");
                netStatus->setTextColor(nvgRGB(110, 220, 130));
            },
            [netStatus](std::string) {
                netStatus->setText("✗ Indisponivel");
                netStatus->setTextColor(nvgRGB(220, 90, 90));
            });

        this->registerAction("Ativar/Desativar", brls::BUTTON_A, [onToggle](brls::View*) {
            onToggle();
            return true;
        });
        this->registerAction("Remover", brls::BUTTON_RB, [onRemove](brls::View*) {
            onRemove();
            return true;
        });
    }
};

// One extra catalog from CatalogStore (Cinemeta's genre/year/rating
// variants -- see catalog_store.cpp). Simpler than AddonRow: no manifest to
// probe, no removing -- just on/off, since it's a fixed, known set rather
// than something the user pastes in.
class CatalogRow : public brls::Box
{
  public:
    CatalogRow(const std::string& title, bool enabled, std::function<void()> onToggle)
    {
        this->setAxis(brls::Axis::ROW);
        this->setFocusable(true);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        this->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->setPadding(14, 20, 14, 20);
        this->setMarginBottom(10);
        this->setCornerRadius(8);
        this->setBackgroundColor(nvgRGB(26, 29, 36));

        auto* titleLabel = new brls::Label();
        titleLabel->setText(title);
        titleLabel->setFontSize(17);
        this->addView(titleLabel);

        auto* status = new brls::Label();
        status->setText(enabled ? "Ativo" : "Desativado");
        status->setFontSize(14);
        status->setTextColor(enabled ? nvgRGB(110, 220, 130) : nvgRGB(150, 150, 150));
        this->addView(status);

        this->registerAction("Ativar/Desativar", brls::BUTTON_A, [onToggle](brls::View*) {
            onToggle();
            return true;
        });
    }
};

} // namespace

AddonsScreen::AddonsScreen()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(30, 50, 30, 50);
    // A safe place to park focus while rebuildList() tears down whichever
    // row's own action (toggle/remove) triggered the rebuild -- see there.
    this->setFocusable(true);

    auto* title = new brls::Label();
    title->setText("Addons");
    title->setFontSize(26);
    title->setMarginBottom(8);
    this->addView(title);

    auto* hint = new brls::Label();
    hint->setText("Pressione X para adicionar um addon pela URL do manifesto.");
    hint->setFontSize(15);
    hint->setTextColor(nvgRGB(150, 150, 150));
    hint->setMarginBottom(20);
    this->addView(hint);

    // The list (and the X-to-add hint) only appear once "loaded" -- see
    // buildContent(). Reading the config file is quick, but this keeps
    // every screen to the same rule: nothing interactive shows before its
    // data actually exists.
    this->loadingGate = new LoadingGate([this]() { this->buildContent(); }, 350);
    this->addView(this->loadingGate);
}

void AddonsScreen::buildContent()
{
    this->removeView(this->loadingGate);
    this->loadingGate = nullptr;

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setDimensions(brls::View::AUTO, brls::View::AUTO);

    this->listBox = new brls::Box();
    this->listBox->setAxis(brls::Axis::COLUMN);
    this->listBox->setDimensions(brls::View::AUTO, brls::View::AUTO);
    content->addView(this->listBox);

    auto* catalogsHeader = new brls::Label();
    catalogsHeader->setText("Catalogos");
    catalogsHeader->setFontSize(20);
    catalogsHeader->setMarginTop(28);
    catalogsHeader->setMarginBottom(4);
    content->addView(catalogsHeader);

    auto* catalogsHint = new brls::Label();
    catalogsHint->setText("Slices extras do Cinemeta (genero/ano/nota) que aparecem em Buscar. O catalogo principal do Cinemeta e sempre ativo e nao aparece aqui.");
    catalogsHint->setFontSize(13);
    catalogsHint->setTextColor(nvgRGB(150, 150, 150));
    catalogsHint->setIsWrapping(true);
    catalogsHint->setMarginBottom(14);
    content->addView(catalogsHint);

    this->catalogListBox = new brls::Box();
    this->catalogListBox->setAxis(brls::Axis::COLUMN);
    this->catalogListBox->setDimensions(brls::View::AUTO, brls::View::AUTO);
    content->addView(this->catalogListBox);

    scroll->setContentView(content);
    this->addView(scroll);

    this->registerAction("Adicionar Addon", brls::BUTTON_X, [this](brls::View*) {
        this->openAddDialog();
        return true;
    });

    AddonStore::instance().load();
    CatalogStore::instance().load();
    this->rebuildList();
    this->rebuildCatalogList();
}

void AddonsScreen::openAddDialog()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            std::string error = AddonStore::instance().add(text);
            if (error.empty())
                brls::Application::notify("Addon adicionado");
            else
                brls::Application::notify(error);
            this->rebuildList();
        },
        "Adicionar Addon", "Cole a URL do manifesto (ex: https://addon.exemplo/manifest.json)", 256, "");
}

void AddonsScreen::rebuildList()
{
    // A row's own A/RB action is what triggers this rebuild (toggling or
    // removing itself), which destroys every row including the one whose
    // callback is still running -- park focus on the screen itself first so
    // nothing is left pointing at a view that's about to be freed, then
    // hand it back to the list once it's rebuilt.
    bool focusWasInList = false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v != nullptr; v = v->getParent())
        if (v == this->listBox)
        {
            focusWasInList = true;
            break;
        }
    if (focusWasInList)
        brls::Application::giveFocus(this);

    this->listBox->clearViews();

    auto& addons = AddonStore::instance().all();

    if (addons.empty())
    {
        auto* empty = new brls::Label();
        empty->setText("Nenhum addon configurado ainda -- pressione X para adicionar um.");
        empty->setTextColor(nvgRGB(150, 150, 150));
        this->listBox->addView(empty);
    }
    else
    {
        for (size_t i = 0; i < addons.size(); i++)
        {
            this->listBox->addView(new AddonRow(
                addons[i].url, addons[i].enabled,
                [this, i]() {
                    AddonStore::instance().toggle(i);
                    brls::Application::notify(
                        AddonStore::instance().all()[i].enabled ? "Addon ativado" : "Addon desativado");
                    this->rebuildList();
                },
                [this, i]() {
                    AddonStore::instance().remove(i);
                    brls::Application::notify("Addon removido");
                    this->rebuildList();
                }));
        }
    }

    if (focusWasInList)
        brls::sync([this]() { brls::Application::giveFocus(this->listBox); });
}

void AddonsScreen::rebuildCatalogList()
{
    // Same reasoning as rebuildList() above: a row's own toggle triggers
    // this rebuild, so park focus off the list before tearing it down.
    bool focusWasInList = false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v != nullptr; v = v->getParent())
        if (v == this->catalogListBox)
        {
            focusWasInList = true;
            break;
        }
    if (focusWasInList)
        brls::Application::giveFocus(this);

    this->catalogListBox->clearViews();

    auto& catalogs = CatalogStore::instance().all();
    for (size_t i = 0; i < catalogs.size(); i++)
    {
        this->catalogListBox->addView(new CatalogRow(catalogs[i].title, catalogs[i].enabled, [this, i]() {
            CatalogStore::instance().toggle(i);
            brls::Application::notify(
                CatalogStore::instance().all()[i].enabled ? "Catalogo ativado" : "Catalogo desativado");
            this->rebuildCatalogList();
        }));
    }

    if (focusWasInList)
        brls::sync([this]() { brls::Application::giveFocus(this->catalogListBox); });
}
