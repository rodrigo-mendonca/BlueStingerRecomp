#pragma once
// Analise estatica do executavel SH-4: descoberta de funcoes, corpo de cada
// funcao, constant pools e tabelas de salto (ARCHITECTURE.md secao 4).
#include "decoder/sh4_decoder.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace bs::analysis {

// Codigo que o jogo copia do executavel para outro endereco e executa la (R3), p.ex.
// os vetores de excecao/interrupcao em VBR+0x100/0x400/0x600. As instrucoes sao
// analisadas no destino (mova/PC-relativas corretas); o pool e lido da RAM em tempo
// de execucao, porque o jogo pode corrigi-lo ao instalar a copia.
struct Relocated {
    uint32_t dest = 0;    // endereco de execucao (P1)
    uint32_t source = 0;  // origem no executavel (P1)
    uint32_t size = 0;
};

struct Image {
    uint32_t               base = 0;  // endereco de carga (P1, p.ex. 0x8C010000)
    std::vector<uint8_t>   bytes;
    std::vector<Relocated> relocated;

    uint32_t end() const { return base + uint32_t(bytes.size()); }
    bool     inMain(uint32_t a, uint32_t n = 1) const { return a >= base && a - base + n <= bytes.size(); }
    const Relocated* relocatedAt(uint32_t a, uint32_t n = 1) const {
        for (const auto& r : relocated)
            if (a >= r.dest && a - r.dest + n <= r.size) return &r;
        return nullptr;
    }
    bool contains(uint32_t a, uint32_t n = 1) const { return inMain(a, n) || relocatedAt(a, n); }
    uint8_t byte(uint32_t a) const {
        if (const Relocated* r = relocatedAt(a)) a = r->source + (a - r->dest);
        return bytes[a - base];
    }
    uint16_t read16(uint32_t a) const { return uint16_t(byte(a) | (byte(a + 1) << 8)); }
    uint32_t read32(uint32_t a) const { return read16(a) | (uint32_t(read16(a + 2)) << 16); }
    // Converte qualquer espelho (P0/P1/P2/P3) para o segmento da imagem; 0 se fora dela.
    uint32_t normalize(uint32_t a) const;
};

struct JumpTableOverride {
    uint32_t insn = 0;   // endereco do braf/jmp
    uint32_t table = 0;  // inicio da tabela
    uint32_t count = 0;
};

struct Config {
    uint32_t                       entry = 0;
    std::vector<uint32_t>          extra_functions;
    std::vector<JumpTableOverride> jump_tables;
    std::vector<std::pair<uint32_t, uint32_t>> data_ranges;  // [inicio, fim)
    bool                           pointer_candidates = true;  // constantes do pool que apontam para codigo
    bool                           data_pointers      = true;  // palavras de dados que apontam para inicio de funcao
};

// PointerTable: entrada de uma tabela de ponteiros para codigo em dado (vtable, tabela de
// callbacks), aceita com evidencia da propria tabela (ver Analyzer::fromPointerTables).
enum class Origin : uint8_t { Entry, Call, Pointer, Config, DataPointer, PointerTable };

struct JumpTable {
    uint32_t              insn = 0;        // braf ou jmp
    uint32_t              table = 0;
    uint32_t              entry_size = 2;  // 2 = relativo a insn+4 (mov.w), 4 = absoluto (mov.l)
    bool                  bounded = false; // limite achado no codigo (false = heuristica)
    std::vector<uint32_t> targets;
};

struct Function {
    uint32_t                     entry = 0;
    Origin                       origin = Origin::Call;
    std::set<uint32_t>           insns;        // instrucoes emitidas como passos proprios
    std::set<uint32_t>           slots;        // delay slots (emitidos junto do desvio)
    std::set<uint32_t>           labels;       // alvos de desvio dentro do corpo
    std::map<uint32_t, uint32_t> direct;       // pc de jsr/bsrf/jmp -> alvo resolvido com seguranca
    std::set<uint32_t>           tail_calls;   // pc de bra/jmp que saem para outra funcao
    std::set<uint32_t>           dead_ends;    // pc onde o fluxo cai em dado/instrucao invalida
    std::set<uint32_t>           unresolved;   // pc de desvio indireto que vai para o despacho
};

struct Stats {
    size_t functions = 0, by_call = 0, by_pointer = 0, by_data_pointer = 0, by_config = 0, by_pointer_table = 0;
    size_t instructions = 0, unique_instructions = 0;
    size_t jump_tables = 0, jump_tables_heuristic = 0;
    size_t direct_indirects = 0, unresolved_indirects = 0, dead_ends = 0;
    size_t pool_words = 0;
    size_t orphans = 0;  // instrucoes da fase 1 fora de qualquer funcao (deve ser 0)
};

struct Analysis {
    std::map<uint32_t, Function>  functions;
    std::map<uint32_t, JumpTable> jump_tables;  // por pc do braf/jmp
    std::set<uint32_t>            data;         // enderecos (alinhados a 2) classificados como dado
    std::set<uint32_t>            code;         // instrucoes alcancadas (todas as funcoes)
    std::vector<std::string>      warnings;
    Stats                         stats;
};

Analysis analyze(const Image& img, const Config& cfg);

// Exposto para testes: registradores de uso geral que a instrucao pode escrever.
uint32_t gprWrites(const sh4::Insn& in);  // mascara de bits (bit r = Rr)

} // namespace bs::analysis
