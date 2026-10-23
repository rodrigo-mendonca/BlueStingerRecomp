#include "free_camera.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace bs::transitional::free_camera {
namespace ni = bs::native::input;

// ---- rig ----

void CameraRig::reset() { yaw_ = pitch_ = vx_ = vy_ = idle_ = 0, returning_ = false; }

void CameraRig::absorb(float degrees) {
    // so com a vista fora do enquadramento do jogo: sem deslocamento, a vista segue a camera do jogo (como no Original);
    // quando a camera do jogo alcanca a vista (o sinal troca), o deslocamento acaba ali
    if (cfg_.mode == st::CameraMode::Original || returning_ || yaw_ == 0 || !std::isfinite(degrees)) return;
    // a passagem por zero se ve no valor sem normalizar: +170 - (-20) = +190 (= -170) e a vista ficando, nao a camera
    // alcancando (revisao Codex 2026-10-23)
    const float raw = yaw_ - degrees;
    if ((raw > 0) != (yaw_ > 0) || std::fabs(raw) < 0.05f) return void(yaw_ = 0);
    yaw_ = raw > 180 ? raw - 360 : raw < -180 ? raw + 360 : raw;
}

void CameraRig::step(const RigInput& in, bool suspended) {
    if (cfg_.mode == st::CameraMode::Original || suspended) return reset();
    float x = std::isfinite(in.x) ? std::clamp(in.x, -1.f, 1.f) : 0, y = std::isfinite(in.y) ? std::clamp(in.y, -1.f, 1.f) : 0;
    // zona morta radial com reescala (o fim do curso continua 1)
    const float m = std::hypot(x, y);
    if (m <= cfg_.deadzone) x = y = 0;
    else {
        const float k = std::min(1.f, (m - cfg_.deadzone) / (1 - cfg_.deadzone)) / m;
        x *= k, y *= k;
    }
    const bool moving = x != 0 || y != 0;
    if (moving) returning_ = false, idle_ = 0;
    else idle_ += kStep;
    if (in.recenter && cfg_.r3_recenter) returning_ = true;
    if (cfg_.mode == st::CameraMode::Hybrid && idle_ >= cfg_.return_s) returning_ = true;
    // suavizacao: a velocidade segue o analogico com um filtro de primeira ordem (fator por 1/60 s)
    const float a = std::pow(cfg_.smoothing, kStep * 60.f);
    vx_ = vx_ * a + x * cfg_.sens_x * (1 - a);
    vy_ = vy_ * a + (cfg_.invert_y ? y : -y) * cfg_.sens_y * (1 - a);  // analogico para cima: olha para cima
    if (returning_ && !moving) {  // volta suave ao enquadramento do jogo (constante de 0,25 s)
        const float k = std::exp(-kStep / 0.25f);
        yaw_ *= k, pitch_ *= k, vx_ = vy_ = 0;
        if (std::fabs(yaw_) < 0.05f && std::fabs(pitch_) < 0.05f) yaw_ = pitch_ = 0, returning_ = false;
        return;
    }
    yaw_ += vx_ * kStep, pitch_ += vy_ * kStep;
    if (yaw_ > 180) yaw_ -= 360;
    if (yaw_ < -180) yaw_ += 360;
    pitch_ = std::clamp(pitch_, -cfg_.pitch_down, cfg_.pitch_up);
}

// ---- geometria ----

int32_t units(float degrees) { return int32_t(std::lround(double(degrees) * 65536.0 / 360.0)); }

static double radians(int32_t a) { return double(uint32_t(a) & 0xFFFF) * (6.283185307179586 / 65536.0); }  // como o fsca

Vec3 forward(int32_t yaw, int32_t pitch) {
    const double y = radians(yaw), p = radians(pitch);
    return {float(-std::sin(y) * std::cos(p)), float(std::sin(p)), float(-std::cos(y) * std::cos(p))};
}

Vec3 orbit(const Vec3& pos, int32_t yaw, int32_t pitch, const Vec3& player, int32_t dyaw, int32_t dpitch) {
    const Vec3  f0 = forward(yaw, pitch);
    const float t  = std::clamp((player.x - pos.x) * f0.x + (player.y - pos.y) * f0.y + (player.z - pos.z) * f0.z, 0.f, kMaxOrbit);
    const Vec3  pv{pos.x + f0.x * t, pos.y + f0.y * t, pos.z + f0.z * t};
    const Vec3  f1 = forward(yaw + dyaw, pitch + dpitch);
    return {pv.x - f1.x * t, pv.y - f1.y * t, pv.z - f1.z * t};
}

// ---- interceptacao ----

namespace {

constexpr uint32_t kCamera = 0x8C24CD50;  // posicao +0, yaw +0x10
constexpr uint32_t kScene  = 0x8C24CD68;  // != 0: camera roteirizada (secao 23.1)
constexpr uint32_t kPlayer = 0x8C24D668;
// A referencia do movimento do Eliot: a copia do yaw da camera do jogo (0x8C24D2CC + 0x3B4), feita de CD60 em
// 8C041674/8C041830/8C04199E e refeita durante o toque quando o analogico muda bastante (BS_RAM_WATCH, 2026-10-22)
constexpr uint32_t kMoveRef = 0x8C24D680;
constexpr int32_t  kMaxPitch     = 14563;  // 80 graus: o pitch final nunca passa disso
constexpr float    kCutDistance  = 60.0f;  // salto de posicao entre montagens: corte de camera
constexpr int32_t  kCutYaw       = 7282;   // 40 graus
constexpr uint64_t kIdleVblanks  = 30;     // sem montagem de gameplay: suspende

enum Fn { Pitch, Yaw, Position, Translate, RotY, RotX, kFns };
struct Site {
    uint32_t addr, pr;
};
constexpr Site kSites[kFns] = {{0x8C174148, 0x8C052892}, {0x8C174180, 0x8C05289A}, {0x8C174308, 0x8C0528AC},
                               {0x8C1713A4, 0x8C0528DA}, {0x8C170DC8, 0x8C0528E4}, {0x8C170D34, 0x8C05290C}};

struct ScriptLine {
    uint64_t vblank;
    RigInput in;
};

struct MoveLatch {  // movimentacao relativa: o toque atual do analogico esquerdo
    bool     held = false, engaged = false;
    int32_t  bias = 0, lastView = 0;  // soma dos saltos da vista nos cortes (o rumo continua) e a ultima vista
    uint64_t cuts = 0;
};

struct State {
    Options                 o;
    MoveLatch               move;
    CameraRig               rig;
    Sh4Func                 prev[kFns] = {};
    bool                    pending = false;  // montagem em curso (do pitch ao giro em X)
    int32_t                 dyaw = 0, dpitch = 0;
    float                   moveDeg = 0;     // yaw da vista na ultima montagem, em graus (para a direita = positivo)
    int32_t                 moveRef = 0;     // kMoveRef na ultima montagem
    Vec3                    delta;           // posicao orbitada - posicao do jogo
    uint64_t                vb = 0, lastBuild = 0;
    bool                    haveLast = false, scene = false;
    Vec3                    lastPos;
    int32_t                 lastYaw = 0;
    std::vector<ScriptLine> script;
    bool                    fixed = false;  // BS_CAMERA_FIXA: deslocamento constante (testes)
    float                   fixedYaw = 0, fixedPitch = 0;
    FILE*                   log = nullptr;
    Stats                   stats;
};
State  g_state;
State* g = &g_state;

float rf(Sh4Context* c, uint32_t a) {
    const uint32_t v = sh4_read32(c, a);
    float          f;
    std::memcpy(&f, &v, 4);
    return f;
}
float fr(const Sh4Context* c, int i) {
    float f;
    std::memcpy(&f, &c->fr[i], 4);
    return f;
}
void setFr(Sh4Context* c, int i, float f) { std::memcpy(&c->fr[i], &f, 4); }

// O pitch e o primeiro: le a camera do jogo, decide o deslocamento desta montagem e o guarda para as outras cinco.
void fPitch(Sh4Context* c) {
    State& s = *g;
    if (c->pr != kSites[Pitch].pr) return ++s.stats.foreign, s.prev[Pitch](c);
    ++s.stats.builds;
    const Vec3    pos{rf(c, kCamera), rf(c, kCamera + 4), rf(c, kCamera + 8)};
    const int32_t yaw   = int32_t(sh4_read32(c, kCamera + 0x10));
    const int32_t pitch = int32_t(c->r[5]);
    const Vec3    player{rf(c, kPlayer), rf(c, kPlayer + 4), rf(c, kPlayer + 8)};
    s.scene     = sh4_read16(c, kScene) != 0;
    s.moveRef   = int32_t(sh4_read16(c, kMoveRef));
    s.lastBuild = s.vb;
    if (s.haveLast) {
        const float   d  = std::hypot(pos.x - s.lastPos.x, pos.y - s.lastPos.y, pos.z - s.lastPos.z);
        const int32_t dy = int16_t(uint16_t(uint32_t(yaw - s.lastYaw) & 0xFFFF));
        // movimentacao relativa: a vista fica ancorada no mundo quando a camera do jogo gira sozinha (a de perseguicao
        // vai para tras do Eliot, as vezes 45 graus numa montagem: R12, 2026-10-23); so o salto de posicao e corte.
        // O yaw do jogo cresce girando para a esquerda, entao -dy e o giro para a direita.
        const bool anchored = s.o.cfg.movement == st::MoveMode::Camera && !s.fixed && !s.scene;
        if (!std::isfinite(d) || d > kCutDistance || (!anchored && std::abs(dy) > kCutYaw)) ++s.stats.cuts, s.rig.reset();
        else if (anchored) s.rig.absorb(-float(dy) * (360.0f / 65536.0f));
    }
    s.haveLast = true, s.lastPos = pos, s.lastYaw = yaw;
    s.dyaw = s.dpitch = 0, s.delta = {}, s.moveDeg = 0;
    const bool finite = std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) && std::isfinite(player.x) &&
                        std::isfinite(player.y) && std::isfinite(player.z);
    if (s.scene || !finite) {
        ++s.stats.suspended, s.rig.reset();
    } else {
        const float yd = s.fixed ? s.fixedYaw : s.rig.yaw(), pd = s.fixed ? s.fixedPitch : s.rig.pitch();
        s.dyaw          = units(-yd);  // o yaw do jogo cresce girando a vista para a esquerda (capturas, 2026-10-22)
        s.moveDeg       = s.dyaw ? -float(s.dyaw) * (360.0f / 65536.0f) : 0;  // o que a vista girou de fato
        const int32_t p0 = int16_t(uint16_t(uint32_t(pitch) & 0xFFFF));
        s.dpitch        = std::clamp(p0 + units(pd), -kMaxPitch, kMaxPitch) - std::clamp(p0, -kMaxPitch, kMaxPitch);
        if (s.dyaw || s.dpitch) {
            const Vec3 np = orbit(pos, yaw, pitch, player, s.dyaw, s.dpitch);
            s.delta       = {np.x - pos.x, np.y - pos.y, np.z - pos.z};
            ++s.stats.applied;
        }
    }
    if (s.log)
        std::fprintf(s.log, "%llu\t%.3f\t%.3f\t%.3f\t%d\t%d\t%.3f\t%.3f\t%.3f\t%d\t%d\t%d\t%.3f\t%.3f\t%.3f\n",
                     static_cast<unsigned long long>(s.vb), pos.x, pos.y, pos.z, int(yaw), int(pitch), player.x, player.y, player.z,
                     int(s.scene), int(s.dyaw), int(s.dpitch), s.delta.x, s.delta.y, s.delta.z);
    s.pending = true;
    c->r[5] = uint32_t(pitch + s.dpitch);
    s.prev[Pitch](c);
}

template <Fn F>
void fSite(Sh4Context* c) {
    State& s = *g;
    if (c->pr != kSites[F].pr || !s.pending) return ++s.stats.foreign, s.prev[F](c);
    switch (F) {
    case Yaw: c->r[5] += uint32_t(s.dyaw); break;
    case Position:  // fr4..fr6 = posicao do jogo (CD50..CD58)
        setFr(c, 4, fr(c, 4) + s.delta.x), setFr(c, 5, fr(c, 5) + s.delta.y), setFr(c, 6, fr(c, 6) + s.delta.z);
        break;
    case Translate:  // fr4..fr6 = (x, -y, -z) da posicao do jogo
        setFr(c, 4, fr(c, 4) + s.delta.x), setFr(c, 5, fr(c, 5) - s.delta.y), setFr(c, 6, fr(c, 6) - s.delta.z);
        break;
    case RotY: c->r[5] -= uint32_t(s.dyaw); break;
    case RotX: c->r[5] += uint32_t(s.dpitch), s.pending = false; break;
    default: break;
    }
    s.prev[F](c);
}

constexpr Sh4Func kWrappers[kFns] = {&fPitch, &fSite<Yaw>, &fSite<Position>, &fSite<Translate>, &fSite<RotY>, &fSite<RotX>};

std::vector<ScriptLine> readScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("BS_CAMERA_ROTEIRO: nao abre " + path);
    std::vector<ScriptLine> out;
    std::string             line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        ScriptLine         l;
        int                r3 = 0;
        if (!(ls >> l.vblank >> l.in.x >> l.in.y)) throw std::runtime_error("BS_CAMERA_ROTEIRO: linha invalida: " + line);
        ls >> r3;
        l.in.recenter = r3 != 0;
        out.push_back(l);
    }
    std::stable_sort(out.begin(), out.end(), [](const ScriptLine& a, const ScriptLine& b) { return a.vblank < b.vblank; });
    return out;
}

void doInstall(BoundaryRegistry& registry) {
    for (const Site& site : kSites) {
        if (!sh4_lookup_generated(site.addr)) throw std::runtime_error("camera livre: funcao do SDK sem codigo gerado");
        if (!mods::has_slot(site.addr)) {
            char hexa[16];
            std::snprintf(hexa, sizeof hexa, "%08X", site.addr);
            throw std::runtime_error(std::string("camera livre: a funcao ") + hexa +
                                     " nao tem slot (gere o codigo de novo com [native] slots)");
        }
    }
    for (int i = 0; i < kFns; ++i) {
        registry.add(kSites[i].addr, dcmap::Subsystem::Render, "camera");
        registry.set_native(kSites[i].addr, kWrappers[i]);
        if (registry.select(kSites[i].addr, Provider::Native) != SelectResult::Ok)
            throw std::runtime_error("camera livre: troca de provedor falhou");
        g->prev[i] = registry.entries().at(sh4_phys(kSites[i].addr)).previous;
        if (!g->prev[i]) g->prev[i] = sh4_lookup_generated(kSites[i].addr);
    }
}

bool g_installed = false;

} // namespace

void configure(BoundaryRegistry& registry, Options o) {
    o.cfg = st::clamp_camera(o.cfg);
    if (g->log) std::fclose(g->log), g->log = nullptr;
    g->o      = std::move(o);
    g->stats  = {};
    g->script = {};
    g->fixed  = false;
    // Original: nada de diagnostico (nem roteiro, nem log); o menu ainda pode trocar o modo depois (com a entrada guardada)
    if (g->o.cfg.mode != st::CameraMode::Original) {
        if (!g->o.script.empty()) g->script = readScript(g->o.script);
        g->fixed = g->o.fixed, g->fixedYaw = g->o.fixedYaw, g->fixedPitch = g->o.fixedPitch;
        if (!g->o.log.empty()) {
            g->log = std::fopen(g->o.log.c_str(), "w");
            if (!g->log) throw std::runtime_error("BS_CAMERA_LOG: nao abre " + g->o.log);
            std::fprintf(g->log, "vblank\tx\ty\tz\tyaw\tpitch\tpx\tpy\tpz\tcena\tdyaw\tdpitch\tdx\tdy\tdz\n");
        }
    }
    set_settings(registry, g->o.cfg);
}

void set_settings(BoundaryRegistry& registry, const st::CameraSettings& cfg) {
    g->o.cfg = st::clamp_camera(cfg);
    g->rig.configure(g->o.cfg);
    g->rig.reset();
    g->pending = false;
    g->move    = {};
    if (g->o.cfg.mode == st::CameraMode::Original) {
        if (g_installed) {
            for (const Site& site : kSites) registry.select(site.addr, Provider::Legacy);
            g_installed = false;
        }
        return;
    }
    if (!g_installed) {
        doInstall(registry);
        g_installed = true;
        g->haveLast = false;
    }
}

void uninstall(BoundaryRegistry& registry) {
    if (g_installed)
        for (const Site& site : kSites) registry.select(site.addr, Provider::Legacy);
    g_installed = false;
    if (g->log) std::fclose(g->log), g->log = nullptr;
}

bool installed() { return g_installed; }

void vblank(uint64_t n) {
    if (!g_installed) return;
    State& s = *g;
    s.vb     = n;
    RigInput in;
    if (!s.script.empty()) {
        auto it = std::upper_bound(s.script.begin(), s.script.end(), n, [](uint64_t v, const ScriptLine& l) { return v < l.vblank; });
        if (it != s.script.begin()) in = std::prev(it)->in;
    } else if (s.o.input) {
        const ni::ControllerState cs = s.o.input();
        in.x = cs.axis(ni::Axis::CameraX), in.y = cs.axis(ni::Axis::CameraY);
        in.recenter = (cs.buttons & ni::CameraRecenter) != 0;
    }
    s.rig.step(in, s.scene || n - s.lastBuild > kIdleVblanks);
}

void rotate_stick(float& x, float& y, float degrees) {
    if (!std::isfinite(x) || !std::isfinite(y)) x = y = 0;
    const float m = std::hypot(x, y);
    if (m > 1) x /= m, y /= m;
    const double r = double(degrees) * (3.141592653589793 / 180.0), c = std::cos(r), s = std::sin(r);
    // y para baixo: girar "para frente" (0, -1) para a direita da (sen, -cos)
    const float nx = float(x * c - y * s), ny = float(x * s + y * c);
    x = std::clamp(nx, -1.f, 1.f), y = std::clamp(ny, -1.f, 1.f);
}

float relative_degrees(int32_t view, int32_t ref) {
    const int32_t a = int16_t(uint16_t(uint32_t(view - ref) & 0xFFFF));  // a diferenca de 16 bits com sinal
    return -float(a) * (360.0f / 65536.0f);                              // o yaw do SDK cresce para a esquerda
}

float move_yaw() {
    const State& s = *g;
    if (!g_installed || s.o.cfg.movement != st::MoveMode::Camera || s.scene || s.vb - s.lastBuild > kIdleVblanks) return 0;
    return s.moveDeg;
}

// O rumo do Eliot (medido em gameplay, docs/N7-PLANO.md 23.3) e a referencia do movimento (kMoveRef, uma copia do yaw da
// camera do jogo que o jogo faz no toque e refaz quando o analogico muda bastante) mais o angulo do analogico. Para andar
// para onde a vista olha, o angulo entregue e (vista - kMoveRef). So depois que a vista sai do enquadramento do jogo
// (deslocamento != 0) nesse toque; antes disso a entrada passa intacta (igual ao Original). Num corte de camera o salto
// da vista entra no `bias`: o rumo continua, como no original.
ni::ControllerState relative_input(const ni::ControllerState& in) {
    State&      s = *g;
    MoveLatch&  m = s.move;
    const float x0 = in.axis(ni::Axis::StickX), y0 = in.axis(ni::Axis::StickY);
    if (!g_installed || s.o.cfg.movement != st::MoveMode::Camera || s.scene || s.vb - s.lastBuild > kIdleVblanks ||
        (x0 == 0 && y0 == 0)) {
        m = {};
        return in;
    }
    const int32_t view = s.lastYaw + s.dyaw;  // o yaw da vista na ultima montagem (unidades do SDK)
    if (!m.held) m.held = true, m.lastView = view, m.cuts = s.stats.cuts;
    if (s.stats.cuts != m.cuts) {
        if (m.engaged) m.bias += m.lastView - view;
        m.cuts = s.stats.cuts;
    }
    if (s.dyaw != 0) m.engaged = true;
    m.lastView = view;
    if (!m.engaged) return in;
    const float d = relative_degrees(view + m.bias, s.moveRef);
    if (d == 0) return in;
    ni::ControllerState out = in;
    float               x = x0, y = y0;
    rotate_stick(x, y, d);
    out.axis(ni::Axis::StickX) = x, out.axis(ni::Axis::StickY) = y;
    ++s.stats.moved;
    return out;
}

Stats stats() { return g->stats; }

std::string summary() {
    const Stats& s = g->stats;
    char         b[320];
    std::snprintf(b, sizeof b,
                  "[camera] modo %s: montagens %llu, com deslocamento %llu, suspensas %llu, cortes %llu, outras chamadas %llu; "
                  "movimento %s: amostras giradas %llu\n",
                  st::camera_mode_name(g->o.cfg.mode), static_cast<unsigned long long>(s.builds),
                  static_cast<unsigned long long>(s.applied), static_cast<unsigned long long>(s.suspended),
                  static_cast<unsigned long long>(s.cuts), static_cast<unsigned long long>(s.foreign),
                  st::move_mode_name(g->o.cfg.movement), static_cast<unsigned long long>(s.moved));
    return b;
}

} // namespace bs::transitional::free_camera
