#include "debug/snapshot.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace bs {

namespace {
constexpr char     kMagic[4] = {'B', 'S', 'S', '1'};
constexpr uint32_t kRamSize  = 16u << 20;
} // namespace

Snapshot Snapshot::load(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("snapshot: nao foi possivel abrir " + p.string());
    char     magic[4];
    uint32_t version = 0, ramSize = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    f.read(reinterpret_cast<char*>(&ramSize), 4);
    if (!f || std::memcmp(magic, kMagic, 4) != 0 || version != 1 || ramSize != kRamSize)
        throw std::runtime_error("snapshot: formato invalido em " + p.string());
    Snapshot s;
    f.read(reinterpret_cast<char*>(&s.regs), sizeof(s.regs));
    s.ram.resize(ramSize);
    f.read(reinterpret_cast<char*>(s.ram.data()), ramSize);
    if (!f) throw std::runtime_error("snapshot: truncado " + p.string());
    return s;
}

void Snapshot::save(const std::filesystem::path& p) const {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    const uint32_t version = 1, ramSize = kRamSize;
    f.write(kMagic, 4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(&ramSize), 4);
    f.write(reinterpret_cast<const char*>(&regs), sizeof(regs));
    f.write(reinterpret_cast<const char*>(ram.data()), std::streamsize(ram.size()));
    if (!f) throw std::runtime_error("snapshot: falha ao gravar " + p.string());
}

void Snapshot::apply(Sh4Context& c) const {
    std::memcpy(c.ram, ram.data(), kRamSize);
    std::memcpy(c.r, regs.r, sizeof(c.r));
    std::memcpy(c.rbank, regs.rbank, sizeof(c.rbank));
    std::memcpy(c.fr, regs.fr, sizeof(c.fr));
    if (regs.flags & kSnapXfValid) std::memcpy(c.xf, regs.xf, sizeof(c.xf));
    c.pc = regs.pc; c.pr = regs.pr; c.gbr = regs.gbr; c.vbr = regs.vbr;
    c.mach = regs.mach; c.macl = regs.macl; c.fpul = regs.fpul;
    c.ssr = regs.ssr; c.spc = regs.spc;
    c.sr = regs.sr & kSrMask & ~kSrT;
    c.t  = regs.sr & kSrT;
    c.fpscr = regs.fpscr & kFpscrMask;
    sh4_apply_host_fp_mode(c.fpscr);
}

Snapshot Snapshot::capture(const Sh4Context& c, uint32_t pc) {
    Snapshot s;
    s.ram.assign(c.ram, c.ram + kRamSize);
    std::memcpy(s.regs.r, c.r, sizeof(c.r));
    std::memcpy(s.regs.rbank, c.rbank, sizeof(c.rbank));
    std::memcpy(s.regs.fr, c.fr, sizeof(c.fr));
    std::memcpy(s.regs.xf, c.xf, sizeof(c.xf));
    s.regs.flags = kSnapXfValid;
    s.regs.pc = pc; s.regs.pr = c.pr; s.regs.gbr = c.gbr; s.regs.vbr = c.vbr;
    s.regs.mach = c.mach; s.regs.macl = c.macl; s.regs.fpul = c.fpul;
    s.regs.ssr = c.ssr; s.regs.spc = c.spc;
    s.regs.sr = sh4_get_sr(c);
    s.regs.fpscr = c.fpscr;
    return s;
}

std::vector<Divergence> compare(const Snapshot& e, const Snapshot& a, bool compare_pc) {
    std::vector<Divergence> d;
    auto chk = [&](const std::string& what, uint32_t x, uint32_t y) {
        if (x != y) d.push_back({what, x, y});
    };
    for (int i = 0; i < 16; ++i) chk("r" + std::to_string(i), e.regs.r[i], a.regs.r[i]);
    for (int i = 0; i < 8; ++i) chk("r" + std::to_string(i) + "_bank", e.regs.rbank[i], a.regs.rbank[i]);
    if (compare_pc) chk("pc", e.regs.pc, a.regs.pc);
    chk("pr", e.regs.pr, a.regs.pr);
    chk("gbr", e.regs.gbr, a.regs.gbr);
    chk("vbr", e.regs.vbr, a.regs.vbr);
    chk("mach", e.regs.mach, a.regs.mach);
    chk("macl", e.regs.macl, a.regs.macl);
    chk("sr", e.regs.sr, a.regs.sr);
    chk("fpul", e.regs.fpul, a.regs.fpul);
    chk("fpscr", e.regs.fpscr, a.regs.fpscr);
    for (int i = 0; i < 16; ++i) chk("fr" + std::to_string(i), e.regs.fr[i], a.regs.fr[i]);
    if ((e.regs.flags & a.regs.flags) & kSnapXfValid)
        for (int i = 0; i < 16; ++i) chk("xf" + std::to_string(i), e.regs.xf[i], a.regs.xf[i]);
    chk("ssr", e.regs.ssr, a.regs.ssr);
    chk("spc", e.regs.spc, a.regs.spc);
    for (size_t off = 0; off + 4 <= e.ram.size() && off + 4 <= a.ram.size(); off += 4) {
        uint32_t x, y;
        std::memcpy(&x, &e.ram[off], 4);
        std::memcpy(&y, &a.ram[off], 4);
        if (x != y) {
            char b[32];
            std::snprintf(b, sizeof(b), "ram 0x%08X", 0x0C000000u + uint32_t(off));
            d.push_back({b, x, y});
        }
    }
    return d;
}

} // namespace bs
