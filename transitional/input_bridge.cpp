#include "input_bridge.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace bs::transitional::input_bridge {
namespace ni = native::input;
namespace {

uint8_t roundByte(double x) { return uint8_t(std::clamp(std::floor(x + 0.5), 0.0, 255.0)); }  // x >= 0: meio para cima

// PadButton (bits do controle do Dreamcast em logica positiva) <-> Button do nucleo nativo
struct Pair {
    uint16_t  pad;
    ni::Button native;
};
constexpr Pair kButtons[] = {{kPadC, ni::C},          {kPadB, ni::B},        {kPadA, ni::A},
                             {kPadStart, ni::Start},  {kPadUp, ni::DpadUp},  {kPadDown, ni::DpadDown},
                             {kPadLeft, ni::DpadLeft}, {kPadRight, ni::DpadRight}, {kPadZ, ni::Z},
                             {kPadY, ni::Y},          {kPadX, ni::X},        {kPadD, ni::D}};

class LatchSource final : public InputSource {
public:
    explicit LatchSource(ni::InputSystem* s) : sys_(s) {}
    PadState read(int port, uint64_t seq) override {
        if (port != 0) return {};
        return to_pad(sys_->latch({seq}));
    }
    bool connected(int port, uint64_t seq) override { return port == 0 && sys_->connected({seq}); }

private:
    ni::InputSystem* sys_;
};

} // namespace

uint8_t axis_byte(float v) { return roundByte(127.5 * double(std::clamp(v, -1.0f, 1.0f)) + 127.5); }
float   axis_from_byte(uint8_t b) { return float((double(b) - 127.5) / 127.5); }
uint8_t trigger_byte(float v) { return roundByte(255.0 * double(std::clamp(v, 0.0f, 1.0f))); }
float   trigger_from_byte(uint8_t b) { return float(double(b) / 255.0); }

ni::ControllerState to_controller(const PadState& p) {
    ni::ControllerState s;
    for (const Pair& b : kButtons)
        if (p.buttons & b.pad) s.buttons |= b.native;
    s.axis(ni::Axis::StickX)   = axis_from_byte(p.joyx);
    s.axis(ni::Axis::StickY)   = axis_from_byte(p.joyy);
    s.axis(ni::Axis::Stick2X)  = axis_from_byte(p.joy2x);
    s.axis(ni::Axis::Stick2Y)  = axis_from_byte(p.joy2y);
    s.axis(ni::Axis::TriggerL) = trigger_from_byte(p.ltrig);
    s.axis(ni::Axis::TriggerR) = trigger_from_byte(p.rtrig);
    s.connected                = p.connected;
    return s;
}

PadState to_pad(const ni::ControllerState& s) {
    PadState p;
    for (const Pair& b : kButtons)
        if (s.buttons & b.native) p.buttons |= b.pad;
    p.joyx      = axis_byte(s.axis(ni::Axis::StickX));
    p.joyy      = axis_byte(s.axis(ni::Axis::StickY));
    p.joy2x     = axis_byte(s.axis(ni::Axis::Stick2X));
    p.joy2y     = axis_byte(s.axis(ni::Axis::Stick2Y));
    p.ltrig     = trigger_byte(s.axis(ni::Axis::TriggerL));
    p.rtrig     = trigger_byte(s.axis(ni::Axis::TriggerR));
    p.connected = s.connected;
    return p;
}

void install(ni::InputSystem* sys) { input_set_source(sys ? std::make_unique<LatchSource>(sys) : nullptr); }

ni::SampleFn from_runtime_source(std::shared_ptr<InputSource> src) {
    return [src = std::move(src)](const ni::SampleContext& ctx) { return to_controller(src->read(0, ctx.sample)); };
}

SessionRecorder::SessionRecorder(Writer write, const std::string& baseText, uint64_t baseUntil)
    : write_(std::move(write)), baseUntil_(baseText.empty() ? 0 : baseUntil) {
    if (baseText.empty()) return;
    base_ = std::shared_ptr<InputSource>(make_scripted_input(baseText));  // lanca com a linha em caso de erro
    std::istringstream in(baseText);
    for (std::string l; std::getline(in, l);)
        if (l.substr(0, l.find('#')).find("DESCONECTADO") != std::string::npos)
            throw std::runtime_error("roteiro base da gravacao: DESCONECTADO nao e aceito (presenca fora do filtro)");
}

ni::ControllerState SessionRecorder::filter(const ni::ControllerState& live) {
    const uint64_t  s = input_step_or(UINT64_MAX);
    std::lock_guard lk(mu_);
    if (s == step_) return frozen_;
    const PadState p = base_ && s < baseUntil_ ? base_->read(0, 0) : to_pad(live);
    frozen_          = to_controller(p);
    if (s == UINT64_MAX) return frozen_;  // antes do contador de passos (nao acontece com --tempo nativo)
    step_               = s;
    const std::string t = pad_state_text(p);
    if (t != last_) write_(std::to_string(s) + " " + t), last_ = t;
    return frozen_;
}

void SessionRecorder::comment(const std::string& text) {
    std::lock_guard lk(mu_);
    write_("# " + text);
}

} // namespace bs::transitional::input_bridge
