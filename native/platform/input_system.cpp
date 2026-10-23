// Nucleo de entrada (N3-1): dispositivos do host -> jogador 1, amostra no ponto de amostragem.
#include "platform/input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

namespace bs::native::input {

namespace {

struct Pad {
    uint32_t                     id = 0;
    std::string                  name;
    std::string                  guid;     // N3-3: perfil por modelo ([perfis."<guid>"])
    std::string                  profile;  // nome do perfil resolvido na conexao (vazio: o [controle])
    bool                         connected = true;
    std::set<std::string>        down;   // botoes apertados (nomes do controle)
    std::map<std::string, float> axes;   // eixos (nomes do controle)
    uint64_t                     activity = 0;
};

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& ch : out) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool neutralPair(float a, float b) { return a == 0 && b == 0; }

// Estado de um controle pelas ligacoes do mapa.
ControllerState padState(const Pad& p, const ActionMap& m) {
    ControllerState s;
    auto axis = [&](const std::string& n) {
        const auto it = p.axes.find(n);
        return it == p.axes.end() ? 0.0f : it->second;
    };
    const Profile* prof = m.profile_for(p.guid);  // sem perfil: o [controle]
    for (const Binding& b : prof ? prof->gamepad : m.gamepad) {
        const Target& t             = b.target;
        const bool    isTriggerAxis = b.control == "lefttrigger" || b.control == "righttrigger";
        const bool    isAxis        = isTriggerAxis || b.control == "leftx" || b.control == "lefty" ||
                            b.control == "rightx" || b.control == "righty";
        switch (t.kind) {
        case Target::Kind::Button:
            if (p.down.count(b.control)) s.buttons |= t.button;
            break;
        case Target::Kind::AxisFull:
            if (const float v = axis(b.control) * t.sign; v != 0 && s.axis(t.axis) == 0) s.axis(t.axis) = v;
            break;
        case Target::Kind::AxisDirection: {
            float v = 0;
            if (isTriggerAxis) v = std::max(0.0f, axis(b.control));
            else if (!isAxis && p.down.count(b.control)) v = 1;
            if (v > 0) {
                float& a = s.axis(t.axis);
                if (t.axis == Axis::TriggerL || t.axis == Axis::TriggerR) a = std::max(a, v);
                else a = t.sign * v;
            }
            break;
        }
        }
    }
    const float dz = prof && prof->deadzone >= 0 ? prof->deadzone : m.options.deadzone;
    apply_radial_deadzone(s.axis(Axis::StickX), s.axis(Axis::StickY), dz);
    apply_radial_deadzone(s.axis(Axis::Stick2X), s.axis(Axis::Stick2Y), dz);
    // os eixos da camera livre ficam crus: a zona morta deles e a da camera ([camera] em controles.toml)
    return s;
}

// Estado do teclado: botoes por OU; analogico e gatilhos no limite. Com os dois lados apertados,
// direita e baixo prevalecem (como o mapa de teclado anterior ao N3-1).
ControllerState keyboardState(const std::set<std::string>& down, const ActionMap& m) {
    ControllerState s;
    bool left = false, right = false, up = false, downDir = false, lt = false, rt = false;
    for (const Binding& b : m.keyboard) {
        if (!down.count(b.control)) continue;
        const Target& t = b.target;
        if (t.kind == Target::Kind::Button) s.buttons |= t.button;
        else if (t.axis == Axis::TriggerL) lt = true;
        else if (t.axis == Axis::TriggerR) rt = true;
        else if (t.axis == Axis::StickX) (t.sign < 0 ? left : right) = true;
        else if (t.axis == Axis::StickY) (t.sign < 0 ? up : downDir) = true;
    }
    if (lt) s.axis(Axis::TriggerL) = 1;
    if (rt) s.axis(Axis::TriggerR) = 1;
    if (left) s.axis(Axis::StickX) = -1;
    if (right) s.axis(Axis::StickX) = 1;
    if (up) s.axis(Axis::StickY) = -1;
    if (downDir) s.axis(Axis::StickY) = 1;
    return s;
}

// Junta as fontes do jogador 1 na ordem (teclado, controle): botoes por OU; cada par de eixos
// (analogico, segundo analogico, gatilhos) vem da primeira fonte fora do neutro.
ControllerState merge(const std::vector<ControllerState>& in) {
    ControllerState out;
    bool            stick = false, stick2 = false, trig = false, cam = false;
    for (const auto& s : in) {
        out.buttons |= s.buttons;
        if (!stick && !neutralPair(s.axis(Axis::StickX), s.axis(Axis::StickY))) {
            out.axis(Axis::StickX) = s.axis(Axis::StickX), out.axis(Axis::StickY) = s.axis(Axis::StickY), stick = true;
        }
        if (!stick2 && !neutralPair(s.axis(Axis::Stick2X), s.axis(Axis::Stick2Y))) {
            out.axis(Axis::Stick2X) = s.axis(Axis::Stick2X), out.axis(Axis::Stick2Y) = s.axis(Axis::Stick2Y), stick2 = true;
        }
        if (!cam && !neutralPair(s.axis(Axis::CameraX), s.axis(Axis::CameraY))) {
            out.axis(Axis::CameraX) = s.axis(Axis::CameraX), out.axis(Axis::CameraY) = s.axis(Axis::CameraY), cam = true;
        }
        if (!trig && !neutralPair(s.axis(Axis::TriggerL), s.axis(Axis::TriggerR))) {
            out.axis(Axis::TriggerL) = s.axis(Axis::TriggerL), out.axis(Axis::TriggerR) = s.axis(Axis::TriggerR), trig = true;
        }
    }
    return out;
}

constexpr uint32_t kXInputBase = 0xFFFF0000u;  // ids dos controles XInput (fora da faixa do SDL)

bool readXInput(DWORD slot, InputSystem::XInputPad& out) {
    XINPUT_STATE x{};
    if (XInputGetState(slot, &x) != ERROR_SUCCESS) return false;
    out = {x.Gamepad.wButtons, x.Gamepad.bLeftTrigger, x.Gamepad.bRightTrigger, x.Gamepad.sThumbLX,
           x.Gamepad.sThumbLY,  x.Gamepad.sThumbRX,     x.Gamepad.sThumbRY};
    return true;
}

} // namespace

struct InputSystem::Impl {
    mutable std::mutex           mu;
    std::set<std::string>        keys;
    std::vector<Pad>             pads;  // na ordem de chegada; desconectados ficam (indice estavel)
    bool                         xinput = false;
    XInputReader                 xreader;  // vazio: XInputGetState
    SampleFn                     test;
    InputSystem::FilterFn        filter;
    InputSystem::FilterFn        transform;  // N7c: movimentacao relativa a camera (antes do filtro)
    bool                         transformTest = false;
    ControllerState              last;
    std::vector<ConnectionEvent> events;
    uint64_t                     tick = 0;

    Pad* find(uint32_t id) {
        for (Pad& p : pads)
            if (p.id == id && p.connected) return &p;
        return nullptr;
    }
    // conectar nao e atividade: um controle neutro que chega nao toma o jogador 1 (revisao Codex
    // 2026-10-18); sem atividade em nenhum, vale o primeiro conectado
    int added(uint32_t id, std::string_view name, std::string_view guid, const ActionMap& m) {
        Pad p;
        p.id   = id;
        p.name = std::string(name);
        p.guid = lower(guid);
        if (const Profile* prof = m.profile_for(guid)) p.profile = prof->name;
        events.push_back({ConnectionEvent::Kind::Connected, int(pads.size()), p.name, p.guid, p.profile});
        pads.push_back(std::move(p));
        return int(pads.size() - 1);
    }
    void removed(uint32_t id) {
        for (size_t i = 0; i < pads.size(); ++i)
            if (pads[i].id == id && pads[i].connected) {
                pads[i].connected = false;
                pads[i].down.clear();
                pads[i].axes.clear();
                events.push_back({ConnectionEvent::Kind::Disconnected, int(i), pads[i].name, pads[i].guid, pads[i].profile});
            }
    }
    int active(const Options& o) const {
        int best = -1;
        if (!o.preferred.empty()) {
            const std::string want = lower(o.preferred);
            for (size_t i = 0; i < pads.size(); ++i)
                if (pads[i].connected && lower(pads[i].name).find(want) != std::string::npos) return int(i);
        }
        for (size_t i = 0; i < pads.size(); ++i) {
            if (!pads[i].connected) continue;
            if (best < 0) best = int(i);
            else if (o.auto_switch && pads[i].activity > pads[size_t(best)].activity) best = int(i);
        }
        return best;
    }
    void pollXInput(const ActionMap& m) {
        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
            InputSystem::XInputPad x;
            const bool             ok = xreader ? xreader(int(slot), x) : readXInput(slot, x);
            const uint32_t         id = kXInputBase + slot;
            Pad*                   p  = find(id);
            if (!ok) {
                if (p) removed(id);
                continue;
            }
            if (!p) p = &pads[size_t(added(id, "XInput " + std::to_string(slot + 1), "xinput", m))];
            const WORD b = x.buttons;
            struct M {
                WORD        bit;
                const char* name;
            } static const kMap[] = {{XINPUT_GAMEPAD_A, "a"},           {XINPUT_GAMEPAD_B, "b"},
                                     {XINPUT_GAMEPAD_X, "x"},           {XINPUT_GAMEPAD_Y, "y"},
                                     {XINPUT_GAMEPAD_START, "start"},   {XINPUT_GAMEPAD_BACK, "back"},
                                     {XINPUT_GAMEPAD_DPAD_UP, "dpup"},  {XINPUT_GAMEPAD_DPAD_DOWN, "dpdown"},
                                     {XINPUT_GAMEPAD_DPAD_LEFT, "dpleft"}, {XINPUT_GAMEPAD_DPAD_RIGHT, "dpright"},
                                     {XINPUT_GAMEPAD_LEFT_SHOULDER, "leftshoulder"},
                                     {XINPUT_GAMEPAD_RIGHT_SHOULDER, "rightshoulder"},
                                     {XINPUT_GAMEPAD_LEFT_THUMB, "leftstick"}, {XINPUT_GAMEPAD_RIGHT_THUMB, "rightstick"}};
            std::set<std::string> down;
            for (const M& k : kMap)
                if (b & k.bit) down.insert(k.name);
            std::map<std::string, float> axes;
            axes["leftx"]        = axis_from_s16(x.lx);
            axes["lefty"]        = -axis_from_s16(x.ly);  // XInput: y para cima
            axes["rightx"]       = axis_from_s16(x.rx);
            axes["righty"]       = -axis_from_s16(x.ry);
            axes["lefttrigger"]  = trigger_from_u8(x.lt);
            axes["righttrigger"] = trigger_from_u8(x.rt);
            // atividade como no SDL: botao novo ou eixo que passou de meio curso
            bool active = false;
            for (const std::string& n : down) active = active || !p->down.count(n);
            for (const auto& [n, v] : axes) {
                const auto it = p->axes.find(n);
                active        = active || (std::abs(v) >= 0.5f && (it == p->axes.end() || std::abs(it->second) < 0.5f));
            }
            if (active) p->activity = ++tick;
            p->down = std::move(down);
            p->axes = std::move(axes);
        }
    }
};

InputSystem::InputSystem(ActionMap map) : map_(std::move(map)), impl_(std::make_unique<Impl>()) {}
InputSystem::~InputSystem() = default;

void InputSystem::key(std::string_view name, bool down) {
    std::lock_guard lk(impl_->mu);
    if (down) impl_->keys.insert(std::string(name));
    else impl_->keys.erase(std::string(name));
}

// So o teclado: o SDL nao reenvia um botao de controle que continua apertado depois da volta do
// foco, entao limpar os controles aqui os deixaria soltos ate o proximo aperto (revisao Codex
// 2026-10-18). Sem foco o SDL ja ignora apertos novos de controle.
void InputSystem::release_keyboard() {
    std::lock_guard lk(impl_->mu);
    impl_->keys.clear();
}

bool InputSystem::connected(const SampleContext& ctx) {
    std::lock_guard lk(impl_->mu);
    return impl_->test ? impl_->test(ctx).connected : true;
}

int InputSystem::gamepad_added(uint32_t id, std::string_view name, std::string_view guid) {
    std::lock_guard lk(impl_->mu);
    if (impl_->find(id)) return -1;
    return impl_->added(id, name, guid, map_);
}

void InputSystem::gamepad_removed(uint32_t id) {
    std::lock_guard lk(impl_->mu);
    impl_->removed(id);
}

void InputSystem::gamepad_button(uint32_t id, std::string_view control, bool down) {
    std::lock_guard lk(impl_->mu);
    if (Pad* p = impl_->find(id)) {
        if (down) p->down.insert(std::string(control)), p->activity = ++impl_->tick;
        else p->down.erase(std::string(control));
    }
}

void InputSystem::gamepad_axis(uint32_t id, std::string_view control, float value) {
    std::lock_guard lk(impl_->mu);
    if (Pad* p = impl_->find(id)) {
        float& a = p->axes[std::string(control)];
        if (std::abs(value) >= 0.5f && std::abs(a) < 0.5f) p->activity = ++impl_->tick;  // inclinou: atividade
        a = value;
    }
}

void InputSystem::enable_xinput(XInputReader reader) {
    std::lock_guard lk(impl_->mu);
    impl_->xinput  = true;
    impl_->xreader = std::move(reader);
}

void InputSystem::set_test_source(SampleFn fn) {
    std::lock_guard lk(impl_->mu);
    impl_->test = std::move(fn);
}

void InputSystem::set_filter(FilterFn fn) {
    std::lock_guard lk(impl_->mu);
    impl_->filter = std::move(fn);
}

void InputSystem::set_transform(FilterFn fn, bool test_source) {
    std::lock_guard lk(impl_->mu);
    impl_->transform = std::move(fn), impl_->transformTest = test_source;
}

ControllerState InputSystem::latch(const SampleContext& ctx) {
    std::lock_guard lk(impl_->mu);
    if (impl_->test) {
        ControllerState s = impl_->test(ctx);
        if (impl_->transform && impl_->transformTest) s = impl_->transform(ctx, s);
        return impl_->last = s;
    }
    if (impl_->xinput) impl_->pollXInput(map_);
    std::vector<ControllerState> parts;
    parts.push_back(keyboardState(impl_->keys, map_));
    if (const int a = impl_->active(map_.options); a >= 0) parts.push_back(padState(impl_->pads[size_t(a)], map_));
    ControllerState s = merge(parts);
    s.connected       = true;  // a janela sempre tem o teclado: a porta do jogador 1 nunca fica vazia
    if (impl_->transform) s = impl_->transform(ctx, s);
    if (impl_->filter) s = impl_->filter(ctx, s);
    return impl_->last = s;
}

ControllerState InputSystem::peek() {
    // sempre os dispositivos do host, mesmo com a fonte de teste: um roteiro controla o jogo e o analogico direito real
    // (ou o controle virtual do SDL) ainda gira a camera, que nao muda o estado do jogo
    std::lock_guard lk(impl_->mu);
    std::vector<ControllerState> parts;
    parts.push_back(keyboardState(impl_->keys, map_));
    if (const int a = impl_->active(map_.options); a >= 0) parts.push_back(padState(impl_->pads[size_t(a)], map_));
    return merge(parts);
}

ControllerState InputSystem::last() const {
    std::lock_guard lk(impl_->mu);
    return impl_->last;
}

std::vector<ConnectionEvent> InputSystem::drain_events() {
    std::lock_guard lk(impl_->mu);
    std::vector<ConnectionEvent> out;
    out.swap(impl_->events);
    return out;
}

int InputSystem::active_gamepad() const {
    std::lock_guard lk(impl_->mu);
    return impl_->active(map_.options);
}

std::vector<std::string> InputSystem::gamepad_names() const {
    std::lock_guard lk(impl_->mu);
    std::vector<std::string> v;
    for (const Pad& p : impl_->pads) v.push_back(p.name + (p.connected ? "" : " (desconectado)"));
    return v;
}

} // namespace bs::native::input
