// N8.3: servico nativo das syscalls do sistema (runtime/system/syscall_service, docs/N8-3-BOOT.md). Dados sinteticos
// (sem nada do jogo). Prova o contrato (GD-ROM, flash, sysinfo), a equivalencia com o modelo HLE no mesmo contrato
// (r0, RAM e hash do estado a cada passo) e a recusa explicita de tudo o que saiu do produto.
#include "test.h"

#include "boot/native_boot.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "debug/file_log.h"
#include "gdrom/gdrom.h"
#include "hle/bios.h"
#include "hle/flashrom.h"
#include "runtime_data.h"
#include "system/syscall_service.h"
#include "system/system_area.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bs;

namespace {

struct Dir {
    fs::path p;
    explicit Dir(const wchar_t* name) {
        p = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p / L"disc");
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

void put(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream(p, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}

std::vector<uint8_t> filled(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = uint8_t(seed + i * 7);
    return v;
}

// faixa de dados 100..199 (FAD 250..349): metadado do volume em 100, A.BIN (3000 bytes) em 110
void writeSample(const fs::path& dir) {
    put(dir / L"disc" / L"A.BIN", filled(3000, 1));
    std::vector<uint8_t> idx(4);
    const uint32_t       lba = 100;
    std::memcpy(idx.data(), &lba, 4);
    put(dir / L"disc_meta.idx", idx);
    put(dir / L"disc_meta.bin", filled(2048, 50));
    std::ofstream m(dir / L"disc.manifest", std::ios::binary | std::ios::trunc);
    m << "bs-runtime-data 2\nversion teste\nboot A.BIN 00 0 100\nmeta x x\ntrack 1 1 100 100 1\nfile 110 3000 x A.BIN\n";
}

// o mesmo disco para o modelo HLE (DiscSource): os setores do RuntimeDisc
class SameDisc final : public DiscSource {
public:
    explicit SameDisc(const fs::path& dir) : d_(dir) { t_.push_back({1, 1, 250, 100, 4}); }
    const std::vector<DiscTrack>& tracks() const override { return t_; }
    bool read(uint32_t fad, uint8_t* out) override { return fad >= 150 && d_.readSector(fad - 150, out); }

private:
    disc::RuntimeDisc      d_;
    std::vector<DiscTrack> t_;
};

[[noreturn]] void throwingFatal(Sh4Context*, const char* what, uint32_t, uint32_t) { throw std::runtime_error(what); }

template <class F> std::string expectFatal(F&& fn) {
    sh4_set_fatal_handler(throwingFatal);
    std::string msg;
    try {
        fn();
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    sh4_set_fatal_handler(nullptr);
    return msg;
}

struct Sys {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    Sys() {
        c.ram = ram.data();
        syscall_service_reset();
        syscall_service_install(&c);
    }
    ~Sys() {
        syscall_service_set_data(nullptr);
        sys_disc_use(nullptr);
    }
    // chamada como o stub do SDK: r4-r7, salto pelo vetor, volta como rts
    uint32_t call(uint32_t vec, uint32_t r7, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0) {
        c.r[0] = 0xDEADBEEF, c.r[4] = r4, c.r[5] = r5, c.r[6] = r6, c.r[7] = r7;
        c.pr = c.ret_expect = 0x8C010100;
        sh4_dispatch(&c, 0, sh4_read32(&c, vec));
        return c.r[0];
    }
    uint32_t gd(uint32_t f, uint32_t r4 = 0, uint32_t r5 = 0) { return call(kBiosVecGdrom, f, r4, r5); }
};

constexpr uint32_t kParams = 0x8C200000, kStat = 0x8C200100, kBuf = 0x8C210000;

void dmaParams(Sh4Context& c, uint32_t fad, uint32_t n, uint32_t dest) {
    sh4_write32(&c, kParams, fad);
    sh4_write32(&c, kParams + 4, n);
    sh4_write32(&c, kParams + 8, dest);
    sh4_write32(&c, kParams + 12, 0);
}

} // namespace

// O ciclo do jogo no boot (medido): INIT_SYSTEM, GET_DRV_STAT, INIT, GET_VERSION e 2 leituras por DMA de 1 setor.
BS_TEST(n83_gdrom_contract_lifecycle) {
    Dir d(L"bs unit n83 gdrom");
    writeSample(d.p);
    Sys s;
    syscall_service_set_data(std::make_unique<disc::RuntimeDisc>(d.p));
    BS_CHECK(sh4_read32(&s.c, kBiosVecGdrom) == kSyscallGdrom && sh4_read32(&s.c, kBiosVecFlashrom) == kSyscallFlashrom);
    BS_CHECK(s.gd(3) == 0xDEADBEEF);  // INIT_SYSTEM nao mexe em r0
    BS_CHECK(s.gd(4, kStat) == 0 && sh4_read32(&s.c, kStat) == 1 && sh4_read32(&s.c, kStat + 4) == 0x80);
    uint32_t id = s.gd(0, 0x18, 0);  // INIT
    BS_CHECK(id == 1 && s.gd(1, id, kStat) == 1);
    s.gd(2);
    BS_CHECK(s.gd(1, id, kStat) == 2 && s.gd(1, id, kStat) == 0);
    sh4_write32(&s.c, kParams, kBuf);
    id = s.gd(0, 0x28, kParams);  // GET_VERSION
    s.gd(2);
    BS_CHECK(s.gd(1, id, kStat) == 2 && sh4_read8(&s.c, kBuf) == 'G' && sh4_read8(&s.c, kBuf + 27) == 0x02);
    const auto meta = filled(2048, 50), a = filled(3000, 1);
    dmaParams(s.c, 250, 1, kBuf);  // metadado (LBA 100)
    id = s.gd(0, 0x11, kParams);
    BS_CHECK(s.gd(0, 0x11, kParams) == 0);  // ocupado: um pedido por vez
    s.gd(2);
    BS_CHECK(s.gd(1, id, kStat) == 2 && sh4_read32(&s.c, kStat + 8) == 2048);
    BS_CHECK(std::memcmp(s.ram.data() + (kBuf & 0xFFFFFF), meta.data(), 2048) == 0);
    dmaParams(s.c, 260, 2, kBuf);  // A.BIN (LBA 110-111): o fim do arquivo vem com zeros
    id = s.gd(0, 0x11, kParams);
    s.gd(2);
    BS_CHECK(s.gd(1, id, kStat) == 2 && sh4_read32(&s.c, kStat + 8) == 4096);
    BS_CHECK(std::memcmp(s.ram.data() + (kBuf & 0xFFFFFF), a.data(), 3000) == 0 && s.ram[(kBuf & 0xFFFFFF) + 3000] == 0);
    const SyscallCounts& n = syscall_service_counts();
    BS_CHECK(n.gdInit == 1 && n.gdDrvStat == 1 && n.cmdInit == 1 && n.cmdVersion == 1 && n.cmdDmaRead == 2);
    BS_CHECK(n.sectors == 3 && n.refused == 0 && sys_disc_event_count() == 0);
}

// Equivalencia com o modelo HLE no contrato: a mesma sequencia de pedidos da o mesmo r0, a mesma RAM e o mesmo hash
// do estado (cri_idle) a cada passo.
BS_TEST(n83_gdrom_matches_hle_model_step_by_step) {
    Dir d(L"bs unit n83 equivalencia");
    writeSample(d.p);
    Sys nat;
    syscall_service_set_data(std::make_unique<disc::RuntimeDisc>(d.p));
    std::vector<uint8_t> ram2(16u << 20);
    Sh4Context           h{};
    h.ram = ram2.data();
    ram2  = nat.ram;  // os vetores que o boot grava
    gdrom_reset();
    gdrom_set_timing(false);
    gdrom_set_disc(std::make_unique<SameDisc>(d.p));
    auto hle = [&](uint32_t f, uint32_t r4 = 0, uint32_t r5 = 0) {
        h.r[0] = 0xDEADBEEF;
        gdrom_syscall(&h, f, r4, r5);
        return h.r[0];
    };
    struct Step {
        uint32_t f, r4, r5;
        uint32_t fad, n;  // > 0: parametros de leitura antes do passo
    } steps[] = {{3, 0, 0, 0, 0},       {4, kStat, 0, 0, 0},     {0, 0x18, 0, 0, 0},  {1, 1, kStat, 0, 0},
                 {2, 0, 0, 0, 0},       {1, 1, kStat, 0, 0},     {1, 1, kStat, 0, 0}, {0, 0x11, kParams, 250, 1},
                 {0, 0x11, kParams, 0, 0}, {1, 7, kStat, 0, 0},  {2, 0, 0, 0, 0},     {1, 2, kStat, 0, 0},
                 {0, 0x11, kParams, 260, 2}, {2, 0, 0, 0, 0},    {2, 0, 0, 0, 0},     {1, 3, kStat, 0, 0},
                 {3, 0, 0, 0, 0},       {0, 0x28, kParams, 0, 0}, {2, 0, 0, 0, 0},    {1, 1, kStat, 0, 0}};
    int k = 0;
    for (const auto& st : steps) {
        if (st.fad) {
            dmaParams(nat.c, st.fad, st.n, kBuf);
            dmaParams(h, st.fad, st.n, kBuf);
        } else if (st.r4 == 0x28) {
            sh4_write32(&nat.c, kParams, kBuf);
            sh4_write32(&h, kParams, kBuf);
        }
        const uint32_t a = nat.gd(st.f, st.r4, st.r5), b = hle(st.f, st.r4, st.r5);
        BS_CHECK(a == b);
        BS_CHECK(sys_disc_state_hash() == gdrom_state_hash());
        BS_CHECK(nat.ram == ram2);
        if (a != b || nat.ram != ram2) std::fprintf(stderr, "  passo %d (funcao %u) diverge\n", k, st.f);
        ++k;
    }
    gdrom_set_disc(nullptr);
}

// Flash: so a particao 2, volatil e em branco a cada sessao; mesmo efeito do modelo (AND na escrita).
BS_TEST(n83_flash_partition2_volatile_matches_model) {
    Sys s;
    std::vector<uint8_t> ram2(16u << 20);
    Sh4Context           h{};
    h.ram = ram2.data();
    ram2  = s.ram;  // os vetores que o boot grava
    std::fill_n(flashrom_data(), kFlashSize, uint8_t(0xFF));
    auto model = [&](uint32_t r7, uint32_t r4, uint32_t r5, uint32_t r6) {
        h.r[4] = r4, h.r[5] = r5, h.r[6] = r6, h.r[7] = r7, h.r[0] = 0xDEADBEEF;
        flashrom_syscall(&h);
        return h.r[0];
    };
    auto both = [&](uint32_t r7, uint32_t r4, uint32_t r5, uint32_t r6) {
        const uint32_t a = s.call(kBiosVecFlashrom, r7, r4, r5, r6), b = model(r7, r4, r5, r6);
        BS_CHECK(a == b);
        BS_CHECK(s.ram == ram2);
        return a;
    };
    BS_CHECK(both(0, 2, kStat, 0) == 0 && sh4_read32(&s.c, kStat) == 0x1C000 && sh4_read32(&s.c, kStat + 4) == 0x4000);
    BS_CHECK(both(1, 0x1C000, kBuf, 0x40) == 0 && sh4_read8(&s.c, kBuf) == 0xFF);
    BS_CHECK(both(3, 0x1C000, 0, 0) == 0);
    for (uint32_t i = 0; i < 0x12; ++i) {
        sh4_write8(&s.c, 0x8C220000 + i, uint8_t(0xA5 ^ i));
        sh4_write8(&h, 0x8C220000 + i, uint8_t(0xA5 ^ i));
    }
    BS_CHECK(both(2, 0x1C000, 0x8C220000, 0x12) == 0x12);
    sh4_write8(&s.c, 0x8C220000, 0x0F);
    sh4_write8(&h, 0x8C220000, 0x0F);
    BS_CHECK(both(2, 0x1C000, 0x8C220000, 1) == 1);  // a flash so apaga bits: 0xA5 & 0x0F
    BS_CHECK(both(1, 0x1C000, kBuf, 0x40) == 0 && sh4_read8(&s.c, kBuf) == 0x05 && sh4_read8(&s.c, kBuf + 0x12) == 0xFF);
    BS_CHECK(both(1, 0x1FFC0, kBuf, 0x40) == 0);  // fim da particao (o jogo le os ultimos 0x40 bytes)
    syscall_service_reset();  // nova sessao: em branco (volatil)
    BS_CHECK(s.call(kBiosVecFlashrom, 1, 0x1C000, kBuf, 0x40) == 0 && sh4_read8(&s.c, kBuf) == 0xFF);
    BS_CHECK(s.call(kBiosVecSysinfo, 3) == kSysinfoData);
    BS_CHECK(syscall_service_counts().refused == 0);
}

// Tudo o que saiu do produto e recusado com erro explicito (sem recuo silencioso) e contado.
BS_TEST(n83_out_of_contract_is_explicit_error) {
    Dir d(L"bs unit n83 recusa");
    writeSample(d.p);
    Sys s;
    syscall_service_set_data(std::make_unique<disc::RuntimeDisc>(d.p));
    struct Case {
        const char* what;
        uint32_t    vec, r7, r4, r6;
    } cases[] = {
        {"leitura por PIO", kBiosVecGdrom, 0, 0x10, 0},  {"TOC", kBiosVecGdrom, 0, 0x13, 0},
        {"sessao", kBiosVecGdrom, 0, 0x23, 0},           {"CD-DA", kBiosVecGdrom, 0, 0x14, 0},
        {"RESET do drive", kBiosVecGdrom, 9, 0, 0},      {"superfuncao", kBiosVecGdrom, 0, 0x18, 0xFFFFFFFF},
        {"particao 0", kBiosVecFlashrom, 0, 0, 0},       {"leitura da fabrica", kBiosVecFlashrom, 1, 0x1A056, 8},
        {"apagar outra", kBiosVecFlashrom, 3, 0x1A000, 0}, {"escrita fora", kBiosVecFlashrom, 2, 0x1FFF0, 0x20},
        {"funcao 4", kBiosVecFlashrom, 4, 0, 0},         {"SYSINFO INIT", kBiosVecSysinfo, 0, 0, 0},
        {"SYSINFO ICON", kBiosVecSysinfo, 2, 0, 0},      {"ROMFONT", kBiosVecRomfont, 0, 0, 0},
        {"GDROM2", kBiosVecGdrom2, 0, 0, 0},             {"MISC", kBiosVecMisc, 0, 2, 0},
    };
    sh4_write32(&s.c, kBiosVecRomfont, kSyscallRomfont);  // o boot nativo grava todos os vetores
    uint64_t n = 0;
    for (const auto& k : cases) {
        const std::string msg = expectFatal([&] { s.call(k.vec, k.r7, k.r4, kStat, k.r6); });
        const bool        ok  = msg.find("componente removido do produto") != std::string::npos;
        BS_CHECK(ok);
        BS_CHECK(syscall_service_counts().refused == ++n);
        if (!ok) std::fprintf(stderr, "  %s: [%s]\n", k.what, msg.c_str());
    }
}

// Dados que faltam: erro claro, nunca setor em branco.
BS_TEST(n83_missing_data_is_clear_error) {
    Sys s;
    dmaParams(s.c, 250, 1, kBuf);
    const uint32_t id = s.gd(0, 0x11, kParams);
    BS_CHECK(id == 1);
    std::string msg = expectFatal([&] { s.gd(2); });
    BS_CHECK(msg.find("setor 100 ilegivel") != std::string::npos);
    Dir d(L"bs unit n83 sem setor");
    writeSample(d.p);
    syscall_service_reset();
    syscall_service_set_data(std::make_unique<disc::RuntimeDisc>(d.p));
    dmaParams(s.c, 450, 1, kBuf);  // fora da faixa de dados
    s.gd(0, 0x11, kParams);
    msg = expectFatal([&] { s.gd(2); });
    BS_CHECK(msg.find("setor 300 ilegivel") != std::string::npos);
}

// Registro de leituras do N1: o mesmo do modelo (origem "gdrom": metadado como "outro", setor de arquivo como dado).
BS_TEST(n83_file_log_matches_model_contract) {
    Dir d(L"bs unit n83 registro");
    writeSample(d.p);
    Sys s;
    syscall_service_set_data(std::make_unique<disc::RuntimeDisc>(d.p));
    const fs::path log = d.p / L"arquivos.tsv";
    file_log_init(log.string());
    dmaParams(s.c, 250, 1, kBuf);
    s.gd(0, 0x11, kParams);
    s.gd(2);
    BS_CHECK(s.gd(1, 1, kStat) == 2);  // conclusao consumida: o drive aceita o proximo pedido
    dmaParams(s.c, 261, 1, kBuf);
    s.gd(0, 0x11, kParams);
    s.gd(2);
    const std::string dump = file_log_dump();
    file_log_init("");
    BS_CHECK(dump.find("gdrom\toutro\tsetor\t100") != std::string::npos);
    BS_CHECK(dump.find("gdrom\tdado\tA.BIN\t2048\t952") != std::string::npos);
    if (dump.find("A.BIN") == std::string::npos) std::fprintf(stderr, "%s", dump.c_str());
}

// Sem drive de GD-ROM nem DMA do G1: os registradores do drive nunca, e do G1 so a escrita GDST = 0 da tabela de
// inicializacao do SDK (registrador inerte). O resto e erro explicito.
BS_TEST(n83_gdrom_and_g1_registers_are_removed) {
    Sys s;
    sh4_write32(&s.c, 0xA05F7418, 0);  // tabela do SDK (8C0B8FB4): DMA do GD parado
    const char* bad[] = {"ler o status do drive", "escrever comando no drive", "ler o DMA do G1", "disparar o DMA do G1",
                         "ler o drive em 16 bits"};
    std::string m[5];
    m[0] = expectFatal([&] { (void)sh4_read8(&s.c, 0xA05F709C); });
    m[1] = expectFatal([&] { sh4_write8(&s.c, 0xA05F709C, 0xA0); });
    m[2] = expectFatal([&] { (void)sh4_read32(&s.c, 0xA05F7418); });
    m[3] = expectFatal([&] { sh4_write32(&s.c, 0xA05F7418, 1); });
    m[4] = expectFatal([&] { (void)sh4_read16(&s.c, 0xA05F7080); });
    for (int i = 0; i < 5; ++i) {
        const bool ok = m[i].find("componente removido do produto") != std::string::npos;
        BS_CHECK(ok);
        if (!ok) std::fprintf(stderr, "  %s: [%s]\n", bad[i], m[i].c_str());
    }
    BS_CHECK(sh4_read32(&s.c, 0xA05F6900) == sh4_read32(&s.c, 0xA05F6900));  // o resto do system block continua
}

// Boot nativo sem BIOS nem flash: boot_prepare instala o servico nativo (vetores, provedor nativo da fachada) e cada
// sessao comeca com a flash em branco e o drive ocioso.
BS_TEST(n83_native_boot_installs_service_per_session) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    BootFiles f;
    f.ipBin.assign(0x8000, 0);
    f.firstRead.assign(64, 0);
    sys_disc_use(gdrom_disc_backend());  // sessao anterior com o modelo (validacao)
    boot_prepare(c, f, 0);
    const uint32_t vec[] = {kBiosVecSysinfo, kBiosVecRomfont, kBiosVecFlashrom, kBiosVecGdrom, kBiosVecGdrom2, kBiosVecMisc};
    const uint32_t tgt[] = {kSyscallSysinfo, kSyscallRomfont, kSyscallFlashrom, kSyscallGdrom, kSyscallGdrom2, kSyscallMisc};
    for (int i = 0; i < 6; ++i) BS_CHECK(sh4_read32(&c, vec[i]) == tgt[i]);
    auto call = [&](uint32_t v, uint32_t r7, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0) {
        c.r[4] = r4, c.r[5] = r5, c.r[6] = r6, c.r[7] = r7;
        c.pr = c.ret_expect = 0x8C010100;
        sh4_dispatch(&c, 0, sh4_read32(&c, v));
        return c.r[0];
    };
    BS_CHECK(call(kBiosVecSysinfo, 3) == kSysinfoData);
    sh4_write8(&c, 0x8C220000, 0x5A);
    BS_CHECK(call(kBiosVecFlashrom, 2, 0x1C000, 0x8C220000, 1) == 1);
    BS_CHECK(call(kBiosVecFlashrom, 1, 0x1C000, 0x8C230000, 1) == 0 && sh4_read8(&c, 0x8C230000) == 0x5A);
    const uint64_t idle = sys_disc_state_hash();
    BS_CHECK(call(kBiosVecGdrom, 0, 0x18, 0) == 1 && sys_disc_state_hash() != idle);  // pedido pendente
    Sh4Context c2{};
    c2.ram = ram.data();
    boot_prepare(c2, f, 0);  // nova sessao
    BS_CHECK(sys_disc_state_hash() == idle);
    c2.r[4] = 0x1C000, c2.r[5] = 0x8C230000, c2.r[6] = 1, c2.r[7] = 1;
    c2.pr = c2.ret_expect = 0x8C010100;
    sh4_dispatch(&c2, 0, sh4_read32(&c2, kBiosVecFlashrom));
    BS_CHECK(c2.r[0] == 0 && sh4_read8(&c2, 0x8C230000) == 0xFF);  // flash volatil: em branco
}
