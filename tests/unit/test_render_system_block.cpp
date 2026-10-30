// N8.7 (docs/N8-7-KAMUI.md, passo 5b): o system block do Holly que sobra no produto (native/render/kamui/system_block):
// revisao, ultimo valor escrito com a primeira leitura, componentes removidos e as partidas pedidas pela escrita.
#include "test.h"

#include "render/kamui/system_block.h"

using bs::native::render::kamui::SystemBlock;

// SB_SBREV e so leitura: 0x0B, nunca a primeira leitura de um registrador sem modelo
BS_TEST(system_block_revision_is_read_only) {
    SystemBlock sb;
    BS_CHECK(sb.read(SystemBlock::kSbRev).value == 0x0B && !sb.read(SystemBlock::kSbRev).first);
    BS_CHECK(sb.write(SystemBlock::kSbRev, 0) == SystemBlock::Action::None);
    BS_CHECK(sb.read(SystemBlock::kSbRev).value == 0x0B && sb.reg(SystemBlock::kSbRev) == 0);
}

// os outros devolvem o ultimo valor escrito; a primeira leitura de cada um e marcada uma vez (ate o reset)
BS_TEST(system_block_last_written_value_and_first_read) {
    SystemBlock sb;
    const SystemBlock::Read a = sb.read(0x005F688C);
    BS_CHECK(a.value == 0 && a.first);
    BS_CHECK(!sb.read(0x005F688C).first);
    BS_CHECK(sb.write(0x005F6884, 0x12345678) == SystemBlock::Action::None);
    const SystemBlock::Read b = sb.read(0x005F6884);
    BS_CHECK(b.value == 0x12345678 && b.first);
    sb.reset();
    BS_CHECK(sb.read(0x005F6884).value == 0 && sb.read(0x005F688C).first);
}

// GD-ROM e DMA do G1 sao componentes removidos, salvo a escrita de GDST = 0 (DMA parado) pela tabela do SDK
BS_TEST(system_block_removed_ranges) {
    using R = SystemBlock::Region;
    BS_CHECK(SystemBlock::removed(0x005F7000, 4, false, 0) == R::GdRom);
    BS_CHECK(SystemBlock::removed(0x005F70FC, 1, true, 0) == R::GdRom);
    BS_CHECK(SystemBlock::removed(0x005F7100, 4, false, 0) == R::None);
    BS_CHECK(SystemBlock::removed(0x005F7418, 4, true, 0) == R::None);
    BS_CHECK(SystemBlock::removed(0x005F7418, 4, true, 1) == R::G1);
    BS_CHECK(SystemBlock::removed(0x005F7418, 4, false, 0) == R::G1);
    BS_CHECK(SystemBlock::removed(0x005F7418, 2, true, 0) == R::G1);
    BS_CHECK(SystemBlock::removed(0x005F7404, 4, true, 0) == R::G1);
    BS_CHECK(SystemBlock::removed(0x005F689C, 4, false, 0) == R::None);
    BS_CHECK(SystemBlock::contains(0x005F6800, 4) && SystemBlock::contains(0x005F7FFC, 4));
    BS_CHECK(!SystemBlock::contains(0x005F8000, 4) && !SystemBlock::contains(0x005F6800, 2));
}

// SB_C2DST com o bit 0 pede a copia do canal 2 (que depois avanca SB_C2DSTAT e zera os outros); os registradores de
// partida do G2 com o bit 0 pedem o DMA do G2 (removido no produto); com 0, nada
BS_TEST(system_block_write_actions) {
    using A = SystemBlock::Action;
    SystemBlock sb;
    BS_CHECK(sb.write(SystemBlock::kC2Dstat, 0x10000000) == A::None);
    BS_CHECK(sb.write(SystemBlock::kC2Dlen, 64) == A::None);
    BS_CHECK(sb.write(SystemBlock::kC2Dst, 0) == A::None);
    BS_CHECK(sb.write(SystemBlock::kC2Dst, 1) == A::Ch2Start);
    sb.at(SystemBlock::kC2Dstat) += 64, sb.at(SystemBlock::kC2Dlen) = 0, sb.at(SystemBlock::kC2Dst) = 0;
    BS_CHECK(sb.read(SystemBlock::kC2Dstat).value == 0x10000040 && sb.read(SystemBlock::kC2Dst).value == 0);
    for (uint32_t p : {0x005F7818u, 0x005F7838u, 0x005F7858u, 0x005F7878u}) {
        BS_CHECK(sb.write(p, 0) == A::None);
        BS_CHECK(sb.write(p, 1) == A::G2Start);
    }
    BS_CHECK(sb.write(0x005F781C, 1) == A::None);
}
