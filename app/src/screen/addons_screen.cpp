#include "screen/addons_screen.hpp"
#include "model/addon_store.hpp"
#include "model/catalog_addon_store.hpp"
#include "model/subtitle_addon_store.hpp"
#include "view/loading_gate.hpp"
#include "api/stremio_api.hpp"
#include "utils/mouse.hpp"
#include "view/ui_kit.hpp"
#include "theme/streamnx_theme.hpp"

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
class AddonRow : public HoverBox
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
        this->setHoverColors(ui::color("surface"), ui::color("surface_raised"));

        // Row axis so a PC-only "Remover" button (see below) can sit as a
        // sibling of -- not nested inside -- the part that toggles
        // enabled/disabled on click: borealis' tap gesture bubbles up to
        // every ancestor that has its own recognizer with no way to
        // "consume" it, so a clickable child inside a clickable parent
        // would fire both actions on the same click.
        auto* outer = new brls::Box();
        outer->setAxis(brls::Axis::ROW);
        outer->setDimensions(brls::View::AUTO, brls::View::AUTO);
        this->addView(outer);

        auto* toggleArea = new brls::Box();
        toggleArea->setAxis(brls::Axis::COLUMN);
        toggleArea->setFocusable(true);
        toggleArea->setGrow(1.0f);
        outer->addView(toggleArea);

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

        toggleArea->addView(topRow);

        auto* urlLabel = new brls::Label();
        urlLabel->setText(url);
        urlLabel->setFontSize(13);
        urlLabel->setTextColor(nvgRGB(140, 140, 150));
        urlLabel->setMarginTop(4);
        toggleArea->addView(urlLabel);

        // Whether the URL is enabled/disabled says nothing about whether it
        // actually answers -- probe it the same way a real client would
        // before trying to use it, via the one request every addon must
        // support (its manifest).
        auto* netStatus = new brls::Label();
        netStatus->setText("Verificando conexao...");
        netStatus->setFontSize(13);
        netStatus->setTextColor(nvgRGB(180, 160, 90));
        netStatus->setMarginTop(6);
        toggleArea->addView(netStatus);

        stremio::fetchManifest(
            url, this->rowAlive,
            [netStatus](stremio::ManifestInfo info) {
                // Answers, but won't work as added: this URL is the
                // unconfigured one (see ManifestInfo::configurationRequired).
                if (info.configurationRequired)
                {
                    netStatus->setText("Precisa configurar -- abra a pagina de configuracao do addon e cole a URL "
                                       "que ela gerar");
                    netStatus->setTextColor(nvgRGB(220, 170, 80));
                    return;
                }
                netStatus->setText("Online");
                netStatus->setTextColor(nvgRGB(110, 220, 130));
            },
            [netStatus](std::string) {
                netStatus->setText("Indisponivel");
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
        makeClickable(toggleArea, [onToggle](brls::View*) {
            onToggle();
            return true;
        });

#ifdef STREAMNX_PC_UI
        // RB has no click equivalent -- a real, visible button instead.
        auto* removeButton = ui::button("Remover", ui::ButtonStyle::Danger, onRemove, ui::icons::trash);
        removeButton->setMarginLeft(12);

        outer->addView(removeButton);
#endif
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

#ifndef STREAMNX_PC_UI
    auto* hint = new brls::Label();
    hint->setText("Pressione X para adicionar um addon pela URL do manifesto. ZL/ZR trocam de aba.");
    hint->setFontSize(15);
    hint->setTextColor(nvgRGB(150, 150, 150));
    hint->setMarginBottom(20);
    this->addView(hint);
#endif

    // The list (and the X-to-add hint) only appear once "loaded" -- see
    // buildContent(). Reading the config file is quick, but this keeps
    // every screen to the same rule: nothing interactive shows before its
    // data actually exists.
    this->loadingGate = new LoadingGate([this]() { this->buildContent(); }, 350);
    this->addView(this->loadingGate);
}

namespace
{

// One tab's body: an optional (PC) "add" button above a scrolling column.
// Returns the column to fill.
brls::Box* makeTabBody(brls::Box* tab, const std::string& addLabel, std::function<void()> onAdd)
{
    tab->setAxis(brls::Axis::COLUMN);
    tab->setGrow(1.0f);

#ifdef STREAMNX_PC_UI
    auto* addButton = ui::button(addLabel, ui::ButtonStyle::Primary, onAdd, ui::icons::add);
    addButton->setAlignSelf(brls::AlignSelf::FLEX_START);
    addButton->setMarginBottom(20);
    tab->addView(addButton);
#else
    (void)addLabel;
    (void)onAdd;
#endif

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setDimensions(brls::View::AUTO, brls::View::AUTO);

    scroll->setContentView(content);
    tab->addView(scroll);
    return content;
}

brls::Box* makeListBox()
{
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setDimensions(brls::View::AUTO, brls::View::AUTO);
    return box;
}

brls::Label* makeSectionHint(const std::string& text)
{
    auto* hint = new brls::Label();
    hint->setText(text);
    hint->setFontSize(13);
    hint->setTextColor(nvgRGB(150, 150, 150));
    hint->setIsWrapping(true);
    hint->setMarginBottom(14);
    return hint;
}

} // namespace

void AddonsScreen::buildContent()
{
    this->removeView(this->loadingGate);
    this->loadingGate = nullptr;

    // Tab bar -- the active tab gets the accent color (see showTab()).
    auto* tabBar = new brls::Box();
    tabBar->setAxis(brls::Axis::ROW);
    tabBar->setMarginBottom(18);

    const struct
    {
        const char* label;
        const char* icon;
    } tabInfo[TAB_COUNT] = {
        { "Streams", ui::icons::play },
        { "Catalogos", ui::icons::extension },
        { "Legendas", ui::icons::movie },
    };
    for (int t = 0; t < TAB_COUNT; t++)
    {
        this->tabButtons[t] = ui::button(
            tabInfo[t].label, ui::ButtonStyle::Secondary, [this, t]() { this->showTab(t); }, tabInfo[t].icon);
        if (t + 1 < TAB_COUNT)
            this->tabButtons[t]->setMarginRight(10);
        tabBar->addView(this->tabButtons[t]);
    }
    this->addView(tabBar);

    auto onAdd = [this]() { this->openAddDialog(); };

    // --- Streams: the stream addons this screen always listed.
    this->tabs[TAB_STREAMS] = new brls::Box();
    auto* streamsContent    = makeTabBody(this->tabs[TAB_STREAMS], "Adicionar Addon", onAdd);

    this->listBox = makeListBox();
    streamsContent->addView(this->listBox);

    // --- Catalogos: catalog addons, on top of the always-on Cinemeta.
    this->tabs[TAB_CATALOGS] = new brls::Box();
    auto* catalogsContent    = makeTabBody(this->tabs[TAB_CATALOGS], "Adicionar Addon de Catalogo", onAdd);

    catalogsContent->addView(makeSectionHint(
        "Addons de catalogo (ex: Kitsu, TMDB, MDBList) somam seus catalogos de filmes e series aos do Cinemeta em "
        "Buscar, entram na busca por nome e fornecem os detalhes dos titulos que listam."));

    this->catalogAddonListBox = makeListBox();
    catalogsContent->addView(this->catalogAddonListBox);

    // --- Legendas: subtitles addons + the preferred language.
    this->tabs[TAB_SUBTITLES] = new brls::Box();
    auto* subtitlesContent    = makeTabBody(this->tabs[TAB_SUBTITLES], "Adicionar Addon de Legendas", onAdd);

    subtitlesContent->addView(makeSectionHint(
        "Addons de legenda (ex: OpenSubtitles) buscam legendas externas para o que voce assiste. No player, elas "
        "aparecem em Opcoes de reproducao > Legenda, e a melhor no idioma preferido ja comeca ativa."));

    // A cycles through the languages.
    auto* languageRow = new HoverBox();
    languageRow->setAxis(brls::Axis::ROW);
    languageRow->setFocusable(true);
    languageRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    languageRow->setDimensions(brls::View::AUTO, brls::View::AUTO);
    languageRow->setPadding(14, 20, 14, 20);
    languageRow->setMarginBottom(24);
    languageRow->setCornerRadius(8);
    languageRow->setHoverColors(ui::color("surface"), ui::color("surface_raised"));

    auto* languageTitle = new brls::Label();
    languageTitle->setText("Idioma preferido");
    languageTitle->setFontSize(17);
    languageRow->addView(languageTitle);

    this->languageValue = new brls::Label();
    this->languageValue->setFontSize(15);
    this->languageValue->setTextColor(ui::color("accent"));
    languageRow->addView(this->languageValue);

    languageRow->registerAction("Trocar idioma", brls::BUTTON_A, [this](brls::View*) {
        SubtitleAddonStore::instance().cycleLanguage();
        this->languageValue->setText(SubtitleAddonStore::instance().language().label);
        return true;
    });
    makeClickable(languageRow);
    subtitlesContent->addView(languageRow);

    this->subtitleListBox = makeListBox();
    subtitlesContent->addView(this->subtitleListBox);

    for (auto* tab : this->tabs)
        this->addView(tab);

    this->registerAction("Adicionar Addon", brls::BUTTON_X, [this](brls::View*) {
        this->openAddDialog();
        return true;
    });
    this->registerAction("Aba anterior", brls::BUTTON_LT, [this](brls::View*) {
        this->showTab((this->currentTab + TAB_COUNT - 1) % TAB_COUNT);
        return true;
    });
    this->registerAction("Proxima aba", brls::BUTTON_RT, [this](brls::View*) {
        this->showTab((this->currentTab + 1) % TAB_COUNT);
        return true;
    });

    AddonStore::instance().load();
    CatalogAddonStore::instance().load();
    SubtitleAddonStore::instance().load();
    this->languageValue->setText(SubtitleAddonStore::instance().language().label);
    this->rebuildList();
    this->rebuildCatalogAddonList();
    this->rebuildSubtitleList();
    this->showTab(TAB_STREAMS);
}

void AddonsScreen::showTab(int tab)
{
    // Focus left inside a tab that's about to be hidden would sit on an
    // invisible view -- park it on the screen, then hand it to the new tab.
    bool focusWasHidden = false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v != nullptr; v = v->getParent())
        for (int t = 0; t < TAB_COUNT; t++)
            if (t != tab && v == this->tabs[t])
                focusWasHidden = true;
    if (focusWasHidden)
        brls::Application::giveFocus(this);

    this->currentTab = tab;

    for (int t = 0; t < TAB_COUNT; t++)
    {
        bool active = t == tab;
        this->tabs[t]->setVisibility(active ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        this->tabButtons[t]->setHoverColors(active ? ui::color("accent") : ui::color("surface_raised"),
            active ? ui::color("accent_hover") : ui::color("surface_hover"));
    }

    if (focusWasHidden)
    {
        brls::Box* showing = this->tabs[tab];
        brls::sync([showing]() { brls::Application::giveFocus(showing); });
    }
}

void AddonsScreen::openAddDialog()
{
    if (this->currentTab == TAB_CATALOGS || this->currentTab == TAB_SUBTITLES)
    {
        bool catalogs = this->currentTab == TAB_CATALOGS;
        brls::Application::getImeManager()->openForText(
            [this, catalogs](std::string text) {
                AddonListStore& store = catalogs ? (AddonListStore&)CatalogAddonStore::instance()
                                                 : (AddonListStore&)SubtitleAddonStore::instance();
                std::string error = store.add(text);
                if (error.empty())
                    brls::Application::notify(catalogs ? "Addon de catalogo adicionado" : "Addon de legendas adicionado");
                else
                    brls::Application::notify(error);
                if (catalogs)
                    this->rebuildCatalogAddonList();
                else
                    this->rebuildSubtitleList();
            },
            catalogs ? "Adicionar Addon de Catalogo" : "Adicionar Addon de Legendas",
            catalogs ? "Cole a URL do manifesto (ex: anime-kitsu.strem.fun/manifest.json -- o https:// e opcional)"
                     : "Cole a URL do manifesto (ex: opensubtitles-v3.strem.io/manifest.json -- o https:// e opcional)",
            256, "");
        return;
    }

    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            std::string error = AddonStore::instance().add(text);
            if (error.empty())
                brls::Application::notify("Addon adicionado");
            else
                brls::Application::notify(error);
            this->rebuildList();
        },
        "Adicionar Addon", "Cole a URL do manifesto (ex: addon.exemplo/manifest.json -- o https:// e opcional)", 256, "");
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

void AddonsScreen::rebuildAddonList(AddonListStore& store, brls::Box* box, const std::string& kind)
{
    // Same reasoning as rebuildList(): a row's own toggle/remove triggers
    // this rebuild, so park focus off the list before tearing it down.
    bool focusWasInList = false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v != nullptr; v = v->getParent())
        if (v == box)
        {
            focusWasInList = true;
            break;
        }
    if (focusWasInList)
        brls::Application::giveFocus(this);

    box->clearViews();

    auto& addons = store.all();

    if (addons.empty())
    {
        auto* empty = new brls::Label();
        empty->setText("Nenhum addon de " + kind + " configurado ainda -- pressione X para adicionar um.");
        empty->setTextColor(nvgRGB(150, 150, 150));
        box->addView(empty);
    }
    else
    {
        for (size_t i = 0; i < addons.size(); i++)
        {
            box->addView(new AddonRow(
                addons[i].url, addons[i].enabled,
                [this, &store, box, kind, i]() {
                    store.toggle(i);
                    brls::Application::notify(
                        "Addon de " + kind + (store.all()[i].enabled ? " ativado" : " desativado"));
                    this->rebuildAddonList(store, box, kind);
                },
                [this, &store, box, kind, i]() {
                    store.remove(i);
                    brls::Application::notify("Addon de " + kind + " removido");
                    this->rebuildAddonList(store, box, kind);
                }));
        }
    }

    if (focusWasInList)
        brls::sync([box]() { brls::Application::giveFocus(box); });
}

void AddonsScreen::rebuildCatalogAddonList()
{
    this->rebuildAddonList(CatalogAddonStore::instance(), this->catalogAddonListBox, "catalogo");
}

void AddonsScreen::rebuildSubtitleList()
{
    this->rebuildAddonList(SubtitleAddonStore::instance(), this->subtitleListBox, "legendas");
}
