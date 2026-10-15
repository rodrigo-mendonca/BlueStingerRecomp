// Dados de execucao a partir da imagem real (so com BS_DISC_IMAGE; senao 77 = SKIP).
// Prova que o jogo nativo nao precisa do CDI: os dados preparados reconstroem TODOS os
// setores de dados da imagem; o boot a partir deles e igual ao boot a partir do CDI; a
// fonte do GD-ROM le os mesmos setores; um arquivo alterado e detectado. Grava numa pasta
// temporaria, apagada no fim.
#include "../unit/test.h"

#include "boot/disc_boot.h"
#include "boot/native_boot.h"
#include "disc/cdi_image.h"
#include "gdrom/data_source.h"
#include "gdrom/image_source.h"
#include "runtime_data.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>

namespace fs = std::filesystem;
using namespace bs;

static int run() {
    const char* path = std::getenv("BS_DISC_IMAGE");
    if (!path || !*path) {
        std::printf("BS_DISC_IMAGE nao definido: teste ignorado\n");
        return 77;
    }
    const fs::path dir = fs::temp_directory_path() / "bs runtime data teste ç";
    std::error_code ec;
    fs::remove_all(dir, ec);
    {
        disc::CdiImage img(path);
        const auto     r = disc::prepare_runtime_data(img, dir);
        BS_CHECK(r.versionId == "bluestinger_us_v1000" && r.files == 2092);
        BS_CHECK(disc::verify_runtime_data(img, dir).empty());  // todos os setores de dados
        BS_CHECK(disc::check_runtime_files(dir).empty());
    }
    // boot: dados == CDI
    const BootFiles a = boot_read_disc(path), b = boot_read_data(dir);
    BS_CHECK(a.ipBin == b.ipBin && a.firstRead == b.firstRead && a.versionId == b.versionId);
    // fonte do GD-ROM: faixas e uma amostra de setores (inclusive o ISO e a faixa 1)
    {
        auto     cdi = open_disc_image(path);
        auto     dat = open_runtime_data(dir);
        BS_CHECK(cdi->tracks().size() == dat->tracks().size());
        for (size_t i = 0; i < cdi->tracks().size() && i < dat->tracks().size(); ++i)
            BS_CHECK(cdi->tracks()[i].start_fad == dat->tracks()[i].start_fad &&
                     cdi->tracks()[i].length == dat->tracks()[i].length && cdi->tracks()[i].ctrl == dat->tracks()[i].ctrl);
        uint8_t x[2048], y[2048];
        for (uint32_t fad : {150u + 0u, 150u + 25u, 150u + 239355u, 150u + 239371u, 150u + 300000u, 150u + 358702u}) {
            const bool okx = cdi->read(fad, x), oky = dat->read(fad, y);
            BS_CHECK(okx == oky && (!okx || std::memcmp(x, y, 2048) == 0));
        }
    }
    // arquivo alterado (mesmo tamanho): o --verify-data do jogo acusa
    {
        const fs::path f = dir / "disc" / "BSDATA" / "CDG000.NJ";
        std::fstream   s(f, std::ios::binary | std::ios::in | std::ios::out);
        s.seekp(100);
        s.put('\x5A');
        s.close();
        const std::string bad = disc::check_runtime_files(dir);
        BS_CHECK(bad.find("CDG000.NJ") != std::string::npos);
    }
    // setores de sistema alterados sem mudar o tamanho (hard links propagariam): detectados
    // (revisao Codex 2026-10-15)
    {
        std::fstream s(dir / "disc_meta.bin", std::ios::binary | std::ios::in | std::ios::out);
        s.seekp(4096);
        s.put('\x33');
        s.close();
        const std::string bad = disc::check_runtime_files(dir);
        BS_CHECK(bad.find("disc_meta.bin") != std::string::npos);
    }
    fs::remove_all(dir, ec);
    return bs::test::failures() ? 1 : 0;
}

int main() {
    try {
        const int rc = run();
        std::printf(rc == 0 ? "ok: dados de execucao identicos a imagem\n" : rc == 77 ? "" : "FALHOU\n");
        return rc;
    } catch (const std::exception& e) {
        std::printf("excecao: %s\n", e.what());
        return 1;
    }
}
