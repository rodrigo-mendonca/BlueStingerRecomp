#include "debug/call_trace.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>

namespace bs {
namespace {

constexpr int kMax = 64;

struct State {
    std::mutex                    mu;
    std::string                   path;
    std::array<uint32_t, kMax>    addr{};
    std::array<Sh4Func, kMax>     orig{};
    int                           count = 0;
    std::ostringstream            out;
    uint64_t                      seq = 0;
    bool                          atexitDone = false;
};

State& st() {
    static State s;
    return s;
}

std::string guestString(Sh4Context* c, uint32_t a) {
    if (!sh4_is_ram(a)) return {};
    std::string s;
    for (uint32_t i = 0; i < 64; ++i) {
        const uint8_t ch = c->ram[(a + i) & kRamMask];
        if (!ch) break;
        if (ch < 0x20 || ch > 0x7E) return {};
        s += char(ch);
    }
    return s.size() >= 2 ? s : std::string();
}

template <int I>
void thunk(Sh4Context* c) {
    State&         s      = st();
    const uint32_t caller = c->cur_fn;
    const uint32_t a[4]   = {c->r[4], c->r[5], c->r[6], c->r[7]};
    s.orig[I](c);
    std::lock_guard lk(s.mu);
    char line[160];
    std::snprintf(line, sizeof line, "%llu\t%08X\t%08X\t%08X\t%08X\t%08X\t%08X\t%08X\t",
                  static_cast<unsigned long long>(s.seq++), s.addr[I], caller, a[0], a[1], a[2], a[3], c->r[0]);
    s.out << line;
    for (int k = 0; k < 4; ++k) {
        const std::string str = guestString(c, a[k]);
        if (!str.empty()) s.out << "r" << (4 + k) << "=\"" << str << "\" ";
    }
    s.out << '\n';
}

template <int... I>
constexpr std::array<Sh4Func, kMax> thunks(std::integer_sequence<int, I...>) {
    return {&thunk<I>...};
}
const std::array<Sh4Func, kMax> kThunks = thunks(std::make_integer_sequence<int, kMax>{});

void atExit() { call_trace_flush(); }

} // namespace

void call_trace_install(const std::string& path, const std::vector<uint32_t>& addrs) {
    State& s = st();
    for (int i = 0; i < s.count; ++i) mods::replace(s.addr[i], nullptr);
    s.count = 0;
    s.path  = path;
    s.out.str({});
    s.seq = 0;
    if (path.empty()) return;
    for (uint32_t a : addrs) {
        if (s.count == kMax) break;
        const Sh4Func o = sh4_lookup(a);
        if (!o) continue;
        s.addr[s.count] = a;
        s.orig[s.count] = o;
        if (mods::replace(a, kThunks[size_t(s.count)])) ++s.count;
    }
    if (!s.atexitDone) {
        std::atexit(atExit);
        s.atexitDone = true;
    }
    std::fprintf(stderr, "[call-trace] %d funcoes envolvidas%s\n", s.count,
                 mods::has_slot(s.count ? s.addr[0] : 0) ? "" : " (sem slots: so chamadas indiretas)");
}

void call_trace_install() {
    const char* env = std::getenv("BS_CALL_TRACE");
    if (!env || !*env) return;
    const std::string v = env;
    const size_t      semi = v.find(';');
    if (semi == std::string::npos) return;
    std::vector<uint32_t> addrs;
    std::stringstream     ss(v.substr(semi + 1));
    std::string           tok;
    while (std::getline(ss, tok, ','))
        if (!tok.empty()) addrs.push_back(uint32_t(std::stoul(tok, nullptr, 16)));
    call_trace_install(v.substr(0, semi), addrs);
}

bool call_trace_flush() {
    State&          s = st();
    std::lock_guard lk(s.mu);
    if (s.path.empty()) return false;
    FILE* f = std::fopen(s.path.c_str(), "wb");
    if (!f) return false;
    const std::string text = "seq\tfuncao\tchamador\tr4\tr5\tr6\tr7\tr0\tstrings\n" + s.out.str();
    const bool        ok   = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace bs
