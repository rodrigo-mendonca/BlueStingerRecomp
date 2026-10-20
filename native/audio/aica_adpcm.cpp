#include "audio/aica_adpcm.h"

#include <algorithm>

namespace bs::native::audio {

namespace {
constexpr int kDiff[16] = {1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15};
constexpr int kStep[16] = {230, 230, 230, 230, 307, 409, 512, 614, 230, 230, 230, 230, 307, 409, 512, 614};

int16_t step(uint8_t nib, AicaAdpcmState& st) {
    st.signal = std::clamp(st.signal + st.step * kDiff[nib & 15] / 8, -32768, 32767);
    st.step   = std::clamp(st.step * kStep[nib & 15] / 256, 127, 24576);
    return int16_t(st.signal);
}
} // namespace

void aica_adpcm_decode(std::span<const uint8_t> data, std::vector<int16_t>& out, AicaAdpcmState& st, size_t samples) {
    const size_t n = std::min(samples, data.size() * 2);
    out.reserve(out.size() + n);
    for (size_t i = 0; i < n; ++i) out.push_back(step(uint8_t(i & 1 ? data[i / 2] >> 4 : data[i / 2] & 15), st));
}

std::vector<uint8_t> aica_adpcm_encode(std::span<const int16_t> pcm, AicaAdpcmState& st) {
    std::vector<uint8_t> out((pcm.size() + 1) / 2, 0);
    for (size_t i = 0; i < pcm.size(); ++i) {
        // o nibble cujo resultado fica mais perto da amostra (o decodificador e o juiz)
        int best = 0, bestErr = 1 << 30;
        for (int nib = 0; nib < 16; ++nib) {
            AicaAdpcmState t = st;
            const int      e = std::abs(int(step(uint8_t(nib), t)) - int(pcm[i]));
            if (e < bestErr) best = nib, bestErr = e;
        }
        step(uint8_t(best), st);
        out[i / 2] |= uint8_t(i & 1 ? best << 4 : best);
    }
    return out;
}

} // namespace bs::native::audio
