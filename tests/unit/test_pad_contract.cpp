// Contrato do servidor pd (transitional/pad_contract, N3-0). Casos tirados do disassembly de
// 8C15C4E6 (servidor), 8C15C24C (decodificador), 8C15C1A2 (sem dispositivo) e 8C15C490 (estado);
// a conferencia contra o legado em execucao e da sonda BS_PAD_CONTRATO (tools/dist/test_controles.ps1).
#include "test.h"

#include "pad_contract.h"

#include <cstring>

using namespace bs::transitional::pad;

namespace {

constexpr uint32_t kSupport = 0x000F06FE;  // controle padrao (medido): r, l, x1, y1

Unit controller(uint16_t pressed, uint8_t r = 0, uint8_t l = 0, uint8_t x = 0x80, uint8_t y = 0x80) {
    Unit           u;
    u.info         = 0x8C2CFFA8;
    u.infoFlags    = 1;
    u.support      = kSupport;
    u.response     = 8;
    const uint16_t b = uint16_t(~pressed);
    u.cond[0] = uint8_t(b), u.cond[1] = uint8_t(b >> 8), u.cond[2] = r, u.cond[3] = l, u.cond[4] = x, u.cond[5] = y;
    u.cond[6] = u.cond[7] = 0x80;
    return u;
}

} // namespace

BS_TEST(pad_contract_decodes_buttons_triggers_and_axes) {
    Pds p = server_unit(0, {}, controller(0), 0);
    BS_CHECK(p.on == 0 && p.off == ~0u && p.press == 0 && p.release == 0);
    BS_CHECK(p.id == kSupport && p.support == kSupport && p.name == 0x8C2CFFA8 + 20 && p.info == 0x8C2CFFA8);
    BS_CHECK(p.x1 == 0 && p.y1 == 0 && p.x2 == 0 && p.y2 == 0);  // sem bits 20-21: x2/y2 ficam 0
    p = server_unit(0, {}, controller(0x4), 0);                  // A
    BS_CHECK(p.on == 0x4 && p.press == 0x4);
    // gatilho: bit 7 do byte (128 aperta, 127 nao); R -> 0x10000 (byte 2), L -> 0x20000 (byte 3)
    p = server_unit(0, {}, controller(0, 128, 127), 0);
    BS_CHECK(p.on == 0x10000 && p.r == 128 && p.l == 127);
    p = server_unit(0, {}, controller(0, 127, 128), 0);
    BS_CHECK(p.on == 0x20000 && p.r == 127 && p.l == 128);
    // eixos: byte - 128, sem arredondamento nem zona morta (a do jogo e 8C064878)
    p = server_unit(0, {}, controller(0, 0, 0, 0, 255), 0);
    BS_CHECK(p.x1 == -128 && p.y1 == 127);
    p = server_unit(0, {}, controller(0, 0, 0, 80, 176), 0);
    BS_CHECK(p.x1 == -48 && p.y1 == 48);
    // support sem os bits 16-19: r, l, x1, y1 ficam 0, mas o gatilho continua botao digital
    Unit u    = controller(0, 255, 255, 0, 0);
    u.support = 0x000006FE;
    p         = server_unit(0, {}, u, 0);
    BS_CHECK(p.r == 0 && p.l == 0 && p.x1 == 0 && p.y1 == 0 && p.on == 0x30000 && p.id == 0x000006FE);
}

BS_TEST(pad_contract_edges_use_old_from_ram) {
    Pds before;
    before.on = 0x4;  // A apertado no passo anterior (como esta na RAM)
    Pds p     = server_unit(0, before, controller(0x6), 0);
    BS_CHECK(p.old == 0x4 && p.press == 0x2 && p.release == 0);
    p = server_unit(0, before, controller(0), 0);
    BS_CHECK(p.press == 0 && p.release == 0x4);
    // o jogo zerou o on depois do servidor (8C064878 sem controle compativel): o press volta a valer
    before.on = 0;
    p         = server_unit(0, before, controller(0x4), 0);
    BS_CHECK(p.press == 0x4);
    // old_nativo (negativo): com o on decodificado antes (0x4) no lugar da RAM (0), o press some
    p = server_unit(0, before, controller(0x4), 0, kMutOldNative, 0x4);
    BS_CHECK(p.press == 0);
}

BS_TEST(pad_contract_mode_nonzero) {
    Pds before;
    before.on = 0xFFFFFFFBu;
    const Pds p = server_unit(0, before, controller(0x4), 1);
    // modo != 0: on fica ativo em 0; press = ~(on^old) | on; release = ~(on^old) | off
    BS_CHECK(p.on == 0xFFFFFFFBu && p.off == 0x4);
    BS_CHECK(p.press == (~(p.on ^ before.on) | p.on) && p.release == (~(p.on ^ before.on) | p.off));
    Pds a = server_unit(0, before, Unit{}, 1);  // sem dispositivo no modo 1
    BS_CHECK(a.on == ~0u && a.off == 0 && a.id == 0 && a.name == kNoDeviceName);
}

BS_TEST(pad_contract_absent_subunit_and_keyboard) {
    Pds before;
    before.on     = 0x4;
    before.r      = 9;
    before.extend = 0x1234;
    Unit gone;  // resposta 0xFF: porta vazia
    gone.info     = 0x8C2CFF00;
    gone.response = 0xFF;
    Pds p         = server_unit(0, before, gone, 0);
    BS_CHECK(p.id == 0 && p.support == 0 && p.on == 0 && p.off == ~0u && p.r == 0 && p.extend == 0);
    BS_CHECK(p.press == 0 && p.release == 0x4);  // as bordas do servidor sobrescrevem as da limpeza
    BS_CHECK(p.name == kNoDeviceName && p.info == 0x8C2CFF00 && p.old == 0x4);
    // controle com erro na descricao: sem dispositivo
    Unit err  = controller(0x4);
    err.error = 1;
    BS_CHECK(classify(0, err) == Kind::Absent);
    // subunidade: so info, old e nome; nada decodificado
    p = server_unit(1, before, controller(0x4), 0);
    BS_CHECK(p.on == 0x4 && p.old == 0x4 && p.r == 9 && p.name == 0x8C2CFFA8 + 20 && p.extend == 0x1234);
    BS_CHECK(classify(6, controller(0)) == Kind::Controller && classify(7, controller(0)) == Kind::Subunit);
    Unit kb      = controller(0);
    kb.infoFlags = 0x40;
    BS_CHECK(classify(0, kb) == Kind::Keyboard);
    kb.infoFlags = 0x20;
    BS_CHECK(classify(0, kb) == Kind::Controller);
}

BS_TEST(pad_contract_status_info_and_negatives) {
    BS_CHECK(status_code(0) == 0 && status_code(5) == 0 && status_code(6) == 0 && status_code(8) == 0);
    BS_CHECK(status_code(0xFF) == 0 && status_code(0xFC) == 0xFF0000FCu && status_code(0xFD) == 0xFF000000u);
    uint8_t info[kInfoBytes];
    controller_info(info, 0xFE060F00, kMutNone);
    BS_CHECK(info[0] == 1 && info[4] == 0x00 && info[5] == 0x0F && info[6] == 0x06 && info[7] == 0xFE);
    BS_CHECK(info[0x10] == 1 && info[0x11] == 0xFF && std::memcmp(info + 0x14, "Dreamcast Controller  ", 22) == 0);
    BS_CHECK(info[0x32] == 0 && std::memcmp(info + 0x34, "Produced By", 11) == 0 && info[0x70] == 0);
    BS_CHECK(info[0x74] == 0xAE && info[0x75] == 0x01 && info[0x76] == 0xF4 && info[0x77] == 0x01);
    controller_info(info, 0xFE060F00, kMutInfoNoConnect);
    BS_CHECK(info[0] == 0);
    // negativos: cada mutacao muda o resultado no caso que ela cobre
    BS_CHECK(server_unit(0, {}, controller(0x4), 0, kMutSwapAB).on == 0x2);
    BS_CHECK(server_unit(0, {}, controller(0, 127), 0, kMutTrigger127).on == 0x10000);
    BS_CHECK(server_unit(0, {}, controller(0, 127), 0).on == 0);
    // pds_load/pds_store: ida e volta byte a byte
    uint8_t raw[kUnitSize];
    for (uint32_t i = 0; i < kUnitSize; ++i) raw[i] = uint8_t(i * 7 + 3);
    uint8_t back[kUnitSize];
    pds_store(back, pds_load(raw));
    BS_CHECK(std::memcmp(raw, back, kUnitSize) == 0);
}
