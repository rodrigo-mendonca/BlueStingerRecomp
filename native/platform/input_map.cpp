// Mapa de acoes e arquivo de configuracao dos controles (N3-1).
#include "platform/input.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace bs::native::input {
namespace {

struct ActionDef {
    const char* name;
    Target      target;
};

Target button(uint32_t b) { return {Target::Kind::Button, b, Axis::StickX, 1}; }
Target direction(Axis a, float s) { return {Target::Kind::AxisDirection, 0, a, s}; }
Target full(Axis a) { return {Target::Kind::AxisFull, 0, a, 1}; }

// Acoes que teclado e controle podem acionar
const ActionDef kActions[] = {
    {"A", button(A)},          {"B", button(B)},           {"X", button(X)},
    {"Y", button(Y)},          {"START", button(Start)},   {"UP", button(DpadUp)},
    {"DOWN", button(DpadDown)}, {"LEFT", button(DpadLeft)}, {"RIGHT", button(DpadRight)},
    {"C", button(C)},          {"Z", button(Z)},           {"D", button(D)},
    {"LT", direction(Axis::TriggerL, 1)},       {"RT", direction(Axis::TriggerR, 1)},
    {"ANALOG_UP", direction(Axis::StickY, -1)}, {"ANALOG_DOWN", direction(Axis::StickY, 1)},
    {"ANALOG_LEFT", direction(Axis::StickX, -1)}, {"ANALOG_RIGHT", direction(Axis::StickX, 1)},
};
// So do controle: eixos inteiros (um eixo fisico por eixo logico)
const ActionDef kAxisActions[] = {
    {"ANALOG_X", full(Axis::StickX)},   {"ANALOG_Y", full(Axis::StickY)},
    {"ANALOG2_X", full(Axis::Stick2X)}, {"ANALOG2_Y", full(Axis::Stick2Y)},
};

const char* const kGamepadButtons[] = {"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
                                       "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright",
                                       "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
                                       "misc2", "misc3", "misc4", "misc5", "misc6"};  // os do SDL3 fixado
const char* const kGamepadAxes[] = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& ch : out) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& ch : out) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool validKey(const std::string& k) {
    if (k.size() == 1 && std::isalnum(static_cast<unsigned char>(k[0]))) return true;
    static const std::set<std::string> named = {"ENTER", "SPACE", "ESC", "TAB", "BACKSPACE", "SHIFT", "CTRL",
                                                "UP", "DOWN", "LEFT", "RIGHT"};
    if (named.count(k)) return true;
    if (k.size() >= 2 && k[0] == 'F') {
        const int f = std::atoi(k.c_str() + 1);
        return f >= 1 && f <= 12 && std::to_string(f) == k.substr(1);
    }
    return false;
}

bool isGamepadButton(std::string_view c) {
    return std::find(std::begin(kGamepadButtons), std::end(kGamepadButtons), c) != std::end(kGamepadButtons);
}
bool isGamepadAxis(std::string_view c) {
    if (!c.empty() && c[0] == '-') c.remove_prefix(1);
    return std::find(std::begin(kGamepadAxes), std::end(kGamepadAxes), c) != std::end(kGamepadAxes);
}

const ActionDef* findAction(const std::string& n, bool gamepad) {
    for (const auto& a : kActions)
        if (n == a.name) return &a;
    if (gamepad)
        for (const auto& a : kAxisActions)
            if (n == a.name) return &a;
    return nullptr;
}

const char* kDefaultKeyboard[][2] = {{"UP", "UP"},       {"DOWN", "DOWN"},  {"LEFT", "LEFT"},  {"RIGHT", "RIGHT"},
                                     {"START", "ENTER"}, {"A", "Z"},        {"B", "X"},        {"X", "A"},
                                     {"Y", "S"},         {"LT", "Q"},       {"RT", "W"},       {"ANALOG_UP", "I"},
                                     {"ANALOG_DOWN", "K"}, {"ANALOG_LEFT", "J"}, {"ANALOG_RIGHT", "L"}};
const char* kDefaultGamepad[][2] = {{"A", "a"},        {"B", "b"},           {"X", "x"},          {"Y", "y"},
                                    {"START", "start"}, {"UP", "dpup"},       {"DOWN", "dpdown"},  {"LEFT", "dpleft"},
                                    {"RIGHT", "dpright"}, {"LT", "lefttrigger"}, {"RT", "righttrigger"},
                                    {"ANALOG_X", "leftx"}, {"ANALOG_Y", "lefty"}};

std::vector<Binding> bindingsFrom(const char* (*table)[2], size_t n, bool gamepad) {
    std::vector<Binding> v;
    for (size_t i = 0; i < n; ++i) v.push_back({table[i][1], findAction(table[i][0], gamepad)->target});
    return v;
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

// Uma secao [teclado] ou [controle]: cada acao presente substitui as ligacoes padrao dela.
bool parseSection(const toml::table& t, bool gamepad, std::vector<Binding>& out, std::string* error,
                  const std::string& section, const std::set<std::string>& skip = {}) {
    for (const auto& [key, node] : t) {
        if (skip.count(std::string(key.str()))) continue;
        const std::string action = upper(key.str());
        const ActionDef*  def    = findAction(action, gamepad);
        const auto        where  = "[" + section + "] " + std::string(key.str()) + " (linha " +
                           std::to_string(node.source().begin.line) + ")";
        if (!def) return fail(error, where + ": acao desconhecida");
        std::vector<std::string> controls;
        if (auto s = node.value<std::string>()) controls.push_back(*s);
        else if (auto* a = node.as_array()) {
            for (const auto& e : *a) {
                auto item = e.value<std::string>();
                if (!item) return fail(error, where + ": a lista so pode ter textos");
                controls.push_back(*item);
            }
        } else return fail(error, where + ": use um texto (\"Z\") ou uma lista ([\"Z\", \"ENTER\"])");
        std::erase_if(out, [&](const Binding& b) { return b.target == def->target; });
        for (std::string c : controls) {
            if (!gamepad) {
                c = upper(c);
                if (!validKey(c)) return fail(error, where + ": tecla desconhecida \"" + c + "\"");
            } else {
                const bool fullAxis = def->target.kind == Target::Kind::AxisFull;
                const bool trig     = def->target.kind == Target::Kind::AxisDirection &&
                                  (def->target.axis == Axis::TriggerL || def->target.axis == Axis::TriggerR);
                if (fullAxis && !isGamepadAxis(c))
                    return fail(error, where + ": \"" + c + "\" nao e um eixo do controle (leftx, lefty, rightx, righty; \"-\" inverte)");
                if (!fullAxis && !isGamepadButton(c) && !(trig && isGamepadAxis(c) && c[0] != '-'))
                    return fail(error, where + ": \"" + c + "\" nao e um botao do controle");
            }
            Target tg = def->target;
            if (tg.kind == Target::Kind::AxisFull && !c.empty() && c[0] == '-') tg.sign = -1, c.erase(0, 1);
            out.push_back({c, tg});
        }
    }
    return true;
}

} // namespace

const char* button_name(Button b) {
    for (const auto& a : kActions)
        if (a.target.kind == Target::Kind::Button && a.target.button == b) return a.name;
    return "?";
}

ActionMap ActionMap::defaults() {
    ActionMap m;
    m.keyboard = bindingsFrom(kDefaultKeyboard, std::size(kDefaultKeyboard), false);
    m.gamepad  = bindingsFrom(kDefaultGamepad, std::size(kDefaultGamepad), true);
    return m;
}

Result<ActionMap> ActionMap::parse(std::string_view text, std::string* error) {
    ActionMap   m = defaults();
    toml::table doc;
    try {
        doc = toml::parse(text);
    } catch (const toml::parse_error& e) {
        fail(error, "linha " + std::to_string(e.source().begin.line) + ": " + std::string(e.description()));
        return Error::Corrupt;
    }
    for (const auto& [key, node] : doc) {
        const std::string k(key.str());
        if (k == "versao") {
            if (node.value<int64_t>() != 1) {
                fail(error, "versao: so a versao 1 e conhecida");
                return Error::Unsupported;
            }
        } else if (k == "opcoes") {
            const toml::table* t = node.as_table();
            if (!t) return fail(error, "[opcoes] deve ser uma secao"), Error::Corrupt;
            for (const auto& [ok, on] : *t) {
                const std::string o(ok.str());
                const auto        where = "[opcoes] " + o + " (linha " + std::to_string(on.source().begin.line) + ")";
                if (o == "backend") {
                    auto s = on.value<std::string>();
                    if (!s || (*s != "sdl" && *s != "xinput")) return fail(error, where + ": use \"sdl\" ou \"xinput\""), Error::Corrupt;
                    m.options.backend = *s;
                } else if (o == "zona_morta") {
                    auto v = on.value<double>();
                    if (!v || *v < 0 || *v > 0.9) return fail(error, where + ": numero de 0 a 0.9"), Error::Corrupt;
                    m.options.deadzone = float(*v);
                } else if (o == "troca_automatica") {
                    auto v = on.value<bool>();
                    if (!v) return fail(error, where + ": true ou false"), Error::Corrupt;
                    m.options.auto_switch = *v;
                } else if (o == "controle_preferido") {
                    auto v = on.value<std::string>();
                    if (!v) return fail(error, where + ": texto"), Error::Corrupt;
                    m.options.preferred = *v;
                } else return fail(error, where + ": opcao desconhecida"), Error::Corrupt;
            }
        } else if (k == "teclado" || k == "controle") {
            const toml::table* t = node.as_table();
            if (!t) return fail(error, "[" + k + "] deve ser uma secao"), Error::Corrupt;
            if (!parseSection(*t, k == "controle", k == "controle" ? m.gamepad : m.keyboard, error, k)) return Error::Corrupt;
        } else if (k == "perfis") {
            if (!node.as_table()) return fail(error, "[perfis] deve ser uma secao"), Error::Corrupt;
        } else {
            fail(error, "chave desconhecida: " + k + " (secoes: [opcoes], [teclado], [controle], [perfis.\"<guid>\"])");
            return Error::Corrupt;
        }
    }
    // perfis por GUID (N3-3): depois de [controle], do qual herdam o que nao listam
    if (const toml::table* perfis = doc["perfis"].as_table()) {
        for (const auto& [gk, gn] : *perfis) {
            Profile     p;
            p.guid            = lower(gk.str());
            const auto where  = "[perfis.\"" + std::string(gk.str()) + "\"] (linha " + std::to_string(gn.source().begin.line) + ")";
            const bool hex32  = p.guid.size() == 32 && std::all_of(p.guid.begin(), p.guid.end(), [](char ch) {
                return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
            });
            if (!hex32 && p.guid != "xinput")
                return fail(error, where + ": o GUID tem 32 digitos hexadecimais (o log mostra o do controle) ou e \"xinput\""),
                       Error::Corrupt;
            if (m.profile_for(p.guid)) return fail(error, where + ": perfil repetido"), Error::Corrupt;
            const toml::table* t = gn.as_table();
            if (!t) return fail(error, where + ": deve ser uma secao"), Error::Corrupt;
            if (const toml::node* n = t->get("nome")) {
                auto v = n->value<std::string>();
                if (!v) return fail(error, where + " nome: texto"), Error::Corrupt;
                p.name = *v;
            }
            if (const toml::node* n = t->get("zona_morta")) {
                auto v = n->value<double>();
                if (!v || *v < 0 || *v > 0.9) return fail(error, where + " zona_morta: numero de 0 a 0.9"), Error::Corrupt;
                p.deadzone = float(*v);
            }
            p.gamepad = m.gamepad;
            if (!parseSection(*t, true, p.gamepad, error, "perfis.\"" + std::string(gk.str()) + "\"", {"nome", "zona_morta"}))
                return Error::Corrupt;
            if (p.name.empty()) p.name = p.guid;
            m.profiles.push_back(std::move(p));
        }
    }
    return m;
}

const Profile* ActionMap::profile_for(std::string_view guid) const {
    if (guid.empty()) return nullptr;
    const std::string g = lower(guid);
    for (const Profile& p : profiles)
        if (p.guid == g) return &p;
    return nullptr;
}

Result<void> ActionMap::set_keyboard_legacy(std::string_view text, std::string* error) {
    std::vector<Binding> out;
    std::istringstream   in{std::string(text)};
    std::string          line;
    int                  n = 0;
    while (std::getline(in, line)) {
        ++n;
        line = line.substr(0, line.find('#'));
        const auto a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) return fail(error, "mapa de teclado, linha " + std::to_string(n) + ": falta '='"), Error::Corrupt;
        auto trim = [](std::string s) {
            const auto b = s.find_first_not_of(" \t\r"), e = s.find_last_not_of(" \t\r");
            return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
        };
        const std::string action = upper(trim(line.substr(0, eq))), keyName = upper(trim(line.substr(eq + 1)));
        const ActionDef*  def    = findAction(action, false);
        if (!def) return fail(error, "mapa de teclado, linha " + std::to_string(n) + ": acao " + action), Error::Corrupt;
        if (!validKey(keyName)) return fail(error, "mapa de teclado, linha " + std::to_string(n) + ": tecla " + keyName), Error::Corrupt;
        out.push_back({keyName, def->target});
    }
    keyboard = std::move(out);
    return {};
}

std::string ActionMap::default_text() {
    std::string t =
        "# Blue Stinger - controles (criado na primeira execucao; apague para voltar ao padrao).\n"
        "# Acoes: A B X Y START UP DOWN LEFT RIGHT (direcional) C Z D, LT RT (gatilhos),\n"
        "#        ANALOG_UP ANALOG_DOWN ANALOG_LEFT ANALOG_RIGHT (analogico pelo teclado),\n"
        "#        ANALOG_X ANALOG_Y ANALOG2_X ANALOG2_Y (eixos do controle; \"-lefty\" inverte).\n"
        "# Cada acao aceita um texto ou uma lista: A = [\"Z\", \"SPACE\"]. Uma acao listada aqui substitui o\n"
        "# padrao dela; as que nao aparecem continuam com o padrao.\n"
        "versao = 1\n\n"
        "[opcoes]\n"
        "backend = \"sdl\"          # \"sdl\" (controles pelo SDL3) ou \"xinput\" (reserva)\n"
        "zona_morta = 0.0         # 0 a 0.9; 0 = como no jogo original (o jogo ja tem a dele)\n"
        "troca_automatica = true  # o ultimo controle usado vira o jogador 1\n"
        "controle_preferido = \"\"  # parte do nome do controle; vazio = o ultimo usado\n\n"
        "[teclado]  # letras, digitos, ENTER SPACE ESC TAB BACKSPACE SHIFT CTRL UP DOWN LEFT RIGHT F1..F12\n";
    for (const auto& kv : kDefaultKeyboard) t += std::string(kv[0]) + " = \"" + kv[1] + "\"\n";
    t += "\n[controle]  # a b x y start back dpup dpdown dpleft dpright leftshoulder rightshoulder\n"
         "           # leftstick rightstick; eixos leftx lefty rightx righty lefttrigger righttrigger\n";
    for (const auto& kv : kDefaultGamepad) t += std::string(kv[0]) + " = \"" + kv[1] + "\"\n";
    t += "\n# Perfis por modelo de controle: o log (bluestinger.log) mostra o GUID de cada controle conectado.\n"
         "# Um perfil herda o [controle] e substitui so as acoes que lista; controle sem perfil usa o [controle].\n"
         "# [perfis.\"030000005e0400008e02000000007200\"]\n"
         "# nome = \"Xbox 360\"\n"
         "# zona_morta = 0.1     # opcional; senao, a de [opcoes]\n"
         "# A = \"b\"\n"
         "# B = \"a\"\n"
         "# Controles pelo XInput de reserva: [perfis.xinput]\n";
    return t;
}

float axis_from_s16(int v) { return std::clamp(float(v) / 32767.0f, -1.0f, 1.0f); }
float trigger_from_u8(int v) { return std::clamp(float(v) / 255.0f, 0.0f, 1.0f); }
float trigger_from_s16(int v) { return std::clamp(float(v) / 32767.0f, 0.0f, 1.0f); }

void apply_radial_deadzone(float& x, float& y, float deadzone) {
    if (deadzone <= 0) return;
    const float m = std::sqrt(x * x + y * y);
    if (m <= deadzone) {
        x = y = 0;
        return;
    }
    // reescala para comecar em 0 na borda da zona morta e chegar a 1 no fim do curso
    const float s = std::min(1.0f, (m - deadzone) / (1.0f - deadzone)) / m;
    x *= s;
    y *= s;
}

} // namespace bs::native::input
