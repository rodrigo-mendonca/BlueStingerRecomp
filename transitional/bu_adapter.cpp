#include "bu_adapter.h"

#include "bu_machine.h"
#include "guest_abi.h"
#include "mods/mod_api.h"
#include "time_adapter.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bs::transitional::bu {

namespace {

namespace ns = native::saves;

struct State {
    ns::ISaveStore*          store = nullptr;
    std::unique_ptr<Machine> m;
    uint32_t                 cbDone = 0, cbProgress = 0;
    uint32_t                 loadDst[kDrives] = {};
    Stats                    stats;
};

State& st() {
    static State s;
    return s;
}

// Chamada do guest a partir do nativo, convencao normal (r4-r7), retorno conferido pelo runtime.
uint32_t callGuest(Sh4Context* c, uint32_t func, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    if (!func) return 0;
    const uint32_t savedPr = c->pr, savedRet = c->ret_expect, savedSp = c->ret_sp, savedFn = c->cur_fn;
    const uint32_t savedR4 = c->r[4], savedR5 = c->r[5], savedR6 = c->r[6], savedR7 = c->r[7];
    constexpr uint32_t kBack = 0xFFFFFFF0u;  // retorno sentinela (so comparado)
    c->pr = c->ret_expect = kBack;
    c->ret_sp            = c->r[15];
    c->r[4] = a0, c->r[5] = a1, c->r[6] = a2, c->r[7] = a3;
    sh4_dispatch(c, 0, func);
    const uint32_t r0 = c->r[0];
    c->pr = savedPr, c->ret_expect = savedRet, c->ret_sp = savedSp, c->cur_fn = savedFn;
    c->r[4] = savedR4, c->r[5] = savedR5, c->r[6] = savedR6, c->r[7] = savedR7;
    return r0;
}

int drive(Sh4Context* c) { return int(int32_t(c->r[4])); }

std::string name(GuestCall& call, uint32_t ptr) {
    const auto n = call.memory().cstring(ptr, 12);
    return n && ns::valid_save_name(*n) ? *n : std::string();
}

void fInit(Sh4Context* c) {
    State&    s = st();
    GuestCall call(c);
    const uint32_t mask = c->r[5], cb = c->r[7];
    s.m = std::make_unique<Machine>(*s.store);
    s.m->init(mask);
    s.cbDone = s.cbProgress = 0;
    ++s.stats.inits;
    const auto arg = call.memory().load<uint32_t>(kSchedArgPtr);
    const uint32_t h = callGuest(c, kSchedRegister, kSchedPriority, kServer, arg.value_or(0), 0);
    call.memory().store<uint32_t>(kSchedHandle, h);
    if (cb) callGuest(c, cb, 0);
    call.memory().store<uint32_t>(kInitFlag, 1);
    call.ret(0);
}

void fRegDone(Sh4Context* c) { st().cbDone = c->r[4], GuestCall(c).ret(0); }
void fRegProgress(Sh4Context* c) { st().cbProgress = c->r[4], GuestCall(c).ret(0); }

void fServer(Sh4Context* c) {
    State& s = st();
    if (!s.m) return GuestCall(c).ret(0);
    ++s.stats.ticks;
    for (Event& e : s.m->tick(true)) {  // ocupada durante os progressos
        if (e.kind == Event::Kind::Progress) {
            callGuest(c, s.cbProgress, uint32_t(e.drive), e.op, e.a, e.b);
            continue;
        }
        if (e.op == kOpLoad) {
            if (e.a == uint32_t(kOk) && !GuestMemory(c).write(s.loadDst[e.drive], e.data))
                sh4_fatal(c, "bu nativo: destino do LOAD fora da RAM principal", c->pc, s.loadDst[e.drive]);
            ++(e.a == uint32_t(kOk) ? s.stats.loadOk : s.stats.loadFail);
        }
        if (e.op == kOpSave) ++(e.a == uint32_t(kOk) ? s.stats.saveOk : s.stats.saveFail);
        // diagnostico BS_SAVES_LOG=<arquivo>: o passo de jogo de cada conclusao de LOAD/SAVE (a lista de excecoes do N2 amarra
        // as diferencas do tempo experimental a uma conclusao de LOAD especifica: tools/dev/n2_sessoes.py)
        static FILE* opLog = [] {
            const char* v = std::getenv("BS_SAVES_LOG");
            FILE*       f = v && *v ? std::fopen(v, "w") : nullptr;
            if (f) std::fprintf(f, "passo\top\tresultado\n");
            return f;
        }();
        if (opLog && (e.op == kOpLoad || e.op == kOpSave))
            std::fprintf(opLog, "%llu\t%s\t%s\n", static_cast<unsigned long long>(time::game_steps()), e.op == kOpLoad ? "load" : "save",
                         e.a == uint32_t(kOk) ? "ok" : "falha"), std::fflush(opLog);
        s.m->release(e.drive);  // livre antes da conclusao: dentro dela o jogo consulta disk_info
        callGuest(c, s.cbDone, uint32_t(e.drive), e.op, e.a);
    }
    GuestCall(c).ret(0);
}

void fMount(Sh4Context* c) {
    ++st().stats.mounts;
    GuestCall(c).ret(uint32_t(st().m ? st().m->mount(drive(c)) : kNoCard));
}

void fDiskInfo(Sh4Context* c) {
    GuestCall call(c);
    std::array<std::byte, kDiskInfoSize> buf{};
    const int32_t r = st().m ? st().m->disk_info(drive(c), buf) : kNoCard;
    if (r == kOk && !call.memory().write(c->r[5], buf)) return call.ret(uint32_t(kBusy));
    call.ret(uint32_t(r));
}

void fExists(Sh4Context* c) {
    GuestCall call(c);
    ++st().stats.exists;
    const std::string n = name(call, c->r[5]);
    call.ret(uint32_t(!st().m ? kNoCard : n.empty() ? kNotFound : st().m->exists(drive(c), n)));
}

// load(drive, nome, destino, bloco_inicial, nblocos): nblocos 0 = ate o fim
void fLoad(Sh4Context* c) {
    State&    s = st();
    GuestCall call(c);
    ++s.stats.loads;
    const std::string n  = name(call, c->r[5]);
    const auto        nb = call.arg(4);
    if (!s.m || n.empty() || !nb) return call.ret(uint32_t(kBusy));
    const int32_t r = s.m->load(drive(c), n, c->r[7], *nb);
    if (r == kOk) s.loadDst[drive(c)] = c->r[6];
    call.ret(uint32_t(r));
}

// save(drive, nome, origem, blocos, tempo*, flag): a imagem e copiada agora (o jogo pode liberar o
// buffer) e gravada no disco na conclusao
void fSave(Sh4Context* c) {
    State&    s = st();
    GuestCall call(c);
    ++s.stats.saves;
    const std::string n      = name(call, c->r[5]);
    const uint32_t    blocks = c->r[7];
    if (!s.m || n.empty() || blocks == 0 || blocks > ns::kCardBlocks) return call.ret(uint32_t(kBusy));
    std::vector<std::byte> img(size_t(blocks) * ns::kBlock);
    if (!call.memory().read(c->r[6], img)) return call.ret(uint32_t(kBusy));
    call.ret(uint32_t(s.m->save(drive(c), n, std::move(img))));
}

void fStatus(Sh4Context* c) {
    const int32_t r = st().m ? st().m->status(drive(c)) : kBusy;
    if (r == kBusy) ++st().stats.statusBusy;
    GuestCall(c).ret(uint32_t(r));
}

void fLastError(Sh4Context* c) { GuestCall(c).ret(uint32_t(st().m ? st().m->last_error(drive(c)) : kBusy)); }

void fExit(Sh4Context* c) {
    const int32_t r = st().m ? st().m->exit() : kOk;
    if (r == kOk && st().m) st().m.reset();
    GuestCall(c).ret(uint32_t(r));
}

void fUnknown0B(Sh4Context* c) {
    ++st().stats.unsupported;
    std::fprintf(stderr, "[saves] chamada sem suporte: 8C08C7BE (op 0x0B) unidade %d\n", drive(c));
    GuestCall(c).ret(uint32_t(kBusy));
}

const std::pair<uint32_t, Sh4Func> kFns[] = {
    {kInit, &fInit},       {kServer, &fServer}, {kRegDone, &fRegDone}, {kRegProgress, &fRegProgress},
    {kMount, &fMount},     {kDiskInfo, &fDiskInfo}, {kExists, &fExists}, {kLoad, &fLoad},
    {kSave, &fSave},       {kStatus, &fStatus}, {kLastError, &fLastError}, {kExit, &fExit},
    {kUnknown0B, &fUnknown0B},
};

// Diagnostico BS_SAVES_H1 (so desenvolvimento, build com mods): o LOAD GAME do titulo abre o painel
// em modo SAVE, como a hipotese H1 do oraculo (docs/N4-PREPARACAO.md). Sem ponto de save no jogo ao
// alcance dos roteiros, e o caminho curto para um SAVE real do jogo no runtime.
constexpr uint32_t kPanel = 0x8C01D200, kTitleLoadReturn = 0x8C0792CC;
void fPanelH1(Sh4Context* c) {
    if ((c->pr & 0x1FFFFFFF) == (kTitleLoadReturn & 0x1FFFFFFF)) c->r[4] = 0;
    mods::call_original(kPanel, c);
}

// Diagnostico BS_SAVES_H2 (so desenvolvimento, build com mods): SAVE no gameplay sem chegar a um ponto
// de save. A consulta de interacao 8C04FB80 (chamada por 8C0407C0 quando o jogador aperta A) devolve o
// tipo 4, o de um objeto kind 98 (bit 0x80 na tabela 8C14CFE0) a frente, quando no modo 0 nao achou
// nada. Dali em diante e o fluxo do proprio jogo: estado 7 em [8C24D2CC+0x25B2], confirmacao, estado 8 e
// 8C01D200(0, 8) chamado por 8C052240, com o estado real da partida. BS_SAVES_H2=<passo>: so a partir
// desse passo de jogo (antes dele os roteiros apertam A em portas e nao podem mudar).
constexpr uint32_t kInteract = 0x8C04FB80;
uint64_t           g_h2Step  = 0;
void fInteractH2(Sh4Context* c) {
    const uint32_t mode = c->r[6];
    mods::call_original(kInteract, c);
    if (mode == 0 && c->r[0] == 0 && time::game_steps() >= g_h2Step) {
        c->r[0] = 4;
        std::fprintf(stderr, "[saves] BS_SAVES_H2: ponto de save simulado (A no gameplay)\n");
    }
}

} // namespace

void h1_install() {
    if (const char* h2 = std::getenv("BS_SAVES_H2"); h2 && *h2) {
        g_h2Step = std::stoull(h2);
        if (!mods::has_slot(kInteract) || !mods::replace(kInteract, &fInteractH2))
            throw std::runtime_error("BS_SAVES_H2 exige o build com mods (slot em 8C04FB80)");
        std::fprintf(stderr, "[saves] BS_SAVES_H2: a partir do passo %llu, A sem objeto a frente abre o SAVE do gameplay (diagnostico)\n",
                     static_cast<unsigned long long>(g_h2Step));
    }
    const char* v = std::getenv("BS_SAVES_H1");
    if (!v || std::string(v) != "1") return;
    if (!mods::has_slot(kPanel) || !mods::replace(kPanel, &fPanelH1))
        throw std::runtime_error("BS_SAVES_H1 exige o build com mods (slot em 8C01D200)");
    std::fprintf(stderr, "[saves] BS_SAVES_H1: LOAD GAME do titulo abre o painel de SAVE (diagnostico)\n");
}

void install(BoundaryRegistry& registry, ns::ISaveStore& store) {
    for (const auto& [addr, fn] : kFns)
        if (!sh4_lookup_generated(addr) || !mods::has_slot(addr)) {
            char hexa[16];
            std::snprintf(hexa, sizeof hexa, "%08X", addr);
            throw std::runtime_error(std::string("provedor nativo de saves: a funcao bu ") + hexa +
                                     " nao tem slot (gere o codigo de novo com [native] slots)");
        }
    State& s = st();
    s       = State{};
    s.store = &store;
    for (const auto& [addr, fn] : kFns) {
        registry.add(addr, dcmap::Subsystem::Saves, "bu");
        registry.set_native(addr, fn);
        if (registry.select(addr, Provider::Native) != SelectResult::Ok)
            throw std::runtime_error("provedor nativo de saves: troca de provedor falhou");
    }
}

void uninstall(BoundaryRegistry& registry) {
    for (const auto& [addr, fn] : kFns) registry.select(addr, Provider::Legacy);
    State& s = st();
    s.m.reset();
    s.store = nullptr;
}

bool installed() { return st().store != nullptr; }

Stats stats() { return st().stats; }

std::string summary() {
    const Stats& s = st().stats;
    char         buf[256];
    std::snprintf(buf, sizeof buf,
                  "[saves] nativo: init %llu, passadas %llu, mount %llu, exists %llu, SAVE %llu (ok %llu, falha %llu), "
                  "LOAD %llu (ok %llu, falha %llu), status ocupado %llu, sem suporte %llu\n",
                  (unsigned long long)s.inits, (unsigned long long)s.ticks, (unsigned long long)s.mounts,
                  (unsigned long long)s.exists, (unsigned long long)s.saves, (unsigned long long)s.saveOk,
                  (unsigned long long)s.saveFail, (unsigned long long)s.loads, (unsigned long long)s.loadOk,
                  (unsigned long long)s.loadFail, (unsigned long long)s.statusBusy, (unsigned long long)s.unsupported);
    return buf;
}

} // namespace bs::transitional::bu
