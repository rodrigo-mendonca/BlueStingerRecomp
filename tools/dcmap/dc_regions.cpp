#include "dc_regions.h"

namespace bs::dcmap {

const char* region_name(Region r) {
    switch (r) {
    case Region::None: return "nenhuma";
    case Region::BiosRom: return "bios_rom";
    case Region::Flash: return "flash";
    case Region::BiosVectors: return "bios_vetores";
    case Region::BiosGdVector: return "bios_vetor_gdrom";
    case Region::BiosFlashVector: return "bios_vetor_flash";
    case Region::SystemArea: return "area_sistema";
    case Region::HollySb: return "holly_sb";
    case Region::HollyIntc: return "holly_intc";
    case Region::Maple: return "maple";
    case Region::GdRom: return "gdrom";
    case Region::G1: return "g1";
    case Region::G2: return "g2";
    case Region::PvrDma: return "pvr_dma";
    case Region::Pvr: return "pvr";
    case Region::Modem: return "modem";
    case Region::AicaRegs: return "aica_regs";
    case Region::AicaRtc: return "aica_rtc";
    case Region::AicaRam: return "aica_ram";
    case Region::Vram64: return "vram64";
    case Region::Vram32: return "vram32";
    case Region::TaFifo: return "ta_fifo";
    case Region::TaYuv: return "ta_yuv";
    case Region::TaTexture: return "ta_textura";
    case Region::StoreQueue: return "store_queue";
    case Region::CacheArrays: return "cache_arrays";
    case Region::Ccn: return "ccn";
    case Region::Ubc: return "ubc";
    case Region::Bsc: return "bsc";
    case Region::Dmac: return "dmac";
    case Region::Cpg: return "cpg";
    case Region::Rtc: return "rtc_sh4";
    case Region::Intc: return "intc_sh4";
    case Region::Tmu: return "tmu";
    case Region::Sci: return "sci";
    case Region::Scif: return "scif";
    case Region::P4Other: return "p4_outro";
    }
    return "?";
}

const char* subsystem_name(Subsystem s) {
    switch (s) {
    case Subsystem::None: return "nenhum";
    case Subsystem::Files: return "arquivos";
    case Subsystem::Time: return "tempo";
    case Subsystem::Input: return "controles";
    case Subsystem::Saves: return "saves";
    case Subsystem::Video: return "video";
    case Subsystem::Audio: return "audio";
    case Subsystem::Render: return "render";
    case Subsystem::System: return "sistema";
    }
    return "?";
}

Subsystem region_subsystem(Region r) {
    switch (r) {
    case Region::None: return Subsystem::None;
    case Region::GdRom:
    case Region::G1:
    case Region::BiosGdVector: return Subsystem::Files;
    case Region::BiosFlashVector: return Subsystem::Saves;
    case Region::HollyIntc:
    case Region::Intc:
    case Region::Tmu:
    case Region::Rtc: return Subsystem::Time;
    case Region::Maple: return Subsystem::Input;
    case Region::Flash: return Subsystem::Saves;
    case Region::AicaRegs:
    case Region::AicaRtc:
    case Region::AicaRam:
    case Region::G2: return Subsystem::Audio;
    case Region::PvrDma:
    case Region::Pvr:
    case Region::Vram64:
    case Region::Vram32:
    case Region::TaFifo:
    case Region::TaYuv:
    case Region::TaTexture:
    case Region::StoreQueue:
    case Region::Dmac: return Subsystem::Render;
    default: return Subsystem::System;
    }
}

} // namespace bs::dcmap
