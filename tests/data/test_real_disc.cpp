// Teste com a imagem real do usuario. So roda se BS_DISC_IMAGE apontar para o
// .cdi; caso contrario retorna 77 (SKIP no CTest). Nao grava nada em disco.
#include "../unit/test.h"

#include "disc/cdi_image.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "known_versions.h"
#include "util/hash.h"

#include <cstdlib>
#include <cstring>

using namespace bs;

int main() {
    const char* path = std::getenv("BS_DISC_IMAGE");
    if (!path || !*path) {
        std::printf("BS_DISC_IMAGE nao definido: teste ignorado\n");
        return 77;
    }
    disc::CdiImage img(path);
    const auto&    boot = img.bootTrack();
    BS_CHECK(img.tracks().size() == 2);
    BS_CHECK(boot.start_lba == 239355);
    BS_CHECK(boot.sector_size == 2336);

    std::vector<uint8_t> ipbin(disc::kIpBinSize);
    for (uint32_t i = 0; i < 16; ++i) img.readUserData(boot.start_lba + i, &ipbin[i * 2048]);
    auto ip = disc::parseIpBin(ipbin.data(), ipbin.size());
    BS_CHECK(ip.product == "T13001N");
    BS_CHECK(ip.version == "V1.000");
    BS_CHECK(!ip.usesWindowsCE());

    disc::Iso9660 iso(img, boot.start_lba + 16);
    BS_CHECK(iso.volumeId() == "BLUE_DCRES");
    const auto* exe = iso.find("1ST_READ.BIN");
    BS_CHECK(exe && exe->size == kKnownVersions[0].boot_size);
    if (exe) {
        auto d = iso.readAll(*exe);
        BS_CHECK(util::sha256Hex(d.data(), d.size()) == kKnownVersions[0].boot_sha256);
    }

    // FMV na primeira sessao: cabecalho "Sofdec Stream" e primeiro pack MPEG em +0x2000.
    const auto* fmv = iso.find("BSDATA/ENDING.SFD");
    BS_CHECK(fmv && img.findTrack(fmv->lba) && img.findTrack(fmv->lba)->session == 1);
    if (fmv) {
        uint8_t s[2048];
        img.readUserData(fmv->lba, s);
        BS_CHECK(std::memcmp(s, "Sofdec Stream", 13) == 0);
        img.readUserData(fmv->lba + 4, s);
        BS_CHECK(s[0] == 0 && s[1] == 0 && s[2] == 1 && s[3] == 0xBA);
    }

    std::printf("[%s] real_disc\n", bs::test::failures() ? "FAIL" : " ok ");
    return bs::test::failures() ? 1 : 0;
}
