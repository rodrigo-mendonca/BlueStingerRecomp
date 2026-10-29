// N8.7 (docs/N8-7-KAMUI.md, incremento 4b): a copia do canal 2 nativa (native/render/kamui/channel_dma): habilitacao,
// FIFO de poligonos x caminho de textura, comprimento fora do multiplo de 32 e destino recusado.
#include "test.h"

#include "render/kamui/channel_dma.h"

#include <utility>
#include <vector>

using bs::native::render::kamui::ChannelDma;

namespace {

struct Bus {
    std::vector<std::pair<uint32_t, uint32_t>> blocks;  // destino, primeira palavra
    uint32_t                                   refuseFrom = ~0u;
};

uint32_t word(void*, uint32_t a) { return a; }  // a origem devolve o proprio endereco
bool     sink(void* ctx, uint32_t dest, const uint32_t* blk) {
    Bus& b = *static_cast<Bus*>(ctx);
    if (b.blocks.size() >= b.refuseFrom) return false;
    b.blocks.emplace_back(dest, blk[0]);
    return blk[7] == blk[0] + 28;
}

void enable(ChannelDma& d, uint32_t src) {
    d.write(ChannelDma::kDmaor, 0x8201);
    d.write(ChannelDma::kBase + 0x2C, 0x12C1);
    d.write(ChannelDma::kBase + 0x20, src);
}

} // namespace

// sem DMAOR.DME ou CHCR2.DE, nada muda; o FIFO recebe os blocos no mesmo endereco
BS_TEST(channel_dma_needs_both_enables_and_feeds_the_fifo) {
    ChannelDma         d;
    Bus                b;
    ChannelDma::SbRegs sb{0x10000000, 64, 1};
    d.reset();
    d.write(ChannelDma::kBase + 0x2C, ChannelDma::kDe);
    BS_CHECK(d.ch2_start(sb, word, sink, &b) == ChannelDma::Result::Disabled && b.blocks.empty() && sb.c2dst == 1);
    enable(d, 0x0C100000);
    d.write(ChannelDma::kBase + 0x28, 2);  // DMATCR2
    BS_CHECK(d.ch2_start(sb, word, sink, &b) == ChannelDma::Result::Done);
    BS_CHECK(b.blocks.size() == 2 && b.blocks[0] == std::make_pair(0x10000000u, 0x0C100000u) &&
             b.blocks[1] == std::make_pair(0x10000000u, 0x0C100020u));
    BS_CHECK(d.reg(ChannelDma::kBase + 0x20) == 0x0C100040 && d.reg(ChannelDma::kBase + 0x28) == 0 &&
             (d.reg(ChannelDma::kBase + 0x2C) & ChannelDma::kTe));
    BS_CHECK(sb.c2dstat == 0x10000040 && sb.c2dlen == 0 && sb.c2dst == 0 && d.transfers() == 1);
}

// caminho de textura (bit 24): o destino avanca 32 bytes por bloco; 40 bytes copiam dois blocos inteiros
BS_TEST(channel_dma_texture_path_advances_and_rounds_up) {
    ChannelDma         d;
    Bus                b;
    ChannelDma::SbRegs sb{0x11000000, 40, 1};
    d.reset();
    enable(d, 0x0C200000);
    BS_CHECK(d.ch2_start(sb, word, sink, &b) == ChannelDma::Result::Done);
    BS_CHECK(b.blocks.size() == 2 && b.blocks[1] == std::make_pair(0x11000020u, 0x0C200020u));
    BS_CHECK(d.reg(ChannelDma::kBase + 0x20) == 0x0C200040 && sb.c2dstat == 0x11000028);
}

// destino recusado: a copia para ali e nenhum registrador muda (o erro explicito fica com quem chama)
BS_TEST(channel_dma_bad_destination_stops_without_updating) {
    ChannelDma         d;
    Bus                b;
    ChannelDma::SbRegs sb{0x11000000, 96, 1};
    b.refuseFrom = 1;
    d.reset();
    enable(d, 0x0C300000);
    BS_CHECK(d.ch2_start(sb, word, sink, &b) == ChannelDma::Result::BadDest && b.blocks.size() == 1);
    BS_CHECK(d.reg(ChannelDma::kBase + 0x20) == 0x0C300000 && !(d.reg(ChannelDma::kBase + 0x2C) & ChannelDma::kTe));
    BS_CHECK(sb.c2dstat == 0x11000000 && sb.c2dlen == 96 && sb.c2dst == 1 && d.transfers() == 0);
    BS_CHECK(ChannelDma::contains(ChannelDma::kDmaor) && ChannelDma::contains(0xFFA0003C) &&
             !ChannelDma::contains(0xFFA00042) && !ChannelDma::contains(0xFFA00044));
}
