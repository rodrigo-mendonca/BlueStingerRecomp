#pragma once
// Leitura de ISO9660 sobre DiscImage (LBAs absolutas do disco).
#include "disc/disc_image.h"

#include <functional>

namespace bs::disc {

struct IsoEntry {
    std::string path;  // relativo a raiz, separador '/', sem ";1"
    uint32_t    lba    = 0;
    uint32_t    size   = 0;
    bool        is_dir = false;
};

class Iso9660 {
public:
    // `pvd_lba`: LBA do Primary Volume Descriptor (inicio da faixa de dados + 16).
    Iso9660(const DiscImage& disc, uint32_t pvd_lba);

    const std::string&           volumeId() const { return volume_id_; }
    const std::vector<IsoEntry>& entries() const { return entries_; }  // so arquivos e diretorios, sem "." e ".."
    const IsoEntry*              find(const std::string& path) const;   // case-insensitive

    // Le o arquivo em blocos de ate 2048 bytes, em ordem.
    void read(const IsoEntry& e, const std::function<void(const uint8_t*, size_t)>& sink) const;
    std::vector<uint8_t> readAll(const IsoEntry& e) const;

private:
    void walk(uint32_t lba, uint32_t size, const std::string& prefix, int depth);

    const DiscImage&      disc_;
    std::string           volume_id_;
    std::vector<IsoEntry> entries_;
};

} // namespace bs::disc
