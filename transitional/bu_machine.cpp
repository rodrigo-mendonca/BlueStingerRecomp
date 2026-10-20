#include "bu_machine.h"

#include <algorithm>
#include <cstring>

namespace bs::transitional::bu {

namespace ns = native::saves;

int32_t result_code(ns::SaveError e) {
    switch (e) {
    case ns::SaveError::None: return kOk;
    case ns::SaveError::NoCard: return kNoCard;
    case ns::SaveError::Unformatted: return kUnformatted;
    case ns::SaveError::NotFound: return kNotFound;
    // Sem codigo proprio observado no oraculo (cheio, somente leitura, envelope invalido, E/S): o
    // jogo trata como "nao abre". Internamente continuam distintos (log e unitarios).
    default: return kCannotOpen;
    }
}

int32_t Machine::card_code(int drive) {
    if (!valid(drive) || !u_[drive].enabled) return kNoCard;
    ns::CardState       s;
    const ns::SaveError e = store_.card(drive, s);
    if (e == ns::SaveError::Unformatted || (e == ns::SaveError::None && s.present && !s.formatted)) return kUnformatted;
    return e == ns::SaveError::None && s.present ? kOk : kNoCard;
}

void Machine::init(uint32_t mask) {
    for (int d = 0; d < kDrives; ++d) {
        u_[d]         = Unit{};
        u_[d].enabled = (mask >> d) & 1u;
        // cartao fisicamente presente (formatado ou nao) recebe a insercao; a montagem de um nao
        // formatado devolve FF03
        if (const int32_t c = card_code(d); c == kOk || c == kUnformatted) u_[d].op = kOpInsert, u_[d].left = ticks_.insert;
    }
}

int32_t Machine::begin(int drive, uint32_t op, int ticks) {
    Unit& u = u_[drive];
    u.op    = op;
    u.left  = std::max(1, ticks);
    return kOk;
}

int32_t Machine::mount(int drive) {
    if (!valid(drive) || busy(drive)) return kBusy;
    if (const int32_t c = card_code(drive); c != kOk) return c;
    return begin(drive, kOpMount, ticks_.mount);
}

// Layout do oraculo (Flycast, VMU padrao): os campos fixos vem do bloco raiz de um cartao
// formatado (cor, 256 blocos, 512 bytes por bloco, data de formatacao convertida do BCD); o jogo
// copia o total (+14) e os livres (+18) em 8C01C8CE.
int32_t Machine::disk_info(int drive, std::span<std::byte, kDiskInfoSize> out) {
    if (!valid(drive) || busy(drive)) return kBusy;
    if (const int32_t c = card_code(drive); c != kOk) return c;
    ns::CardState s;
    if (store_.card(drive, s) != ns::SaveError::None) return kNoCard;
    std::memset(out.data(), 0, out.size());
    auto put8  = [&](size_t o, uint8_t v) { out[o] = std::byte(v); };
    auto put16 = [&](size_t o, uint16_t v) { put8(o, uint8_t(v)), put8(o + 1, uint8_t(v >> 8)); };
    put8(0, 1), put8(1, 0xFF), put8(2, 0xFF), put8(3, 0xFF), put8(4, 0x64);  // cor do cartao
    put16(32, 256);                                  // blocos do cartao
    put16(34, uint16_t(s.total_blocks));             // blocos de usuario
    put16(36, uint16_t(s.free_blocks));              // livres
    put16(38, uint16_t(s.free_blocks));
    put16(40, 128), put16(42, 128), put16(44, 512), put16(46, 5);
    put16(48, 1998), put8(50, 11), put8(51, 27), put8(54, 59), put8(55, 5);  // data de formatacao
    return kOk;
}

int32_t Machine::exists(int drive, const std::string& name) {
    if (!valid(drive) || busy(drive)) return kBusy;
    if (const int32_t c = card_code(drive); c != kOk) return c;
    std::vector<ns::SaveEntry> list;
    if (store_.list(drive, list) != ns::SaveError::None) return kCannotOpen;
    for (const auto& e : list)
        if (e.name == name) return kOk;
    return kNotFound;
}

int32_t Machine::save(int drive, const std::string& name, std::vector<std::byte> image) {
    if (!valid(drive) || busy(drive)) return kBusy;
    if (const int32_t c = card_code(drive); c != kOk) return c;
    u_[drive].name  = name;
    u_[drive].image = std::move(image);
    return begin(drive, kOpSave, ticks_.save);
}

int32_t Machine::load(int drive, const std::string& name, uint32_t startBlock, uint32_t nblocks) {
    if (!valid(drive) || busy(drive)) return kBusy;
    if (const int32_t c = card_code(drive); c != kOk) return c;
    u_[drive].name    = name;
    u_[drive].start   = startBlock;
    u_[drive].nblocks = nblocks;
    return begin(drive, kOpLoad, ticks_.load);
}

int32_t Machine::status(int drive) const { return !valid(drive) || busy(drive) ? kBusy : kOk; }

int32_t Machine::last_error(int drive) const { return valid(drive) ? u_[drive].lastError : kBusy; }

int32_t Machine::exit() {
    for (int d = 0; d < kDrives; ++d)
        if (busy(d)) return kBusy;
    return kOk;
}

void Machine::finish(int drive, Unit& u, std::vector<Event>& out, bool hold) {
    auto progress = [&](uint32_t k, uint32_t total) { out.push_back({Event::Kind::Progress, drive, u.op, k, total, {}}); };
    Event done{Event::Kind::Complete, drive, u.op, 0, 0, {}};
    switch (u.op) {
    case kOpInsert:
        progress(1, 0), progress(2, 0);
        done.a = kWorkBlocks;
        break;
    case kOpMount:
        for (uint32_t k = 1; k <= 0x10; ++k) progress(k, 0x11);
        u.mounted = true;
        break;
    case kOpSave: {
        const ns::SaveError e = store_.save_atomic(drive, u.name, u.image);  // duravel antes da conclusao
        if (e == ns::SaveError::None) {
            const uint32_t total = uint32_t(u.image.size() / ns::kBlock) * 2 + 2;  // 10 para 4 blocos (oraculo)
            for (uint32_t k = 1; k <= total; ++k) progress(k, total);
        }
        done.a = uint32_t(result_code(e));
        u.image.clear();
        break;
    }
    case kOpLoad: {
        std::vector<std::byte> img;
        ns::SaveError          e = store_.load(drive, u.name, img);
        const size_t           off = size_t(u.start) * ns::kBlock;
        if (e == ns::SaveError::None && off >= img.size()) e = ns::SaveError::Corrupt;  // alem do fim
        if (e == ns::SaveError::None) {
            const size_t n = u.nblocks ? std::min(img.size() - off, size_t(u.nblocks) * ns::kBlock) : img.size() - off;
            done.data.assign(img.begin() + ptrdiff_t(off), img.begin() + ptrdiff_t(off + n));
            const uint32_t total = uint32_t(n / ns::kBlock);
            for (uint32_t k = 1; k <= total; ++k) progress(k, total);
        }
        done.a = uint32_t(result_code(e));
        break;
    }
    default: break;
    }
    u.lastError = int32_t(done.a);
    if (u.op == kOpInsert) u.lastError = kOk;
    if (hold) u.releasing = true;  // ocupada durante os progressos; release() antes da conclusao
    else u.op = 0;
    out.push_back(std::move(done));
}

void Machine::release(int drive) {
    if (!valid(drive) || !u_[drive].releasing) return;
    u_[drive].releasing = false;
    u_[drive].op        = 0;
}

std::vector<Event> Machine::tick(bool hold) {
    std::vector<Event> out;
    for (int d = 0; d < kDrives; ++d) {
        Unit& u = u_[d];
        if (u.releasing) release(d);  // conclusao ja entregue sem release(): nao prende a unidade
        if (u.op && --u.left <= 0) finish(d, u, out, hold);
    }
    return out;
}

} // namespace bs::transitional::bu
