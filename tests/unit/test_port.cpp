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
