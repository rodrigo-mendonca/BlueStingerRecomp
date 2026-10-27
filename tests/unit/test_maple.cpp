// Barramento Maple (runtime/maple/maple.cpp) pelo caminho do jogo: tabela na RAM,
// registradores do system block e ISTNRM bit 12.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "maple/maple.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"

#include <cstring>
#include <vector>

using namespace bs;

namespace {

// N8.4: o barramento do produto e o servico sem dispositivos; estes testes exercitam o modelo do Maple (validacao)
struct ModelBus {
    ModelBus() { periph_use(maple_bus_backend()); }
    ~ModelBus() { periph_use(nullptr); }
};

struct MapleHw {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    MapleHw() {
        c.ram = ram.data();
        holly_intc_reset();
        holly_sb_reset();
        sh4_write32(&c, 0xA05F6C14, 1);           // SB_MDEN
        sh4_write32(&c, 0xA05F6C04, 0x0C100000);  // SB_MDSTAR
    }
    void     w(uint32_t a, uint32_t v) { sh4_write32(&c, 0xA0000000 | a, v); }
    uint32_t r(uint32_t a) { return sh4_read32(&c, 0xA0000000 | a); }
    void     start() { sh4_write32(&c, 0xA05F6C18, 1); }
    uint32_t mdst() { return sh4_read32(&c, 0xA05F6C18); }
    uint32_t istnrm() { return sh4_read32(&c, 0xA05F6900); }
};

} // namespace

// Porta A: informacao do dispositivo (controle); porta B: ninguem responde.
BS_TEST(maple_devinfo_port_a_and_empty_port_b) {
    ModelBus bus;
    MapleHw h;
    h.w(0x0C100000, 0x00000000);              // porta 0, 0 palavras de dados
    h.w(0x0C100004, 0x0C200000);              // buffer de recepcao
    h.w(0x0C100008, 0x00002001);              // cmd 01, destino 0x20, origem 0x00
    h.w(0x0C10000C, 0x80010000);              // ultima, porta 1
    h.w(0x0C100010, 0x0C200400);
    h.w(0x0C100014, 0x00006001 | (0x40u << 16));  // destino 0x60, origem 0x40
    h.start();
    BS_CHECK(h.r(0x0C200000) == 0x1C200005);  // resposta 05, para 0x00, de 0x20, 28 palavras
    BS_CHECK(h.r(0x0C200004) == 0x01000000);  // funcao: controle
    BS_CHECK(h.r(0x0C200008) == 0xFE060F00);
    BS_CHECK(std::memcmp(h.ram.data() + 0x200000 + 4 + 18, "Dreamcast Controller  ", 22) == 0);
    BS_CHECK(h.r(0x0C200070) == 0x01F401AE);  // consumo em espera e maximo
    BS_CHECK(h.r(0x0C200400) == 0xFFFFFFFF);  // sem dispositivo
}

// Condicao do controle: botoes ativos em 0, analogicos no centro.
BS_TEST(maple_get_condition_reports_controller_state) {
    ModelBus bus;
    MapleHw h;
    MapleControllerState s;
    s.buttons = 0xFFFF & ~0x0008u;  // START
    maple_set_controller(s);
    h.w(0x0C100000, 0x80000001);              // ultima, porta 0, 1 palavra de dados
    h.w(0x0C100004, 0x0C200000);
    h.w(0x0C100008, 0x01002009);              // cmd 09, destino 0x20, 1 palavra
    h.w(0x0C10000C, 0x01000000);              // funcao: controle
    h.start();
    BS_CHECK(h.r(0x0C200000) == 0x03200008);
    BS_CHECK(h.r(0x0C200004) == 0x01000000);
    BS_CHECK(h.r(0x0C200008) == 0x0000FFF7);
    BS_CHECK(h.r(0x0C20000C) == 0x80808080);
}

// Unidade de expansao (VMU) ausente: sem resposta; funcao nao suportada: FE.
BS_TEST(maple_absent_subunit_and_unsupported_function) {
    ModelBus bus;
    MapleHw h;
    h.w(0x0C100000, 0x00000001);
    h.w(0x0C100004, 0x0C200000);
    h.w(0x0C100008, 0x01000109);              // destino 0x01: VMU do slot 1
    h.w(0x0C10000C, 0x02000000);
    h.w(0x0C100010, 0x80000001);
    h.w(0x0C100014, 0x0C200100);
    h.w(0x0C100018, 0x01002009);
    h.w(0x0C10001C, 0x02000000);              // funcao de memoria no controle
    h.start();
    BS_CHECK(h.r(0x0C200000) == 0xFFFFFFFF);
    BS_CHECK(h.r(0x0C200100) == 0x002000FE);
}

// SB_MDST le 1 enquanto o DMA corre; no fim, 0 e ISTNRM bit 12.
BS_TEST(maple_dma_busy_until_completion_interrupt) {
    ModelBus bus;
    MapleHw h;
    h.w(0x0C100000, 0x80000000);
    h.w(0x0C100004, 0x0C200000);
    h.w(0x0C100008, 0x00002001);
    h.start();
    BS_CHECK(h.mdst() == 1);
    BS_CHECK((h.istnrm() & kIstMapleDma) == 0);
    h.c.cycles += 1000000;
    BS_CHECK(h.mdst() == 0);
    BS_CHECK((h.istnrm() & kIstMapleDma) != 0);
}

// Sem SB_MDEN o inicio e ignorado.
BS_TEST(maple_start_ignored_when_disabled) {
    ModelBus bus;
    MapleHw h;
    sh4_write32(&h.c, 0xA05F6C14, 0);
    h.w(0x0C100000, 0x80000000);
    h.w(0x0C100004, 0x0C200000);
    h.w(0x0C100008, 0x00002001);
    h.start();
    BS_CHECK(h.mdst() == 0 && h.r(0x0C200000) == 0);
}

// O DMA segue os descritores ate o bit LAST, mesmo com mais de 8 (revisao Codex 2026-10-14).
BS_TEST(maple_follows_descriptors_until_last) {
    ModelBus bus;
    MapleHw h;
    uint32_t a = 0x0C100000;
    for (uint32_t n = 0; n < 10; ++n) {
        h.w(a, (n == 9 ? 0x80000000u : 0) | ((n & 3) << 16));  // portas 0-3 alternadas
        h.w(a + 4, 0x0C200000 + n * 0x100);
        h.w(a + 8, 0x00000001 | ((((n & 3) << 6) | 0x20) << 8) | ((n & 3) << 6 << 16));
        a += 12;
    }
    h.start();
    BS_CHECK(h.r(0x0C200000) == 0x1C200005);  // n = 0: controle da porta A
    BS_CHECK(h.r(0x0C200800) == 0x1C200005);  // n = 8: porta A de novo
    BS_CHECK(h.r(0x0C200900) == 0xFFFFFFFF);  // n = 9 (LAST): porta B vazia
}
