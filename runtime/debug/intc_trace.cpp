#include "debug/intc_trace.h"

#include "debug/event_trace.h"
#include "pvr/holly_intc.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <vector>

namespace bs {

bool g_intc_trace = false;

namespace {

struct Entry {
    uint64_t   pos, step, vblank, cycles;
    char       kind;
    IntcSource src;
    uint32_t   a, b, ist, iml6, iml4, iml2;
    uint64_t   n;
};

struct State {
    std::mutex         mu;
    std::vector<Entry> entries;
    std::string        path;
    uint64_t           step       = 0;
    bool               atexitDone = false;
};

State& state() {
    static State s;
    return s;
}

const char* srcName(IntcSource s) {
    static const char* const k[] = {"update", "raise", "ISTNRM", "ISTEXT", "ISTERR", "TMU", "SPG", "MDST", "RTC", "IST",
                                    "load_pedido", "load_fim", "save_pedido", "save_fim", "cadencia", "forcado", "ldc_sr",
                                    "hardware", "sleep"};
    return static_cast<size_t>(s) < sizeof k / sizeof k[0] ? k[static_cast<size_t>(s)] : "?";
}

uint64_t pos() { return g_event_trace ? event_trace_count() : 0; }

uint8_t (*g_originFn)() = nullptr;

void atExit() { intc_trace_flush(); }

const bool kInit = [] {
    if (const char* p = std::getenv("BS_INTC_TRACE"); p && *p) intc_trace_init(p);
    return true;
}();

} // namespace

void intc_trace_init(const std::string& path) {
    State& s = state();
    {
        std::lock_guard lk(s.mu);
        s.entries.clear();
        s.path = path;
        s.step = 0;
        if (!path.empty() && !s.atexitDone) {
            std::atexit(atExit);
            s.atexitDone = true;
        }
    }
    g_intc_trace = !path.empty();
}

void intc_trace_step(uint64_t step) {
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.step = step;
}

void intc_trace_holly(char kind, IntcSource src, uint64_t cycles, uint32_t a, uint32_t b, uint32_t ist, uint32_t iml6,
                      uint32_t iml4, uint32_t iml2) {
    const uint64_t  p = pos();
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.entries.push_back({p, s.step, holly_vblank_count(), cycles, kind, src, a, b, ist, iml6, iml4, iml2,
                         kind != 'H' ? 1u : g_originFn ? g_originFn() : 0u});
}

void intc_trace_set_origin_fn(uint8_t (*fn)()) { g_originFn = fn; }

void intc_trace_service(IntcSource origin, uint64_t cycles, uint32_t holly_events, uint32_t deliveries, uint64_t blocks) {
    const uint64_t  p = pos();
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.entries.push_back({p, s.step, holly_vblank_count(), cycles, 'S', origin, holly_events, deliveries, 0, 0, 0, 0, blocks});
}

void intc_trace_clock(IntcSource src, uint32_t value) {
    const uint64_t  p = pos();
    State&          s = state();
    std::lock_guard lk(s.mu);
    if (!s.entries.empty()) {
        Entry& e = s.entries.back();
        if (e.kind == 'C' && e.src == src && e.pos == p) {  // a mesma fonte, nada no meio: soma na linha
            e.b = value;
            ++e.n;
            return;
        }
    }
    s.entries.push_back({p, s.step, holly_vblank_count(), 0, 'C', src, value, value, 0, 0, 0, 0, 1});
}

void intc_trace_card(IntcSource src, uint32_t result, uint32_t drive) {
    const uint64_t  p = pos();
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.entries.push_back({p, s.step, holly_vblank_count(), 0, 'L', src, result, drive, 0, 0, 0, 0, 1});
}

void intc_trace_delivery(IntcSource origin, uint64_t cycles, uint32_t intevt, uint32_t spc, uint32_t sr) {
    uint32_t        ist = 0, m6 = 0, m4 = 0, m2 = 0;
    holly_intc_peek(ist, m6, m4, m2);
    const uint64_t  p = pos();
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.entries.push_back({p, s.step, holly_vblank_count(), cycles, 'E', origin, intevt, spc, ist, m6, m4, m2, sr});
}

std::string intc_trace_dump() {
    State&             s = state();
    std::lock_guard    lk(s.mu);
    std::ostringstream o;
    o << "# trace das fontes de interrupcao (N8.1, BS_INTC_TRACE): H bits acesos, A bits limpos, C leituras de relogio,"
         " L cartao; pos = eventos do BS_EVENT_TRACE antes deste\n";
    o << "pos\tpasso\tvblank\tciclos\ttipo\tfonte\ta\tb\tistnrm\timl6nrm\timl4nrm\timl2nrm\tn\n";
    char line[192];
    for (const Entry& e : s.entries) {
        std::snprintf(line, sizeof line, "%llu\t%llu\t%llu\t%llu\t%c\t%s\t%08X\t%08X\t%08X\t%08X\t%08X\t%08X\t%llu\n",
                      static_cast<unsigned long long>(e.pos), static_cast<unsigned long long>(e.step),
                      static_cast<unsigned long long>(e.vblank), static_cast<unsigned long long>(e.cycles), e.kind,
                      srcName(e.src), e.a, e.b, e.ist, e.iml6, e.iml4, e.iml2, static_cast<unsigned long long>(e.n));
        o << line;
    }
    return o.str();
}

bool intc_trace_flush() {
    std::string path;
    {
        State&          s = state();
        std::lock_guard lk(s.mu);
        path = s.path;
    }
    if (path.empty()) return false;
    const std::string text = intc_trace_dump();
    FILE*             f    = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace bs
