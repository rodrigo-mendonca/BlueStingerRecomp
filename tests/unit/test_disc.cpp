#include "test.h"

#include "disc/cdi_image.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "disc/scramble.h"
#include "util/hash.h"

#include <cstring>
#include <string>

using namespace bs;

// ---------- hashes ----------

BS_TEST(sha256_known_vectors) {
    BS_CHECK(util::sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    BS_CHECK(util::sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    BS_CHECK(util::sha256Hex(m, std::strlen(m)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

BS_TEST(sha256_incremental_matches_oneshot) {
    std::string s(1000, '\0');
    for (size_t i = 0; i < s.size(); ++i) s[i] = char(i * 7);
    util::Sha256 h;
    for (size_t i = 0; i < s.size(); i += 13) h.update(s.data() + i, std::min<size_t>(13, s.size() - i));
    BS_CHECK(h.hexdigest() == util::sha256Hex(s.data(), s.size()));
}

BS_TEST(sha256_million_a) {
    std::string  s(1000000, 'a');
    BS_CHECK(util::sha256Hex(s.data(), s.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

BS_TEST(crc32_check_value) {
    util::Crc32 c;
    c.update("123456789", 9);
    BS_CHECK(c.value() == 0xCBF43926u);
}

// ---------- scramble ----------

BS_TEST(scramble_roundtrip_and_permutes) {
    for (size_t size : {size_t(31), size_t(32), size_t(1000), size_t(65536 + 77), size_t(3 * 1024 * 1024 + 5)}) {
        std::vector<uint8_t> in(size);
        for (size_t i = 0; i < size; ++i) in[i] = uint8_t(i * 31 + (i >> 8));
        auto s = disc::scramble(in);
        BS_CHECK(disc::descramble(s) == in);
        if (size >= 64) BS_CHECK(s != in);
        if (size < 32) BS_CHECK(s == in);  // so a fatia final incompleta: inalterada
    }
}

// ---------- IP.BIN ----------

static std::vector<uint8_t> makeIpBin(const char* periph) {
    std::vector<uint8_t> d(disc::kIpBinSize, ' ');
    auto put = [&](size_t off, const char* s) { std::memcpy(&d[off], s, std::strlen(s)); };
    put(0x00, "SEGA SEGAKATANA ");
    put(0x30, "JUE");
    put(0x38, periph);
    put(0x40, "T99999N");
    put(0x4A, "V1.000");
    put(0x50, "20260101");
    put(0x60, "1ST_READ.BIN");
    put(0x80, "TEST TITLE");
    return d;
}

BS_TEST(ipbin_parses_fields) {
    auto ip = disc::parseIpBin(makeIpBin("0799A10").data(), disc::kIpBinSize);
    BS_CHECK(ip.product == "T99999N");
    BS_CHECK(ip.version == "V1.000");
    BS_CHECK(ip.boot_file == "1ST_READ.BIN");
    BS_CHECK(ip.title == "TEST TITLE");
    BS_CHECK(!ip.usesWindowsCE());
    BS_CHECK(disc::parseIpBin(makeIpBin("0799A11").data(), disc::kIpBinSize).usesWindowsCE());
}

BS_TEST(ipbin_rejects_non_dreamcast) {
    auto d = makeIpBin("0");
    d[0]   = 'X';
    bool threw = false;
    try { disc::parseIpBin(d.data(), d.size()); } catch (const disc::DiscError&) { threw = true; }
    BS_CHECK(threw);
}

// ---------- ISO9660 sobre imagem em memoria ----------

namespace {

class MemoryImage : public disc::DiscImage {
public:
    MemoryImage(uint32_t start_lba, uint32_t sectors) : data(size_t(sectors) * 2048) {
        disc::Track t;
        t.session = 1; t.number = 1; t.mode = disc::TrackMode::Mode1;
        t.start_lba = start_lba; t.length = sectors; t.sector_size = 2048;
        tracks_.push_back(t);
    }
    std::string formatName() const override { return "mem"; }
    uint8_t* sector(uint32_t lba) { return &data[size_t(lba - tracks_[0].start_lba) * 2048]; }
    std::vector<uint8_t> data;

protected:
    void readRaw(uint64_t off, void* dst, size_t n) const override { std::memcpy(dst, &data[off], n); }
};

void le32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }

uint8_t* dirRecord(uint8_t* p, const char* name, uint32_t lba, uint32_t size, bool dir) {
    const size_t nl  = std::strlen(name) ? std::strlen(name) : 1;
    const size_t len = (33 + nl + 1) & ~size_t(1);
    p[0] = uint8_t(len);
    le32(p + 2, lba);
    le32(p + 10, size);
    p[25] = dir ? 2 : 0;
    p[32] = uint8_t(nl);
    if (std::strlen(name)) std::memcpy(p + 33, name, nl); else p[33] = 0;
    return p + len;
}

} // namespace

BS_TEST(iso9660_lists_and_reads_nested_files) {
    const uint32_t base = 1000;
    MemoryImage    img(base, 64);
    uint8_t*       pvd = img.sector(base + 16);
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5);
    std::memcpy(pvd + 40, "TESTVOL", 7);
    dirRecord(pvd + 156, "", base + 20, 2048, true);

    uint8_t* root = img.sector(base + 20);
    root = dirRecord(root, "", base + 20, 2048, true);
    root = dirRecord(root, "\x01", base + 20, 2048, true);
    root = dirRecord(root, "1ST_READ.BIN;1", base + 30, 3000, false);
    dirRecord(root, "SUB", base + 21, 2048, true);

    uint8_t* sub = img.sector(base + 21);
    sub = dirRecord(sub, "", base + 21, 2048, true);
    sub = dirRecord(sub, "\x01", base + 20, 2048, true);
    dirRecord(sub, "A.NJ;1", base + 40, 5, false);

    for (int i = 0; i < 3000; ++i) img.sector(base + 30)[i] = uint8_t(i);  // atravessa 2 setores
    std::memcpy(img.sector(base + 40), "hello", 5);

    disc::Iso9660 iso(img, base + 16);
    BS_CHECK(iso.volumeId() == "TESTVOL");
    BS_CHECK(iso.entries().size() == 3);
    const auto* boot = iso.find("1st_read.bin");
    BS_CHECK(boot && boot->size == 3000);
    if (boot) {
        auto d = iso.readAll(*boot);
        BS_CHECK(d.size() == 3000 && d[2999] == uint8_t(2999 & 0xFF));
    }
    const auto* a = iso.find("SUB/A.NJ");
    BS_CHECK(a && !a->is_dir);
    if (a) BS_CHECK(iso.readAll(*a) == std::vector<uint8_t>({'h', 'e', 'l', 'l', 'o'}));
}

BS_TEST(iso9660_rejects_path_traversal_names) {
    const uint32_t base = 0;
    MemoryImage    img(base, 32);
    uint8_t*       pvd = img.sector(16);
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5);
    dirRecord(pvd + 156, "", 20, 2048, true);
    uint8_t* root = img.sector(20);
    dirRecord(root, "..\\EVIL;1", 25, 1, false);
    bool threw = false;
    try { disc::Iso9660 iso(img, 16); } catch (const disc::DiscError&) { threw = true; }
    BS_CHECK(threw);
}

BS_TEST(track_user_data_offsets) {
    disc::Track t;
    t.mode = disc::TrackMode::Mode2;
    t.sector_size = 2336; BS_CHECK(t.userDataOffset() == 8);
    t.sector_size = 2352; BS_CHECK(t.userDataOffset() == 24);
    t.mode = disc::TrackMode::Mode1; BS_CHECK(t.userDataOffset() == 16);
    t.sector_size = 2048; BS_CHECK(t.userDataOffset() == 0);
}

// ---------- cabecalho CDI sintetico ----------

namespace {

struct CdiWriter {
    std::vector<uint8_t> b;
    void u8(uint32_t v) { b.push_back(uint8_t(v)); }
    void u16(uint32_t v) { u8(v); u8(v >> 8); }
    void u32(uint32_t v) { u16(v); u16(v >> 16); }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
    void track(uint32_t pregap, uint32_t len, uint32_t mode, uint32_t lba, uint32_t sscode) {
        u32(0);
        const uint8_t mark[20] = {0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
        b.insert(b.end(), mark, mark + 20);
        zeros(4);
        u8(4); b.insert(b.end(), {'a', '.', 'c', 'd'});
        zeros(11 + 4 + 4);
        u32(0);  // nao e DJ4
        zeros(2);
        u32(pregap); u32(len);
        zeros(6);
        u32(mode);
        zeros(12);
        u32(lba); u32(pregap + len);
        zeros(16);
        u32(sscode);
        zeros(29);
        zeros(5); u32(0);  // v3+: sem bloco extra
    }
    void endSession() { zeros(4 + 8 + 1); }
};

} // namespace

BS_TEST(cdi_header_two_sessions) {
    CdiWriter w;
    w.u16(2);
    w.u16(1); w.track(150, 1000, 2, 0, 1); w.endSession();
    w.u16(1); w.track(150, 500, 2, 11702, 1); w.endSession();
    const uint64_t dataEnd = uint64_t(1150 + 650) * 2336;
    auto tr = disc::CdiImage::parseHeader(w.b, disc::CdiImage::kV35, dataEnd);
    BS_CHECK(tr.size() == 2);
    BS_CHECK(tr[0].file_offset == 150ull * 2336);
    BS_CHECK(tr[1].session == 2);
    BS_CHECK(tr[1].start_lba == 11702);
    BS_CHECK(tr[1].file_offset == (1150ull + 150) * 2336);
    BS_CHECK(tr[1].sector_size == 2336);
}

BS_TEST(cdi_header_rejects_garbage) {
    CdiWriter w;
    w.u16(1); w.u16(1); w.zeros(300);
    bool threw = false;
    try { disc::CdiImage::parseHeader(w.b, disc::CdiImage::kV35, 1u << 30); } catch (const disc::DiscError&) { threw = true; }
    BS_CHECK(threw);
}
