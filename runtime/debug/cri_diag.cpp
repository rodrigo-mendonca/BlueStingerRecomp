#include "debug/cri_diag.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"
#include "pvr/holly_intc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace bs {
namespace {

constexpr uint32_t kPass      = 0x8C1586B8;  // passada do servidor do CRI
constexpr uint32_t kTable     = 0x8C2D0D84;  // 32 entradas de 120 bytes; +4 = ativa
constexpr uint32_t kEntries   = 32;
constexpr uint32_t kEntrySize = 120;
// Servidor ADX (8C178874): estrutura em 0x8C2F4F38, quantidade de handles em +0xB4, base da
// tabela em +0xB8, passo 0x3EF0; um handle trabalha com o estado (+64) entre 1 e 5.
constexpr uint32_t kAdx       = 0x8C2F4F38;
constexpr uint32_t kAdxStride = 0x3EF0;
// Servidor por objeto (8C175976): ponteiro para o objeto registrado (0 = nenhum).
constexpr uint32_t kSrvObj    = 0x8C1DD618;
// Gerenciador de streaming do objeto servidor (8C174888 -> 8C174998/8C174A36): controle em
// 0x8C2DA958 (+0 = quantidade, +4 = base), entradas de 68 bytes; +0 != 0 = em uso, +48 = pedido.
constexpr uint32_t kStreamCtl    = 0x8C2DA958;
constexpr uint32_t kStreamStride = 68;

enum class Mode { Off, Count, SkipMw, SkipAll, SkipNoStream, SkipFirstPerVblank, RepeatNoStream };

struct State {
    Mode     mode = Mode::Off;
    uint64_t passes = 0, mwIdle = 0, adxIdle = 0, objNull = 0, allIdle = 0, noStream = 0, skipped = 0;
    uint64_t active[kEntries + 1] = {};
    uint64_t lastVblank = ~0ull;  // "menos1": VBlank da ultima passada pulada
};
State g;

uint32_t activeEntries(Sh4Context* c) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < kEntries; ++i) n += sh4_read32(c, kTable + i * kEntrySize + 4) != 0;
    return n;
}

uint32_t activeAdx(Sh4Context* c) {
    const int32_t  count = int32_t(sh4_read32(c, kAdx + 0xB4));
    const uint32_t base  = sh4_read32(c, kAdx + 0xB8);
    uint32_t       n     = 0;
    for (int32_t i = 0; i < count && i < 64; ++i) {
        const uint32_t st = sh4_read32(c, base + uint32_t(i) * kAdxStride + 64);
        n += st >= 1 && st <= 5;
    }
    return n;
}

uint32_t streamsInUse(Sh4Context* c) {
    const int32_t  count = int32_t(sh4_read32(c, kStreamCtl));
    const uint32_t base  = sh4_read32(c, kStreamCtl + 4);
    uint32_t       n     = 0;
    for (int32_t i = 0; i < count && i < 64; ++i) n += sh4_read32(c, base + uint32_t(i) * kStreamStride) != 0;
    return n;
}

void wrapper(Sh4Context* c) {
    const uint32_t mw   = activeEntries(c);
    const uint32_t adx  = activeAdx(c);
    const bool     obj  = sh4_read32(c, kSrvObj) != 0;
    const bool     idle = mw == 0 && adx == 0 && !obj;
    const bool     noSt = mw == 0 && adx == 0 && streamsInUse(c) == 0;
    ++g.passes;
    ++g.active[mw];
    g.mwIdle += mw == 0;
    g.adxIdle += adx == 0;
    g.objNull += !obj;
    g.allIdle += idle;
    g.noStream += noSt;
    if ((g.mode == Mode::SkipMw && mw == 0) || (g.mode == Mode::SkipAll && idle) ||
        (g.mode == Mode::SkipNoStream && noSt) ||
        (g.mode == Mode::SkipFirstPerVblank && g.lastVblank != holly_vblank_count())) {
        g.lastVblank = holly_vblank_count();
        ++g.skipped;  // passada nao executada (so diagnostico)
        return;
    }
    if (g.mode == Mode::RepeatNoStream && noSt) {
        ++g.skipped;  // controle: a mesma passada mais uma vez (so o tempo muda, se ela for ociosa)
        mods::call_original(kPass, c);
    }
    mods::call_original(kPass, c);
}

} // namespace

void cri_diag_install() {
    const char* v = std::getenv("BS_CRI_DIAG");
    if (!v || !*v) return;
    g = State{};
    if (std::strcmp(v, "contar") == 0) g.mode = Mode::Count;
    else if (std::strcmp(v, "pular") == 0) g.mode = Mode::SkipMw;
    else if (std::strcmp(v, "pular2") == 0) g.mode = Mode::SkipAll;
    else if (std::strcmp(v, "pular3") == 0) g.mode = Mode::SkipNoStream;
    else if (std::strcmp(v, "menos1") == 0) g.mode = Mode::SkipFirstPerVblank;
    else if (std::strcmp(v, "repetir3") == 0) g.mode = Mode::RepeatNoStream;
    else throw std::runtime_error(std::string("BS_CRI_DIAG invalido: ") + v + " (contar, pular, pular2, pular3, menos1 ou repetir3)");
    if (!mods::has_slot(kPass))
        throw std::runtime_error("BS_CRI_DIAG precisa do build com slots (BS_MODS): a passada e chamada direto");
    if (!mods::replace(kPass, wrapper)) throw std::runtime_error("BS_CRI_DIAG: passada do CRI nao encontrada");
}

std::string cri_diag_summary() {
    if (g.mode == Mode::Off) return {};
    char b[384];
    std::snprintf(b, sizeof b,
                  "[cri] passadas %llu; sem entrada ativa %llu, sem ADX ativo %llu, sem objeto servidor %llu, "
                  "tudo ocioso %llu, sem stream %llu; puladas %llu; por entradas ativas:",
                  static_cast<unsigned long long>(g.passes), static_cast<unsigned long long>(g.mwIdle),
                  static_cast<unsigned long long>(g.adxIdle), static_cast<unsigned long long>(g.objNull),
                  static_cast<unsigned long long>(g.allIdle), static_cast<unsigned long long>(g.noStream),
                  static_cast<unsigned long long>(g.skipped));
    std::string out = b;
    for (uint32_t n = 0; n <= kEntries; ++n)
        if (g.active[n]) {
            std::snprintf(b, sizeof b, " %u=%llu", n, static_cast<unsigned long long>(g.active[n]));
            out += b;
        }
    return out + "\n";
}

} // namespace bs
