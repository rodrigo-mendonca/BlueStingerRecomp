// N8.3: servico nativo das syscalls do sistema (ver syscall_service.h e docs/N8-3-BOOT.md).
#include "system/syscall_service.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "debug/file_log.h"
#include "runtime_data.h"
#include "system/system_area.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bs {
namespace {

// GDROM (r7) e o estado do pedido: os do modelo (gdrom/gdrom.cpp) no perfil instantaneo, para o mesmo hash
enum : uint32_t { kReqCmd = 0, kGetCmdStat = 1, kExecServer = 2, kInitSystem = 3, kGetDrvStat = 4 };
enum : int32_t { kOk = 0, kBusy = 1, kComplete = 2 };
enum : uint32_t { kCmdDmaRead = 0x11, kCmdInit = 0x18, kCmdGetVersion = 0x28 };
constexpr uint32_t kStatPause = 1;     // gd_drv_stat
constexpr uint32_t kDiscGdrom = 0x80;  // tipo de disco: GD-ROM (o IP.BIN declara GD-ROM)
constexpr uint32_t kFadOffset = 150;   // FAD = LBA + 150

struct Request {
    int32_t  status = kOk;
    uint32_t lastId = 0xFFFFFFFF, nextId = 1;
    uint32_t command = 0;
    uint32_t params[4] = {};
    uint32_t result[4] = {};
} g;

// FLASHROM: particao 2 (usuario), a unica que o jogo usa
constexpr uint32_t kFlashPart     = 2;
constexpr uint32_t kFlashPartBase = 0x1C000;
constexpr uint32_t kFlashPartSize = 16u << 10;
std::array<uint8_t, kFlashPartSize> g_part;

std::unique_ptr<disc::RuntimeDisc> g_data;
const SysDiscBackend*              g_backend = nullptr;
SyscallCounts                      g_counts;

void log(const char* what, const Sh4Context* c) {
    static const bool on = std::getenv("BS_SISTEMA_LOG") != nullptr;
    if (!on) return;
    std::fprintf(stderr, "[sistema] %s r4=%08X r5=%08X r6=%08X r7=%08X pr=%08X\n", what, c->r[4], c->r[5], c->r[6],
                 c->r[7], c->pr);
}

// Pedido fora do contrato: o componente que o atenderia nao existe no produto.
[[noreturn]] void removed(Sh4Context* c, const char* family, uint32_t entry, const char* detail, uint32_t value) {
    ++g_counts.refused;
    static std::string msg;
    char               buf[256];
    std::snprintf(buf, sizeof buf,
                  "%s fora do contrato do servico nativo (%s %08X, r4=%08X r6=%08X r7=%08X, pr=%08X): componente "
                  "removido do produto no N8.3 (o modelo HLE so existe no build de validacao)",
                  family, detail, value, c->r[4], c->r[6], c->r[7], c->pr);
    msg = buf;
    std::fprintf(stderr, "[sistema] %s\n", msg.c_str());
    sh4_fatal(c, msg.c_str(), entry, c->r[7]);
}

// Volta como um rts: PR precisa ser o retorno que o chamador espera.
void ret(Sh4Context* c, uint32_t entry) {
    if (c->pr != c->ret_expect) sh4_bad_return(c, entry, c->pr);
}

void writeVersion(Sh4Context* c, uint32_t dest) {
    // versao do driver de GD-ROM que o jogo le (a mesma do modelo; o ultimo byte e 0x02)
    static const char kVer[] = "GDC Version 1.10 1999-03-31 ";
    for (size_t i = 0; i + 1 < sizeof(kVer); ++i)
        sh4_write8(c, dest + uint32_t(i), i + 2 == sizeof(kVer) ? 0x02 : uint8_t(kVer[i]));
}

void readSectors(Sh4Context* c, uint32_t fad, uint32_t count, uint32_t dest) {
    uint8_t buf[2048];
    for (uint32_t k = 0; k < count; ++k, dest += 2048) {
        const uint32_t lba = fad + k - kFadOffset;
        bool           ok  = false;
        if (g_data && fad + k >= kFadOffset) {
            try {
                ok = g_data->readSector(lba, buf);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[sistema] dados do jogo: %s\n", e.what());
            }
        }
        if (!ok) {
            // os dados do jogo foram conferidos na abertura: um setor que falta e instalacao incompleta ou corrompida
            static std::string msg;
            msg = "dados do jogo: setor " + std::to_string(lba) + " ilegivel (instalacao incompleta ou corrompida; "
                  "confira com --verify-data ou gere de novo com o Builder)";
            std::fprintf(stderr, "[sistema] %s\n", msg.c_str());
            sh4_fatal(c, msg.c_str(), kSyscallGdrom, fad + k);
        }
        ++g_counts.sectors;
        if (g_file_log) {  // contrato do N1: setor -> (arquivo, deslocamento); o mesmo registro do modelo
            std::string path;
            uint64_t    off = 0, size = 0;
            if (g_data->locate(lba, path, off, size) && off < size) {
                std::transform(path.begin(), path.end(), path.begin(),
                               [](unsigned char ch) { return char(std::toupper(ch)); });
                file_log_chunk("gdrom", path, off, buf, size_t(std::min<uint64_t>(2048, size - off)));
            } else {
                file_log_other("gdrom", "setor", lba);
            }
        }
        for (uint32_t i = 0; i < 2048; i += 4) {
            uint32_t w;
            std::memcpy(&w, buf + i, 4);
            sh4_write32(c, dest + i, w);
        }
    }
}

void execute(Sh4Context* c) {
    switch (g.command) {
    case kCmdInit:
        ++g_counts.cmdInit;
        break;
    case kCmdGetVersion:
        ++g_counts.cmdVersion;
        writeVersion(c, g.params[0]);
        break;
    case kCmdDmaRead:
        ++g_counts.cmdDmaRead;
        readSectors(c, g.params[0] & 0xFFFFFF, g.params[1], g.params[2]);
        g.result[2] = g.params[1] * 2048;
        break;
    }
    g.status = kComplete;
}

void gdrom(Sh4Context* c) {
    log("GDROM", c);
    if (c->r[6] != 0) removed(c, "GDROM", kSyscallGdrom, "superfuncao", c->r[6]);
    uint32_t&      r0 = c->r[0];
    const uint32_t r4 = c->r[4], r5 = c->r[5];
    switch (c->r[7]) {
    case kReqCmd:
        if (r4 != kCmdInit && r4 != kCmdGetVersion && r4 != kCmdDmaRead) removed(c, "GDROM", kSyscallGdrom, "comando", r4);
        ++g_counts.gdReqCmd;
        if (g.status != kOk) {
            r0 = 0;  // ocupado
            break;
        }
        for (int i = 0; i < 4; ++i) g.params[i] = r5 ? sh4_read32(c, r5 + 4 * i) : 0;
        for (auto& r : g.result) r = 0;
        if (g.nextId == 0 || g.nextId == 0xFFFFFFFF) g.nextId = 1;
        g.lastId  = g.nextId++;
        g.status  = kBusy;
        g.command = r4;
        r0        = g.lastId;
        break;
    case kGetCmdStat:
        ++g_counts.gdCmdStat;
        for (int i = 0; i < 4; ++i) sh4_write32(c, r5 + 4 * i, g.result[i]);
        if (g.status == kOk || g.status == kBusy) {
            r0 = uint32_t(g.status);
        } else if (r4 != g.lastId) {
            r0 = kOk;  // nenhum pedido com esse id
        } else {
            r0       = uint32_t(g.status);  // concluido: informado uma vez
            g.status = kOk;
            g.lastId = 0xFFFFFFFF;
        }
        break;
    case kExecServer:
        ++g_counts.gdExecServer;
        if (g.status == kBusy) execute(c);
        break;
    case kInitSystem:
        ++g_counts.gdInit;
        g = {};
        break;
    case kGetDrvStat:
        ++g_counts.gdDrvStat;
        sh4_write32(c, r4, kStatPause);
        sh4_write32(c, r4 + 4, kDiscGdrom);
        r0 = kOk;
        break;
    default:
        removed(c, "GDROM", kSyscallGdrom, "funcao", c->r[7]);
    }
    ret(c, kSyscallGdrom);
}

bool inPart(uint32_t off, uint32_t n) {
    return off >= kFlashPartBase && off - kFlashPartBase <= kFlashPartSize && n <= kFlashPartSize - (off - kFlashPartBase);
}

void flashrom(Sh4Context* c) {
    log("FLASHROM", c);
    uint32_t&      r0 = c->r[0];
    const uint32_t r4 = c->r[4], r5 = c->r[5], r6 = c->r[6];
    switch (c->r[7]) {
    case 0:  // INFO(particao, [inicio, tamanho])
        if (r4 != kFlashPart) removed(c, "FLASHROM", kSyscallFlashrom, "particao", r4);
        ++g_counts.flashInfo;
        sh4_write32(c, r5, kFlashPartBase);
        sh4_write32(c, r5 + 4, kFlashPartSize);
        r0 = 0;
        break;
    case 1:  // READ(posicao, destino, bytes)
        if (!inPart(r4, r6)) removed(c, "FLASHROM", kSyscallFlashrom, "leitura em", r4);
        ++g_counts.flashRead;
        for (uint32_t i = 0; i < r6; ++i) sh4_write8(c, r5 + i, g_part[r4 - kFlashPartBase + i]);
        r0 = 0;
        break;
    case 2:  // WRITE(posicao, origem, bytes): a flash so apaga bits
        if (!inPart(r4, r6)) removed(c, "FLASHROM", kSyscallFlashrom, "escrita em", r4);
        ++g_counts.flashWrite;
        for (uint32_t i = 0; i < r6; ++i) g_part[r4 - kFlashPartBase + i] &= sh4_read8(c, r5 + i);
        r0 = r6;
        break;
    case 3:  // DELETE(inicio da particao)
        if (r4 != kFlashPartBase) removed(c, "FLASHROM", kSyscallFlashrom, "apagamento em", r4);
        ++g_counts.flashDelete;
        g_part.fill(0xFF);
        r0 = 0;
        break;
    default:
        removed(c, "FLASHROM", kSyscallFlashrom, "funcao", c->r[7]);
    }
    ret(c, kSyscallFlashrom);
}

void sysinfo(Sh4Context* c) {
    log("SYSINFO", c);
    if (c->r[7] != 3) removed(c, "SYSINFO", kSyscallSysinfo, "funcao", c->r[7]);
    ++g_counts.sysinfoId;
    c->r[0] = kSysinfoData;
    ret(c, kSyscallSysinfo);
}

void romfont(Sh4Context* c) { removed(c, "ROMFONT", kSyscallRomfont, "vetor", kBiosVecRomfont); }
void gdrom2(Sh4Context* c) { removed(c, "GDROM2", kSyscallGdrom2, "vetor", kBiosVecGdrom2); }
void misc(Sh4Context* c) { removed(c, "MISC", kSyscallMisc, "vetor", kBiosVecMisc); }

void     nativePoll(Sh4Context*) {}  // tudo termina dentro do EXEC_SERVER
uint64_t nativeEvents() { return 0; }
uint64_t nativeHash() {
    // a mesma sequencia do modelo (gdrom_state_hash); a transferencia em blocos nao existe aqui (zeros)
    uint64_t       h   = 1469598103934665603ull;
    const uint32_t f[] = {uint32_t(g.status), g.lastId, g.nextId, g.command, g.params[0], g.params[1], g.params[2],
                          g.params[3], g.result[0], g.result[1], g.result[2], g.result[3], 0, 0, 0, 0};
    for (uint32_t v : f) h = (h ^ v) * 1099511628211ull;
    h = (h ^ 0ull) * 1099511628211ull;
    return h;
}

} // namespace

void syscall_service_install(Sh4Context* c) {
    const struct {
        uint32_t vec, addr;
        Sh4Func  fn;
    } k[] = {{kBiosVecSysinfo, kSyscallSysinfo, sysinfo},    {kBiosVecRomfont, kSyscallRomfont, romfont},
             {kBiosVecFlashrom, kSyscallFlashrom, flashrom}, {kBiosVecGdrom, kSyscallGdrom, gdrom},
             {kBiosVecGdrom2, kSyscallGdrom2, gdrom2},       {kBiosVecMisc, kSyscallMisc, misc}};
    for (const auto& e : k) {
        sh4_register_hle(e.addr, e.fn);
        sh4_write32(c, e.vec, e.addr);
    }
    g_backend = nullptr;
}

void syscall_service_reset() {
    g = {};
    g_part.fill(0xFF);
    g_counts = {};
}

void syscall_service_set_data(std::unique_ptr<disc::RuntimeDisc> data) { g_data = std::move(data); }

void sys_disc_use(const SysDiscBackend* b) { g_backend = b; }
void sys_disc_poll(Sh4Context* c) { g_backend ? g_backend->poll(c) : nativePoll(c); }
uint64_t sys_disc_state_hash() { return g_backend ? g_backend->state_hash() : nativeHash(); }
uint64_t sys_disc_event_count() { return g_backend ? g_backend->event_count() : nativeEvents(); }

const SyscallCounts& syscall_service_counts() { return g_counts; }

} // namespace bs
