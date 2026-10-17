// Registradores do SH-4 modelados no runtime (runtime/memory/), pelo caminho de memoria do guest.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "memory/sh4_ccn.h"
#include "memory/sh4_port.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "pvr/pvr_regs.h"

#include <vector>

using namespace bs;

namespace {

struct PortFixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    PortFixture() {
        c.ram = ram.data();
        sh4_port_reset();
        sh4_port_set_cable(VideoCable::Composite);
    }
};

} // namespace

// Boot do Blue Stinger (8C15E86E): mov.w @(0xFF800030),r4; and #0x0300 -> tipo de cabo.
BS_TEST(port_pdtra_reports_cable_on_input_pins) {
    PortFixture f;
    BS_CHECK((sh4_read16(&f.c, 0xFF800030) & 0x0300) == 0x0300);
    sh4_port_set_cable(VideoCable::Vga);
    BS_CHECK((sh4_read16(&f.c, 0xFF800030) & 0x0300) == 0x0000);
    sh4_port_set_cable(VideoCable::Rgb);
    BS_CHECK((sh4_read16(&f.c, 0x1F800030) & 0x0300) == 0x0200);  // espelho da area 7
}

BS_TEST(port_pdtra_output_pins_return_written_value) {
    PortFixture f;
    sh4_write32(&f.c, 0xFF80002C, 0x00050005);  // pinos 0, 1, 8 e 9 como saida
    BS_CHECK(sh4_read32(&f.c, 0xFF80002C) == 0x00050005);
    sh4_write16(&f.c, 0xFF800030, 0xFFFF);
    // pinos de saida devolvem o escrito (cabo mascarado); entradas restantes leem 0
    BS_CHECK(sh4_read16(&f.c, 0xFF800030) == 0x0303);
    sh4_write16(&f.c, 0xFF800030, 0x0000);
    BS_CHECK(sh4_read16(&f.c, 0xFF800030) == 0x0000);
}

// Boot do Blue Stinger (8C010000): CCR = (CCR & 0x89AF) | 0x800 (ICI); ICI e OCI leem 0.
BS_TEST(ccn_ccr_write_only_bits_read_as_zero) {
    PortFixture f;
    sh4_ccn_reset();
    BS_CHECK(sh4_read32(&f.c, 0xFF00001C) == 0);
    sh4_write32(&f.c, 0xFF00001C, 0x0000090D);
    BS_CHECK(sh4_read32(&f.c, 0xFF00001C) == 0x00000105);
    sh4_write32(&f.c, 0xF4000020, 0);  // invalidacao do OC pelo array de enderecos: aceita
    BS_CHECK(sh4_read32(&f.c, 0xF4000020) == 0);
}

// Boot do Blue Stinger (8C172620): espera SPG_STATUS.vsync (bit 13) ligar e depois desligar.
BS_TEST(pvr_spg_status_vsync_toggles_once_per_field) {
    PortFixture f;
    pvr_regs_reset();
    sh4_write32(&f.c, 0xA05F80D8, (262u << 16) | 857);  // SPG_LOAD: 263 linhas
    sh4_write32(&f.c, 0xA05F80DC, (21u << 16) | 240);   // SPG_VBLANK: de 240 a 21
    const uint64_t field = 200000000ull * 1001 / 60000;
    int rises = 0;
    bool prev = false;
    for (uint64_t t = field * 100 / 263; t < field * 100 / 263 + 3 * field; t += 997) {
        f.c.cycles = t;
        const uint32_t st = sh4_read32(&f.c, 0xA05F810C);
        BS_CHECK((st & 0x3FF) < 263);
        const bool vs = (st >> 13) & 1;
        rises += vs && !prev;
        prev = vs;
    }
    BS_CHECK(rises == 3);
    sh4_write32(&f.c, 0xA05F810C, 0xFFFFFFFF);  // so leitura
    f.c.cycles = field / 2;
    BS_CHECK(((sh4_read32(&f.c, 0xA05F810C) >> 13) & 1) == 0);
}

// N2b-2: alvo das esperas que leem o SPG (8C172624, 8C1C6D08). A linha tem de mudar exatamente no
// instante devolvido (nem antes, nem um campo depois: o defeito do arredondamento no limite de
// linha fazia a espera da linha 0 saltar um campo inteiro com 525 linhas), e andar de troca em
// troca percorre cada linha do campo uma vez.
BS_TEST(pvr_spg_next_line_change_is_exact) {
    PortFixture f;
    // (linhas, linha e campo deixados pelo BIOS em SPG_STATUS -> fase, entrelacado)
    struct Setup {
        uint32_t lines, biosLine, biosField;
        bool     interlace;
    };
    for (const Setup s : {Setup{263, 0, 0, false}, Setup{525, 0, 0, false}, Setup{263, 123, 1, true},
                          Setup{525, 524, 1, true}, Setup{525, 0, 1, true}}) {
        const uint32_t lines = s.lines;
        pvr_regs_reset();
        std::vector<uint32_t> bank(0x2000 / 4, 0);
        bank[(0x005F80D8 - 0x005F8000) / 4] = ((lines - 1) << 16) | 857;               // SPG_LOAD
        bank[(0x005F80DC - 0x005F8000) / 4] = (21u << 16) | 240;                        // SPG_VBLANK
        bank[(0x005F80D0 - 0x005F8000) / 4] = s.interlace ? 0x10 : 0;                   // SPG_CONTROL
        bank[(0x005F810C - 0x005F8000) / 4] = s.biosLine | (s.biosField << 10);         // SPG_STATUS: fase
        pvr_regs_load(bank.data());
        const uint64_t field = 200000000ull * 1001 / 60000;
        auto           status = [&](uint64_t t) {
            f.c.cycles = t;
            return sh4_read32(&f.c, 0xA05F810C);
        };
        std::vector<uint64_t> probes;
        for (uint64_t after = 0; after < 2 * field; after += 7919) probes.push_back(after);
        // os instantes dos eventos de VBlank (inicio de linha truncado de pvr_spg_next_crossing): e ai
        // que a espera fica parada quando o tempo anda de evento em evento
        for (uint32_t l = 0; l < lines; ++l) probes.push_back(pvr_spg_next_crossing(field, l));
        for (uint64_t after : probes) {
            const uint64_t t    = pvr_spg_next_line_change(after);
            const uint32_t from = pvr_spg_position(after).line;
            BS_CHECK(t > after);
            BS_CHECK(t - after <= field / lines + 2);
            BS_CHECK(pvr_spg_position(t).line == (from + 1) % lines);
            BS_CHECK(pvr_spg_position(t - 1).line == from);
            // SPG_STATUS inteiro (linha, vsync, campo) parado ate t e diferente em t
            BS_CHECK(status(t - 1) == status(after));
            BS_CHECK(status(t) != status(after));
        }
        // de troca em troca: da primeira linha 0 ate a seguinte, cada linha do campo uma vez
        uint64_t t     = 0;
        uint32_t steps = 0;
        while (pvr_spg_position(t).line != 0 && steps++ < 2 * lines) t = pvr_spg_next_line_change(t);
        steps = 0;
        do t = pvr_spg_next_line_change(t), ++steps;
        while (pvr_spg_position(t).line != 0 && steps < 2 * lines);
        BS_CHECK(steps == lines);
    }
}

// Boot do Blue Stinger (8C1727F6): limpa ISTNRM.VBlankIn (escrevendo 8) e espera ele voltar.
BS_TEST(holly_istnrm_vblank_in_sets_once_per_field_and_clears_on_write) {
    PortFixture f;
    pvr_regs_reset();
    holly_intc_reset();
    sh4_write32(&f.c, 0xA05F80D8, (262u << 16) | 857);  // SPG_LOAD: 263 linhas
    sh4_write32(&f.c, 0xA05F80CC, (21u << 16) | 240);   // SPG_VBLANK_INT: out 21, in 240
    const uint64_t field = 200000000ull * 1001 / 60000;
    f.c.cycles = field / 10;  // linha ~26: passou do VBlank-Out, antes do VBlank-In
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x18) == 0x10);
    sh4_write32(&f.c, 0xA05F6900, 0x10);
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x18) == 0);
    f.c.cycles = field * 240 / 263 + 1000;  // logo depois da linha 240
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x08) == 0x08);
    sh4_write32(&f.c, 0xA05F6900, 0x08);
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x08) == 0);
    f.c.cycles += field / 2;  // cruza a linha 21 do campo seguinte, nao a 240
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x18) == 0x10);
    f.c.cycles += field;  // um campo inteiro: VBlank-In de novo
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x08) == 0x08);
}

// Boot do Blue Stinger (8C1C59D6): le ID e REVISION do PVR e escolhe o caminho pela revisao.
BS_TEST(pvr_id_and_revision_are_read_only) {
    PortFixture f;
    pvr_regs_reset();
    BS_CHECK(sh4_read32(&f.c, 0xA05F8000) == 0x17FD11DB);
    BS_CHECK(sh4_read32(&f.c, 0xA05F8004) == 0x00000011);
    sh4_write32(&f.c, 0xA05F8004, 0);
    BS_CHECK(sh4_read32(&f.c, 0xA05F8004) == 0x00000011);
}

// Boot do Blue Stinger (8C1C41DC): le SB_SBREV; o resto do system block guarda o escrito.
BS_TEST(holly_sb_revision_and_latched_registers) {
    PortFixture f;
    holly_sb_reset();
    holly_intc_reset();
    BS_CHECK(sh4_read32(&f.c, 0xA05F689C) == 0x0000000B);
    sh4_write32(&f.c, 0xA05F689C, 0);
    BS_CHECK(sh4_read32(&f.c, 0xA05F689C) == 0x0000000B);
    sh4_write32(&f.c, 0xA05F6800, 0x11FF0000);  // SB_C2DSTAT
    BS_CHECK(sh4_read32(&f.c, 0xA05F6800) == 0x11FF0000);
    sh4_write32(&f.c, 0xA05F6910, 0x1234);  // IML2NRM: tratado pelo controlador de interrupcoes
    BS_CHECK(sh4_read32(&f.c, 0xA05F6910) == 0x1234);
}

// Revisao Codex M4: reprogramar SPG_VBLANK_INT nao pode gerar evento em tempo ja passado.
BS_TEST(holly_istnrm_spg_reprogram_is_not_retroactive) {
    PortFixture f;
    pvr_regs_reset();
    holly_intc_reset();
    sh4_write32(&f.c, 0xA05F80D8, (262u << 16) | 857);
    sh4_write32(&f.c, 0xA05F80CC, (200u << 16) | 250);  // in 250, out 200
    const uint64_t field = 200000000ull * 1001 / 60000;
    f.c.cycles = field * 100 / 263;                      // linha ~100: nada cruzado
    sh4_write32(&f.c, 0xA05F80CC, (150u << 16) | 50);   // agora in 50, out 150 (50 ja passou)
    f.c.cycles = field * 120 / 263;
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x18) == 0);
    f.c.cycles = field * 160 / 263;
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 0x18) == 0x10);
}
