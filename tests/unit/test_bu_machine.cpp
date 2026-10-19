// N4: protocolo da bu sobre o ISaveStore (transitional/bu_machine.h). A referencia e a sequencia do
// oraculo (jogo original no Flycast com VMU, docs/N4-PREPARACAO.md): eventos, codigos, progresso e
// geometria do LOAD. So amostras sinteticas.
#include "test.h"

#include "bu_machine.h"
#include "platform/saves.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace sv = bs::native::saves;
namespace bu = bs::transitional::bu;
using bu::Event;

namespace {

using Bytes = std::vector<std::byte>;

Bytes makeVms(uint32_t seed) {  // VMS do jogo: 4 blocos, CRC valido
    std::mt19937 r(seed);
    Bytes        v(0x80 + 0x200 + 1200);
    for (auto& b : v) b = std::byte(r());
    auto put16 = [&](size_t o, uint16_t x) { v[o] = std::byte(x), v[o + 1] = std::byte(x >> 8); };
    put16(0x40, 1), put16(0x42, 8), put16(0x44, 0), put16(0x46, 0);
    for (int i = 0; i < 4; ++i) v[0x48 + i] = std::byte(1200 >> (8 * i));
    put16(0x46, sv::vms_crc16(v));
    v.resize(2048);
    return v;
}

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-bu-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(p, ec); }
};

sv::FileStoreOptions opts() {
    sv::FileStoreOptions o;
    o.now = [] { return int64_t(1700000000); };
    return o;
}

// Roda passadas ate a unidade ficar livre; devolve os eventos e quantas consultas deram ocupada.
std::vector<Event> runUntilFree(bu::Machine& m, int drive, int* busyPolls = nullptr) {
    std::vector<Event> all;
    int                busy = 0;
    for (int i = 0; i < 100 && m.busy(drive); ++i) {
        if (m.status(drive) == bu::kBusy) ++busy;
        for (auto& e : m.tick()) all.push_back(std::move(e));
    }
    if (busyPolls) *busyPolls = busy;
    return all;
}

bool isProgressRun(const std::vector<Event>& ev, uint32_t op, uint32_t first, uint32_t last, uint32_t total) {
    size_t n = 0;
    for (const auto& e : ev)
        if (e.kind == Event::Kind::Progress) {
            if (e.op != op || e.a != first + n || e.b != total) return false;
            ++n;
        }
    return n == last - first + 1;
}

const Event* completion(const std::vector<Event>& ev) {
    return !ev.empty() && ev.back().kind == Event::Kind::Complete ? &ev.back() : nullptr;
}

} // namespace

// Boot do oraculo c1: insercao (op 0x12, 15 blocos de trabalho), montagem (op 1), seis exists FF05,
// status 0 e ultimo erro 0.
BS_TEST(bu_boot_sequence_matches_oracle) {
    TempDir     d("boot");
    auto        store = sv::make_file_store(d.p, opts());
    bu::Machine m(*store);
    m.init(3);
    BS_CHECK(m.busy(0) && !m.busy(1));  // so a unidade 0 tem cartao
    auto ins = runUntilFree(m, 0);
    BS_CHECK(isProgressRun(ins, bu::kOpInsert, 1, 2, 0));
    const Event* c = completion(ins);
    BS_CHECK(c && c->op == bu::kOpInsert && c->a == bu::kWorkBlocks && c->drive == 0);
    BS_CHECK(m.mount(0) == bu::kOk);
    auto mnt = runUntilFree(m, 0);
    BS_CHECK(isProgressRun(mnt, bu::kOpMount, 1, 0x10, 0x11));
    c = completion(mnt);
    BS_CHECK(c && c->op == bu::kOpMount && c->a == 0);
    for (int i = 0; i < 6; ++i) BS_CHECK(m.exists(0, "BLUESTIN.00" + std::to_string(i)) == bu::kNotFound);
    BS_CHECK(m.status(0) == bu::kOk && m.last_error(0) == bu::kOk);
    BS_CHECK(m.mount(1) == bu::kNoCard);  // unidade 1 habilitada pela mascara, sem cartao
}

// SAVE do oraculo c2: aceito (0), consultas ocupadas, progresso 1..10/10, conclusao (0x0C, 0) com o
// arquivo ja no disco; depois status 0. Uma segunda operacao durante o ocupado e recusada.
BS_TEST(bu_save_then_load_matches_oracle) {
    TempDir     d("save");
    auto        store = sv::make_file_store(d.p, opts());
    bu::Machine m(*store);
    m.init(3);
    runUntilFree(m, 0);
    m.mount(0), runUntilFree(m, 0);
    const Bytes vms = makeVms(11);
    BS_CHECK(m.save(0, "BLUESTIN.000", vms) == bu::kOk);
    BS_CHECK(m.status(0) == bu::kBusy);
    BS_CHECK(m.load(0, "BLUESTIN.000", 1, 0) == bu::kBusy);  // pendente intacto
    BS_CHECK(m.save(0, "BLUESTIN.001", vms) == bu::kBusy);
    int  busy = 0;
    auto ev   = runUntilFree(m, 0, &busy);
    BS_CHECK(busy >= 1 && busy < 450);
    BS_CHECK(isProgressRun(ev, bu::kOpSave, 1, 10, 10));
    const Event* c = completion(ev);
    BS_CHECK(c && c->op == bu::kOpSave && c->a == 0);
    BS_CHECK(m.status(0) == bu::kOk && m.last_error(0) == bu::kOk);
    Bytes back;
    BS_CHECK(store->load(0, "BLUESTIN.000", back) == sv::SaveError::None && back == vms);
    BS_CHECK(m.exists(0, "BLUESTIN.000") == bu::kOk && m.exists(0, "BLUESTIN.001") == bu::kNotFound);
    // conferencia do jogo (bloco 1, 1 bloco) e LOAD completo (bloco 1, nblocos 0 = ate o fim)
    BS_CHECK(m.load(0, "BLUESTIN.000", 1, 1) == bu::kOk);
    ev = runUntilFree(m, 0);
    c  = completion(ev);
    BS_CHECK(isProgressRun(ev, bu::kOpLoad, 1, 1, 1) && c && c->a == 0 && c->data.size() == 512);
    BS_CHECK(c && std::memcmp(c->data.data(), vms.data() + 512, 512) == 0);
    BS_CHECK(m.load(0, "BLUESTIN.000", 1, 0) == bu::kOk);
    ev = runUntilFree(m, 0);
    c  = completion(ev);
    BS_CHECK(isProgressRun(ev, bu::kOpLoad, 1, 3, 3) && c && c->a == 0 && c->data.size() == 1536);
    BS_CHECK(c && std::memcmp(c->data.data(), vms.data() + 512, 1536) == 0);  // payload em +0x80
}

// disk_info: layout do oraculo e os livres que o jogo copia (200 -> 196 depois de um save de 4).
BS_TEST(bu_disk_info_tracks_free_blocks) {
    TempDir     d("info");
    auto        store = sv::make_file_store(d.p, opts());
    bu::Machine m(*store);
    m.init(1);
    runUntilFree(m, 0);
    std::array<std::byte, bu::kDiskInfoSize> di{};
    auto u16 = [&](size_t o) { return uint16_t(uint8_t(di[o]) | uint8_t(di[o + 1]) << 8); };
    BS_CHECK(m.disk_info(0, di) == bu::kOk);
    BS_CHECK(uint8_t(di[0]) == 1 && u16(32) == 256 && u16(34) == 200 && u16(36) == 200 && u16(38) == 200);
    BS_CHECK(u16(44) == 512 && u16(48) == 1998);
    m.save(0, "BLUESTIN.003", makeVms(3)), runUntilFree(m, 0);
    BS_CHECK(m.disk_info(0, di) == bu::kOk && u16(34) == 200 && u16(36) == 196 && u16(38) == 196);
    m.save(0, "BLUESTIN.003", makeVms(4)), runUntilFree(m, 0);  // sobrescrita nao consome blocos
    BS_CHECK(m.disk_info(0, di) == bu::kOk && u16(36) == 196);
}

// Persistencia: maquina e store novos (processo novo) acham o slot e o LOAD entrega o VMS gravado.
BS_TEST(bu_persistence_across_sessions) {
    TempDir     d("persist");
    const Bytes vms = makeVms(21);
    {
        auto        store = sv::make_file_store(d.p, opts());
        bu::Machine m(*store);
        m.init(1), runUntilFree(m, 0);
        m.save(0, "BLUESTIN.005", vms), runUntilFree(m, 0);
    }
    auto        store = sv::make_file_store(d.p, opts());
    bu::Machine m(*store);
    m.init(1), runUntilFree(m, 0);
    BS_CHECK(m.exists(0, "BLUESTIN.005") == bu::kOk && m.exists(0, "BLUESTIN.000") == bu::kNotFound);
    m.load(0, "BLUESTIN.005", 1, 0);
    const auto   ev = runUntilFree(m, 0);
    const Event* c  = completion(ev);
    BS_CHECK(c && c->a == 0 && c->data.size() == 1536 && std::memcmp(c->data.data(), vms.data() + 512, 1536) == 0);
}

// Erros: LOAD de slot ausente conclui com FF05 sem dados; falha de escrita conclui com FF07 e o save
// anterior continua; sem cartao, o init nao insere e as chamadas devolvem FF01.
BS_TEST(bu_errors) {
    TempDir     d("erros");
    bool        failSwap = false;
    auto        o        = opts();
    o.fault              = [&](sv::FaultPoint p) { return failSwap && p == sv::FaultPoint::Swap; };
    auto        store    = sv::make_file_store(d.p, o);
    bu::Machine m(*store);
    m.init(1), runUntilFree(m, 0);
    m.load(0, "BLUESTIN.002", 1, 0);
    auto         ev = runUntilFree(m, 0);
    const Event* c  = completion(ev);
    BS_CHECK(c && c->op == bu::kOpLoad && int32_t(c->a) == bu::kNotFound && c->data.empty());
    BS_CHECK(m.last_error(0) == bu::kNotFound);
    const Bytes first = makeVms(1);
    m.save(0, "BLUESTIN.000", first), runUntilFree(m, 0);
    failSwap = true;
    m.save(0, "BLUESTIN.000", makeVms(2));
    ev = runUntilFree(m, 0);
    c  = completion(ev);
    BS_CHECK(c && int32_t(c->a) == bu::kCannotOpen);
    for (const auto& e : ev) BS_CHECK(e.kind != Event::Kind::Progress);  // sem progresso de sucesso
    Bytes back;
    BS_CHECK(store->load(0, "BLUESTIN.000", back) == sv::SaveError::None && back == first);
    BS_CHECK(bu::result_code(sv::SaveError::NoCard) == bu::kNoCard &&
             bu::result_code(sv::SaveError::Unformatted) == bu::kUnformatted &&
             bu::result_code(sv::SaveError::NoSpace) == bu::kCannotOpen);
    // unidade sem cartao (o store tem 1 cartao)
    bu::Machine m2(*store);
    m2.init(2);
    BS_CHECK(!m2.busy(1) && m2.mount(1) == bu::kNoCard && m2.exists(1, "BLUESTIN.000") == bu::kNoCard);
    BS_CHECK(m2.status(9) == bu::kBusy);  // unidade invalida
}
