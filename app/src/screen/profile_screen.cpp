#include "screen/profile_screen.hpp"
#include "api/cloud_sync.hpp"
#include "api/updater.hpp"
#include "utils/pin_input.hpp"
#include "view/ui_kit.hpp"

#include <cctype>

namespace
{

std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Same rule the backend enforces -- checked here first so a typo on the PC
// keyboard doesn't cost a round trip (or one of the 5 attempts).
bool validPin(const std::string& pin)
{
    if (pin.size() < 4 || pin.size() > 8)
        return false;
    for (unsigned char c : pin)
        if (!std::isdigit(c))
            return false;
    return true;
}

constexpr const char* PIN_RULE = "O PIN precisa ter de 4 a 8 numeros.";

bool isInside(brls::View* view, brls::View* ancestor)
{
    for (brls::View* v = view; v != nullptr; v = v->getParent())
        if (v == ancestor)
            return true;
    return false;
}

brls::Box* buttonRow()
{
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setDimensions(brls::View::AUTO, brls::View::AUTO);
    row->setMarginTop(24);
    return row;
}

void addButton(brls::Box* row, brls::View* button)
{
    if (!row->getChildren().empty())
        button->setMarginLeft(12);
    row->addView(button);
}

} // namespace

ProfileScreen::ProfileScreen()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(30, 50, 30, 50);
    // Somewhere to park focus while rebuild() replaces the buttons -- the
    // one that was pressed is among the views being destroyed.
    this->setFocusable(true);

    auto* title = ui::label("Perfil", 26, ui::color("text"));
    title->setMarginBottom(20);
    this->addView(title);

    this->body = new brls::Box();
    this->body->setAxis(brls::Axis::COLUMN);
    this->body->setDimensions(brls::View::AUTO, brls::View::AUTO);
    this->addView(this->body);

    // Outside `body`, so rebuild() (login/logout) leaves it alone.
    auto* version = ui::label("StreamNX versao " + updater::currentVersion(), 13, ui::color("text_muted"));
    version->setMarginTop(16);
    this->addView(version);

    this->rebuild();
    this->refreshStatusLoop();
}

ProfileScreen::~ProfileScreen()
{
    *this->alive = false;
}

void ProfileScreen::rebuild()
{
    brls::View* focus = brls::Application::getCurrentFocus();
    bool hadFocus     = isInside(focus, this);
    if (hadFocus)
        brls::Application::giveFocus(this);

    this->body->clearViews();
    this->statusLabel = nullptr;

    auto* card = new brls::Box();
    card->setAxis(brls::Axis::COLUMN);
    card->setDimensions(brls::View::AUTO, brls::View::AUTO);
    card->setMaxWidth(760);
    card->setPadding(28, 32, 28, 32);
    card->setCornerRadius(12);
    card->setBackgroundColor(ui::color("surface"));
    this->body->addView(card);

    if (this->busy)
    {
        // Filled in by setBusy(); no buttons while a request is running.
    }
    else if (cloud::isLoggedIn())
        this->buildLoggedIn(card);
    else
        this->buildLoggedOut(card);

    if (hadFocus && !this->busy)
        brls::sync([this, alive = this->alive]() {
            if (!*alive)
                return;
            if (brls::View* first = this->body->getDefaultFocus())
                brls::Application::giveFocus(first);
        });
}

void ProfileScreen::buildLoggedOut(brls::Box* card)
{
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->addView(ui::icon(ui::icons::person, 40, ui::color("text_muted")));
    auto* title = ui::label("Nenhum perfil conectado", 21, ui::color("text"));
    title->setMarginLeft(14);
    header->addView(title);
    card->addView(header);

    auto* hint = ui::label(
        "Entre num perfil para levar seus Favoritos, o Continuar Assistindo e os Addons entre o Switch e o PC. "
        "O que ja estiver salvo neste aparelho entra no perfil, nada se perde.",
        15, ui::color("text_muted"));
    hint->setIsWrapping(true);
    hint->setMarginTop(14);
    card->addView(hint);

    auto* row = buttonRow();
    addButton(row, ui::button("Entrar", ui::ButtonStyle::Primary, [this]() { this->startLogin(); }, ui::icons::person));
    addButton(row, ui::button("Criar perfil", ui::ButtonStyle::Secondary, [this]() { this->startCreate(); }, ui::icons::add));
    card->addView(row);
}

void ProfileScreen::buildLoggedIn(brls::Box* card)
{
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->addView(ui::icon(ui::icons::person, 40, ui::color("accent")));

    auto* names = new brls::Box();
    names->setAxis(brls::Axis::COLUMN);
    names->setMarginLeft(14);
    names->addView(ui::label(cloud::profileName(), 22, ui::color("text")));
    auto* sub = ui::label("Perfil conectado neste aparelho", 14, ui::color("text_muted"));
    sub->setMarginTop(2);
    names->addView(sub);
    header->addView(names);
    card->addView(header);

    this->statusLabel = ui::label(cloud::statusText(), 15, ui::color("text_muted"));
    this->statusLabel->setIsWrapping(true);
    this->statusLabel->setMarginTop(18);
    card->addView(this->statusLabel);

    auto* row = buttonRow();
    addButton(row, ui::button("Sincronizar agora", ui::ButtonStyle::Primary, [this]() {
        this->setBusy("Sincronizando...");
        cloud::syncNow(this->alive, [this](const std::string& error) { this->finish(error, "Perfil sincronizado"); });
    }, ui::icons::sync));
    addButton(row, ui::button("Sair do perfil", ui::ButtonStyle::Danger, [this]() { this->confirmLogout(); },
        ui::icons::logout));
    card->addView(row);

    auto* server = ui::label("Servidor: " + cloud::serverUrl(), 12, ui::color("text_muted"));
    server->setMarginTop(20);
    card->addView(server);
}

void ProfileScreen::refreshStatusLoop()
{
    // "Sincronizando..." -> "Sincronizado" happens in the background (e.g.
    // a favorite sent from another screen), so keep the line current.
    if (this->statusLabel && !this->busy)
        this->statusLabel->setText(cloud::statusText());

    brls::delay(1000, [this, alive = this->alive]() {
        if (*alive)
            this->refreshStatusLoop();
    });
}

void ProfileScreen::setBusy(const std::string& message)
{
    this->busy = true;
    this->rebuild();

    auto* card  = static_cast<brls::Box*>(this->body->getChildren().front());
    auto* label = ui::label(message, 18, ui::color("text"));
    card->addView(label);
}

void ProfileScreen::finish(const std::string& error, const std::string& successMessage)
{
    this->busy = false;
    brls::Application::notify(error.empty() ? successMessage : error);
    // Busy mode parked focus on the screen itself; rebuild() hands it back
    // to the first button.
    this->rebuild();
}

// Each keyboard is opened a frame after the previous one closed: on the
// Switch the system keyboard is still up while its callback runs, and a
// second one can't be shown on top of it.
void ProfileScreen::startLogin()
{
    auto alive = this->alive;
    brls::Application::getImeManager()->openForText(
        [this, alive](std::string text) {
            std::string name = trim(text);
            if (name.empty())
                return;
            brls::sync([this, alive, name]() {
                if (!*alive)
                    return;
                pin_input::open("PIN de " + name, "Digite o PIN do perfil (4 a 8 numeros)", [this, alive, name](std::string pin) {
                    brls::sync([this, alive, name, pin]() {
                        if (!*alive)
                            return;
                        if (!validPin(pin))
                        {
                            brls::Application::notify(PIN_RULE);
                            return;
                        }
                        this->setBusy("Entrando...");
                        cloud::login(name, pin, alive, [this](const std::string& error) {
                            this->finish(error, "Conectado como " + cloud::profileName());
                        });
                    });
                });
            });
        },
        "Entrar no perfil", "Digite o nome do perfil", 32, "");
}

void ProfileScreen::startCreate()
{
    auto alive = this->alive;
    brls::Application::getImeManager()->openForText(
        [this, alive](std::string text) {
            std::string name = trim(text);
            if (name.empty())
                return;
            brls::sync([this, alive, name]() {
                if (!*alive)
                    return;
                pin_input::open("Crie um PIN", "De 4 a 8 numeros. Ele sera pedido ao entrar em outro aparelho.",
                    [this, alive, name](std::string pin) {
                        brls::sync([this, alive, name, pin]() {
                            if (!*alive)
                                return;
                            if (!validPin(pin))
                            {
                                brls::Application::notify(PIN_RULE);
                                return;
                            }
                            pin_input::open("Confirme o PIN", "Digite o mesmo PIN de novo", [this, alive, name, pin](std::string again) {
                                brls::sync([this, alive, name, pin, again]() {
                                    if (!*alive)
                                        return;
                                    if (again != pin)
                                    {
                                        brls::Application::notify("Os PINs nao conferem. Tente de novo.");
                                        return;
                                    }
                                    this->setBusy("Criando perfil...");
                                    cloud::createProfile(name, pin, alive, [this](const std::string& error) {
                                        this->finish(error, "Perfil " + cloud::profileName() + " criado");
                                    });
                                });
                            });
                        });
                    });
            });
        },
        "Criar perfil", "Escolha um nome para o perfil", 32, "");
}

void ProfileScreen::confirmLogout()
{
    auto* dialog = new brls::Dialog("Sair do perfil \"" + cloud::profileName()
        + "\"? Os Favoritos, o Continuar Assistindo e os Addons dele saem deste aparelho, mas continuam salvos no perfil.");
    dialog->addButton("Cancelar", []() {});
    dialog->addButton("Sair", [this, alive = this->alive]() {
        cloud::logout();
        brls::Application::notify("Voce saiu do perfil");
        if (*alive)
            this->rebuild();
    });
    dialog->open();
}
