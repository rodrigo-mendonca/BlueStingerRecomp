#include "debug/cycle_noise.h"

#include <cstdlib>

namespace bs {

void cycle_noise(Sh4Context* c) {
    static const uint64_t kNoise = [] {
        const char* v = std::getenv("BS_CICLOS_RUIDO");
        return v && *v ? std::strtoull(v, nullptr, 10) : 0ull;
    }();
    if (!kNoise) return;
    static uint64_t x = 0x9E3779B97F4A7C15ull;
    x ^= x << 13, x ^= x >> 7, x ^= x << 17;
    c->cycles += x % kNoise;
}

} // namespace bs
