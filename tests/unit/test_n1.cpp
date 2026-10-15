// N1: sistema de arquivos nativo (DataFileSystem) e adaptador gdFs.
#include "test.h"

#include "boundary_registry.h"
#include "debug/file_log.h"
#include "gdfs_adapter.h"
#include "codegen/codegen.h"
#include "hwscan/closure.h"
#include "mods/mod_api.h"
#include "platform/data_files.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace bs;
namespace fs  = std::filesystem;
namespace nf  = bs::native::files;
namespace gd  = bs::transitional::gdfs;
using transitional::BoundaryRegistry;

namespace {

struct TempTree {
    fs::path root;
    TempTree() {
        root = fs::temp_directory_path() / ("bs_n1_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root / "disc" / "BSDATA");
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& rel, const std::string& bytes) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << bytes;
    }
};

std::string pattern(size_t n, char seed) {
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i) s[i] = char(seed + i % 251);
    return s;
}

bool throwsWith(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::exception& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

} // namespace

BS_TEST(n1_canonical_path) {
    BS_CHECK(nf::canonical_path("\\BSDATA\\cel000.nj;1") == "BSDATA/CEL000.NJ");
    BS_CHECK(nf::canonical_path("/bsdata//./a.bin") == "BSDATA/A.BIN");
    BS_CHECK(nf::canonical_path("1st_read.bin") == "1ST_READ.BIN");
    BS_CHECK(nf::canonical_path("") == "");
}

BS_TEST(n1_data_files_open_read_list) {
    TempTree t;
    const std::string a = pattern(5000, 'a');
    t.write("disc/BSDATA/A.BIN", a);
    t.write("disc/1ST_READ.BIN", "boot");
    nf::DataFileSystem fsys(t.root / "disc");
    BS_CHECK(fsys.file_count() == 2);
    auto h = fsys.open("\\bsdata\\a.bin;1");  // maiusculas, '\', versao
    BS_CHECK(h && fsys.size(h.value()).value() == 5000);
    BS_CHECK(fsys.logical_name(h.value()) == "BSDATA/A.BIN");
    std::vector<std::byte> buf(2048);
    BS_CHECK(fsys.read(h.value(), 4096, buf).value() == 904);  // recortado no fim
    BS_CHECK(std::memcmp(buf.data(), a.data() + 4096, 904) == 0);
    BS_CHECK(fsys.read(h.value(), 5000, buf).value() == 0);
    BS_CHECK(fsys.read(h.value(), 5001, buf).error() == native::Error::OutOfRange);
    auto rq = fsys.read_async(h.value(), 0, buf);
    BS_CHECK(rq && fsys.poll(rq.value()).state == nf::RequestState::Done);
    BS_CHECK(fsys.poll(rq.value()).state == nf::RequestState::Failed);  // consumido
    const auto root = fsys.list("");
    BS_CHECK(root && root.value().size() == 2);
    const auto dir = fsys.list("\\BSDATA");
    BS_CHECK(dir && dir.value().size() == 1 && dir.value()[0].name == "A.BIN" && dir.value()[0].size == 5000);
    BS_CHECK(fsys.list("NADA").error() == native::Error::NotFound);
    fsys.close(h.value());
    BS_CHECK(fsys.size(h.value()).error() == native::Error::InvalidHandle);
    BS_CHECK(fsys.open("BSDATA/B.BIN").error() == native::Error::NotFound);
}

// Mods de recursos: qualquer tamanho; mod sem recurso correspondente e erro com o caminho.
BS_TEST(n1_data_files_mods_verified) {
    TempTree t;
    t.write("disc/BSDATA/A.BIN", pattern(100, 'a'));
    t.write("mods/BSDATA/a.bin", pattern(9000, 'm'));  // maior que o original
    {
        nf::DataFileSystem fsys(t.root / "disc", t.root / "mods");
        BS_CHECK(fsys.mods().size() == 1 && fsys.mods()[0].originalSize == 100 && fsys.mods()[0].modSize == 9000);
        auto h = fsys.open("BSDATA/A.BIN");
        BS_CHECK(h && fsys.size(h.value()).value() == 9000);
        std::vector<std::byte> b(1);
        fsys.read(h.value(), 0, b);
        BS_CHECK(char(b[0]) == 'm');
    }
    t.write("mods/BSDATA/NOVO.BIN", "x");
    BS_CHECK(throwsWith([&] { nf::DataFileSystem f(t.root / "disc", t.root / "mods"); },
                        "BSDATA/NOVO.BIN: nao substitui nenhum recurso"));
    BS_CHECK(throwsWith([&] { nf::DataFileSystem f(t.root / "nada"); }, "pasta de recursos do jogo ausente"));
    // sem pasta de mods: tudo bem
    nf::DataFileSystem ok(t.root / "disc", t.root / "sem_mods");
    BS_CHECK(ok.mods().empty());
}

namespace {

// Guest sintetico: funcoes "legadas" nos enderecos do gdFs, com slots (como o codigo gerado com
// [native] slots), para o adaptador trocar o provedor.
int g_legacyCalls = 0;
void legacyFn(Sh4Context* c) {
    ++g_legacyCalls;
    c->r[0] = 0x1234;
}

constexpr gd::Addresses kA = gd::kBlueStingerUS;
const uint32_t kAll[] = {kA.open, kA.changeDir, kA.close, kA.getFileSize, kA.getFileSctSize, kA.getFileFad,
                         kA.seek, kA.tell, kA.eof, kA.setTransMode, kA.read, kA.reqPio, kA.reqRd32, kA.wait,
                         kA.getStat, kA.getTransStat, kA.stop, kA.getErr, kA.cbReadEnd, kA.cb2, kA.cbErr, kA.server};

struct Guest {
    std::vector<Sh4FuncEntry> table;
    std::vector<Sh4Func>      slots;
    std::vector<mods::Slot>   slotList;
    std::vector<uint8_t>      ram = std::vector<uint8_t>(16u << 20);
    Sh4Context                c{};
    BoundaryRegistry          registry;

    Guest() {
        std::vector<uint32_t> sorted(std::begin(kAll), std::end(kAll));
        std::sort(sorted.begin(), sorted.end());
        slots.resize(sorted.size(), &legacyFn);
        for (size_t i = 0; i < sorted.size(); ++i) {
            table.push_back({sorted[i] & 0x1FFFFFFF, &legacyFn});
            slotList.push_back({sorted[i], &slots[i], &legacyFn});
        }
        sh4_set_function_table(table.data(), table.size());
        mods::set_slots(slotList.data(), slotList.size());
        c.ram        = ram.data();
        c.next_event = ~0ull;
    }
    ~Guest() {
        gd::uninstall(registry);
        mods::reset();
        mods::set_slots(nullptr, 0);
        sh4_set_function_table(nullptr, 0);
    }
    uint32_t call(uint32_t addr, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0) {
        c.r[4] = r4, c.r[5] = r5, c.r[6] = r6;
        sh4_lookup(addr)(&c);  // pelo slot/override, como o codigo gerado
        return c.r[0];
    }
    void str(uint32_t a, const std::string& s) { std::memcpy(ram.data() + (a & 0xFFFFFF), s.c_str(), s.size() + 1); }
    uint32_t u32(uint32_t a) const {
        uint32_t v;
        std::memcpy(&v, ram.data() + (a & 0xFFFFFF), 4);
        return v;
    }
};

[[noreturn]] void throwingFatal(Sh4Context*, const char* what, uint32_t, uint32_t) { throw std::runtime_error(what); }

} // namespace

// Contratos da API gdFs (docs/N1-PLANO.md, secao 2) sobre o sistema de arquivos nativo.
BS_TEST(n1_gdfs_adapter_contracts) {
    TempTree t;
    const std::string a = pattern(5000, 'a');  // 3 setores da API (2048 + 2048 + 904)
    t.write("disc/BSDATA/A.BIN", a);
    nf::DataFileSystem fsys(t.root / "disc");
    Guest              g;
    gd::install(g.registry, fsys);
    BS_CHECK(g.registry.count(transitional::Provider::Native) == std::size(kAll));
    file_log_init("bs_n1_log_teste.tsv");

    g.str(0x8C100000, "\\BSDATA");
    BS_CHECK(g.call(kA.changeDir, 0x8C100000) == 0);
    g.str(0x8C100000, "\\NADA");
    BS_CHECK(g.call(kA.changeDir, 0x8C100000) == uint32_t(-1));
    g.str(0x8C100010, "a.bin");  // relativo ao diretorio corrente, minusculas
    const uint32_t h = g.call(kA.open, 0x8C100010, 0);
    BS_CHECK(gd::is_native_handle(h));
    BS_CHECK(g.call(kA.getStat, h) == 0);  // ocioso
    BS_CHECK(g.call(kA.getFileSize, h, 0x8C100100) == 1 && g.u32(0x8C100100) == 5000);
    BS_CHECK(g.call(kA.getFileSctSize, h, 0x8C100100) == 1 && g.u32(0x8C100100) == 3);

    // leitura sincrona de 2 unidades; posicao avanca
    BS_CHECK(g.call(kA.read, h, 2, 0x8C200000) == 0);
    BS_CHECK(std::memcmp(g.ram.data() + 0x200000, a.data(), 4096) == 0);
    BS_CHECK(g.call(kA.tell, h) == 2 && g.call(kA.getStat, h) == 1 && g.call(kA.getTransStat, h) == 4096);
    // pedido assincrono alem do fim: recortado a 1 unidade, ultimo setor completado com zeros
    std::memset(g.ram.data() + 0x300000, 0xEE, 4096);
    // contrato do binario (8C15B966/8C15B978): o pedido fica ocupado (2) com 0 bytes ate o
    // servidor do gdFs rodar; outro pedido com o drive ocupado devolve 0
    BS_CHECK(g.call(kA.reqRd32, h, 5, 0x8C300000) == 1);
    BS_CHECK(g.call(kA.getStat, h) == 2 && g.call(kA.getTransStat, h) == 0 && g.call(kA.tell, h) == 2);
    BS_CHECK(g.ram[0x300000] == 0xEE);  // nada transferido ainda
    BS_CHECK(g.call(kA.reqRd32, h, 1, 0x8C300000) == 0);
    g_legacyCalls = 0;
    g.call(kA.server);  // servidor: conclui o pedido nativo e segue com o legado
    BS_CHECK(g_legacyCalls == 1 && g.call(kA.tell, h) == 3);
    BS_CHECK(std::memcmp(g.ram.data() + 0x300000, a.data() + 4096, 904) == 0);
    BS_CHECK(g.ram[0x300000 + 904] == 0 && g.ram[0x300000 + 2047] == 0 && g.ram[0x300000 + 2048] == 0xEE);
    BS_CHECK(g.call(kA.getStat, h) == 1 && g.call(kA.getTransStat, h) == 2048);
    // no fim: 0 unidades, concluido
    BS_CHECK(g.call(kA.reqRd32, h, 1, 0x8C300000) == 0 && g.call(kA.getStat, h) == 1);
    BS_CHECK(g.call(kA.eof, h, 0x8C100100) == 0 && g.u32(0x8C100100) == 1);
    // erros: alinhamento, origem, faixa
    BS_CHECK(g.call(kA.reqRd32, h, 1, 0x8C300004) == uint32_t(-14));
    BS_CHECK(g.call(kA.seek, h, 0, 3) == uint32_t(-16));
    BS_CHECK(g.call(kA.seek, h, 1, 2) == uint32_t(-17));
    BS_CHECK(g.call(kA.seek, h, uint32_t(-1), 2) == 0 && g.call(kA.tell, h) == 2);
    BS_CHECK(g.call(kA.seek, h, 0, 0) == 0 && g.call(kA.tell, h) == 0);
    BS_CHECK(g.call(kA.stop, h) == 0 && g.call(kA.getStat, h) == 0);
    // Stop cancela o pedido pendente: o servidor nao transfere nada depois
    std::memset(g.ram.data() + 0x380000, 0xEE, 2048);
    BS_CHECK(g.call(kA.reqRd32, h, 1, 0x8C380000) == 1 && g.call(kA.stop, h) == 0);
    g.call(kA.server);
    BS_CHECK(g.ram[0x380000] == 0xEE && g.call(kA.tell, h) == 0 && g.call(kA.getStat, h) == 0);
    BS_CHECK(g.call(kA.setTransMode, h, 1) == 0 && g.call(kA.getErr, h) == 0);
    // log de leituras: blocos de 2 048 com o nome logico
    const std::string log = file_log_dump();
    file_log_init("");
    BS_CHECK(log.find("nativo\tdado\tBSDATA/A.BIN\t0\t2048\t") != std::string::npos);
    BS_CHECK(log.find("nativo\tdado\tBSDATA/A.BIN\t4096\t904\t") != std::string::npos);
    // fechar invalida o handle
    g.call(kA.close, h);
    BS_CHECK(g.call(kA.getStat, h) == uint32_t(-10) && g.call(kA.getFileSize, h, 0x8C100100) == 0);
    BS_CHECK(g.call(kA.read, h, 1, 0x8C200000) == uint32_t(-10));
    // inexistente: 0
    g.str(0x8C100010, "NAO.BIN");
    BS_CHECK(g.call(kA.open, 0x8C100010, 0) == 0 && gd::stats().notFound == 1);
    BS_CHECK(gd::stats().opens == 1 && gd::stats().reads == 2 && gd::stats().bytes == 5000);
}

// GDFS legado (ponteiro na RAM) vai para a funcao original; handle nativo numa funcao sem
// equivalente nativo e erro fatal com mensagem.
BS_TEST(n1_gdfs_adapter_legacy_passthrough_and_guards) {
    TempTree t;
    t.write("disc/BSDATA/A.BIN", "abc");
    nf::DataFileSystem fsys(t.root / "disc");
    Guest              g;
    gd::install(g.registry, fsys);
    g_legacyCalls = 0;
    BS_CHECK(g.call(kA.getStat, 0x8C2972B4) == 0x1234 && g_legacyCalls == 1);
    BS_CHECK(g.call(kA.getFileFad, 0x8C2972B4, 0x8C100100) == 0x1234 && g_legacyCalls == 2);
    g.str(0x8C100010, "\\BSDATA\\A.BIN");
    const uint32_t h = g.call(kA.open, 0x8C100010, 0);
    sh4_set_fatal_handler(&throwingFatal);
    BS_CHECK(throwsWith([&] { g.call(kA.getFileFad, h, 0x8C100100); }, "gdFsGetFileFad"));
    BS_CHECK(throwsWith([&] { g.call(kA.wait, h); }, "espera de pedido"));
    sh4_set_fatal_handler(nullptr);
    // registro de diretorio explicito: sem equivalente nativo, 0
    BS_CHECK(g.call(kA.open, 0x8C100010, 0x8C100200) == 0);
}

// Sem slot (codigo gerado sem [native] slots), instalar e erro: chamadas diretas iriam ao legado
// com handles nativos.
BS_TEST(n1_gdfs_adapter_requires_slots) {
    TempTree t;
    t.write("disc/X.BIN", "x");
    nf::DataFileSystem fsys(t.root / "disc");
    const Sh4FuncEntry table[] = {{kA.open & 0x1FFFFFFF, &legacyFn}};
    sh4_set_function_table(table, 1);
    BoundaryRegistry r;
    BS_CHECK(throwsWith([&] { gd::install(r, fsys); }, "nao tem slot"));
    BS_CHECK(r.count(transitional::Provider::Native) == 0);
    sh4_set_function_table(nullptr, 0);
}

// Fronteira: uma funcao com provedor nativo nao executa o corpo legado; o alcance de hardware
// de quem a chama nao passa por ela (legado.subsistema.* mede so o que resta).
BS_TEST(n1_hw_reach_stops_at_native_provider) {
    using namespace bs::hwscan;
    constexpr uint32_t G = 0x8C010000, W = 0x8C150000, O = 0x8C15BF60, D = 0x8C15C016;
    Scan  s;
    SdkId id;
    id.functions[G].layer = Layer::Game;
    for (uint32_t f : {W, O, D}) id.functions[f].layer = Layer::Sdk;
    s.edges = {{G, W, 0, EdgeKind::Call}, {W, O, 0, EdgeKind::Call}, {G, D, 0, EdgeKind::Call}};
    s.refs  = {{O, 0, 0x8C0000BC, 4, Kind::Read, dcmap::Region::BiosGdVector},
               {D, 0, 0x8C0000BC, 4, Kind::Read, dcmap::Region::BiosGdVector}};
    BS_CHECK(derive_boundary(s, id).at(W).subsystems.count(dcmap::Subsystem::Files));
    const auto b = derive_boundary(s, id, {O});
    BS_CHECK(b.at(W).subsystems.empty());                             // so passava pelo gdFsOpen
    BS_CHECK(b.at(D).subsystems.count(dcmap::Subsystem::Files));      // estado do drive: legado
}

// Revisao Codex 2026-10-15: emitFunction respeita Options::always_slot (sem estado global).
BS_TEST(n1_codegen_always_slot_in_emit_function) {
    analysis::Image img;
    img.base = 0x8C010000;
    for (uint16_t w : {uint16_t(0xB002), uint16_t(0x0009), uint16_t(0x000B), uint16_t(0x0009), uint16_t(0x000B),
                       uint16_t(0x0009)}) {  // bsr f; nop; rts; nop | f: rts; nop
        img.bytes.push_back(uint8_t(w));
        img.bytes.push_back(uint8_t(w >> 8));
    }
    analysis::Config cfg;
    cfg.entry              = 0x8C010000;
    cfg.pointer_candidates = cfg.data_pointers = false;
    const auto      an = analysis::analyze(img, cfg);
    codegen::Options opt;
    opt.comments = false;
    BS_CHECK(codegen::emitFunction(img, an, an.functions.at(0x8C010000), opt).find("BSG_FN(8C010008)") != std::string::npos);
    opt.always_slot = {0x8C010008};
    BS_CHECK(codegen::emitFunction(img, an, an.functions.at(0x8C010000), opt).find("BSG_SLOT(8C010008)") != std::string::npos);
}

// Revisao Codex 2026-10-15 (nao verificado antes): mod que nao pode ser lido e erro claro com o
// caminho. No Windows, um arquivo aberto com acesso exclusivo por outro processo/handle.
BS_TEST(n1_data_files_unreadable_mod) {
    TempTree t;
    t.write("disc/BSDATA/A.BIN", "abc");
    t.write("mods/BSDATA/A.BIN", "xyz");
    const HANDLE lock = CreateFileW((t.root / "mods" / "BSDATA" / "A.BIN").c_str(), GENERIC_READ, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    BS_CHECK(lock != INVALID_HANDLE_VALUE);
    BS_CHECK(throwsWith([&] { nf::DataFileSystem f(t.root / "disc", t.root / "mods"); }, "BSDATA/A.BIN: nao pode ser lido"));
    CloseHandle(lock);
    nf::DataFileSystem ok(t.root / "disc", t.root / "mods");  // liberado: aplica
    BS_CHECK(ok.mods().size() == 1);
}

// O projeto exportado traz mods/assets/LEIA-ME.txt: documentacao na raiz nao e mod (achado da
// regressao do N1: o Builder recusava o pacote); o mesmo nome dentro de uma pasta continua erro.
BS_TEST(n1_data_files_mods_readme_ignored) {
    TempTree t;
    t.write("disc/BSDATA/A.BIN", "abc");
    t.write("mods/LEIA-ME.txt", "instrucoes");
    t.write("mods/README.md", "instrucoes");
    nf::DataFileSystem ok(t.root / "disc", t.root / "mods");
    BS_CHECK(ok.mods().empty());
    t.write("mods/BSDATA/LEIA-ME.txt", "x");
    BS_CHECK(throwsWith([&] { nf::DataFileSystem f(t.root / "disc", t.root / "mods"); }, "BSDATA/LEIA-ME.TXT: nao substitui"));
}
