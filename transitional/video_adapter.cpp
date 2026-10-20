#include "video_adapter.h"

#include "cpu/time_hooks.h"
#include "guest_abi.h"
#include "mods/mod_api.h"
#include "video_mw_glue.h"

#include <cstdio>
#include <memory>
#include <stdexcept>

namespace bs::transitional::video {

namespace {

std::unique_ptr<MwAdapter> g_adapter;
bool                       g_installed = false;

// A memoria e o relogio do guest para a cola (video_mw_glue.h).
class SlotPort final : public GuestPort {
public:
    explicit SlotPort(Sh4Context* c) : c_(c), call_(c) {}
    std::optional<uint32_t>    load32(uint32_t a) override { return call_.memory().load<uint32_t>(a); }
    bool                       store32(uint32_t a, uint32_t v) override { return call_.memory().store<uint32_t>(a, v); }
    std::optional<std::string> cstring(uint32_t a, size_t max) override { return call_.memory().cstring(a, max); }
    int64_t                    now100ns() override { return int64_t(time_now(c_) / 20); }  // ticks de 200 MHz -> 100 ns
    GuestCall&                 call() { return call_; }

private:
    Sh4Context* c_;
    GuestCall   call_;
};

template <uint32_t Entry>
void slot(Sh4Context* c) {
    SlotPort        port(c);
    const MwOutcome r = g_adapter->call(Entry, {c->r[4], c->r[5]}, port);
    if (r.forward) return mods::call_original(Entry, c);
    port.call().ret(r.r0);
}

const std::pair<uint32_t, Sh4Func> kFns[] = {
    {kEntCreate, &slot<kEntCreate>},   {kEntParam, &slot<kEntParam>}, {kEntBase, &slot<kEntBase>},
    {kEntOffset, &slot<kEntOffset>},   {kEntDestroy, &slot<kEntDestroy>}, {kEntStart, &slot<kEntStart>},
    {kEntStop, &slot<kEntStop>},       {kEntStat, &slot<kEntStat>}};

} // namespace

void install(BoundaryRegistry& registry, MwCoreOptions o, FrameSink sink) {
    for (const auto& [a, fn] : kFns)
        if (!sh4_lookup_generated(a) || !mods::has_slot(a)) {
            char hexa[16];
            std::snprintf(hexa, sizeof hexa, "%08X", a);
            throw std::runtime_error(std::string("filmes nativos: a funcao MW ") + hexa +
                                     " nao tem slot (gere o codigo de novo com [native] slots)");
        }
    g_adapter = std::make_unique<MwAdapter>(std::move(o), std::move(sink));
    for (const auto& [a, fn] : kFns) {
        registry.add(a, dcmap::Subsystem::Video, "mw");
        registry.set_native(a, fn);
        if (registry.select(a, Provider::Native) != SelectResult::Ok) throw std::runtime_error("filmes nativos: troca de provedor falhou");
    }
    g_installed = true;
}

void uninstall(BoundaryRegistry& registry) {
    if (!g_installed) return;
    for (const auto& [a, fn] : kFns) registry.select(a, Provider::Legacy);
    g_adapter.reset();
    g_installed = false;
}

bool installed() { return g_installed; }
bool playing() { return g_adapter && g_adapter->playing(); }

std::string summary() {
    if (!g_adapter) return {};
    const auto& z = g_adapter->core().stats();
    const auto& s = g_adapter->stats();
    char        b[400];
    // "entrada exposta ao servidor": +64 != 0 na entrada nativa (o servidor por handle do MW, 8C1588D8,
    // a processaria; docs/N5-INTEGRACAO.md secao 2). Tem que ser 0.
    std::snprintf(b, sizeof b,
                  "[video] nativo: create %llu (falhas %llu), start %llu, consultas %llu, quadros %llu (pulados %llu), "
                  "descartes de audio %llu%s, chamadas de outros players %llu, entrada exposta ao servidor %llu\n",
                  static_cast<unsigned long long>(s.creates), static_cast<unsigned long long>(s.createFailures),
                  static_cast<unsigned long long>(z.starts), static_cast<unsigned long long>(z.statusCalls),
                  static_cast<unsigned long long>(z.framesShown), static_cast<unsigned long long>(z.framesSkipped),
                  static_cast<unsigned long long>(z.audioFlushes), z.audioLost ? ", dispositivo perdido" : "",
                  static_cast<unsigned long long>(s.foreign), static_cast<unsigned long long>(s.serverExposed));
    return b;
}

} // namespace bs::transitional::video
