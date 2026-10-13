#include "disc/ip_bin.h"

#include "disc/disc_image.h"

#include <cctype>

namespace bs::disc {
namespace {

std::string field(const uint8_t* d, size_t off, size_t len) {
    std::string s;
    for (size_t i = 0; i < len; ++i) {
        const char c = static_cast<char>(d[off + i]);
        s += (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace

bool IpBin::usesWindowsCE() const {
    const unsigned long v = std::stoul(peripherals.empty() ? "0" : peripherals, nullptr, 16);
    return (v & 1) != 0;
}

IpBin parseIpBin(const uint8_t* d, size_t size) {
    if (size < 0x100) throw DiscError("IP.BIN truncado");
    IpBin ip;
    ip.hardware_id  = field(d, 0x00, 16);
    ip.maker_id     = field(d, 0x10, 16);
    ip.device_info  = field(d, 0x20, 16);
    ip.area         = field(d, 0x30, 8);
    ip.peripherals  = field(d, 0x38, 8);
    ip.product      = field(d, 0x40, 10);
    ip.version      = field(d, 0x4A, 6);
    ip.release_date = field(d, 0x50, 16);
    ip.boot_file    = field(d, 0x60, 16);
    ip.company      = field(d, 0x70, 16);
    ip.title        = field(d, 0x80, 128);
    if (ip.hardware_id != "SEGA SEGAKATANA") throw DiscError("IP.BIN invalido: hardware ID '" + ip.hardware_id + "'");
    for (char c : ip.peripherals)
        if (!std::isxdigit(static_cast<unsigned char>(c))) throw DiscError("IP.BIN: campo de perifericos invalido");
    return ip;
}

} // namespace bs::disc
