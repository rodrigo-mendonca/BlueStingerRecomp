#pragma once
// SHA-256 e CRC32 (implementacao propria, sem dependencias).
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bs::util {

class Sha256 {
public:
    Sha256();
    void                    update(const void* data, size_t n);
    std::array<uint8_t, 32> finish();
    std::string             hexdigest();  // finish() em hexadecimal minusculo

private:
    void block(const uint8_t* p);

    uint32_t h_[8];
    uint8_t  buf_[64];
    size_t   buflen_ = 0;
    uint64_t total_  = 0;
};

class Crc32 {
public:
    void     update(const void* data, size_t n);
    uint32_t value() const { return ~crc_; }

private:
    uint32_t crc_ = 0xFFFFFFFF;
};

std::string sha256Hex(const void* data, size_t n);
std::string hex32(uint32_t v);

} // namespace bs::util
