// bs-diff: testes diferenciais do codigo recompilado contra estados capturados
// no Flycast (tools/dev/flycast_oracle.py).
//
//   bs-diff call  <func> <entrada.bss> <saida.bss>   executa a funcao recompilada
//                                                    a partir de <entrada> e compara
//                                                    com o estado do Flycast em <saida>
//   bs-diff trace <func> <entrada.bss> <trace.tsv> [max]
//                                                    grava cada entrada de funcao
//                                                    (endereco + registradores), ate
//                                                    `max` chamadas (padrao 100000)
//   bs-diff until <func> <entrada.bss> <esperado.bss> <alvo> <n>
//                                                    roda <func> ate a n-esima entrada
//                                                    de <alvo> e compara PC, registradores,
//                                                    FPU, 16 MB de RAM e, se existir
//                                                    <esperado>.vram, os 8 MB de VRAM
//                                                    (checkpoint `snap --hit` do oraculo).
//                                                    <alvo> = STARTRENDER: para na n-esima
//                                                    escrita em STARTRENDER, antes de ela
//                                                    ser guardada (oraculo: breakpoint na
//                                                    instrucao que escreve).
//                                                    Comparacao PARCIAL: o snapshot nao tem
//                                                    SGR, DBR, XF (o stub GDB do Flycast nao
//                                                    os expoe), ciclos nem registradores
//                                                    de hardware.
//
// Codigo de saida: 0 = igual, 1 = divergencia ou erro fatal, 2 = uso.
#include "debug/snapshot.h"
#include "funcs.h"
#include "aica/aica_regs.h"
#include "maple/maple.h"
#include "gdrom/image_source.h"
#include "hle/bios.h"
#include "hle/flashrom.h"
#include "boot/disc_boot.h"
#include "boot/native_boot.h"
#include "input/input.h"
#include "cpu/sh4_threads.h"
#include "pvr/pvr_regs.h"
#include "pvr/vram.h"
#include "system/graphics_ports.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace bs;

namespace {

struct FatalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Fim pedido da execucao (limite do trace ou checkpoint alcancado): nao e erro.
struct Stop {};

void onFatal(Sh4Context*, const char* what, uint32_t pc, uint32_t extra) {
    char b[256];
    std::snprintf(b, sizeof(b), "%s em PC=%08X (valor %08X)", what, pc, extra);
    throw FatalError(b);
}

std::FILE* g_traceOut   = nullptr;
size_t     g_traceCount = 0;
size_t     g_traceMax   = 100000;
uint32_t   g_untilFunc  = 0;
bool       g_untilStartRender = false;  // alvo "STARTRENDER": para na n-esima escrita

size_t     g_untilHit   = 0;
size_t     g_untilSeen  = 0;
void onStartRender(Sh4Context*, int n) {
    if (g_untilStartRender && size_t(n) == g_untilHit) throw Stop{};
}

// BS_TRACE_FROM=<funcao>:<n>: o trace so comeca na n-esima entrada da funcao (alinha
// com `flycast_oracle.py trace <funcao> --hit n`); `max` conta so as linhas gravadas.
uint32_t g_fromFunc = 0;
size_t   g_fromHit = 0, g_fromSeen = 0, g_traced = 0;

void onEnter(Sh4Context* c, uint32_t entry) {
    if (g_traceOut && g_traced == g_traceMax) throw Stop{};
    if (g_untilHit && !g_untilStartRender && entry == g_untilFunc && ++g_untilSeen == g_untilHit) {
        c->pc = entry;
        throw Stop{};
    }
    ++g_traceCount;
    if (!g_traceOut) return;
    if (g_fromHit && g_fromSeen < g_fromHit && !(entry == g_fromFunc && ++g_fromSeen == g_fromHit)) return;
    ++g_traced;
    std::fprintf(g_traceOut, "%08X", entry);
    for (int i = 0; i < 16; ++i) std::fprintf(g_traceOut, "\t%08X", c->r[i]);
    std::fprintf(g_traceOut, "\t%08X\t%08X\t%08X\t%08X\n", c->pr, sh4_get_sr(*c), c->fpscr, c->fpul);
}

// Le exatamente `n` bytes de um arquivo de exatamente `n` bytes (capturas do oraculo).
bool readExact(const std::filesystem::path& p, void* out, size_t n) {
    std::error_code ec;
    if (std::filesystem::file_size(p, ec) != n || ec) return false;
    std::ifstream f(p, std::ios::binary);
    return bool(f.read(static_cast<char*>(out), std::streamsize(n)));
}

// Agrupa palavras divergentes em faixas contiguas: "base+inicio..fim (n palavras)".
void printRanges(const char* name, uint32_t base, const std::vector<uint8_t>& e, const std::vector<uint8_t>& a) {
    size_t total = 0, shown = 0;
    for (size_t off = 0; off + 4 <= e.size() && off + 4 <= a.size();) {
        if (std::memcmp(&e[off], &a[off], 4) == 0) {
            off += 4;
            continue;
        }
        size_t end = off;
        while (end + 4 <= e.size() && std::memcmp(&e[end], &a[end], 4) != 0) end += 4;
        total += (end - off) / 4;
        if (shown++ < 40) {
            uint32_t x, y;
            std::memcpy(&x, &e[off], 4);
            std::memcpy(&y, &a[off], 4);
            std::printf("  %s %08X..%08X (%zu palavras) primeira: flycast=%08X recompilado=%08X\n", name,
                        base + uint32_t(off), base + uint32_t(end - 1), (end - off) / 4, x, y);
        }
        off = end;
    }
    if (shown > 40) std::printf("  ... %zu faixas no total\n", shown);
    std::printf("%s: %zu palavras divergentes em %zu faixas\n", name, total, shown);
}

int run(const char* mode, uint32_t func, const char* entryPath, const char* outPath) {
    bsg::register_functions();
    sh4_set_fatal_handler(onFatal);
    sh4_set_trace(onEnter);
    pvr_set_startrender_hook(onStartRender);

    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    // BS_NATIVE_BOOT=<imagem>: estado de entrada pelo boot nativo (runtime/boot), sem o
    // snapshot do Flycast (a <entrada> e ignorada); a hora do RTC e a do snapshot de
    // referencia, se houver, para comparar com o mesmo trace.
    // N8.4/N8.6: o bs-diff compara com o oraculo: os modelos (Maple com o controle, AICA), nao os servicos nativos
    periph_use(maple_bus_backend());
    snd_bus_use(aica_bus_backend());
    const char* nativeImage = std::getenv("BS_NATIVE_BOOT");
    if (nativeImage) {
        uint32_t rtc = boot_rtc_now();
        if (std::filesystem::exists(entryPath)) {
            const Snapshot ref = Snapshot::load(entryPath);
            std::memcpy(&rtc, &ref.ram[0x78], 4);
        }
        boot_prepare(c, boot_read_disc(nativeImage), rtc);  // RAM, CPU, PVR, RTC e vetores
        std::fprintf(stderr, "boot nativo de %s\n", nativeImage);
    } else {
        const Snapshot in = Snapshot::load(entryPath);
        in.apply(c);
        c.ret_expect = c.pr;
        c.next_event = 0;  // poll ativo desde o inicio: interrupcoes sao entregues como no jogo
        // O snapshot nao tem ciclos nem registradores de hardware (CCR, porta A, PVR,
        // Holly): tudo comeca do reset. Divergencia em funcao que leia hardware antes de
        // escreve-lo pode vir disso, e nao do codigo recompilado.
        std::fprintf(stderr, "aviso: estado de hardware e ciclos partem do reset (fora do snapshot)\n");
        // RTC: a hora que o reios do Flycast gravou no boot (0x8C000078, configuracao de
        // sistema da flash). O RTC do Flycast segue a hora do host e nunca coincide exatamente.
        uint32_t bootTime;
        std::memcpy(&bootTime, &ram[0x78], 4);
        aica_rtc_set(bootTime, 0);
    }
    // Registradores do PVR como o BIOS deixa na entrada do jogo (`snap 0x8C010000 --pvr`,
    // <entrada>.pvr): o boot le SPG_VBLANK antes de escreve-lo.
    if (!nativeImage) {
        std::filesystem::path pvr = entryPath;
        pvr.replace_extension(".pvr");
        if (std::filesystem::exists(pvr)) {
            std::vector<uint32_t> w(2048);
            if (!readExact(pvr, w.data(), 8192)) throw std::runtime_error("arquivo .pvr invalido: " + pvr.string());
            pvr_regs_load(w.data());
        } else {
            std::fprintf(stderr, "aviso: sem %s (registradores do PVR partem de zero)\n", pvr.string().c_str());
        }
    }
    // Syscalls do BIOS: o modelo HLE (N8.3: o bs-diff compara com o oraculo; o servico nativo e do jogo). Os vetores do
    // snapshot (reios) ja apontam para 0x8C001000+; no boot nativo, o boot os grava (os mesmos enderecos).
    gdrom_reset();
    bios_hle_install(&c, false);
    sys_disc_use(gdrom_disc_backend());
    // Disco: BS_DISC_IMAGE (a mesma imagem que o Flycast usou para gerar o snapshot).
    if (const char* disc = std::getenv("BS_DISC_IMAGE")) gdrom_set_disc(open_disc_image(disc));
    // Entrada: so roteiro (deterministico) ou nenhuma. Entrada ao vivo (BS_INPUT=host)
    // tornaria a comparacao nao reproduzivel; so com BS_DIFF_LIVE_INPUT=1, avisando.
    if (const char* inMode = std::getenv("BS_INPUT"); inMode && std::string(inMode) == "host" && !std::getenv("BS_DIFF_LIVE_INPUT")) {
        std::fprintf(stderr, "bs-diff: BS_INPUT=host recusado (nao deterministico); use BS_INPUT_SCRIPT ou "
                             "BS_DIFF_LIVE_INPUT=1\n");
        return 2;
    }
    if (std::getenv("BS_DIFF_LIVE_INPUT")) std::fprintf(stderr, "aviso: entrada ao vivo; resultado NAO deterministico\n");
    input_configure_from_env();
    // Flash: BS_FLASH_IMAGE (a do Flycast, data/dc_nvmem.bin, para comparar exatamente).
    if (const char* flash = std::getenv("BS_FLASH_IMAGE"); flash && !flashrom_load(flash))
        std::fprintf(stderr, "aviso: flash %s invalida (esperado 128 KB)\n", flash);

    // Diagnostico de laco infinito: BS_DIFF_WATCHDOG=<segundos> imprime o ultimo PC
    // registrado pelo poll (back-edges) e encerra.
    if (const char* w = std::getenv("BS_DIFF_WATCHDOG")) {
        const int secs = std::atoi(w);
        std::thread([secs, &c] {
            std::this_thread::sleep_for(std::chrono::seconds(secs));
            std::fprintf(stderr, "watchdog: %d s; pc=%08X pr=%08X sr=%08X r15=%08X chamadas=%zu\n", secs, c.pc, c.pr,
                         sh4_get_sr(c), c.r[15], g_traceCount);
            std::fflush(nullptr);
            std::_Exit(3);
        }).detach();
    }

    Sh4Func f = sh4_lookup(func);
    if (!f) {
        std::fprintf(stderr, "funcao %08X nao existe no codigo gerado\n", func);
        return 1;
    }
    const bool trace = std::strcmp(mode, "trace") == 0;
    const bool until = std::strcmp(mode, "until") == 0;
    if (trace) {
        g_traceOut = std::fopen(outPath, "w");
        if (!g_traceOut) { std::fprintf(stderr, "nao foi possivel criar %s\n", outPath); return 1; }
    }
    std::string fatal;
    bool        stopped = false;
    try {
        f(&c);
        // longjmp que nenhum quadro da thread 0 reconheceu (subiu ate aqui)
        if (c.redirect) sh4_fatal(&c, "longjmp sem quadro de destino nesta thread", c.pc, c.redirect);
    } catch (const FatalError& e) {
        fatal = e.what();
    } catch (const Stop&) {
        stopped = true;
    }
    if (g_traceOut) std::fclose(g_traceOut);
    if (trace && stopped) std::printf("limite de %zu chamadas atingido\n", g_traceMax);
    std::printf("funcoes executadas: %zu\n", g_traceCount);
    std::printf("threads do guest: %d\n", sh4_thread_count() ? sh4_thread_count() : 1);
    if (!fatal.empty()) {
        std::printf("ERRO FATAL: %s\n", fatal.c_str());
        return 1;
    }
    if (trace) return 0;
    if (until && !stopped) {
        std::printf("a funcao terminou antes da entrada %zu de %08X (vista %zu vezes)\n", g_untilHit, g_untilFunc,
                    g_untilSeen);
        return 1;
    }

    const Snapshot expected = Snapshot::load(outPath);
    const Snapshot actual   = Snapshot::capture(c, until ? c.pc : expected.regs.pc);
    // Diagnostico: BS_DIFF_SAVE=<arquivo> grava o estado recompilado (mesmo formato do oraculo)
    // (+ <arquivo>.vram, 8 MB no caminho de 32 bits, e <arquivo>.pvr, banco do PVR).
    if (const char* save = std::getenv("BS_DIFF_SAVE")) {
        actual.save(save);
        std::filesystem::path v = save, r = save;
        std::ofstream(v.replace_extension(".vram"), std::ios::binary)
            .write(reinterpret_cast<const char*>(texture_memory()), kVramSize);
        std::vector<uint32_t> bank(2048);
        pvr_regs_dump(&c, bank.data());
        std::ofstream(r.replace_extension(".pvr"), std::ios::binary)
            .write(reinterpret_cast<const char*>(bank.data()), 8192);
    }
    const auto     d        = compare(expected, actual);
    size_t         regDiffs = 0;
    for (const auto& x : d) {
        if (x.what.rfind("ram ", 0) == 0) continue;
        ++regDiffs;
        std::printf("  %-16s flycast=%08X recompilado=%08X\n", x.what.c_str(), x.expected, x.actual);
    }
    std::printf("registradores/FPU: %zu divergencias\n", regDiffs);
    // PC por endereco fisico: o Flycast pode estar no espelho P2 (AC...) do mesmo codigo
    // alvo STARTRENDER: o recompilado para dentro da escrita (sem PC de instrucao); o
    // oraculo para na instrucao que a faz. O PC nao e comparavel nesse modo.
    const bool pcOk = !until || g_untilStartRender || sh4_phys(expected.regs.pc) == sh4_phys(c.pc);
    if (!pcOk) std::printf("  pc               flycast=%08X recompilado=%08X\n", expected.regs.pc, c.pc);
    printRanges("ram", 0x0C000000, expected.ram, actual.ram);
    bool same = d.empty() && pcOk;

    std::filesystem::path vramPath = outPath;
    vramPath.replace_extension(".vram");
    if (std::filesystem::exists(vramPath)) {
        std::vector<uint8_t> ev(kVramSize);
        std::ifstream(vramPath, std::ios::binary).read(reinterpret_cast<char*>(ev.data()), ev.size());
        const std::vector<uint8_t> av(texture_memory(), texture_memory() + kVramSize);
        printRanges("vram32", 0x05000000, ev, av);
        same = same && ev == av;
    } else {
        std::printf("vram: sem %s (nao comparada)\n", vramPath.string().c_str());
    }

    // Registradores do PVR (oraculo `snap --pvr`): cada registrador divergente e listado;
    // SPG_STATUS (fase do video por ciclos estimados) e marcado, mas conta como divergencia.
    std::filesystem::path pvrPath = outPath;
    pvrPath.replace_extension(".pvr");
    if (std::filesystem::exists(pvrPath)) {
        std::vector<uint32_t> ep(2048), ap(2048);
        if (!readExact(pvrPath, ep.data(), 8192))
            throw std::runtime_error("arquivo .pvr invalido: " + pvrPath.string());
        pvr_regs_dump(&c, ap.data());
        size_t n = 0;
        for (uint32_t i = 0; i < 2048; ++i) {
            if (ep[i] == ap[i]) continue;
            const uint32_t addr = 0x005F8000 + 4 * i;
            if (n++ < 60)
                std::printf("  pvr %08X flycast=%08X recompilado=%08X%s\n", addr, ep[i], ap[i],
                            addr == 0x005F810C ? " (SPG_STATUS: fase do video)" : "");
        }
        std::printf("pvr: %zu registradores divergentes\n", n);
        same = same && n == 0;
    }
    std::printf(same ? "IGUAL\n" : "DIVERGENTE\n");
    return same ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const char* mode  = argc >= 2 ? argv[1] : "";
    const bool  ok    = (std::strcmp(mode, "call") == 0 && argc == 5) ||
                    (std::strcmp(mode, "trace") == 0 && (argc == 5 || argc == 6)) ||
                    (std::strcmp(mode, "until") == 0 && argc == 7);
    if (!ok) {
        std::fprintf(stderr, "uso: bs-diff call <func> <entrada.bss> <saida.bss>\n"
                             "     bs-diff trace <func> <entrada.bss> <trace.tsv> [max]\n"
                             "     bs-diff until <func> <entrada.bss> <esperado.bss> <alvo> <n>\n");
        return 2;
    }
    if (std::strcmp(mode, "trace") == 0 && argc == 6) g_traceMax = std::strtoull(argv[5], nullptr, 0);
    if (const char* from = std::getenv("BS_TRACE_FROM")) {
        char* end = nullptr;
        g_fromFunc = uint32_t(std::strtoul(from, &end, 0));
        g_fromHit  = (end && *end == ':') ? std::strtoull(end + 1, nullptr, 0) : 1;
    }
    if (std::strcmp(mode, "until") == 0) {
        g_untilStartRender = std::strcmp(argv[5], "STARTRENDER") == 0;
        g_untilFunc        = g_untilStartRender ? 0 : uint32_t(std::strtoul(argv[5], nullptr, 0));
        g_untilHit  = std::strtoull(argv[6], nullptr, 0);
        if (!g_untilHit) {
            std::fprintf(stderr, "n deve ser >= 1\n");
            return 2;
        }
    }
    try {
        return run(mode, uint32_t(std::strtoul(argv[2], nullptr, 0)), argv[3], argv[4]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
