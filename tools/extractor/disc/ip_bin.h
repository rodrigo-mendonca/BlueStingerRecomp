#pragma once
// Cabecalho do IP.BIN (primeiros 256 bytes da faixa de boot).
#include <cstdint>
#include <string>

namespace bs::disc {

constexpr uint32_t kIpBinSize = 0x8000;

struct IpBin {
    std::string hardware_id;    // "SEGA SEGAKATANA"
    std::string maker_id;
    std::string device_info;
    std::string area;           // "JUE"
    std::string peripherals;    // hexadecimal, bit 0 = Windows CE
    std::string product;        // "T13001N"
    std::string version;        // "V1.000"
    std::string release_date;   // "19990705"
    std::string boot_file;      // "1ST_READ.BIN"
    std::string company;
    std::string title;

    bool usesWindowsCE() const;
};

// Lanca DiscError se os primeiros 256 bytes nao forem um IP.BIN do Dreamcast.
IpBin parseIpBin(const uint8_t* data, size_t size);

} // namespace bs::disc
