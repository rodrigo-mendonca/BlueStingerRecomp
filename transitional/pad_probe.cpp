#include "pad_probe.h"

#include "pad_contract.h"

#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"

#include <array>
#include <bitset>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace bs::transitional::pad {
namespace {

constexpr uint32_t kServer = 0x8C15C4E6, kPerRaw = 0x8C08F4B6;

struct State {
    bool                            on = false, inServer = false, done = false;
    std::string                     path;
    uint32_t                        mutations = kMutNone;
    bool                            forceMode = false, garbageUnused = false;
    uint32_t                        mode      = 0;
    std::array<uint32_t, kUnits>    raw{};
    std::array<uint32_t, kUnits>    nativeOld{};
    // contagens
    uint64_t calls = 0, units = 0, controller = 0, absent = 0, keyboard = 0, subunit = 0, divergences = 0;
    uint64_t pressEvents = 0, releaseEvents = 0, connects = 0, disconnects = 0, rawMissing = 0;
    std::array<int, kUnits>         lastKind{};  // 0 nada, 1 controle, 2 ausente
    // cobertura (unidades de controle)
    std::array<std::bitset<256>, 6> bytes;       // gatilho R, gatilho L, x1, y1, x2, y2 (bytes 2..7)
    std::array<uint64_t, 4>         oldKinds{};  // old x on: 0 e on != 0, igual, ambos != 0 e diferentes, on 0 e old != 0
    std::bitset<16 * 16>            pairs;       // pares de botoes (bits 0..15 do on) apertados juntos
    std::bitset<16>                 buttons;
    std::bitset<64>                 supports;    // bits 16..21 do support
    std::set<uint32_t>              modes, supportValues;
    uint64_t                        trigWithButton = 0;
    std::vector<std::string>        details;
};

State& st() {
    static State s;
    return s;
}

const uint8_t* ram(Sh4Context* c, uint32_t a, uint32_t n) {
    const uint32_t p = a & 0x1FFFFFFF;
    if ((p >> 24) != 0x0C || (p & 0xFFFFFF) + n > (16u << 20)) return nullptr;
    return c->ram + (p & 0xFFFFFF);
}
uint32_t rd32(Sh4Context* c, uint32_t a) {
    const uint8_t* p = ram(c, a, 4);
    uint32_t       v = 0;
    if (p) std::memcpy(&v, p, 4);
    return v;
}

uint32_t bswap(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }

void note(State& s, const std::string& d) {
    ++s.divergences;
    if (s.details.size() < 40) s.details.push_back(d);
}

void compare(State& s, int i, const Pds& want, const Pds& got) {
    if (want == got) return;
    char b[512];
    std::snprintf(b, sizeof b,
                  "chamada %llu unidade %d: esperado on %08X off %08X press %08X release %08X id %08X sup %08X r %u l %u "
                  "x1 %d y1 %d x2 %d y2 %d name %08X old %08X | legado on %08X off %08X press %08X release %08X id %08X "
                  "sup %08X r %u l %u x1 %d y1 %d x2 %d y2 %d name %08X old %08X",
                  static_cast<unsigned long long>(s.calls), i, want.on, want.off, want.press, want.release, want.id,
                  want.support, want.r, want.l, want.x1, want.y1, want.x2, want.y2, want.name, want.old, got.on, got.off,
                  got.press, got.release, got.id, got.support, got.r, got.l, got.x1, got.y1, got.x2, got.y2, got.name,
                  got.old);
    note(s, b);
}

void server(Sh4Context* c) {
    State&         s        = st();
    uint8_t*       modeRam  = const_cast<uint8_t*>(ram(c, kModeAddr, 4));
    const uint32_t origMode = rd32(c, kModeAddr);
    if (s.forceMode && modeRam) std::memcpy(modeRam, &s.mode, 4);
    std::array<Pds, kUnits>      before;
    std::array<uint32_t, kUnits> statusBefore{};
    for (int i = 0; i < kUnits; ++i) before[size_t(i)] = pds_load(ram(c, kTable + kUnitSize * uint32_t(i), kUnitSize));
    for (int i = 0; i < kUnits; ++i) statusBefore[size_t(i)] = rd32(c, kStatusTable + 4 * uint32_t(i));
    s.raw.fill(0);
    s.inServer = true;
    mods::call_original(kServer, c);
    s.inServer = false;
    ++s.calls;
    const uint32_t mode = rd32(c, kModeAddr);
    s.modes.insert(mode);
    for (int i = 0; i < kUnits; ++i) {
        const Pds got = pds_load(ram(c, kTable + kUnitSize * uint32_t(i), kUnitSize));
        Unit      u;
        u.info      = got.info;
        u.infoFlags = rd32(c, u.info);
        u.support   = bswap(rd32(c, u.info + 4));
        const uint32_t r = s.raw[size_t(i)];
        const uint8_t* rp = r ? ram(c, r, 12) : nullptr;
        if (!rp) {
            ++s.rawMissing;
            char b[96];
            std::snprintf(b, sizeof b, "chamada %llu unidade %d: condicao bruta nao capturada (%08X)",
                          static_cast<unsigned long long>(s.calls), i, r);
            note(s, b);
            continue;
        }
        u.error    = rp[2];
        u.response = rp[4];
        uint32_t frame;
        std::memcpy(&frame, rp + 8, 4);
        if (const uint8_t* fp = ram(c, frame + 8, 8)) std::memcpy(u.cond, fp, 8);
        const Kind kind = classify(i, u);
        ++s.units;
        const uint32_t status = rd32(c, kStatusTable + 4 * uint32_t(i));
        if (status != status_code(u.response)) {
            char b[128];
            std::snprintf(b, sizeof b, "chamada %llu unidade %d: estado %08X, esperado %08X (resposta %02X)",
                          static_cast<unsigned long long>(s.calls), i, status, status_code(u.response), u.response);
            note(s, b);
        }
        if (kind == Kind::Keyboard) {
            ++s.keyboard;  // fora do contrato (8C17B60C); o teste exige 0 na rota
            continue;
        }
        const Pds want = server_unit(i, before[size_t(i)], u, mode, s.mutations, s.nativeOld[size_t(i)]);
        compare(s, i, want, got);
        if (kind == Kind::Subunit) {
            ++s.subunit;
            continue;
        }
        s.nativeOld[size_t(i)] = got.on;  // o on que o servidor escreveu (antes das escritas do jogo)
        if (got.press) ++s.pressEvents;
        if (got.release) ++s.releaseEvents;
        const int k = kind == Kind::Controller ? 1 : 2;
        if (s.lastKind[size_t(i)] == 2 && k == 1) ++s.connects;
        if (s.lastKind[size_t(i)] == 1 && k == 2) ++s.disconnects;
        s.lastKind[size_t(i)] = k;
        if (kind == Kind::Absent) {
            ++s.absent;
            continue;
        }
        ++s.controller;
        // info do controle: o conteudo que o provedor nativo vai sintetizar
        uint8_t want_info[kInfoBytes];
        controller_info(want_info, rd32(c, u.info + 4), s.mutations);
        if (const uint8_t* ip = ram(c, u.info, kInfoBytes); ip && std::memcmp(ip, want_info, kInfoBytes) != 0) {
            char b[96];
            std::snprintf(b, sizeof b, "chamada %llu unidade %d: conteudo da info em %08X difere",
                          static_cast<unsigned long long>(s.calls), i, u.info);
            note(s, b);
        }
        for (int b = 0; b < 6; ++b) s.bytes[size_t(b)].set(u.cond[2 + b]);
        {
            const uint32_t o = got.old, n = got.on;
            if (o == n) ++s.oldKinds[1];
            else if (o == 0) ++s.oldKinds[0];
            else if (n == 0) ++s.oldKinds[3];
            else ++s.oldKinds[2];
        }
        s.supports.set((u.support >> 16) & 63);
        s.supportValues.insert(u.support);
        const uint32_t pressed = mode == 0 ? got.on : ~got.on;
        for (int a = 0; a < 16; ++a)
            if (pressed & (1u << a)) {
                s.buttons.set(size_t(a));
                for (int b = a + 1; b < 16; ++b)
                    if (pressed & (1u << b)) s.pairs.set(size_t(a * 16 + b));
            }
        if ((pressed & 0x30000) && (pressed & 0xFFFF)) ++s.trigWithButton;
    }
    // campos_nao_usados: depois de conferida a passada, lixo nos campos que o jogo nao le (r, l, x2,
    // y2, old, name, extend); quadros e traco iguais provam, na rota, que nenhum consumidor os usa
    if (s.garbageUnused)
        for (int i = 0; i < kUnits; i += 6) {
            uint8_t* up = const_cast<uint8_t*>(ram(c, kTable + kUnitSize * uint32_t(i), kUnitSize));
            Pds      g  = pds_load(up);
            g.r = 0x1234, g.l = 0x4321, g.x2 = 0x55, g.y2 = -0x55;
            g.old = 0xDEADBEEF, g.extend = 0xCAFEBABE, g.name = kNoDeviceName + 1;
            pds_store(up, g);
        }
    // BS_PAD_MODO: a passada no modo forcado ja foi conferida; o jogo recebe a tabela do modo
    // original (pelo contrato, a partir da mesma entrada) para seguir jogando como sempre
    if (s.forceMode && s.mode != origMode && modeRam) {
        for (int i = 0; i < kUnits; ++i) {
            uint8_t*       up = const_cast<uint8_t*>(ram(c, kTable + kUnitSize * uint32_t(i), kUnitSize));
            const Pds      got = pds_load(up);
            Unit           u;
            u.info      = got.info;
            u.infoFlags = rd32(c, u.info);
            u.support   = bswap(rd32(c, u.info + 4));
            const uint8_t* rp = s.raw[size_t(i)] ? ram(c, s.raw[size_t(i)], 12) : nullptr;
            if (!rp || classify(i, u) == Kind::Keyboard) continue;
            u.error    = rp[2];
            u.response = rp[4];
            uint32_t frame;
            std::memcpy(&frame, rp + 8, 4);
            if (const uint8_t* fp = ram(c, frame + 8, 8)) std::memcpy(u.cond, fp, 8);
            pds_store(up, server_unit(i, before[size_t(i)], u, origMode));
        }
        std::memcpy(modeRam, &origMode, 4);
    }
}

void perRaw(Sh4Context* c) {
    State&         s = st();
    const uint32_t i = c->r[4];
    mods::call_original(kPerRaw, c);
    if (s.inServer && i < uint32_t(kUnits)) s.raw[i] = c->r[0];
}

void atExit() { probe_finish(); }

} // namespace

void probe_install() {
    State&      s = st();
    const char* p = std::getenv("BS_PAD_CONTRATO");
    if (!p || !*p) return;
    if (!mods::has_slot(kServer) || !mods::has_slot(kPerRaw))
        throw std::runtime_error("BS_PAD_CONTRATO exige o build com mods (slots nas chamadas diretas)");
    s.path = p;
    if (const char* m = std::getenv("BS_PAD_CONTRATO_MUTACAO"); m && *m) {
        const std::string v = m;
        if (v.find("troca_ab") != std::string::npos) s.mutations |= kMutSwapAB;
        if (v.find("gatilho127") != std::string::npos) s.mutations |= kMutTrigger127;
        if (v.find("old_nativo") != std::string::npos) s.mutations |= kMutOldNative;
        if (v.find("info_sem_conectado") != std::string::npos) s.mutations |= kMutInfoNoConnect;
        s.garbageUnused = v.find("campos_nao_usados") != std::string::npos;
    }
    if (const char* m = std::getenv("BS_PAD_MODO"); m && *m) s.forceMode = true, s.mode = uint32_t(std::strtoul(m, nullptr, 0));
    mods::replace(kServer, &server);
    mods::replace(kPerRaw, &perRaw);
    s.on = true;
    std::atexit(atExit);
    std::fprintf(stderr, "[pad] teste de contrato do servidor pd ligado (%s)\n", p);
}

void probe_finish() {
    State& s = st();
    if (!s.on || s.done) return;
    s.done  = true;
    FILE* f = std::fopen(s.path.c_str(), "w");
    if (!f) return;
    auto kv = [&](const char* k, unsigned long long v) { std::fprintf(f, "%s\t%llu\n", k, v); };
    kv("chamadas", s.calls);
    kv("unidades", s.units);
    kv("controle", s.controller);
    kv("ausente", s.absent);
    kv("subunidade", s.subunit);
    kv("teclado", s.keyboard);
    kv("sem_condicao", s.rawMissing);
    kv("divergencias", s.divergences);
    kv("press", s.pressEvents);
    kv("release", s.releaseEvents);
    kv("conexoes", s.connects);
    kv("desconexoes", s.disconnects);
    kv("gatilho_com_botao", s.trigWithButton);
    static const char* const kByte[] = {"valores_gatilho_r", "valores_gatilho_l", "valores_x1", "valores_y1", "valores_x2", "valores_y2"};
    for (int b = 0; b < 6; ++b) kv(kByte[b], s.bytes[size_t(b)].count());
    static const char* const kOld[] = {"old_zero_on_nao", "old_igual_on", "old_diferente", "old_nao_on_zero"};
    for (int k = 0; k < 4; ++k) kv(kOld[k], s.oldKinds[size_t(k)]);
    kv("botoes", s.buttons.count());
    kv("pares", s.pairs.count());
    kv("supports", s.supports.count());
    std::string modes;
    for (uint32_t m : s.modes) modes += (modes.empty() ? "" : ",") + std::to_string(m);
    std::fprintf(f, "modos\t%s\n", modes.c_str());
    std::string sups;
    for (uint32_t v : s.supportValues) {
        char b[12];
        std::snprintf(b, sizeof b, "%08X", v);
        sups += (sups.empty() ? "" : ",") + std::string(b);
    }
    std::fprintf(f, "support_valores\t%s\n", sups.c_str());
    std::fprintf(f, "botoes_mascara\t%04lX\n", static_cast<unsigned long>(s.buttons.to_ulong()));
    for (const auto& d : s.details) std::fprintf(f, "divergencia\t%s\n", d.c_str());
    std::fclose(f);
}

} // namespace bs::transitional::pad
