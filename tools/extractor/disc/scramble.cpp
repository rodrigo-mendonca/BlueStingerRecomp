#include "disc/scramble.h"

#include <cstring>

namespace bs::disc {
namespace {

constexpr size_t kMaxChunk = 2048 * 1024;
constexpr size_t kSlice    = 32;

// O arquivo e dividido em blocos de 2 MB, depois 1 MB, ... ate 32 bytes. Dentro
// de cada bloco, as fatias de 32 bytes sao permutadas por um PRNG semeado com o
// tamanho do arquivo. Sobra < 32 bytes no fim, que nao e alterada.
template <bool kDescramble>
std::vector<uint8_t> transform(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out(in.size());
    uint32_t             seed = static_cast<uint32_t>(in.size()) & 0xFFFF;
    auto                 rnd  = [&] {
        seed = (seed * 2109 + 9273) & 0x7FFF;
        return (seed + 0xC000) & 0xFFFF;
    };
    std::vector<uint32_t> idx(kMaxChunk / kSlice);
    size_t                pos  = 0;
    size_t                left = in.size();
    for (size_t chunk = kMaxChunk; chunk >= kSlice; chunk >>= 1) {
        while (left >= chunk) {
            const size_t slices = chunk / kSlice;
            for (size_t i = 0; i < slices; ++i) idx[i] = static_cast<uint32_t>(i);
            size_t seq = 0;  // fatia sequencial no lado embaralhado
            for (size_t i = slices; i-- > 0;) {
                const size_t x = (size_t(rnd()) * i) >> 16;
                std::swap(idx[i], idx[x]);
                const size_t scrambled = pos + kSlice * seq++;
                const size_t plain     = pos + kSlice * idx[i];
                if constexpr (kDescramble)
                    std::memcpy(&out[plain], &in[scrambled], kSlice);
                else
                    std::memcpy(&out[scrambled], &in[plain], kSlice);
            }
            pos += chunk;
            left -= chunk;
        }
    }
    if (left) std::memcpy(&out[pos], &in[pos], left);
    return out;
}

} // namespace

std::vector<uint8_t> descramble(const std::vector<uint8_t>& in) { return transform<true>(in); }
std::vector<uint8_t> scramble(const std::vector<uint8_t>& in) { return transform<false>(in); }

} // namespace bs::disc
