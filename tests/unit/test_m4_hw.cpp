// Subsistemas do M4 usados pelo boot: store queues, TA, DMA do canal 2, GD-ROM (HLE),
// flash e SYSINFO. Cada teste parte do estado de reset do modulo.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "gdrom/gdrom.h"
#include "hle/bios.h"
#include "hle/flashrom.h"
#include "memory/sh4_dmac.h"
#include "memory/sh4_sq.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "pvr/vram.h"

#include <cstring>
#include <vector>

using namespace bs;

namespace {

struct Hw {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    Hw() {
        c.ram = ram.data();
        sh4_sq_reset();
        sh4_dmac_reset();
        ta_reset();
        holly_intc_reset();
        holly_sb_reset();
        gdrom_reset();
        vram_reset();
    }
    uint32_t istnrm() { return sh4_read32(&c, 0xA05F6900); }
    void     sq(uint32_t addr, std::initializer_list<uint32_t> words) {
        uint32_t i = 0;
        for (uint32_t w : words) sh4_write32(&c, (addr & ~0x1Fu) + 4 * i++, w);
        sh4_sq_flush(&c, addr);
    }
};

} // namespace

// QACR escolhe a area de destino: 0x0C -> RAM (area 3), 0x10 -> TA (area 4).
BS_TEST(sq_flush_to_ram_and_to_ta) {
    Hw h;
    sh4_write32(&h.c, 0xFF000038, 0x0C);  // QACR0: area 3
    h.sq(0xE0E48D00, {1, 2, 3, 4, 5, 6, 7, 8});
    BS_CHECK(sh4_sq_target(0xE0E48D00) == 0x0CE48D00);
    BS_CHECK(sh4_read32(&h.c, 0x8CE48D00) == 1 && sh4_read32(&h.c, 0x8CE48D1C) == 8);
    sh4_write32(&h.c, 0xFF00003C, 0x10);  // QACR1: area 4 (bit 5 do endereco = SQ1)
    h.sq(0xE0000020, {0x80000000, 0, 0, 0, 0, 0, 0, 0});  // global de poligono, lista opaca
    BS_CHECK(ta_params().size() == 1 && ta_params()[0].w[0] == 0x80000000);
}

// Lista opaca: global (32 B), vertices (32 B), fim de lista -> ISTNRM bit 7.
// Translucida com textura + cor float: vertice de 64 B em duas metades.
BS_TEST(ta_lists_and_end_of_list_interrupts) {
    Hw h;
    sh4_write32(&h.c, 0xFF000038, 0x10);
    h.sq(0xE0000000, {0x80000000});        // global opaco, sem textura
    h.sq(0xE0000000, {0xE0000000, 1, 2});  // vertice
    h.sq(0xE0000000, {0xF0000000, 3, 4});  // vertice, fim de strip
    BS_CHECK(!(h.istnrm() & 0x80));
    h.sq(0xE0000000, {0x00000000});  // fim de lista
    BS_CHECK(h.istnrm() & 0x80);
    BS_CHECK(ta_params().size() == 4 && ta_params()[1].words == 8);
    // translucida: textura (bit 3) + cor float (col_type 1, bits 5-4)
    h.sq(0xE0000000, {0x82000018});
    h.sq(0xE0000000, {0xE0000000, 10, 11, 12, 13, 14, 15, 16});  // 1a metade
    BS_CHECK(ta_params().size() == 5);
    h.sq(0xE0000000, {17, 18, 19, 20, 21, 22, 23, 24});  // 2a metade
    BS_CHECK(ta_params().size() == 6 && ta_params()[5].words == 16 && ta_params()[5].w[8] == 17);
    h.sq(0xE0000000, {0x00000000});
    BS_CHECK(h.istnrm() & 0x200);
    ta_list_init();
    BS_CHECK(ta_params().empty());
}

// Caminho direto de textura (0x11000000): escreve na VRAM pelo caminho de 64 bits.
BS_TEST(ta_texture_path_writes_vram_64bit) {
    Hw h;
    sh4_write32(&h.c, 0xFF000038, 0x10);
    h.sq(0xE1000000, {0xAAAAAAAA, 0xBBBBBBBB, 0, 0, 0, 0, 0, 0});  // -> 0x11000000
    BS_CHECK(sh4_read32(&h.c, 0xA5000000) == 0xAAAAAAAA && sh4_read32(&h.c, 0xA5400000) == 0xBBBBBBBB);
}

// SB_C2DST = 1: 64 bytes da RAM (SAR2) para o TA; no fim, CHCR2.TE, C2DST = 0,
// C2DSTAT avanca e ISTNRM bit 19. Sem DMAOR.DME/CHCR2.DE, nada acontece.
BS_TEST(dmac_channel2_to_ta) {
    Hw h;
    const uint32_t src = 0x8C100000;
    const uint32_t blk[16] = {0x81000000, 0, 0, 0, 0, 0, 0, 0, 0x00000000};
    for (int i = 0; i < 16; ++i) sh4_write32(&h.c, src + 4 * i, blk[i]);
    sh4_write32(&h.c, 0xFFA00020, src);
    sh4_write32(&h.c, 0xA05F6800, 0x10000000);
    sh4_write32(&h.c, 0xA05F6804, 64);
    sh4_write32(&h.c, 0xA05F6808, 1);  // DMA desligado: ignorado
    BS_CHECK(ta_params().empty() && sh4_read32(&h.c, 0xA05F6808) == 1);
    sh4_write32(&h.c, 0xFFA00040, 0x8201);  // DMAOR: DME
    sh4_write32(&h.c, 0xFFA0002C, 0x12C1);  // CHCR2: DE
    sh4_write32(&h.c, 0xA05F6808, 1);
    BS_CHECK(ta_params().size() == 2 && (h.istnrm() & 0x100));  // global + fim (modificador opaco)
    BS_CHECK(sh4_read32(&h.c, 0xFFA0002C) & 2);
    BS_CHECK(sh4_read32(&h.c, 0xA05F6808) == 0 && sh4_read32(&h.c, 0xA05F6804) == 0);
    BS_CHECK(sh4_read32(&h.c, 0xA05F6800) == 0x10000040 && sh4_read32(&h.c, 0xFFA00020) == src + 64);
    BS_CHECK(h.istnrm() & (1u << 19));
}

namespace {

// Disco sintetico: faixa 1 de audio, faixa 2 de dados; cada setor vale (FAD & 0xFF).
class FakeDisc final : public DiscSource {
public:
    FakeDisc() {
        tracks_.push_back({1, 1, 150, 1000, 0});
        tracks_.push_back({2, 2, 45150, 500, 4});
    }
    const std::vector<DiscTrack>& tracks() const override { return tracks_; }
    bool read(uint32_t fad, uint8_t* out) override {
        if (fad < 45150 || fad >= 45650) return false;
        std::memset(out, int(fad & 0xFF), 2048);
        return true;
    }

private:
    std::vector<DiscTrack> tracks_;
};

uint32_t gdCall(Sh4Context& c, uint32_t func, uint32_t r4 = 0, uint32_t r5 = 0) {
    c.r[0] = 0xDEADBEEF;
    gdrom_syscall(&c, func, r4, r5);
    return c.r[0];
}

} // namespace

// Ciclo de um pedido: REQ_CMD -> id; GET_CMD_STAT ocupado; EXEC_SERVER executa;
// GET_CMD_STAT concluido (uma vez) e depois ocioso.
BS_TEST(gdrom_request_lifecycle_and_reads) {
    Hw h;
    gdrom_set_disc(std::make_unique<FakeDisc>());
    const uint32_t params = 0x8C200000, stat = 0x8C200100, buf = 0x8C210000;
    sh4_write32(&h.c, params, 45150 + 16);  // FAD
    sh4_write32(&h.c, params + 4, 2);       // setores
    sh4_write32(&h.c, params + 8, buf);
    sh4_write32(&h.c, params + 12, 0);
    const uint32_t id = gdCall(h.c, 0, 0x11, params);  // DMAREAD
    BS_CHECK(id == 1);
    BS_CHECK(gdCall(h.c, 0, 0x11, params) == 0);  // ocupado: so um pedido
    BS_CHECK(gdCall(h.c, 1, id, stat) == 1);
    BS_CHECK(gdCall(h.c, 2) == 0xDEADBEEF);  // EXEC_SERVER nao mexe em r0
    BS_CHECK(sh4_read8(&h.c, buf) == ((45150 + 16) & 0xFF) && sh4_read8(&h.c, buf + 2048) == ((45150 + 17) & 0xFF));
    BS_CHECK(gdCall(h.c, 1, id, stat) == 2 && sh4_read32(&h.c, stat + 8) == 4096);
    BS_CHECK(gdCall(h.c, 1, id, stat) == 0);
    // leitura fora da faixa de dados: erro
    sh4_write32(&h.c, params, 200);
    const uint32_t bad = gdCall(h.c, 0, 0x10, params);  // PIOREAD
    gdCall(h.c, 2);
    BS_CHECK(gdCall(h.c, 1, bad, stat) == 0xFFFFFFFF);
}

// Perfil de tempo "reios" (o do oraculo): DMAREAD de 12 setores em blocos de 5. Restando
// 12 e 7 setores (> 10 240 bytes), cada bloco leva 1 000 000 ciclos; o ultimo (2 setores),
// 2 ciclos por byte = 8 192. Ocupado ate o ultimo bloco; result[2] = bytes ja copiados.
BS_TEST(gdrom_reios_timing_reads_in_timed_blocks) {
    Hw h;
    gdrom_set_disc(std::make_unique<FakeDisc>());
    gdrom_set_timing(true);
    const uint32_t params = 0x8C200000, stat = 0x8C200100, buf = 0x8C210000;
    sh4_write32(&h.c, params, 45150);
    sh4_write32(&h.c, params + 4, 12);
    sh4_write32(&h.c, params + 8, buf);
    sh4_write32(&h.c, params + 12, 0);
    const uint32_t id = gdCall(h.c, 0, 0x11, params);
    gdCall(h.c, 2);  // EXEC_SERVER: comeca no ciclo c0
    const uint64_t c0 = h.c.cycles;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 1 && sh4_read32(&h.c, stat + 8) == 0);
    BS_CHECK(sh4_read8(&h.c, buf) == 0);
    h.c.cycles = c0 + 1000000;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 1 && sh4_read32(&h.c, stat + 8) == 5 * 2048);
    BS_CHECK(sh4_read8(&h.c, buf + 4 * 2048) == ((45150 + 4) & 0xFF) && sh4_read8(&h.c, buf + 5 * 2048) == 0);
    h.c.cycles = c0 + 2000000 + 8191;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 1 && sh4_read32(&h.c, stat + 8) == 10 * 2048);
    h.c.cycles = c0 + 2000000 + 8192;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 2 && sh4_read32(&h.c, stat + 8) == 12 * 2048);
    BS_CHECK(sh4_read8(&h.c, buf + 11 * 2048) == ((45150 + 11) & 0xFF));
    gdrom_set_timing(false);
}

namespace {
// DMAREAD de `n` setores a partir de `fad` para `buf`; devolve o id (EXEC_SERVER chamado).
uint32_t dmaRead(Hw& h, uint32_t fad, uint32_t n, uint32_t buf) {
    const uint32_t params = 0x8C200000;
    sh4_write32(&h.c, params, fad);
    sh4_write32(&h.c, params + 4, n);
    sh4_write32(&h.c, params + 8, buf);
    sh4_write32(&h.c, params + 12, 0);
    const uint32_t id = gdCall(h.c, 0, 0x11, params);
    gdCall(h.c, 2);
    return id;
}
} // namespace

// Bordas do perfil reios (revisao Codex 2026-10-14): 5 setores = um bloco de 2 ciclos por
// byte (20 480); 6 setores = 1 000 000 + 4 096; 0 setores termina na hora; RESET seguido
// de pedido novo descarta a transferencia antiga; erro no meio de um bloco; e o avanco
// pelo poll central (gdrom_poll), sem nova syscall.
BS_TEST(gdrom_reios_timing_edge_cases) {
    Hw h;
    gdrom_set_disc(std::make_unique<FakeDisc>());
    gdrom_set_timing(true);
    const uint32_t stat = 0x8C200100, buf = 0x8C210000;
    // 5 setores
    uint32_t       id = dmaRead(h, 45150, 5, buf);
    uint64_t       c0 = h.c.cycles;
    h.c.cycles        = c0 + 20479;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 1);
    h.c.cycles = c0 + 20480;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 2 && sh4_read32(&h.c, stat + 8) == 5 * 2048);
    // 6 setores, avancando so pelo poll central: o buffer fica pronto sem syscall
    id = dmaRead(h, 45200, 6, buf);
    c0 = h.c.cycles;
    h.c.cycles = c0 + 1000000 + 4096;
    gdrom_poll(&h.c);
    BS_CHECK(sh4_read8(&h.c, buf + 5 * 2048) == ((45200 + 5) & 0xFF));
    BS_CHECK(gdCall(h.c, 1, id, stat) == 2 && sh4_read32(&h.c, stat + 8) == 6 * 2048);
    // 0 setores: termina dentro do EXEC_SERVER
    id = dmaRead(h, 45150, 0, buf);
    BS_CHECK(gdCall(h.c, 1, id, stat) == 2);
    // RESET no meio de uma transferencia; o pedido seguinte nao herda a antiga
    dmaRead(h, 45300, 12, buf);
    gdCall(h.c, 9);  // RESET
    sh4_write8(&h.c, buf + 2 * 2048, 0x5A);
    id = dmaRead(h, 45400, 2, buf);
    c0 = h.c.cycles;
    h.c.cycles = c0 + 8192;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 2 && sh4_read32(&h.c, stat + 8) == 2 * 2048);
    BS_CHECK(sh4_read8(&h.c, buf) == (45400 & 0xFF) && sh4_read8(&h.c, buf + 2 * 2048) == 0x5A);
    h.c.cycles += 5000000;  // os prazos da transferencia antiga passam sem efeito
    gdrom_poll(&h.c);
    BS_CHECK(sh4_read8(&h.c, buf + 2 * 2048) == 0x5A);
    // erro de leitura no meio do 1o bloco (o disco sintetico acaba no FAD 45649)
    id = dmaRead(h, 45647, 6, buf);
    h.c.cycles += 1000000;
    BS_CHECK(gdCall(h.c, 1, id, stat) == 0xFFFFFFFF);
    gdrom_set_timing(false);
}

// TOC: [faixa-1] = ctrl<<28 | adr<<24 | FAD; [99]/[100] primeira/ultima; [101] lead-out.
BS_TEST(gdrom_toc_version_and_drive_status) {
    Hw h;
    gdrom_set_disc(std::make_unique<FakeDisc>());
    const uint32_t params = 0x8C200000, toc = 0x8C220000, stat = 0x8C200100;
    sh4_write32(&h.c, params, 0);
    sh4_write32(&h.c, params + 4, toc);
    const uint32_t id = gdCall(h.c, 0, 0x13, params);  // GETTOC2, area 0
    gdCall(h.c, 2);
    BS_CHECK(gdCall(h.c, 1, id, stat) == 2);
    BS_CHECK(sh4_read32(&h.c, toc) == ((0u << 28) | (1u << 24) | 150));
    BS_CHECK(sh4_read32(&h.c, toc + 4) == ((4u << 28) | (1u << 24) | 45150));
    BS_CHECK(sh4_read32(&h.c, toc + 8) == 0xFFFFFFFF);
    BS_CHECK(sh4_read32(&h.c, toc + 99 * 4) == ((0u << 28) | (1u << 24) | (1u << 16)));
    BS_CHECK(sh4_read32(&h.c, toc + 100 * 4) == ((4u << 28) | (1u << 24) | (2u << 16)));
    BS_CHECK(sh4_read32(&h.c, toc + 101 * 4) == ((4u << 28) | (1u << 24) | 45650));
    sh4_write32(&h.c, params, 0x8C230000);
    const uint32_t v = gdCall(h.c, 0, 0x28, params);  // GET_VERSION
    gdCall(h.c, 2);
    gdCall(h.c, 1, v, stat);
    char ver[29] = {};
    for (int i = 0; i < 28; ++i) ver[i] = char(sh4_read8(&h.c, 0x8C230000 + i));
    BS_CHECK(std::memcmp(ver, "GDC Version 1.10 1999-03-31", 27) == 0 && ver[27] == 0x02);
    BS_CHECK(gdCall(h.c, 4, 0x8C240000) == 0);  // GET_DRV_STAT: pausa, GD-ROM
    BS_CHECK(sh4_read32(&h.c, 0x8C240000) == 1 && sh4_read32(&h.c, 0x8C240004) == 0x80);
}

// FLASHROM: particoes, leitura, escrita (so apaga bits) e delete (0xFF).
BS_TEST(flashrom_partitions_read_write_delete) {
    Hw h;
    uint8_t* f = flashrom_data();
    std::memset(f, 0xFF, kFlashSize);
    auto call = [&](uint32_t fn, uint32_t r4, uint32_t r5, uint32_t r6) {
        h.c.r[4] = r4, h.c.r[5] = r5, h.c.r[6] = r6, h.c.r[7] = fn;
        flashrom_syscall(&h.c);
        return h.c.r[0];
    };
    BS_CHECK(call(0, 2, 0x8C200000, 0) == 0);
    BS_CHECK(sh4_read32(&h.c, 0x8C200000) == 0x1C000 && sh4_read32(&h.c, 0x8C200004) == 0x4000);
    BS_CHECK(call(0, 5, 0x8C200000, 0) == 0xFFFFFFFF);
    sh4_write32(&h.c, 0x8C210000, 0x12345678);
    BS_CHECK(call(2, 0x1C000, 0x8C210000, 4) == 4);
    sh4_write32(&h.c, 0x8C210000, 0xFFFF0000);
    call(2, 0x1C000, 0x8C210000, 4);  // AND: 0x12345678 & 0xFFFF0000
    BS_CHECK(call(1, 0x1C000, 0x8C220000, 4) == 0 && sh4_read32(&h.c, 0x8C220000) == 0x12340000);
    BS_CHECK(call(3, 0x1C000, 0, 0) == 0 && f[0x1C000] == 0xFF);
    BS_CHECK(call(3, 0x1C001, 0, 0) == 0xFFFFFFFF);
}

// SYSINFO pelo vetor do BIOS: INIT copia ID/propriedades da flash para 0x8C000068.
BS_TEST(bios_sysinfo_via_vector) {
    Hw h;
    uint8_t* f = flashrom_data();
    for (int i = 0; i < 8; ++i) f[0x1A056 + i] = uint8_t(0x10 + i);
    for (int i = 0; i < 5; ++i) f[0x1A000 + i] = uint8_t(0x30 + i);
    bios_hle_install(&h.c, true);
    BS_CHECK(sh4_read32(&h.c, 0x8C0000B0) == 0x8C001000);
    const uint32_t vec = sh4_read32(&h.c, 0x8C0000B0);
    h.c.pr = h.c.ret_expect = 0x8C010100;  // volta como rts
    h.c.r[7] = 0;
    sh4_dispatch(&h.c, 0, vec);
    BS_CHECK(h.c.r[0] == 0 && sh4_read8(&h.c, 0x8C000068) == 0x10 && sh4_read8(&h.c, 0x8C000070) == 0x30);
    BS_CHECK(sh4_read8(&h.c, 0x8C00007F) == 0);
    h.c.r[7] = 3;
    sh4_dispatch(&h.c, 0, vec);
    BS_CHECK(h.c.r[0] == 0x8C000068);
}

// Revisao Codex: o tamanho com dois volumes vem do bit 6 (Volume / modo normal do
// modificador); o bit 7 (Shadow, habilita o modificador) nao muda o tamanho.
BS_TEST(ta_two_volume_size_comes_from_bit6_not_bit7) {
    Hw h;
    sh4_write32(&h.c, 0xFF000038, 0x10);
    h.sq(0xE0000000, {0x82000080});        // bit 7 so, sem textura: global 32 B, vertice 32 B
    h.sq(0xE0000000, {0xE0000000});
    BS_CHECK(ta_params().size() == 2 && ta_params()[1].words == 8);
    h.sq(0xE0000000, {0x00000000});
    h.sq(0xE0000000, {0x82000048});        // bit 6 + textura, cor empacotada: tipo 11, vertice 64 B
    h.sq(0xE0000000, {0xE0000000});
    BS_CHECK(ta_params().size() == 4);     // vertice ainda incompleto
    h.sq(0xE0000000, {1});
    BS_CHECK(ta_params().size() == 5 && ta_params()[4].words == 16);
}

// CD (como o CDI): todas as faixas na area 0; a area 1 (alta densidade) volta vazia, como
// no Flycast (libGDR_GetToc para disco que nao e GD).
BS_TEST(gdrom_toc_high_density_area_is_empty_for_cd) {
    Hw h;
    gdrom_set_disc(std::make_unique<FakeDisc>());
    const uint32_t params = 0x8C200000, toc = 0x8C220000;
    sh4_write32(&h.c, params, 1);
    sh4_write32(&h.c, params + 4, toc);
    const uint32_t id = gdCall(h.c, 0, 0x13, params);
    gdCall(h.c, 2);
    BS_CHECK(gdCall(h.c, 1, id, 0x8C200100) == 2);
    BS_CHECK(sh4_read32(&h.c, toc) == 0xFFFFFFFF && sh4_read32(&h.c, toc + 101 * 4) == 0xFFFFFFFF);
}

#if BS_VALIDACAO  // N8.5: o conversor YUV so serve o filme legado (validacao); no produto, test_n85_filmes
// Conversor YUV: 2x1 macroblocos YUV420 numa textura de 32x16 (UYVY linear na VRAM, caminho
// de 64 bits); TA_YUV_TEX_CNT conta; o ultimo macrobloco acende ISTNRM bit 6 e recomeca.
// Modo uma-textura-por-macrobloco com entrada YUV422. Escrever TA_YUV_TEX_BASE recomeca.
BS_TEST(ta_yuv_converter_macroblocks_to_vram) {
    Hw h;
    pvr_regs_reset();
    auto feed = [&](const std::vector<uint8_t>& mb) {
        for (size_t o = 0; o < mb.size(); o += 32) {
            uint32_t w[8];
            std::memcpy(w, mb.data() + o, 32);
            ta_fifo_write(&h.c, 0x10800000, w);
        }
    };
    // macrobloco 420: U = 0x40 + i, V = 0x80 + i, Y do bloco b = 0x10*b + i (i = indice no 8x8)
    auto mb420 = [](uint8_t tag) {
        std::vector<uint8_t> m(384);
        for (int i = 0; i < 64; ++i) m[i] = uint8_t(0x40 + i), m[64 + i] = uint8_t(tag + i);
        for (int b = 0; b < 4; ++b)
            for (int i = 0; i < 64; ++i) m[128 + 64 * b + i] = uint8_t(0x10 * b + i);
        return m;
    };
    auto uyvy = [](uint32_t u, uint32_t y0, uint32_t v, uint32_t y1) { return u | (y0 << 8) | (v << 16) | (y1 << 24); };
    auto word64 = [](uint32_t off) {
        uint32_t v;
        std::memcpy(&v, vram_data() + vram_offset64(off), 4);
        return v;
    };
    const uint32_t base = 0x00200000;
    sh4_write32(&h.c, 0xA05F814C, 0x00000001);  // CTRL: 2x1 macroblocos, uma textura, YUV420
    sh4_write32(&h.c, 0xA05F8148, base);        // BASE (recomeca)
    feed(mb420(0x80));
    BS_CHECK(sh4_read32(&h.c, 0xA05F8150) == 1 && !(h.istnrm() & 0x40));
    // pixel (0,0): U[0], Y0[0], V[0], Y0[1]; linha 9, coluna 10 (bloco Y3; croma (5,4))
    BS_CHECK(word64(base) == uyvy(0x40, 0x00, 0x80, 0x01));
    const uint32_t ci = 4 * 8 + 5, yi = 1 * 8 + 2;
    BS_CHECK(word64(base + (9 * 32 + 10) * 2) == uyvy(0x40 + ci, 0x30 + yi, 0x80 + ci, 0x30 + yi + 1));
    feed(mb420(0x90));  // 2o macrobloco: x 16-31
    BS_CHECK(word64(base + 16 * 2) == uyvy(0x40, 0x00, 0x90, 0x01));
    BS_CHECK(sh4_read32(&h.c, 0xA05F8150) == 0 && (h.istnrm() & 0x40));  // quadro completo

    // YUV422, uma textura 16x16 por macrobloco (512 bytes cada, em sequencia)
    sh4_write32(&h.c, 0xA05F814C, 0x01010001);
    sh4_write32(&h.c, 0xA05F8148, base);
    // padrao diferente em cada uma das 6 regioes (U0 V0 Y0 | U1 V1 Y1), para que outra ordem
    // nao passe (revisao Codex 2026-10-15)
    std::vector<uint8_t> m422(512);
    for (int k = 0; k < 512; ++k) m422[k] = uint8_t(k * 37 + (k >> 8) * 101 + 5);
    feed(m422);
    feed(m422);
    // esperado pelo layout do hardware: metade = linha / 8, cada metade U[64] V[64] Y[128]
    // (Y esquerdo 8x8, Y direito 8x8)
    auto expect = [&](uint32_t row, uint32_t col) {
        const uint32_t h  = (row / 8) * 256, ci = (row % 8) * 8 + col / 2;
        auto           yv = [&](uint32_t x) { return uint32_t(m422[h + 128 + (x / 8) * 64 + (row % 8) * 8 + x % 8]); };
        return uyvy(m422[h + ci], yv(col), m422[h + 64 + ci], yv(col + 1));
    };
    BS_CHECK(word64(base + 512 + (3 * 16 + 4) * 2) == expect(3, 4));      // metade de cima, Y esquerdo
    BS_CHECK(word64(base + 512 + (11 * 16 + 10) * 2) == expect(11, 10));  // metade de baixo, Y direito
    BS_CHECK(word64(base + (15 * 16 + 14) * 2) == expect(15, 14));        // 1a textura, ultima linha
}
#endif

#if BS_VALIDACAO  // N8.7: o decodificador de texels da VRAM (pvr_texel.cpp) so existe na validacao
// Textura YUV422 linear: pares de pixels na mesma palavra; cores pelos coeficientes do PowerVR2.
BS_TEST(texture_yuv422_linear) {
    vram_reset();
    BS_CHECK(pvr_yuv_to_argb(128, 128, 128) == 0xFF808080);  // cinza
    BS_CHECK(pvr_yuv_to_argb(255, 128, 255) == 0xFFFFA8FF);  // R e B saturam; G = 255 - 11*127/16
    const uint32_t word = 0x80u | (50u << 8) | (0x80u << 16) | (200u << 24);  // U, Y0, V, Y1
    std::memcpy(vram_data() + vram_offset64(0x1000), &word, 4);
    TexInfo t;
    t.tcw   = (3u << 27) | (1u << 26) | (0x1000 >> 3);  // YUV422 linear
    t.width = 8, t.height = 8;
    BS_CHECK(pvr_texture_supported(t));
    BS_CHECK(pvr_texel(t, 0, 0) == 0xFF323232 && pvr_texel(t, 1, 0) == 0xFFC8C8C8);
    TexInfo tw = t;
    tw.tcw &= ~(1u << 26);  // twiddled: ainda nao
    BS_CHECK(!pvr_texture_supported(tw));
}

// VQ: codebook de 256 x (2x2 texels 16 bits, ordem twiddled) + 1 indice por bloco 2x2
// (twiddled em w/2 x h/2). Mipmap: deslocamento do maior nivel (16 bits, paleta 8 bpp, VQ).
BS_TEST(texture_vq_and_mipmap_offsets) {
    vram_reset();
    auto put16 = [](uint32_t off, uint16_t v) { std::memcpy(vram_data() + vram_offset64(off), &v, 2); };
    auto put8  = [](uint32_t off, uint8_t v) { vram_data()[vram_offset64(off)] = v; };
    const uint32_t base = 0x4000;
    // codigo 5: texels (0,0)=0xF800 (vermelho 565), (0,1)=0x07E0 (verde), (1,0)=0x001F (azul), (1,1)=0xFFFF
    put16(base + 5 * 8 + 0, 0xF800), put16(base + 5 * 8 + 2, 0x07E0), put16(base + 5 * 8 + 4, 0x001F), put16(base + 5 * 8 + 6, 0xFFFF);
    // textura 8x8: bloco (1,0) -> indice twiddled pvr_twiddle(1,0,4,4) = 2
    put8(base + 2048 + pvr_twiddle(1, 0, 4, 4), 5);
    TexInfo t;
    t.tcw   = (1u << 30) | (1u << 27) | (base >> 3);  // VQ, 565, twiddled
    t.width = 8, t.height = 8;
    BS_CHECK(pvr_texture_supported(t));
    BS_CHECK(pvr_texel(t, 2, 0) == 0xFFFF0000 && pvr_texel(t, 2, 1) == 0xFF00FF00 && pvr_texel(t, 3, 0) == 0xFF0000FF &&
             pvr_texel(t, 3, 1) == 0xFFFFFFFF);
    // mipmap: 16 bits 8x8 -> 0x30; VQ 8x8 -> 6 indices; paleta 8 bpp 8x8 -> 3 + 21
    BS_CHECK(pvr_mip_offset(1, false, 8) == 0x30 && pvr_mip_offset(1, false, 2) == 8 && pvr_mip_offset(1, false, 1024) == 0xAAAB0);
    BS_CHECK(pvr_mip_offset(1, true, 8) == 6 && pvr_mip_offset(1, true, 4) == 2 && pvr_mip_offset(1, true, 16) == 0x16);
    BS_CHECK(pvr_mip_offset(6, false, 8) == 24);
    // 16 bits com mipmap: o texel (0,0) do maior nivel fica no deslocamento
    put16(0x8000 + 0x30, 0xF800);
    TexInfo m;
    m.tcw   = (1u << 31) | (1u << 27) | (0x8000 >> 3);
    m.width = 8, m.height = 8;
    BS_CHECK(pvr_texture_supported(m) && pvr_texel(m, 0, 0) == 0xFFFF0000);
}
#endif