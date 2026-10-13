// bs-disasm: disassembly linear de um binario SH-4 (ferramenta de desenvolvimento).
//   bs-disasm <arquivo> [base=0x8C010000] [inicio] [quantidade]
//   bs-disasm --all-opcodes      tabela completa (opcode, mnemonico, operandos) em PC 0x8C010000
//   bs-disasm --stats <arquivo>  histograma de instrucoes e palavras invalidas
#include "decoder/sh4_decoder.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace bs::sh4;

static std::vector<uint8_t> load(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "erro: nao foi possivel abrir %s\n", path);
        std::exit(1);
    }
    return {std::istreambuf_iterator<char>(f), {}};
}

static uint16_t word(const std::vector<uint8_t>& d, size_t off) { return uint16_t(d[off] | (d[off + 1] << 8)); }

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--all-opcodes") {
        for (uint32_t op = 0; op < 65536; ++op) {
            const Insn  i = decode(uint16_t(op));
            std::string s = disasm(i, 0x8C010000);
            std::printf("%04x\t%s\n", op, i.valid() ? s.c_str() : "");
        }
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--stats") {
        const auto              d = load(argv[2]);
        std::map<std::string, size_t> hist;
        size_t                  invalid = 0;
        for (size_t o = 0; o + 1 < d.size(); o += 2) {
            const Insn i = decode(word(d, o));
            if (!i.valid()) ++invalid;
            else ++hist[i.info().name];
        }
        std::vector<std::pair<size_t, std::string>> v;
        for (auto& [k, n] : hist) v.push_back({n, k});
        std::sort(v.rbegin(), v.rend());
        std::printf("palavras: %zu  invalidas: %zu (%.2f%%)  instrucoes distintas: %zu\n", d.size() / 2, invalid,
                    100.0 * invalid / (d.size() / 2), v.size());
        for (size_t k = 0; k < v.size() && k < 40; ++k) std::printf("  %-14s %zu\n", v[k].second.c_str(), v[k].first);
        return 0;
    }
    if (argc < 2) {
        std::fprintf(stderr, "uso: bs-disasm <arquivo> [base] [inicio] [quantidade]\n"
                             "     bs-disasm --all-opcodes | --stats <arquivo>\n");
        return 2;
    }
    const auto     d     = load(argv[1]);
    const uint32_t base  = argc > 2 ? uint32_t(std::strtoul(argv[2], nullptr, 0)) : 0x8C010000u;
    const uint32_t start = argc > 3 ? uint32_t(std::strtoul(argv[3], nullptr, 0)) : base;
    const uint32_t count = argc > 4 ? uint32_t(std::strtoul(argv[4], nullptr, 0)) : 64;
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t pc  = start + 2 * k;
        const size_t   off = pc - base;
        if (pc < base || off + 1 >= d.size()) break;
        const uint16_t w = word(d, off);
        std::printf("%08x  %04x  %s\n", pc, w, disasm(decode(w), pc).c_str());
    }
    return 0;
}
