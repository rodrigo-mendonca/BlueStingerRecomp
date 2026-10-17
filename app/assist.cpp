#include "assist.h"

#include "debug/file_log.h"
#include "input/input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace bs {
namespace fs = std::filesystem;
namespace {

// PNG de 24 bits sem compressao (blocos "stored" do deflate): sem dependencias, so para capturas.
uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        return t;
    }();
    c = ~c;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

bool writePng(const fs::path& path, const GameFrame& f) {
    std::vector<uint8_t> raw;  // linhas com o filtro 0 na frente
    raw.reserve(size_t(f.height) * (1 + f.width * 3));
    for (uint32_t y = 0; y < f.height; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < f.width; ++x) {
            const uint32_t p = f.argb[size_t(y) * f.width + x];
            raw.push_back(uint8_t(p >> 16)), raw.push_back(uint8_t(p >> 8)), raw.push_back(uint8_t(p));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t             a = 1, b = 0;  // adler32
    for (uint8_t v : raw) a = (a + v) % 65521, b = (b + a) % 65521;
    for (size_t off = 0; off < raw.size() || raw.empty();) {
        const size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)), z.push_back(uint8_t(n >> 8)), z.push_back(uint8_t(~n)), z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
        if (raw.empty()) break;
    }
    const uint32_t adler = b << 16 | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back(uint8_t(adler >> s));

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    auto be32  = [](std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> c;
        be32(c, uint32_t(data.size()));
        c.insert(c.end(), type, type + 4);
        c.insert(c.end(), data.begin(), data.end());
        be32(c, crc32(c.data() + 4, c.size() - 4));
        out.write(reinterpret_cast<const char*>(c.data()), std::streamsize(c.size()));
    };
    out.write("\x89PNG\r\n\x1A\n", 8);
    std::vector<uint8_t> ihdr;
    be32(ihdr, f.width), be32(ihdr, f.height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return bool(out);
}

// Nome de arquivo simples (sem pasta): os comandos so gravam dentro da pasta da sessao.
bool plainName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char ch : s)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')) return false;
    return s[0] != '.';
}

} // namespace

struct Assist::Impl {
    fs::path         dir;
    LiveScriptInput* input = nullptr;
    FILE*            resp = nullptr;
    FILE*            session = nullptr;
    uint64_t         cmdOffset = 0;  // bytes de comandos.txt ja lidos
    std::deque<std::string> queue;   // lidos e ainda nao atendidos
    uint64_t         done = 0;       // comandos atendidos
    enum class Run { Pause, Steps, Vblanks } run = Run::Pause;
    uint64_t         until = 0;
    PadState         pad;
    uint64_t         lastPushed = 0;  // ultimo passo com estado acrescentado (ordem do roteiro)
    std::chrono::seconds idleLimit{3600};

    void reply(const std::string& text) {
        std::fprintf(resp, "%llu\t%s\n", static_cast<unsigned long long>(done), text.c_str());
        std::fflush(resp);
    }

    void writeState(const GameOptions::VblankView& v) {
        const fs::path tmp = dir / "estado.tmp";
        if (FILE* f = std::fopen(tmp.string().c_str(), "w")) {
            std::fprintf(f, "pronto\t%llu\t%llu\t%llu\t%llu\t%s\n", static_cast<unsigned long long>(done),
                         static_cast<unsigned long long>(v.vblank), static_cast<unsigned long long>(v.step),
                         static_cast<unsigned long long>(v.reads), pad_state_text(pad).c_str());
            std::fclose(f);
            std::error_code ec;
            fs::rename(tmp, dir / "estado.txt", ec);
        }
    }

    // Linhas completas novas de comandos.txt.
    std::vector<std::string> newCommands() {
        std::vector<std::string> out;
        std::ifstream            in(dir / "comandos.txt", std::ios::binary);
        if (!in) return out;
        in.seekg(std::streamoff(cmdOffset));
        std::string rest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        size_t      pos = 0;
        for (size_t nl; (nl = rest.find('\n', pos)) != std::string::npos; pos = nl + 1) {
            std::string l = rest.substr(pos, nl - pos);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            out.push_back(l);
        }
        cmdOffset += pos;
        return out;
    }

    void push(uint64_t step, const PadState& s, const std::string& text) {
        input->push(step, s);
        lastPushed = step;
        std::fprintf(session, "%llu %s\n", static_cast<unsigned long long>(step), text.c_str());
        std::fflush(session);
    }

    // Executa um comando; devolve true se o jogo deve voltar a rodar. Lanca para encerrar (fim).
    bool execute(const std::string& line, const GameOptions::VblankView& v, bool& quit) {
        std::istringstream ls(line);
        std::string        cmd;
        ls >> cmd;
        auto num = [&](uint64_t& out) {
            std::string t;
            if (!(ls >> t)) return false;
            char* end = nullptr;
            out       = std::strtoull(t.c_str(), &end, 0);
            return !*end;
        };
        uint64_t n = 0;
        if (cmd.empty() || cmd[0] == '#') return false;
        if (cmd == "fim") {
            quit = true;
            reply("ok fim");
            return true;
        }
        if (cmd == "pad") {
            std::string t;
            ls >> t;
            const PadState s  = pad_state_from_text(t.empty() ? "-" : t);
            const uint64_t at = v.step + 1;  // vale do proximo passo em diante
            // um roteiro carregado ainda tem entradas futuras: o pad nao pode ser adiado nem as
            // substituir (revisao Codex 2026-10-17); rodar ate o fim do roteiro antes
            if (lastPushed > at)
                throw std::runtime_error("o roteiro carregado tem entradas ate o passo " + std::to_string(lastPushed) +
                                         "; use ate_passo " + std::to_string(lastPushed) + " antes de pad");
            pad = s;
            push(at, s, pad_state_text(s));
            reply("ok pad " + pad_state_text(s) + " desde o passo " + std::to_string(at));
            return false;
        }
        if ((cmd == "passos" || cmd == "vblanks" || cmd == "ate_passo") && num(n)) {
            run   = cmd == "vblanks" ? Run::Vblanks : Run::Steps;
            until = cmd == "ate_passo" ? n : (cmd == "vblanks" ? v.vblank : v.step) + n;
            reply("ok " + line);
            return true;
        }
        if (cmd == "roteiro") {
            std::string path;
            std::getline(ls >> std::ws, path);
            std::ifstream in(path, std::ios::binary);
            if (!in) throw std::runtime_error("nao abre " + path);
            std::string l;
            bool        byStep = false;
            uint64_t    count  = 0;
            while (std::getline(in, l)) {
                l = l.substr(0, l.find('#'));
                std::istringstream rs(l);
                std::string        a, b;
                if (!(rs >> a)) continue;
                if (a == "indice") {
                    rs >> b;
                    byStep = b == "passo";
                    continue;
                }
                if (!byStep) throw std::runtime_error("o roteiro precisa de \"indice passo\"");
                rs >> b;
                const uint64_t at = std::strtoull(a.c_str(), nullptr, 10);
                if (at <= v.step) continue;  // passos que ja passaram
                const PadState s = pad_state_from_text(b);
                push(std::max(at, lastPushed), s, pad_state_text(s));
                ++count;
            }
            reply("ok roteiro " + std::to_string(count) + " linhas");
            return false;
        }
        if (cmd == "quadro") {
            std::string name;
            ls >> name;
            if (!plainName(name)) throw std::runtime_error("nome invalido: " + name);
            const fs::path p = dir / (name + ".png");
            if (!v.frame.width || !writePng(p, v.frame)) throw std::runtime_error("nao gravou " + p.string());
            reply("ok quadro " + p.string());
            return false;
        }
        if (cmd == "ram" && num(n)) {
            uint64_t len = 0;
            if (!num(len) || !len || len > 4096) throw std::runtime_error("ram <endereco> <1..4096>");
            const uint32_t off = uint32_t(n) & 0xFFFFFF;
            if (!v.ram || off + len > (16u << 20)) throw std::runtime_error("fora da RAM");
            std::string out;
            char        b[16];
            for (uint64_t i = 0; i < len; ++i) {
                if (i % 16 == 0) {
                    if (i) reply(out), out.clear();
                    std::snprintf(b, sizeof b, "%08X:", 0x8C000000u | (off + uint32_t(i)));
                    out += b;
                }
                std::snprintf(b, sizeof b, " %02X", v.ram[off + i]);
                out += b;
            }
            reply(out);
            return false;
        }
        if (cmd == "dump") {
            std::string name;
            ls >> name;
            if (!plainName(name) || !v.ram) throw std::runtime_error("nome invalido: " + name);
            const fs::path p = dir / (name + ".bin");
            std::ofstream  out(p, std::ios::binary);
            out.write(reinterpret_cast<const char*>(v.ram), 16 << 20);
            if (!out) throw std::runtime_error("nao gravou " + p.string());
            reply("ok dump " + p.string());
            return false;
        }
        if (cmd == "marca") {
            std::string text;
            std::getline(ls >> std::ws, text);
            std::fprintf(session, "# passo %llu (VBlank %llu): %s\n", static_cast<unsigned long long>(v.step),
                         static_cast<unsigned long long>(v.vblank), text.c_str());
            std::fflush(session);
            reply("ok marca");
            return false;
        }
        throw std::runtime_error("comando desconhecido: " + line);
    }
};

Assist::Assist(fs::path dir, LiveScriptInput* input) : impl_(std::make_unique<Impl>()) {
    impl_->dir   = std::move(dir);
    impl_->input = input;
    fs::create_directories(impl_->dir);
    for (const char* f : {"comandos.txt", "respostas.txt", "estado.txt"}) {
        std::error_code ec;
        fs::remove(impl_->dir / f, ec);
    }
    { std::ofstream(impl_->dir / "comandos.txt"); }
    impl_->resp    = std::fopen((impl_->dir / "respostas.txt").string().c_str(), "w");
    impl_->session = std::fopen((impl_->dir / "sessao.txt").string().c_str(), "w");
    if (!impl_->resp || !impl_->session) throw std::runtime_error("gameplay assistido: nao abre " + impl_->dir.string());
    std::fprintf(impl_->session, "# sessao de gameplay assistido (app/assist.h)\nindice passo\n");
    std::fflush(impl_->session);
    if (const char* v = std::getenv("BS_ASSISTIDO_ESPERA")) impl_->idleLimit = std::chrono::seconds(std::atoll(v));
}

Assist::~Assist() {
    if (impl_->resp) std::fclose(impl_->resp);
    if (impl_->session) std::fclose(impl_->session);
}

bool Assist::onVblank(const GameOptions::VblankView& v) {
    Impl& m = *impl_;
    if (m.run == Impl::Run::Steps && v.step < m.until) return true;
    if (m.run == Impl::Run::Vblanks && v.vblank < m.until) return true;
    m.run = Impl::Run::Pause;
    if (g_file_log) file_log_flush();  // BS_FILE_LOG: leituras ate aqui (cargas de sala)
    m.writeState(v);
    auto last = std::chrono::steady_clock::now();
    for (;;) {
        for (std::string& line : m.newCommands()) m.queue.push_back(std::move(line));
        while (!m.queue.empty()) {
            const std::string line = m.queue.front();
            m.queue.pop_front();
            bool resume = false, quit = false;
            try {
                resume = m.execute(line, v, quit);
            } catch (const std::exception& e) {
                m.reply(std::string("erro ") + e.what());
            }
            ++m.done;
            last = std::chrono::steady_clock::now();
            if (quit) return false;
            if (resume) return true;  // os comandos seguintes esperam a proxima pausa
        }
        m.writeState(v);
        if (std::chrono::steady_clock::now() - last > m.idleLimit) {
            m.reply("erro sem comandos por " + std::to_string(m.idleLimit.count()) + " s: encerrando");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

} // namespace bs
