#include "debug/hw_probe.h"

#include "dc_regions.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>

namespace bs {

bool g_hw_probe = false;

namespace {

// Memorias (VRAM, FIFOs do TA, RAM de som, ROM) sao agregadas por pagina de 64 KB; registros
// ficam com o endereco exato.
bool isMemory(dcmap::Region r) {
    using dcmap::Region;
    switch (r) {
    case Region::Vram64: case Region::Vram32: case Region::TaFifo: case Region::TaYuv: case Region::TaTexture:
    case Region::AicaRam: case Region::BiosRom: case Region::Flash: case Region::CacheArrays:
        return true;
    default: return false;
    }
}

struct Key {
    char     kind;
    uint32_t fn, addr;
    uint8_t  size;
    bool operator<(const Key& o) const { return std::tie(kind, fn, addr, size) < std::tie(o.kind, o.fn, o.addr, o.size); }
};

struct State {
    std::mutex               mu;
    std::map<Key, uint64_t>  counts;
    std::string              path;
    bool                     atexitDone = false;
};

State& state() {
    static State s;
    return s;
}

void atExit() { hw_probe_flush(); }

// BS_HW_PROBE na inicializacao estatica
const bool kInit = [] {
    if (const char* p = std::getenv("BS_HW_PROBE"); p && *p) hw_probe_init(p);
    return true;
}();

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

} // namespace

void hw_probe_init(const std::string& path) {
    State& s = state();
    {
        std::lock_guard lk(s.mu);
        s.counts.clear();
        s.path = path;
        if (!path.empty() && !s.atexitDone) {
            std::atexit(atExit);
            s.atexitDone = true;
        }
    }
    g_hw_probe = !path.empty();
}

void hw_probe_access(const Sh4Context* c, uint32_t addr, unsigned size, ProbeKind kind) {
    // Store queues: os bits 25..5 do endereco so escolhem o destino (QACR), entao a escrita
    // agrega por fila/palavra; destino fora do hardware (copia por SQ para a RAM) por pagina.
    const dcmap::Region r = dcmap::classify(addr);
    const uint32_t      a = r == dcmap::Region::StoreQueue                 ? (addr & 0xE000003Fu)
                            : (isMemory(r) || !dcmap::is_hardware(r))      ? (addr & ~0xFFFFu)
                                                                           : addr;
    State&              s = state();
    std::lock_guard     lk(s.mu);
    s.counts[{char(kind), c ? c->cur_fn : 0u, a, uint8_t(size)}]++;
}

void hw_probe_dispatch(const Sh4Context* c, uint32_t target) {
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.counts[{char(ProbeKind::Indirect), c ? c->cur_fn : 0u, target, 0}]++;
}

std::string hw_probe_dump() {
    State&             s = state();
    std::lock_guard    lk(s.mu);
    std::ostringstream o;
    o << "# sonda N0 (BS_HW_PROBE): r/w = leitura/escrita no caminho lento, q = store queue (pref),"
         " i = desvio indireto; funcao 0 = runtime (interrupcao, inicio de thread)\n";
    o << "tipo\tfuncao\tendereco\ttamanho\tregiao\tcontagem\n";
    for (const auto& [k, n] : s.counts)
        o << k.kind << '\t' << hex(k.fn) << '\t' << hex(k.addr) << '\t' << int(k.size) << '\t'
          << (k.kind == 'i' ? "-" : dcmap::region_name(dcmap::classify(k.addr))) << '\t' << n << '\n';
    return o.str();
}

bool hw_probe_flush() {
    std::string path;
    {
        State&          s = state();
        std::lock_guard lk(s.mu);
        path = s.path;
    }
    if (path.empty()) return false;
    const std::string text = hw_probe_dump();
    FILE*             f    = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace bs
