#include "debug/clock_log.h"

#include <cstdio>
#include <cstdlib>

namespace bs {

bool g_clock_log = false;

namespace {

struct State {
    std::FILE* f = nullptr;
    uint64_t   n[static_cast<int>(ClockRead::Count)] = {};
    uint64_t   h[static_cast<int>(ClockRead::Count)] = {};
    ~State() {
        if (f) std::fclose(f);
    }
};

State& st() {
    static State s;
    return s;
}

// FNV-1a de 64 bits sobre o valor (a ordem das leituras entra no hash)
void mix(uint64_t& h, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 0x100000001B3ull;
    }
}

const bool g_init = [] {
    if (const char* p = std::getenv("BS_CLOCK_LOG")) clock_log_init(p);
    return true;
}();

} // namespace

void clock_log_init(const std::string& path) {
    State& s = st();
    if (s.f) std::fclose(s.f);
    s.f         = path.empty() ? nullptr : std::fopen(path.c_str(), "w");
    g_clock_log = s.f != nullptr;
    for (int i = 0; i < static_cast<int>(ClockRead::Count); ++i) s.n[i] = 0, s.h[i] = 0xCBF29CE484222325ull;
    if (s.f) std::fprintf(s.f, "vblank\ttmu_n\ttmu_h\tspg_n\tspg_h\tmdst_n\tmdst_h\trtc_n\trtc_h\tist_n\tist_h\taud_n\taud_h\taudrd_n\taudrd_h\n");
}

void clock_log_read(ClockRead kind, uint32_t value) {
    State& s = st();
    ++s.n[static_cast<int>(kind)];
    mix(s.h[static_cast<int>(kind)], value);
}

void clock_log_vblank(uint64_t vblank) {
    State& s = st();
    if (!s.f) return;
    std::fprintf(s.f, "%llu", static_cast<unsigned long long>(vblank));
    for (int i = 0; i < static_cast<int>(ClockRead::Count); ++i) {
        std::fprintf(s.f, "\t%llu\t%016llX", static_cast<unsigned long long>(s.n[i]), static_cast<unsigned long long>(s.h[i]));
        s.n[i] = 0;
        s.h[i] = 0xCBF29CE484222325ull;
    }
    std::fputc('\n', s.f);
}

} // namespace bs
