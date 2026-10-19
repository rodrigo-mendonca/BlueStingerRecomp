// N4: saves nativos (native/platform/saves.h): formato VMS e CRC, envelope, armazenamento atomico,
// persistencia, erros, falhas de escrita e recuperacao. So amostras sinteticas (nenhum byte do jogo).
#include "test.h"

#include "platform/saves.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace sv = bs::native::saves;
using sv::SaveError;

namespace {

using Bytes = std::vector<std::byte>;

// VMS com o layout do save do jogo (cabecalho, 1 icone, payload de 1 200, zeros ate 2 048) e
// conteudo sintetico
Bytes makeVms(uint32_t seed, uint32_t payload = 1200) {
    std::mt19937 r(seed);
    Bytes        v(0x80 + 0x200 + payload);
    for (auto& b : v) b = std::byte(r());
    std::memcpy(v.data(), "SINTETICO       ", 16);
    auto put16 = [&](size_t o, uint16_t x) { v[o] = std::byte(x); v[o + 1] = std::byte(x >> 8); };
    put16(0x40, 1), put16(0x42, 0), put16(0x44, 0), put16(0x46, 0);
    for (int i = 0; i < 4; ++i) v[0x48 + i] = std::byte(payload >> (8 * i));
    put16(0x46, sv::vms_crc16(v));
    v.resize((v.size() + 511) / 512 * 512);
    return v;
}

Bytes text(const char* s) {
    Bytes v(std::strlen(s));
    std::memcpy(v.data(), s, v.size());
    return v;
}

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-saves-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(p, ec); }
};

Bytes readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<char> c((std::istreambuf_iterator<char>(f)), {});
    Bytes b(c.size());
    if (!c.empty()) std::memcpy(b.data(), c.data(), c.size());
    return b;
}
void writeFile(const fs::path& p, const Bytes& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

sv::FileStoreOptions fixedClock() {
    sv::FileStoreOptions o;
    o.now = [] { return int64_t(1700000000); };
    return o;
}

} // namespace

// CRC-16/XMODEM (o da rotina 8C08CDE2) e o layout do VMS do jogo: 2 048 bytes, payload em 0x280.
BS_TEST(saves_vms_crc_and_layout) {
    const Bytes nine = text("123456789");
    BS_CHECK(sv::vms_crc16(nine) == 0x31C3);
    const Bytes v = makeVms(7);
    std::string why;
    BS_CHECK(v.size() == 2048 && sv::vms_check(v, &why));
    for (size_t i = 0x730; i < v.size(); ++i) BS_CHECK(v[i] == std::byte{0});
    Bytes w = v;
    w[0x300] ^= std::byte{1};
    BS_CHECK(!sv::vms_check(w));  // payload alterado: CRC acusa
    w = v, w[0x7FF] = std::byte{1};
    BS_CHECK(!sv::vms_check(w));  // preenchimento nao zero
    w.assign(v.begin(), v.begin() + 1536);
    BS_CHECK(!sv::vms_check(w));  // truncado
    w = v, w[0x44] = std::byte{4};
    BS_CHECK(!sv::vms_check(w));  // eyecatch invalido
    w = v, w[0x40] = std::byte{0};
    BS_CHECK(!sv::vms_check(w));  // sem icone
}

BS_TEST(saves_names) {
    for (const char* ok : {"BLUESTIN.000", "BLUESTIN.005", "A", "SAVE_1.BIN"}) BS_CHECK(sv::valid_save_name(ok));
    for (const char* bad : {"", "bluestin.000", "..\\X", "../X", "A..B", ".BLUE", "BLUESTIN.0000X", "A/B", "A B", "C:X"})
        BS_CHECK(!sv::valid_save_name(bad));
}

// Envelope v1: ida e volta byte a byte; recusa de magic, versao futura, tamanhos, checksum, truncado.
BS_TEST(saves_envelope) {
    const Bytes v   = makeVms(3);
    const Bytes env = sv::envelope_encode(v, 1234567890123);
    BS_CHECK(env.size() == 32 + 2048);
    Bytes   back;
    int64_t t = 0;
    BS_CHECK(sv::envelope_decode(env, back, &t) == SaveError::None && back == v && t == 1234567890123);
    Bytes e = env;
    e[0] = std::byte{'X'};
    BS_CHECK(sv::envelope_decode(e, back) == SaveError::Corrupt);
    e = env, e[4] = std::byte{2};
    BS_CHECK(sv::envelope_decode(e, back) == SaveError::FutureVersion);
    e = env, e[4] = std::byte{0};
    BS_CHECK(sv::envelope_decode(e, back) == SaveError::Corrupt);
    e = env, e[40] ^= std::byte{0x80};
    BS_CHECK(sv::envelope_decode(e, back) == SaveError::Corrupt);  // imagem alterada: CRC-32
    e.assign(env.begin(), env.end() - 1);
    BS_CHECK(sv::envelope_decode(e, back) == SaveError::Corrupt);  // truncado
    e = env, e[8] = std::byte{0x01};  // tamanho da imagem 0x801: fora do multiplo de 512
    BS_CHECK(sv::envelope_decode(e, back) == SaveError::Corrupt);
    BS_CHECK(sv::envelope_decode(Bytes(10), back) == SaveError::Corrupt);
}

// Seis slots: gravar, listar, carregar, sobrescrever (sem consumir blocos), persistir numa nova
// abertura do store; estado do cartao coerente em cada passo.
BS_TEST(saves_store_six_slots_and_persistence) {
    TempDir d("slots");
    {
        auto s = sv::make_file_store(d.p, fixedClock());
        sv::CardState c;
        BS_CHECK(s->card(0, c) == SaveError::None && c.present && c.formatted && !c.read_only);
        BS_CHECK(c.total_blocks == 200 && c.free_blocks == 200 && c.free_entries == c.total_entries);
        Bytes img;
        BS_CHECK(s->load(0, "BLUESTIN.000", img) == SaveError::NotFound);
        for (int i = 0; i < 6; ++i) BS_CHECK(s->save_atomic(0, "BLUESTIN.00" + std::to_string(i), makeVms(i)) == SaveError::None);
        BS_CHECK(s->card(0, c) == SaveError::None && c.free_blocks == 200 - 24 && c.free_entries == c.total_entries - 6);
        std::vector<sv::SaveEntry> l;
        BS_CHECK(s->list(0, l) == SaveError::None && l.size() == 6);
        for (const auto& e : l) BS_CHECK(e.blocks == 4 && e.modified_unix == 1700000000);
        BS_CHECK(s->save_atomic(0, "BLUESTIN.002", makeVms(99)) == SaveError::None);  // sobrescrita
        BS_CHECK(s->card(0, c) == SaveError::None && c.free_blocks == 176);
        BS_CHECK(fs::exists(d.p / "card0" / "BLUESTIN.002.bsv.bak"));
    }
    auto s = sv::make_file_store(d.p, fixedClock());  // outro processo, na pratica
    for (int i = 0; i < 6; ++i) {
        Bytes img;
        BS_CHECK(s->load(0, "BLUESTIN.00" + std::to_string(i), img) == SaveError::None);
        BS_CHECK(img == makeVms(i == 2 ? 99 : uint32_t(i)));
    }
    for (const auto& e : fs::directory_iterator(d.p / "card0")) BS_CHECK(e.path().extension() != ".tmp");
}

BS_TEST(saves_store_errors) {
    TempDir d("erros");
    auto    s = sv::make_file_store(d.p, fixedClock());
    Bytes   img;
    sv::CardState c;
    BS_CHECK(s->card(1, c) == SaveError::NoCard && !c.present);  // um cartao so (unidade 0)
    BS_CHECK(s->save_atomic(1, "BLUESTIN.000", makeVms(1)) == SaveError::NoCard);
    BS_CHECK(s->save_atomic(0, "../BLUESTIN.000", makeVms(1)) == SaveError::BadName);
    BS_CHECK(s->load(0, "..\\..\\X", img) == SaveError::BadName);
    BS_CHECK(s->save_atomic(0, "BLUESTIN.000", Bytes(100)) == SaveError::BadImage);
    BS_CHECK(s->save_atomic(0, "BLUESTIN.000", Bytes()) == SaveError::BadImage);
    BS_CHECK(s->remove(0, "BLUESTIN.000") == SaveError::NotFound);
    // cheio: 197 blocos ocupados, um save de 4 nao cabe; sobrescrever o grande com o mesmo tamanho cabe
    BS_CHECK(s->save_atomic(0, "GRANDE", Bytes(197 * 512)) == SaveError::None);
    BS_CHECK(s->save_atomic(0, "BLUESTIN.000", makeVms(1)) == SaveError::NoSpace);
    BS_CHECK(s->load(0, "BLUESTIN.000", img) == SaveError::NotFound);
    BS_CHECK(s->save_atomic(0, "GRANDE", Bytes(197 * 512, std::byte{1})) == SaveError::None);
    BS_CHECK(s->remove(0, "GRANDE") == SaveError::None && s->card(0, c) == SaveError::None && c.free_blocks == 200);
    // pasta do cartao apagada por fora: sem cartao (nada recriado no meio da sessao)
    fs::remove_all(d.p / "card0");
    BS_CHECK(s->card(0, c) == SaveError::NoCard);
}

// Falha em cada ponto da escrita atomica: o save anterior sobrevive, nenhum temporario fica, e uma
// nova abertura do store ve o anterior.
BS_TEST(saves_store_write_faults_keep_previous) {
    for (sv::FaultPoint p : {sv::FaultPoint::CreateTemp, sv::FaultPoint::Write, sv::FaultPoint::Flush,
                             sv::FaultPoint::Reread, sv::FaultPoint::Swap}) {
        TempDir d("falha");
        bool    armed = false;
        sv::FileStoreOptions o = fixedClock();
        o.fault = [&](sv::FaultPoint q) { return armed && q == p; };
        auto s = sv::make_file_store(d.p, o);
        BS_CHECK(s->save_atomic(0, "BLUESTIN.000", makeVms(1)) == SaveError::None);
        armed = true;
        BS_CHECK(s->save_atomic(0, "BLUESTIN.000", makeVms(2)) == SaveError::Io);
        BS_CHECK(s->save_atomic(0, "BLUESTIN.001", makeVms(3)) == SaveError::Io);  // novo: nao aparece
        Bytes img;
        BS_CHECK(s->load(0, "BLUESTIN.000", img) == SaveError::None && img == makeVms(1));
        BS_CHECK(s->load(0, "BLUESTIN.001", img) == SaveError::NotFound);
        for (const auto& e : fs::directory_iterator(d.p / "card0")) BS_CHECK(e.path().extension() != ".tmp");
        armed = false;
        auto s2 = sv::make_file_store(d.p, fixedClock());
        BS_CHECK(s2->load(0, "BLUESTIN.000", img) == SaveError::None && img == makeVms(1));
    }
}

// Abertura depois de queda: .tmp descartado; destino corrompido volta pelo .bak; os dois invalidos
// viram .corrompido (preservados) e o slot fica vazio; versao futura e recusada e preservada.
BS_TEST(saves_store_recovery_on_open) {
    TempDir d("recupera");
    {
        auto s = sv::make_file_store(d.p, fixedClock());
        BS_CHECK(s->save_atomic(0, "BLUESTIN.000", makeVms(1)) == SaveError::None);
        BS_CHECK(s->save_atomic(0, "BLUESTIN.000", makeVms(2)) == SaveError::None);  // .bak = 1
        BS_CHECK(s->save_atomic(0, "BLUESTIN.001", makeVms(3)) == SaveError::None);
        BS_CHECK(s->save_atomic(0, "BLUESTIN.003", makeVms(4)) == SaveError::None);
    }
    const fs::path c0 = d.p / "card0";
    writeFile(c0 / "BLUESTIN.000.bsv", text("lixo"));                              // destino corrompido
    writeFile(c0 / "BLUESTIN.004.bsv.tmp", sv::envelope_encode(makeVms(5), 1));   // queda antes da troca
    writeFile(c0 / "BLUESTIN.001.bsv", text("lixo"));                              // sem .bak
    Bytes fut = sv::envelope_encode(makeVms(6), 1);
    fut[4] = std::byte{9};
    writeFile(c0 / "BLUESTIN.003.bsv", fut);                                       // versao futura
    std::string log;
    sv::FileStoreOptions o = fixedClock();
    o.log = [&](const std::string& m) { log += m + "\n"; };
    auto  s = sv::make_file_store(d.p, o);
    Bytes img;
    BS_CHECK(s->load(0, "BLUESTIN.000", img) == SaveError::None && img == makeVms(1));
    BS_CHECK(log.find("recuperado") != std::string::npos);
    BS_CHECK(s->load(0, "BLUESTIN.004", img) == SaveError::NotFound && !fs::exists(c0 / "BLUESTIN.004.bsv.tmp"));
    BS_CHECK(s->load(0, "BLUESTIN.001", img) == SaveError::NotFound && fs::exists(c0 / "BLUESTIN.001.bsv.corrompido"));
    BS_CHECK(fs::exists(c0 / "BLUESTIN.000.bsv.corrompido"));
    // versao futura: intocada, recusada no LOAD e nunca sobrescrita
    BS_CHECK(s->load(0, "BLUESTIN.003", img) == SaveError::FutureVersion);
    BS_CHECK(s->save_atomic(0, "BLUESTIN.003", makeVms(7)) == SaveError::FutureVersion);
    BS_CHECK(readFile(c0 / "BLUESTIN.003.bsv") == fut && !fs::exists(c0 / "BLUESTIN.003.bsv.corrompido"));
}

// VMS puro: exportar da o arquivo de dados exato; importar so aceita VMS integro.
BS_TEST(saves_vms_import_export) {
    TempDir d("vms");
    auto    s = sv::make_file_store(d.p / "saves", fixedClock());
    BS_CHECK(s->save_atomic(0, "BLUESTIN.000", makeVms(8)) == SaveError::None);
    BS_CHECK(sv::export_vms(*s, 0, "BLUESTIN.000", d.p / "x.vms") == SaveError::None);
    BS_CHECK(readFile(d.p / "x.vms") == makeVms(8));
    BS_CHECK(sv::import_vms(*s, 0, "BLUESTIN.005", d.p / "x.vms") == SaveError::None);
    Bytes img;
    BS_CHECK(s->load(0, "BLUESTIN.005", img) == SaveError::None && img == makeVms(8));
    Bytes bad = makeVms(8);
    bad[0x300] ^= std::byte{1};
    writeFile(d.p / "ruim.vms", bad);
    BS_CHECK(sv::import_vms(*s, 0, "BLUESTIN.004", d.p / "ruim.vms") == SaveError::BadImage);
    BS_CHECK(sv::import_vms(*s, 0, "BLUESTIN.004", d.p / "nao_existe.vms") == SaveError::NotFound);
    BS_CHECK(s->load(0, "BLUESTIN.004", img) == SaveError::NotFound);
}
