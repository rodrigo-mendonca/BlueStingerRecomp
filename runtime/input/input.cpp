// Entrada do jogador (ver input.h). Fontes do host so sao criadas a pedido
// (BS_INPUT=host); por padrao nenhuma fonte e usada, e o Maple responde "nada
// apertado": os testes diferenciais nao dependem do teclado de quem roda.
#include "input/input.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

namespace bs {
namespace {

std::unique_ptr<InputSource> g_source;
uint64_t (*g_stepCounter)() = nullptr;

std::string upper(std::string s) {
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// --- roteiro ---

class Scripted : public LiveScriptInput {
public:
    std::vector<std::pair<uint64_t, PadState>> steps;  // ordenado pelo indice
    bool                                       byStep = false;  // indice: passo de jogo (senao leitura)

    PadState read(int port, uint64_t seq) override {
        if (port != 0) return {};
        const uint64_t at = byStep ? input_step() : seq;
        PadState       s;
        for (const auto& [from, st] : steps) {
            if (from > at) break;
            s = st;
        }
        return s;
    }
    void push(uint64_t step, const PadState& s) override {
        if (!steps.empty() && step < steps.back().first) throw std::runtime_error("roteiro ao vivo: passo fora de ordem");
        if (!steps.empty() && step == steps.back().first) steps.back().second = s;
        else steps.push_back({step, s});
    }
};

// --- teclado ---

// Acoes alem dos botoes: gatilhos e analogico.
enum : uint32_t { kActLT = 1u << 16, kActRT = 1u << 17, kActAU = 1u << 18, kActAD = 1u << 19, kActAL = 1u << 20,
                  kActAR = 1u << 21 };

uint32_t actionFromName(const std::string& n) {
    if (uint16_t b = pad_button_from_name(n)) return b;
    static const std::map<std::string, uint32_t> extra = {{"LT", kActLT},          {"RT", kActRT},
                                                          {"ANALOG_UP", kActAU},   {"ANALOG_DOWN", kActAD},
                                                          {"ANALOG_LEFT", kActAL}, {"ANALOG_RIGHT", kActAR}};
    const auto it = extra.find(n);
    return it == extra.end() ? 0 : it->second;
}

int keyFromName(const std::string& n) {
    if (n.size() == 1 && (std::isalnum(static_cast<unsigned char>(n[0])))) return n[0];  // VK de A-Z, 0-9
    static const std::map<std::string, int> keys = {
        {"ENTER", VK_RETURN}, {"SPACE", VK_SPACE}, {"ESC", VK_ESCAPE}, {"TAB", VK_TAB},     {"SHIFT", VK_SHIFT},
        {"CTRL", VK_CONTROL}, {"UP", VK_UP},       {"DOWN", VK_DOWN},  {"LEFT", VK_LEFT},   {"RIGHT", VK_RIGHT},
        {"BACKSPACE", VK_BACK}};
    if (const auto it = keys.find(n); it != keys.end()) return it->second;
    if (n.size() >= 2 && n[0] == 'F') {
        const int f = std::atoi(n.c_str() + 1);
        if (f >= 1 && f <= 12) return VK_F1 + f - 1;
    }
    return 0;
}

// Padrao: setas = direcional, ENTER = START, Z/X/A/S = A/B/X/Y, Q/W = gatilhos L/R,
// I/J/K/L = analogico.
const char* kDefaultMap = "UP = UP\nDOWN = DOWN\nLEFT = LEFT\nRIGHT = RIGHT\nSTART = ENTER\n"
                          "A = Z\nB = X\nX = A\nY = S\nLT = Q\nRT = W\n"
                          "ANALOG_UP = I\nANALOG_DOWN = K\nANALOG_LEFT = J\nANALOG_RIGHT = L\n";

PadState stateFromActions(uint32_t act) {
    PadState s;
    s.buttons = uint16_t(act & 0xFFFF);
    if (act & kActLT) s.ltrig = 255;
    if (act & kActRT) s.rtrig = 255;
    if (act & kActAL) s.joyx = 0;
    if (act & kActAR) s.joyx = 255;
    if (act & kActAU) s.joyy = 0;
    if (act & kActAD) s.joyy = 255;
    return s;
}

// "BOTAO = TECLA" por linha -> (nome da tecla em maiusculas, acao).
std::vector<std::pair<std::string, uint32_t>> parseKeyMap(const std::string& mapping) {
    std::vector<std::pair<std::string, uint32_t>> out;
    std::istringstream in(mapping.empty() ? std::string(kDefaultMap) : mapping);
    std::string        line;
    int                n = 0;
    while (std::getline(in, line)) {
        ++n;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) throw std::runtime_error("mapa de teclado, linha " + std::to_string(n));
        const std::string act = upper(trim(line.substr(0, eq))), key = upper(trim(line.substr(eq + 1)));
        const uint32_t    a   = actionFromName(act);
        if (!a || !keyFromName(key))
            throw std::runtime_error("mapa de teclado, linha " + std::to_string(n) + ": " + act + " = " + key);
        out.push_back({key, a});
    }
    return out;
}

class Keyboard : public InputSource {
public:
    std::vector<std::pair<int, uint32_t>> map;  // tecla virtual -> acao

    PadState read(int port, uint64_t) override {
        if (port != 0) return {};
        uint32_t act = 0;
        for (const auto& [vk, a] : map)
            if (GetAsyncKeyState(vk) & 0x8000) act |= a;
        return stateFromActions(act);
    }
};

class KeyEvents final : public KeyEventInput {
public:
    std::vector<std::pair<std::string, uint32_t>> map;

    void key(const std::string& name, bool down) override {
        const std::string     k = upper(name);
        std::lock_guard<std::mutex> lock(mutex_);
        if (down) pressed_.insert(k);
        else pressed_.erase(k);
    }
    void releaseAll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        pressed_.clear();
    }
    PadState read(int port, uint64_t) override {
        if (port != 0) return {};
        uint32_t                    act = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [k, a] : map)
            if (pressed_.count(k)) act |= a;
        return stateFromActions(act);
    }

private:
    std::mutex            mutex_;
    std::set<std::string> pressed_;
};

// --- XInput ---

class XInput : public InputSource {
public:
    PadState read(int port, uint64_t) override {
        PadState s;
        if (port != 0) return s;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE x{};
            if (XInputGetState(i, &x) != ERROR_SUCCESS) continue;
            const WORD b = x.Gamepad.wButtons;
            struct M {
                WORD     x;
                uint16_t dc;
            } static const kMap[] = {{XINPUT_GAMEPAD_A, kPadA},         {XINPUT_GAMEPAD_B, kPadB},
                                     {XINPUT_GAMEPAD_X, kPadX},         {XINPUT_GAMEPAD_Y, kPadY},
                                     {XINPUT_GAMEPAD_START, kPadStart}, {XINPUT_GAMEPAD_DPAD_UP, kPadUp},
                                     {XINPUT_GAMEPAD_DPAD_DOWN, kPadDown}, {XINPUT_GAMEPAD_DPAD_LEFT, kPadLeft},
                                     {XINPUT_GAMEPAD_DPAD_RIGHT, kPadRight}};
            for (const M& m : kMap)
                if (b & m.x) s.buttons |= m.dc;
            s.ltrig = x.Gamepad.bLeftTrigger;
            s.rtrig = x.Gamepad.bRightTrigger;
            // XInput: -32768..32767, y para cima; Dreamcast: 0..255, y para baixo
            auto axis = [](SHORT v, bool invert) {
                int t = (int(v) + 32768) >> 8;
                if (invert) t = 255 - t;
                return uint8_t(std::clamp(t, 0, 255));
            };
            const SHORT dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
            const SHORT lx = std::abs(x.Gamepad.sThumbLX) < dz ? 0 : x.Gamepad.sThumbLX;
            const SHORT ly = std::abs(x.Gamepad.sThumbLY) < dz ? 0 : x.Gamepad.sThumbLY;
            s.joyx = lx ? axis(lx, false) : 0x80;
            s.joyy = ly ? axis(ly, true) : 0x80;
            break;
        }
        return s;
    }
};

class Combined : public InputSource {
public:
    std::vector<std::unique_ptr<InputSource>> srcs;

    PadState read(int port, uint64_t seq) override {
        PadState out;
        bool     analog = false, trig = false;
        for (auto& src : srcs) {
            const PadState s = src->read(port, seq);
            out.buttons |= s.buttons;
            if (!trig && (s.ltrig || s.rtrig)) out.ltrig = s.ltrig, out.rtrig = s.rtrig, trig = true;
            if (!analog && (s.joyx != 0x80 || s.joyy != 0x80)) out.joyx = s.joyx, out.joyy = s.joyy, analog = true;
        }
        return out;
    }
};

std::string readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(std::string("entrada: nao foi possivel abrir ") + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

uint16_t pad_button_from_name(const std::string& name) {
    static const std::map<std::string, uint16_t> names = {
        {"C", kPadC},       {"B", kPadB},       {"A", kPadA},         {"START", kPadStart}, {"UP", kPadUp},
        {"DOWN", kPadDown}, {"LEFT", kPadLeft}, {"RIGHT", kPadRight}, {"Z", kPadZ},         {"Y", kPadY},
        {"X", kPadX},       {"D", kPadD}};
    const auto it = names.find(upper(name));
    return it == names.end() ? 0 : it->second;
}

PadState pad_state_from_text(const std::string& text) {
    if (text == "-") return {};
    uint32_t           act = 0;
    std::istringstream bs(text);
    std::string        b;
    while (std::getline(bs, b, '+')) {
        const uint32_t a = actionFromName(upper(b));
        if (!a) throw std::runtime_error("botao " + b);
        act |= a;
    }
    return stateFromActions(act);
}

std::string pad_state_text(const PadState& s) {
    static const char* const kNames[] = {"C", "B", "A", "START", "UP", "DOWN", "LEFT", "RIGHT", "Z", "Y", "X", "D"};
    std::string out;
    auto        add = [&](const char* n) { out += (out.empty() ? "" : "+") + std::string(n); };
    for (int i = 0; i < 12; ++i)
        if (s.buttons & (1u << i)) add(kNames[i]);
    if (s.ltrig) add("LT");
    if (s.rtrig) add("RT");
    if (s.joyy < 0x80) add("ANALOG_UP");
    if (s.joyy > 0x80) add("ANALOG_DOWN");
    if (s.joyx < 0x80) add("ANALOG_LEFT");
    if (s.joyx > 0x80) add("ANALOG_RIGHT");
    return out.empty() ? "-" : out;
}

std::unique_ptr<InputSource> make_scripted_input(const std::string& text) {
    auto               s = std::make_unique<Scripted>();
    std::istringstream in(text);
    std::string        line;
    int                n = 0;
    while (std::getline(in, line)) {
        ++n;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string        first, btns;
        ls >> first;
        if (first == "indice") {
            std::string kind;
            ls >> kind;
            if (!s->steps.empty() || (kind != "passo" && kind != "leitura"))
                throw std::runtime_error("roteiro de entrada, linha " + std::to_string(n) + ": indice passo|leitura, antes dos passos");
            s->byStep = kind == "passo";
            continue;
        }
        char*          end = nullptr;
        const uint64_t seq = std::strtoull(first.c_str(), &end, 10);
        if (first.empty() || *end || !(ls >> btns)) throw std::runtime_error("roteiro de entrada, linha " + std::to_string(n));
        PadState st;
        try {
            st = pad_state_from_text(btns);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("roteiro de entrada, linha " + std::to_string(n) + ": " + e.what());
        }
        if (!s->steps.empty() && seq < s->steps.back().first)
            throw std::runtime_error("roteiro de entrada, linha " + std::to_string(n) + ": fora de ordem");
        s->steps.push_back({seq, st});
    }
    return s;
}

std::unique_ptr<LiveScriptInput> make_live_script_input() {
    auto s    = std::make_unique<Scripted>();
    s->byStep = true;
    return s;
}

void input_set_step_counter(uint64_t (*fn)()) { g_stepCounter = fn; }

uint64_t input_step() {
    if (!g_stepCounter)
        throw std::runtime_error("roteiro indexado por passo de jogo exige o provedor de tempo nativo ou experimental");
    return g_stepCounter();
}

std::unique_ptr<InputSource> make_keyboard_input(const std::string& mapping) {
    auto k = std::make_unique<Keyboard>();
    for (const auto& [key, a] : parseKeyMap(mapping)) k->map.push_back({keyFromName(key), a});
    return k;
}

std::unique_ptr<KeyEventInput> make_key_event_input(const std::string& mapping) {
    auto k = std::make_unique<KeyEvents>();
    k->map = parseKeyMap(mapping);
    return k;
}

std::unique_ptr<InputSource> make_xinput_input() { return std::make_unique<XInput>(); }

std::unique_ptr<InputSource> make_combined_input(std::vector<std::unique_ptr<InputSource>> sources) {
    auto c  = std::make_unique<Combined>();
    c->srcs = std::move(sources);
    return c;
}

void         input_set_source(std::unique_ptr<InputSource> src) { g_source = std::move(src); }
InputSource* input_source() { return g_source.get(); }

void input_configure_from_env() {
    if (const char* script = std::getenv("BS_INPUT_SCRIPT")) {
        input_set_source(make_scripted_input(readFile(script)));
        std::fprintf(stderr, "[input] roteiro %s\n", script);
        return;
    }
    if (const char* host = std::getenv("BS_INPUT"); host && std::string(host) == "host") {
        std::vector<std::unique_ptr<InputSource>> v;
        const char* map = std::getenv("BS_INPUT_MAP");
        v.push_back(make_keyboard_input(map ? readFile(map) : ""));
        v.push_back(make_xinput_input());
        input_set_source(make_combined_input(std::move(v)));
        std::fprintf(stderr, "[input] teclado%s + XInput\n", map ? " (mapa do usuario)" : "");
    }
}

} // namespace bs
