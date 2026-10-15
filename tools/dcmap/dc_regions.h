#pragma once
// Mapa de enderecos do hardware do Dreamcast (documentacao: KallistiOS e "Dreamcast/Dev.Box
// System Architecture"). Usado pelas ferramentas do N0 (manifesto estatico de acessos,
// sonda dinamica do runtime legado, metricas) para classificar um endereco por regiao e
// subsistema. So fatos de hardware; nada do jogo.
#include <cstdint>

namespace bs::dcmap {

// Subsistema do port nativo responsavel por substituir a regiao (docs/PLANO-PORT-NATIVO.md).
enum class Subsystem : uint8_t {
    None,       // RAM do jogo / codigo: nao e hardware
    Files,      // GD-ROM, sistema de arquivos (N1)
    Time,       // interrupcoes, timers, relogio (N2)
    Input,      // Maple / controles (N3)
    Saves,      // flash, VMU (N4)
    Video,      // reproducao de video (N5, via SFD; hardware usado = render)
    Audio,      // AICA, RAM de som (N6)
    Render,     // PowerVR2/TA/VRAM/store queues/DMA de geometria (N7)
    System,     // BIOS, CPU (cache, barramento, clock), demais perifericos
};

enum class Region : uint8_t {
    None,
    BiosRom, Flash, BiosVectors, BiosGdVector, BiosFlashVector, SystemArea,
    HollySb, HollyIntc, Maple, GdRom, G1, G2, PvrDma, Pvr, Modem,
    AicaRegs, AicaRtc, AicaRam,
    Vram64, Vram32,
    TaFifo, TaYuv, TaTexture,
    StoreQueue, CacheArrays, Ccn, Ubc, Bsc, Dmac, Cpg, Rtc, Intc, Tmu, Sci, Scif, P4Other,
};

const char* region_name(Region r);
const char* subsystem_name(Subsystem s);
Subsystem   region_subsystem(Region r);

// Regiao de um endereco virtual do SH-4 (P0-P3 espelham a area fisica; P4 e unico).
inline Region classify(uint32_t a) {
    if (a >= 0xE0000000u) {
        if (a < 0xE4000000u) return Region::StoreQueue;
        if (a >= 0xF0000000u && a < 0xF8000000u) return Region::CacheArrays;
        switch (a >> 20) {
        case 0xFF0: return Region::Ccn;
        case 0xFF2: return Region::Ubc;
        case 0xFF8: return Region::Bsc;
        case 0xFFA: return Region::Dmac;
        case 0xFFC: return (a & 0x000F0000u) == 0x00080000u ? Region::Rtc : Region::Cpg;
        case 0xFFD: return (a & 0x000F0000u) == 0x00080000u ? Region::Tmu : Region::Intc;
        case 0xFFE: return (a & 0x000F0000u) == 0x00080000u ? Region::Scif : Region::Sci;
        default: return Region::P4Other;
        }
    }
    const uint32_t p = a & 0x1FFFFFFFu;
    if (p < 0x00200000u) return Region::BiosRom;
    if (p < 0x00220000u) return Region::Flash;
    if (p >= 0x005F6800u && p < 0x005F6900u) return Region::HollySb;
    if (p >= 0x005F6900u && p < 0x005F6A00u) return Region::HollyIntc;
    if (p >= 0x005F6C00u && p < 0x005F6D00u) return Region::Maple;
    if (p >= 0x005F7000u && p < 0x005F7100u) return Region::GdRom;
    if (p >= 0x005F7400u && p < 0x005F7500u) return Region::G1;
    if (p >= 0x005F7800u && p < 0x005F7900u) return Region::G2;
    if (p >= 0x005F7C00u && p < 0x005F7D00u) return Region::PvrDma;
    if (p >= 0x005F8000u && p < 0x005FA000u) return Region::Pvr;
    if (p >= 0x00600000u && p < 0x00600800u) return Region::Modem;
    if (p >= 0x00700000u && p < 0x00704600u) return Region::AicaRegs;  // canais, comuns, DSP
    if (p >= 0x00710000u && p < 0x0071000Cu) return Region::AicaRtc;
    if (p >= 0x00800000u && p < 0x00A00000u) return Region::AicaRam;  // 2 MB
    if (p >= 0x04000000u && p < 0x05000000u) return Region::Vram64;
    if (p >= 0x05000000u && p < 0x06000000u) return Region::Vram32;
    if (p >= 0x10000000u && p < 0x10800000u) return Region::TaFifo;
    if (p >= 0x10800000u && p < 0x11000000u) return Region::TaYuv;
    if (p >= 0x11000000u && p < 0x14000000u) return Region::TaTexture;
    // RAM principal (0x0C000000-0x0CFFFFFF, espelhos ate 0x0FFFFFFF): os vetores de syscall do
    // BIOS (0x0C0000B0-0x0C0000EF) e a area do sistema (ate 0x0C00FFFF) sao interface com o BIOS
    if ((p & 0xFC000000u) == 0x0C000000u) {
        const uint32_t r = p & 0x00FFFFFFu;
        // syscalls: 0xBC GD-ROM, 0xB8 flash; 0xB0 sysinfo, 0xB4 fonte, 0xE0 menu e o resto
        if (r >= 0xBC && r < 0xC0) return Region::BiosGdVector;
        if (r >= 0xB8 && r < 0xBC) return Region::BiosFlashVector;
        if (r >= 0xB0 && r < 0xF0) return Region::BiosVectors;
        if (r < 0x00010000u) return Region::SystemArea;
    }
    return Region::None;
}

inline bool is_hardware(Region r) { return r != Region::None; }

} // namespace bs::dcmap
