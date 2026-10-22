// N7c (docs/N7-PLANO.md secao 23.2): os seis slots da camera livre com um Sh4Context e a RAM do guest. As funcoes do
// SDK sao sentinelas (registram os argumentos que receberam); o teste confere o filtro pelo PR, a ordem da montagem,
// a suspensao por CD68, que a camera do jogo (0x8C24CD50) nao muda e que o modo Original devolve os slots.
#include "test.h"

#include "boundary_registry.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "free_camera.h"
#include "mods/mod_api.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace fc = bs::transitional::free_camera;
namespace st = bs::native::settings;

namespace {

struct Seen {
    uint32_t r5 = 0;
    float    f[3] = {};
    int      calls = 0;
};
Seen g_seen[6];

template <int I>
void sentinel(bs::Sh4Context* c) {
    Seen& s = g_seen[I];
    s.r5    = c->r[5];
    for (int k = 0; k < 3; ++k) std::memcpy(&s.f[k], &c->fr[4 + k], 4);
    ++s.calls;
}

constexpr uint32_t kAddr[6] = {0x8C174148, 0x8C174180, 0x8C174308, 0x8C1713A4, 0x8C170DC8, 0x8C170D34};
constexpr uint32_t kPr[6]   = {0x8C052892, 0x8C05289A, 0x8C0528AC, 0x8C0528DA, 0x8C0528E4, 0x8C05290C};
constexpr bs::Sh4Func kSentinel[6] = {&sentinel<0>, &sentinel<1>, &sentinel<2>, &sentinel<3>, &sentinel<4>, &sentinel<5>};
bs::Sh4Func           g_slot[6]    = {&sentinel<0>, &sentinel<1>, &sentinel<2>, &sentinel<3>, &sentinel<4>, &sentinel<5>};
const bs::mods::Slot  kSlots[6]    = {{kAddr[0], &g_slot[0], &sentinel<0>}, {kAddr[1], &g_slot[1], &sentinel<1>},
                                      {kAddr[2], &g_slot[2], &sentinel<2>}, {kAddr[3], &g_slot[3], &sentinel<3>},
                                      {kAddr[4], &g_slot[4], &sentinel<4>}, {kAddr[5], &g_slot[5], &sentinel<5>}};

void putf(std::vector<uint8_t>& ram, uint32_t a, float v) { std::memcpy(&ram[a & bs::kRamMask], &v, 4); }
void put32(std::vector<uint8_t>& ram, uint32_t a, uint32_t v) { std::memcpy(&ram[a & bs::kRamMask], &v, 4); }

// chama o slot i como o codigo gerado: PR do ponto de chamada, r5 e fr4..fr6
void call(bs::Sh4Context& c, int i, uint32_t pr, uint32_t r5, float f4 = 0, float f5 = 0, float f6 = 0) {
    c.pr = pr, c.r[5] = r5;
    std::memcpy(&c.fr[4], &f4, 4), std::memcpy(&c.fr[5], &f5, 4), std::memcpy(&c.fr[6], &f6, 4);
    g_slot[i](&c);
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

} // namespace

BS_TEST(camera_slots_filter_order_and_original) {
    for (int i = 0; i < 6; ++i) bs::sh4_register_hle(kAddr[i], kSentinel[i]);  // "codigo gerado" das seis funcoes
    bs::mods::set_slots(kSlots, 6);
    std::vector<uint8_t> ram(size_t(bs::kRamMask) + 1);
    bs::Sh4Context       c{};
    c.ram = ram.data();
    // camera do jogo em (100, 200, 300) olhando para -z (yaw 0, pitch 0); o Eliot em (100, 200, 0): raio 300
    putf(ram, 0x8C24CD50, 100), putf(ram, 0x8C24CD54, 200), putf(ram, 0x8C24CD58, 300), put32(ram, 0x8C24CD60, 0);
    putf(ram, 0x8C24D668, 100), putf(ram, 0x8C24D66C, 200), putf(ram, 0x8C24D670, 0);
    std::vector<uint8_t> before(ram.begin() + 0x24CD50, ram.begin() + 0x24CD6C);

    bs::transitional::BoundaryRegistry reg;
    fc::Options                        o;
    o.cfg.mode = st::CameraMode::Free, o.fixed = true, o.fixedYaw = 90;  // 90 graus para a direita
    fc::configure(reg, o);
    BS_CHECK(fc::installed());
    for (int i = 0; i < 6; ++i) BS_CHECK(g_slot[i] != kSentinel[i]);

    // a montagem do gameplay, na ordem do 8C052240
    call(c, 0, kPr[0], 0);
    BS_CHECK(g_seen[0].r5 == 0);  // pitch sem deslocamento
    call(c, 1, kPr[1], 0);
    BS_CHECK(g_seen[1].r5 == uint32_t(-0x4000));  // o yaw do jogo cresce para a esquerda: direita = -90 graus
    call(c, 2, kPr[2], 0, 100, 200, 300);
    // orbita em volta de (100, 200, 0): a vista passa a olhar para +x, a camera fica em (-200, 200, 0)
    BS_CHECK(near(g_seen[2].f[0], -200) && near(g_seen[2].f[1], 200) && near(g_seen[2].f[2], 0));
    call(c, 3, kPr[3], 0, 100, -200, -300);  // translacao do desenho: (x, -y, -z)
    BS_CHECK(near(g_seen[3].f[0], -200) && near(g_seen[3].f[1], -200) && near(g_seen[3].f[2], 0));
    call(c, 4, kPr[4], ~0u);  // ~yaw
    BS_CHECK(g_seen[4].r5 == ~uint32_t(-0x4000));
    call(c, 5, kPr[5], 0);
    BS_CHECK(g_seen[5].r5 == 0);

    // outro chamador (o caminho fixo 8C078FC0 usa 8C1713A4 e 8C170D34): sem mudanca
    call(c, 3, 0x8C079688, 0, 1, 2, 3);
    BS_CHECK(g_seen[3].f[0] == 1 && g_seen[3].f[1] == 2 && g_seen[3].f[2] == 3);
    call(c, 5, 0x8C079692, 0x77);
    BS_CHECK(g_seen[5].r5 == 0x77);
    // fora de ordem (montagem ja fechada pelo giro em X): o PR certo nao basta
    call(c, 1, kPr[1], 5);
    BS_CHECK(g_seen[1].r5 == 5);

    // cena roteirizada (CD68 = 1, 16 bits): nada muda na montagem inteira
    ram[0x24CD68] = 1;
    call(c, 0, kPr[0], 0x100), call(c, 1, kPr[1], 0), call(c, 2, kPr[2], 0, 100, 200, 300);
    BS_CHECK(g_seen[0].r5 == 0x100 && g_seen[1].r5 == 0 && g_seen[2].f[0] == 100 && g_seen[2].f[2] == 300);
    call(c, 3, kPr[3], 0, 100, -200, -300), call(c, 4, kPr[4], ~0u), call(c, 5, kPr[5], 0x100);
    BS_CHECK(g_seen[3].f[0] == 100 && g_seen[4].r5 == ~0u && g_seen[5].r5 == 0x100);
    ram[0x24CD68] = 0;

    // a camera do jogo (0x8C24CD50..6B) nunca e escrita
    BS_CHECK(std::memcmp(before.data(), ram.data() + 0x24CD50, before.size()) == 0);

    // Original: os slots voltam ao que eram e a montagem chega ao SDK sem mudanca
    fc::set_settings(reg, st::CameraSettings{});
    BS_CHECK(!fc::installed());
    for (int i = 0; i < 6; ++i) BS_CHECK(g_slot[i] == kSentinel[i]);
    call(c, 1, kPr[1], 0);
    BS_CHECK(g_seen[1].r5 == 0);

    fc::uninstall(reg);
    bs::mods::set_slots(nullptr, 0);
}
