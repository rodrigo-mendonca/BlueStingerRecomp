// Dados de execucao (tools/extractor/runtime_data): o jogo nativo reconstroi os setores do
// disco a partir de arquivos, sem o CDI. Dados sinteticos (sem nada do jogo): composicao dos
// setores e mensagens claras quando algo falta.
#include "test.h"

#include "runtime_data.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::disc;

namespace {

struct Dir {
    fs::path p;
    explicit Dir(const wchar_t* name) {
        p = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p / L"disc" / L"SUB");
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

void put(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream(p, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}

std::vector<uint8_t> filled(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = uint8_t(seed + i * 7);
    return v;
}

// faixa de dados 100..199 e de audio 300..309; A.BIN em 110 (3000 bytes = 2 setores),
// SUB/B.BIN em 150 (2048 bytes); meta em 100 e 111 (sobra nao nula do ultimo setor de A)
void writeSample(const fs::path& dir, const std::string& extra = {}) {
    put(dir / L"disc" / L"A.BIN", filled(3000, 1));
    put(dir / L"disc" / L"SUB" / L"B.BIN", filled(2048, 9));
    std::vector<uint8_t> idx(8), bin;
    const uint32_t       lbas[2] = {100, 111};
    std::memcpy(idx.data(), lbas, 8);
    std::vector<uint8_t> m1 = filled(2048, 50), m2 = filled(2048, 60);
    bin.insert(bin.end(), m1.begin(), m1.end());
    bin.insert(bin.end(), m2.begin(), m2.end());
    put(dir / L"disc_meta.idx", idx);
    put(dir / L"disc_meta.bin", bin);
    std::ofstream m(dir / L"disc.manifest", std::ios::binary | std::ios::trunc);
    m << "bs-runtime-data 2\nversion teste\nboot A.BIN 00 0 100\nmeta x x\n"
      << "track 1 1 100 100 1\ntrack 2 2 300 10 0\n"
      << "file 110 3000 x A.BIN\nfile 150 2048 x SUB/B.BIN\n" << extra;
}

} // namespace

BS_TEST(runtime_data_sector_composition) {
    Dir d(L"bs unit runtime data ç");
    writeSample(d.p);
    RuntimeDisc rd(d.p);
    uint8_t     s[2048];
    const auto  a = filled(3000, 1), b = filled(2048, 9), m1 = filled(2048, 50), m2 = filled(2048, 60);
    BS_CHECK(rd.readSector(100, s) && std::memcmp(s, m1.data(), 2048) == 0);           // meta
    BS_CHECK(rd.readSector(110, s) && std::memcmp(s, a.data(), 2048) == 0);            // arquivo
    BS_CHECK(rd.readSector(111, s) && std::memcmp(s, m2.data(), 2048) == 0);           // meta vence o arquivo
    BS_CHECK(rd.readSector(150, s) && std::memcmp(s, b.data(), 2048) == 0);            // outro arquivo
    BS_CHECK(rd.readSector(120, s) && s[0] == 0 && s[2047] == 0);                      // sem nada: zeros
    BS_CHECK(!rd.readSector(305, s));                                                  // faixa de audio
    BS_CHECK(!rd.readSector(250, s));                                                  // fora das faixas
    BS_CHECK(rd.readFile("sub/b.bin") == b);                                           // sem diferenciar maiusculas
    BS_CHECK(rd.ipBin().size() == 0x8000);
}

BS_TEST(runtime_data_partial_last_sector_is_zero_padded) {
    Dir d(L"bs unit runtime data pad");
    writeSample(d.p);
    // sem a meta da sobra (111): o setor vem do arquivo, completado com zeros
    std::vector<uint8_t> idx(4), bin = filled(2048, 50);
    const uint32_t       lba = 100;
    std::memcpy(idx.data(), &lba, 4);
    put(d.p / L"disc_meta.idx", idx);
    put(d.p / L"disc_meta.bin", bin);
    RuntimeDisc rd(d.p);
    uint8_t     s[2048];
    const auto  a = filled(3000, 1);
    BS_CHECK(rd.readSector(111, s) && std::memcmp(s, a.data() + 2048, 3000 - 2048) == 0 && s[3000 - 2048] == 0 &&
             s[2047] == 0);
}

BS_TEST(runtime_data_clear_errors) {
    auto msg = [](const fs::path& dir) {
        try {
            RuntimeDisc rd(dir);
        } catch (const RuntimeDataError& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    {
        Dir d(L"bs unit runtime data vazio");
        const std::string m = msg(d.p);
        BS_CHECK(m.find("nao encontrados") != std::string::npos && m.find("Builder") != std::string::npos);
    }
    {
        Dir d(L"bs unit runtime data falta");
        writeSample(d.p);
        fs::remove(d.p / L"disc" / L"SUB" / L"B.BIN");
        const std::string m = msg(d.p);
        BS_CHECK(m.find("incompletos") != std::string::npos && m.find("SUB/B.BIN (ausente)") != std::string::npos);
    }
    {
        Dir d(L"bs unit runtime data tamanho");
        writeSample(d.p);
        put(d.p / L"disc" / L"A.BIN", filled(10, 1));
        BS_CHECK(msg(d.p).find("A.BIN (tamanho diferente)") != std::string::npos);
    }
    {
        Dir d(L"bs unit runtime data formato");
        writeSample(d.p);
        std::ofstream(d.p / L"disc.manifest", std::ios::binary | std::ios::trunc) << "bs-runtime-data 99\nversion x\n";
        BS_CHECK(msg(d.p).find("formato 99") != std::string::npos);
    }
    {
        Dir d(L"bs unit runtime data caminho");
        writeSample(d.p, "file 160 1 x ../fora.bin\n");
        BS_CHECK(msg(d.p).find("caminho invalido") != std::string::npos);  // nada fora de disc/
    }
    {
        Dir d(L"bs unit runtime data dispositivo");
        writeSample(d.p, "file 160 1 x SUB/CON.BIN\n");
        BS_CHECK(msg(d.p).find("caminho invalido") != std::string::npos);  // nome de dispositivo do Windows
    }
    {
        // arquivo apagado com o jogo rodando: erro claro na leitura
        Dir d(L"bs unit runtime data sumiu");
        writeSample(d.p);
        RuntimeDisc rd(d.p);
        fs::remove(d.p / L"disc" / L"SUB" / L"B.BIN");
        uint8_t     s[2048];
        std::string m;
        try {
            rd.readSector(150, s);
        } catch (const RuntimeDataError& e) {
            m = e.what();
        }
        BS_CHECK(m.find("ausente") != std::string::npos && m.find("B.BIN") != std::string::npos);
    }
}

// Mods de arquivo (mods/assets/<caminho>): o conteudo do mod substitui o do disco nos setores
// do arquivo (inclusive o ultimo, que no disco vem da meta); menor completa com zeros; maior
// que o espaco original e recusado com explicacao.
BS_TEST(runtime_data_asset_overrides) {
    Dir d(L"bs unit runtime data mods");
    writeSample(d.p);
    const fs::path mods = d.p / L"mods";
    fs::create_directories(mods / L"SUB");
    put(mods / L"A.BIN", filled(3000, 77));         // mesmo tamanho
    put(mods / L"SUB" / L"B.BIN", filled(100, 5));  // menor
    RuntimeDisc disc(d.p, mods);
    BS_CHECK(disc.overridden().size() == 2);
    uint8_t    s[2048];
    const auto a = filled(3000, 77), b = filled(100, 5), m1 = filled(2048, 50);
    BS_CHECK(disc.readSector(110, s) && std::memcmp(s, a.data(), 2048) == 0);
    BS_CHECK(disc.readSector(111, s) && std::memcmp(s, a.data() + 2048, 3000 - 2048) == 0 && s[2047] == 0);  // nao a meta
    BS_CHECK(disc.readSector(150, s) && std::memcmp(s, b.data(), 100) == 0 && s[100] == 0 && s[2047] == 0);
    BS_CHECK(disc.readSector(100, s) && std::memcmp(s, m1.data(), 2048) == 0);  // meta sem arquivo continua
    BS_CHECK(disc.readFile("sub/b.bin") == b);
    // maior que o espaco (A.BIN ocupa 2 setores = 4096 bytes)
    put(mods / L"A.BIN", filled(4097, 1));
    std::string m;
    try {
        RuntimeDisc big(d.p, mods);
    } catch (const RuntimeDataError& e) {
        m = e.what();
    }
    BS_CHECK(m.find("maior que o original") != std::string::npos && m.find("4096") != std::string::npos);
}
