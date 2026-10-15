#include "gdfs_adapter.h"

#include "debug/file_log.h"
#include "guest_abi.h"
#include "handle_table.h"
#include "mods/mod_api.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace bs::transitional::gdfs {

namespace {

namespace nf = native::files;

constexpr uint32_t kSector = 2048;  // unidade da API gdFs (nao do disco)

// Estado (gdFs do SDK: +80 estado, +82 erro, +24 transferidos, +20 posicao...).
enum Status : int32_t { kIdle = 0, kComplete = 1, kBusy = 2, kError = 6 };
constexpr int32_t kErrNotOpen = -10, kErrAlign = -14, kErrOrigin = -16, kErrRange = -17;
constexpr int32_t kErrRead = -1;  // falha de E/S do host (o legado nao tem equivalente observado)

struct Gdfs {
    nf::FileHandle file;
    std::string    logical;
    uint64_t       sizeBytes = 0;
    int32_t        sizeSct = 0, pos = 0;
    uint32_t       trans = 0;
    int32_t        status = kIdle, err = 0;
    uint32_t       mode = 0;
    uint32_t       cbRead[2] = {0, 0}, cb2[2] = {0, 0}, cbErr[2] = {0, 0};
};

struct State {
    nf::IFileSystem*  fs = nullptr;
    Addresses         a{};
    HandleTable<Gdfs> table{15};  // gen <= 15: o handle cabe em 24 bits sob a etiqueta 0x7E
    std::string       dir;        // diretorio corrente (nome logico, "" = raiz)
    Stats             stats;
    // Pedido assincrono pendente (um por vez, como o drive do legado): publicado como ocupado
    // em gdFsReqRd32 e concluido na proxima execucao do servidor gdFs (revisao Codex 2026-10-15).
    uint32_t pendingToken = 0, pendingNsct = 0, pendingBuf = 0;
};

State& st() {
    static State s;
    return s;
}

Gdfs* get(uint32_t token) { return is_native_handle(token) ? st().table.get(token & 0x00FFFFFFu) : nullptr; }

// GDFS legado (ponteiro na RAM): a funcao original trata.
bool passLegacy(Sh4Context* c, uint32_t addr) {
    if (is_native_handle(c->r[4]) || c->r[4] == 0) return false;
    if (!GuestMemory::ram_offset(c->r[4], 4)) return false;
    ++st().stats.legacyPassThrough;
    sh4_lookup_generated(addr)(c);
    return true;
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (!name.empty() && (name[0] == '\\' || name[0] == '/')) return name;
    return dir.empty() ? name : dir + "/" + name;
}

// Chamada do guest a partir do nativo (callback registrado pelo jogo/SDK): convencao normal,
// com retorno conferido pelo runtime.
void callGuest(Sh4Context* c, uint32_t func, uint32_t obj) {
    if (!func) return;
    const uint32_t savedPr = c->pr, savedRet = c->ret_expect, savedSp = c->ret_sp, savedFn = c->cur_fn;
    constexpr uint32_t kBack = 0xFFFFFFF0u;  // retorno sentinela (so comparado)
    c->pr = c->ret_expect = kBack;
    c->ret_sp            = c->r[15];
    c->r[4]              = obj;
    sh4_dispatch(c, 0, func);
    c->pr = savedPr, c->ret_expect = savedRet, c->ret_sp = savedSp, c->cur_fn = savedFn;
}

// Leitura de `nsct` unidades a partir da posicao, para `buf` (RAM do guest). Devolve as
// unidades pedidas (depois do recorte no fim do arquivo) ou 0 no fim do arquivo.
int32_t doRead(Sh4Context* c, Gdfs& g, uint32_t nsct, uint32_t buf) {
    State& s = st();
    if (g.pos >= g.sizeSct || nsct == 0) {
        g.trans = 0, g.status = kComplete;
        return 0;
    }
    nsct = std::min<uint32_t>(nsct, uint32_t(g.sizeSct - g.pos));
    const uint64_t         off = uint64_t(g.pos) * kSector;
    std::vector<std::byte> tmp(size_t(nsct) * kSector, std::byte{0});  // ultimo setor: zeros apos o fim
    const auto             r = s.fs->read(g.file, off, tmp);
    if (!r) {
        std::fprintf(stderr, "[arquivos] falha ao ler %s (%s)\n", g.logical.c_str(), native::error_name(r.error()));
        g.status = kError, g.err = kErrRead, g.trans = 0;
        callGuest(c, g.cbErr[0], g.cbErr[1]);
        return int32_t(nsct);
    }
    GuestMemory mem(c);
    if (!mem.write(buf, tmp))
        sh4_fatal(c, "gdFs nativo: destino da leitura fora da RAM principal", c->pc, buf);
    if (g_file_log)
        for (uint64_t k = 0; k < r.value(); k += kSector)
            file_log_chunk("nativo", g.logical, off + k, reinterpret_cast<const uint8_t*>(tmp.data()) + k,
                           size_t(std::min<uint64_t>(kSector, r.value() - k)));
    g.pos += int32_t(nsct);
    g.trans  = nsct * kSector;
    g.status = kComplete;
    ++s.stats.reads;
    s.stats.bytes += r.value();
    callGuest(c, g.cbRead[0], g.cbRead[1]);
    return int32_t(nsct);
}

// Conclui o pedido pendente (servidor gdFs, leitura sincrona).
void completePending(Sh4Context* c) {
    State& s = st();
    if (!s.pendingToken) return;
    const uint32_t t = s.pendingToken, n = s.pendingNsct, buf = s.pendingBuf;
    s.pendingToken = 0;
    if (Gdfs* g = get(t)) doRead(c, *g, n, buf);
}

void cancelPending(uint32_t token) {
    if (st().pendingToken == token) st().pendingToken = 0;
}

// --- funcoes da API (Sh4Func) ---

void fOpen(Sh4Context* c) {
    State&    s = st();
    GuestCall call(c);
    const auto name = call.memory().cstring(c->r[4], 255);
    if (!name) {
        call.ret(0);
        return;
    }
    if (c->r[5] != 0) {  // registro de diretorio explicito: nunca observado; sem equivalente nativo
        std::fprintf(stderr, "[arquivos] gdFsOpen(%s) com registro de diretorio: nao suportado\n", name->c_str());
        call.ret(0);
        return;
    }
    const std::string logical = joinPath(s.dir, *name);
    const auto        h       = s.fs->open(logical);
    if (!h) {
        ++s.stats.notFound;
        std::fprintf(stderr, "[arquivos] nao encontrado: %s\n", logical.c_str());
        call.ret(0);
        return;
    }
    Gdfs g;
    g.file      = h.value();
    g.logical   = nf::canonical_path(logical);
    g.sizeBytes = s.fs->size(g.file).value();
    if (g.sizeBytes > 0xFFFFFFFFull) {  // o GDFS do jogo guarda o tamanho em 32 bits
        std::fprintf(stderr, "[arquivos] %s: tamanho alem do limite do jogo (4 GB - 1)\n", logical.c_str());
        s.fs->close(h.value());
        call.ret(0);
        return;
    }
    g.sizeSct   = int32_t((g.sizeBytes + kSector - 1) / kSector);
    const uint32_t t = s.table.insert(std::move(g));
    if (!t) {
        s.fs->close(h.value());
        call.ret(0);
        return;
    }
    ++s.stats.opens;
    call.ret(kTokenTag | t);
}

void fChangeDir(Sh4Context* c) {
    State&     s = st();
    GuestCall  call(c);
    const auto name = call.memory().cstring(c->r[4], 255);
    if (!name) {
        call.ret(uint32_t(-1));
        return;
    }
    std::string next = joinPath(s.dir, *name);
    // ".." sobe um nivel
    std::string norm;
    for (size_t i = 0; i <= next.size();) {
        const size_t      j    = std::min(next.find_first_of("\\/", i), next.size());
        const std::string part = next.substr(i, j - i);
        if (part == "..") norm = norm.substr(0, norm.find_last_of('/') == std::string::npos ? 0 : norm.find_last_of('/'));
        else if (!part.empty() && part != ".") norm += (norm.empty() ? "" : "/") + part;
        i = j + 1;
    }
    if (!norm.empty() && !s.fs->list(norm)) {
        std::fprintf(stderr, "[arquivos] diretorio nao encontrado: %s\n", norm.c_str());
        call.ret(uint32_t(-1));
        return;
    }
    s.dir = norm;
    call.ret(0);
}

void fClose(Sh4Context* c) {
    if (passLegacy(c, st().a.close)) return;
    if (!is_native_handle(c->r[4])) return;
    const uint32_t h = c->r[4] & 0x00FFFFFFu;
    cancelPending(c->r[4]);
    if (Gdfs* g = st().table.get(h)) {
        st().fs->close(g->file);
        st().table.remove(h);
    }
}

void fGetFileSize(Sh4Context* c) {
    if (passLegacy(c, st().a.getFileSize)) return;
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(0);
    call.memory().store<uint32_t>(c->r[5], uint32_t(g->sizeBytes));
    call.ret(1);
}

void fGetFileSctSize(Sh4Context* c) {
    if (passLegacy(c, st().a.getFileSctSize)) return;
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(0);
    call.memory().store<uint32_t>(c->r[5], uint32_t(g->sizeSct));
    call.ret(1);
}

void fSeek(Sh4Context* c) {
    if (passLegacy(c, st().a.seek)) return;
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(uint32_t(kErrNotOpen));
    const int32_t off = int32_t(c->r[5]);
    int32_t       np;
    switch (c->r[6]) {
    case 0: np = off; break;
    case 1: np = g->pos + off; break;
    case 2: np = g->sizeSct + off; break;
    default: return call.ret(uint32_t(kErrOrigin));
    }
    if (np < 0 || np > g->sizeSct) return call.ret(uint32_t(kErrRange));
    g->pos = np;
    call.ret(0);
}

void fTell(Sh4Context* c) {
    if (passLegacy(c, st().a.tell)) return;
    Gdfs* g = get(c->r[4]);
    GuestCall(c).ret(g ? uint32_t(g->pos) : 0);
}

void fEof(Sh4Context* c) {
    if (passLegacy(c, st().a.eof)) return;
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(uint32_t(kErrNotOpen));
    call.memory().store<uint32_t>(c->r[5], g->pos >= g->sizeSct ? 1u : 0u);
    call.ret(0);
}

void fSetTransMode(Sh4Context* c) {
    if (passLegacy(c, st().a.setTransMode)) return;
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(uint32_t(kErrNotOpen));
    g->mode = c->r[5] & 0xFFFF;
    call.ret(0);
}

// gdFsRead: sincrona; buffer de qualquer alinhamento (o legado usa PIO se nao alinhado).
void fRead(Sh4Context* c) {
    if (passLegacy(c, st().a.read)) return;
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(uint32_t(kErrNotOpen));
    const uint32_t nsct = c->r[5], buf = c->r[6];
    completePending(c);  // o legado espera o drive livre antes do proprio pedido
    g = get(c->r[4]);    // um callback pode ter fechado o arquivo
    if (!g) return call.ret(uint32_t(kErrNotOpen));
    doRead(c, *g, nsct, buf);
    call.ret(g->status == kError ? uint32_t(g->err) : 0u);
}

// gdFsReqRd32 (contrato do binario, 8C15B8D4): -10 sem arquivo; 0 com o drive ocupado (outro
// pedido pendente); -14 com buffer desalinhado; no fim do arquivo, concluido com 0 unidades;
// senao, recorta no fim, publica "ocupado" (2) com 0 bytes transferidos e devolve as unidades.
// A transferencia (buffer, posicao, bytes, estado 1, callback) acontece na proxima execucao do
// servidor gdFs, como no legado.
void fReqRd32(Sh4Context* c) {
    if (passLegacy(c, st().a.reqRd32)) return;
    State&    s = st();
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(uint32_t(kErrNotOpen));
    if (s.pendingToken) return call.ret(0);
    if (c->r[6] & 31) return call.ret(uint32_t(kErrAlign));
    uint32_t nsct = c->r[5];
    if (g->pos >= g->sizeSct || nsct == 0) {
        g->trans = 0, g->status = kComplete;
        return call.ret(0);
    }
    nsct           = std::min<uint32_t>(nsct, uint32_t(g->sizeSct - g->pos));
    s.pendingToken = c->r[4], s.pendingNsct = nsct, s.pendingBuf = c->r[6];
    g->status = kBusy, g->trans = 0;
    call.ret(nsct);
}

// Servidor do gdFs (8C15AE28, chamado por quadro e pelo laco de espera/CRI): conclui o pedido
// nativo pendente e segue com o servidor legado (estado do drive, inicializacao).
void fServer(Sh4Context* c) {
    completePending(c);
    sh4_lookup_generated(st().a.server)(c);
}

void fGetStat(Sh4Context* c) {
    if (passLegacy(c, st().a.getStat)) return;
    Gdfs* g = get(c->r[4]);
    GuestCall(c).ret(g ? uint32_t(g->status) : uint32_t(kErrNotOpen));
}

void fGetTransStat(Sh4Context* c) {
    if (passLegacy(c, st().a.getTransStat)) return;
    Gdfs* g = get(c->r[4]);
    GuestCall(c).ret(g ? g->trans : 0u);
}

// gdFsStop: cancela o pedido pendente deste arquivo (o legado aborta a transferencia) e volta a
// ocioso.
void fStop(Sh4Context* c) {
    if (passLegacy(c, st().a.stop)) return;
    GuestCall call(c);
    Gdfs*     g = get(c->r[4]);
    if (!g) return call.ret(uint32_t(kErrNotOpen));
    cancelPending(c->r[4]);
    g->status = kIdle;
    call.ret(0);
}

void fGetErr(Sh4Context* c) {
    if (passLegacy(c, st().a.getErr)) return;
    Gdfs* g = get(c->r[4]);
    GuestCall(c).ret(g ? uint32_t(g->err) : 0u);
}

template <int Which>
void fCallback(Sh4Context* c) {
    const uint32_t addr = Which == 0 ? st().a.cbReadEnd : Which == 1 ? st().a.cb2 : st().a.cbErr;
    if (passLegacy(c, addr)) return;
    Gdfs* g = get(c->r[4]);
    if (!g) return;
    uint32_t* slot = Which == 0 ? g->cbRead : Which == 1 ? g->cb2 : g->cbErr;
    slot[0]        = c->r[5];
    slot[1]        = c->r[6];
}

// Funcoes sem equivalente nativo: um handle nativo aqui e erro de integracao, nao dado.
template <int Which>
void fGuard(Sh4Context* c) {
    static const char* kNames[] = {"gdFsGetFileFad (endereco do disco)", "leitura por PIO (interna)",
                                   "espera de pedido (interna)"};
    const uint32_t addr = Which == 0 ? st().a.getFileFad : Which == 1 ? st().a.reqPio : st().a.wait;
    if (is_native_handle(c->r[4])) {
        std::string msg = std::string("gdFs nativo: ") + kNames[Which] + " recebeu um arquivo aberto pelo provedor nativo";
        sh4_fatal(c, msg.c_str(), c->pc, c->r[4]);
        return;
    }
    sh4_lookup_generated(addr)(c);
}

} // namespace

void install(BoundaryRegistry& registry, nf::IFileSystem& fs, const Addresses& a) {
    State& s = st();
    const std::pair<uint32_t, Sh4Func> fns[] = {
        {a.open, &fOpen},           {a.changeDir, &fChangeDir}, {a.close, &fClose},
        {a.getFileSize, &fGetFileSize}, {a.getFileSctSize, &fGetFileSctSize}, {a.getFileFad, &fGuard<0>},
        {a.seek, &fSeek},           {a.tell, &fTell},           {a.eof, &fEof},
        {a.setTransMode, &fSetTransMode}, {a.read, &fRead},     {a.reqPio, &fGuard<1>},
        {a.reqRd32, &fReqRd32},     {a.wait, &fGuard<2>},       {a.getStat, &fGetStat},
        {a.getTransStat, &fGetTransStat}, {a.stop, &fStop},     {a.getErr, &fGetErr},
        {a.cbReadEnd, &fCallback<0>}, {a.cb2, &fCallback<1>},   {a.cbErr, &fCallback<2>}, {a.server, &fServer},
    };
    // Endereco sem codigo gerado (funcao nunca alcancada pela analise) nao pode ser chamado:
    // fica de fora. Codigo gerado sem slot e erro: chamadas diretas iriam ao legado.
    for (const auto& [addr, fn] : fns)
        if (sh4_lookup_generated(addr) && !mods::has_slot(addr)) {
            char hexa[16];
            std::snprintf(hexa, sizeof hexa, "%08X", addr);
            throw std::runtime_error(std::string("provedor nativo de arquivos: a funcao gdFs ") + hexa +
                                     " nao tem slot (gere o codigo de novo com [native] slots)");
        }
    s.fs    = &fs;
    s.a     = a;
    s.dir.clear();
    s.table.clear();
    s.pendingToken = 0;
    s.stats = {};
    for (const auto& [addr, fn] : fns) {
        if (!sh4_lookup_generated(addr)) continue;
        registry.add(addr, dcmap::Subsystem::Files, "gdFs");
        registry.set_native(addr, fn);
        if (registry.select(addr, Provider::Native) != SelectResult::Ok)
            throw std::runtime_error("provedor nativo de arquivos: troca de provedor falhou");
    }
}

void uninstall(BoundaryRegistry& registry) {
    State& s = st();
    for (uint32_t addr : {s.a.open, s.a.changeDir, s.a.close, s.a.getFileSize, s.a.getFileSctSize, s.a.getFileFad,
                          s.a.seek, s.a.tell, s.a.eof, s.a.setTransMode, s.a.read, s.a.reqPio, s.a.reqRd32, s.a.wait,
                          s.a.getStat, s.a.getTransStat, s.a.stop, s.a.getErr, s.a.cbReadEnd, s.a.cb2, s.a.cbErr, s.a.server})
        if (addr) registry.select(addr, Provider::Legacy);
    if (s.fs) s.table.for_each([&](Gdfs& g) { s.fs->close(g.file); });  // o que ficou aberto
    s.table.clear();
    s.pendingToken = 0;
    s.fs = nullptr;
}

void check_sizes(const nf::DataFileSystem& fs) {
    for (const auto& m : fs.mods())
        if (m.modSize > 0xFFFFFFFFull)
            throw std::runtime_error("mod de recurso grande demais para o jogo (limite de 4 GB - 1 byte): " + m.logical);
}

Stats stats() { return st().stats; }

} // namespace bs::transitional::gdfs
