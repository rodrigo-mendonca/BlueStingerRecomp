#include "debug/event_trace.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <vector>

namespace bs {

bool g_event_trace = false;

namespace {

struct Entry {
    uint64_t cycles;
    char     kind;
    uint32_t a, b;
};

struct State {
    std::mutex         mu;
    std::vector<Entry> entries;
    std::string        path;
    bool               atexitDone = false;
};

State& state() {
    static State s;
    return s;
}

void atExit() { event_trace_flush(); }

const bool kInit = [] {
    if (const char* p = std::getenv("BS_EVENT_TRACE"); p && *p) event_trace_init(p);
    return true;
}();

} // namespace

void event_trace_init(const std::string& path) {
    State& s = state();
    {
        std::lock_guard lk(s.mu);
        s.entries.clear();
        s.path = path;
        if (!path.empty() && !s.atexitDone) {
            std::atexit(atExit);
            s.atexitDone = true;
        }
    }
    g_event_trace = !path.empty();
}

void event_trace(TraceEvent e, uint64_t cycles, uint32_t a, uint32_t b) {
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.entries.push_back({cycles, char(e), a, b});
}

std::string event_trace_dump() {
    State&             s = state();
    std::lock_guard    lk(s.mu);
    std::ostringstream o;
    o << "# trace de ordem N0 (BS_EVENT_TRACE): V vblank, I interrupcao, R continuacao, T troca de thread,"
         " N thread nova, J longjmp\n";
    o << "seq\tciclos\ttipo\ta\tb\n";
    char line[96];
    for (size_t i = 0; i < s.entries.size(); ++i) {
        const Entry& e = s.entries[i];
        std::snprintf(line, sizeof line, "%zu\t%llu\t%c\t%08X\t%08X\n", i, static_cast<unsigned long long>(e.cycles),
                      e.kind, e.a, e.b);
        o << line;
    }
    return o.str();
}

bool event_trace_flush() {
    std::string path;
    {
        State&          s = state();
        std::lock_guard lk(s.mu);
        path = s.path;
    }
    if (path.empty()) return false;
    const std::string text = event_trace_dump();
    FILE*             f    = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace bs
