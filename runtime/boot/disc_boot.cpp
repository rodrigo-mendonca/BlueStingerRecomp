#include "boot/disc_boot.h"

#include "disc/cdi_image.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "disc/scramble.h"
#include "known_versions.h"
#include "util/hash.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace bs {

BootFiles boot_read_disc(const std::filesystem::path& image) {
    BootFiles f;
    try {
        disc::CdiImage img(image);
        const auto&    boot = img.bootTrack();
        f.ipBin.resize(disc::kIpBinSize);
        for (uint32_t i = 0; i < disc::kIpBinSize / disc::kUserDataSize; ++i)
            img.readUserData(boot.start_lba + i, f.ipBin.data() + i * disc::kUserDataSize);
        const disc::IpBin ip = disc::parseIpBin(f.ipBin.data(), f.ipBin.size());
        f.product = ip.product, f.version = ip.version, f.title = ip.title;
        disc::Iso9660 iso(img, boot.start_lba + 16);
        const auto*   e = iso.find(ip.boot_file);
        if (!e || e->is_dir) throw std::runtime_error("executavel de boot " + ip.boot_file + " nao encontrado");
        std::vector<uint8_t> raw = iso.readAll(*e);
        // a versao suportada e identificada pelo hash do executavel desembaralhado
        for (int pass = 0; pass < 2 && f.versionId.empty(); ++pass) {
            std::vector<uint8_t> data = pass ? disc::descramble(raw) : raw;
            const std::string    h    = util::sha256Hex(data.data(), data.size());
            for (const auto& v : kKnownVersions)
                if (h == v.boot_sha256) {
                    f.versionId = v.id;
                    f.firstRead = std::move(data);
                }
        }
        if (f.versionId.empty())
            throw std::runtime_error("versao nao suportada: " + ip.product + " " + ip.version +
                                     " (o executavel de boot nao tem o hash da versao recompilada)");
    } catch (const disc::DiscError& err) {
        throw std::runtime_error(std::string("imagem de disco: ") + err.what());
    }
    return f;
}

} // namespace bs
