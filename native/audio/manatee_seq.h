#pragma once
// N6 fase F (docs/N6-PLANO.md secao 18): o sequenciador das musicas do Manatee, nativo e portavel. Reproduz a contagem
// INTEIRA do driver (MANATEE.DRV), lida no codigo ARM7:
//   - o driver chama o sequenciador a 250 Hz exatos (timer A da AICA a 44 amostras, 1 interrupcao em 441 descartada =
//     1000 Hz, e o sequenciador a cada 4: tratador em 0x2A8, laco principal em 0x194);
//   - por chamada, a contagem de cada trilha cai de `passo` (0x4000 com a velocidade normal da porta, 0x8000 em 0x6800);
//   - um evento com delta d soma d * unidade, unidade = (cabecalho1 * tempo) >> 4 (0x7404, 0x46CC, 0x4874); o 0x84 troca o
//     tempo; a nota guarda gate * unidade + a contagem que sobrou (0x4430) e acaba quando essa conta cai a 0 (0x4270);
//   - 0x81: le N eventos a partir de SMSD + deslocamento e volta (o primeiro sem decrementar, 0x4630 e 0x4364);
//   - 0x82: o primeiro guarda o ponto do contador; os seguintes voltam la; contador c = corpo c+1 vezes; 0 = sem fim.
// O sequenciador so produz eventos (nota liga/desliga, controle, programa, pitch) com o numero da chamada; quem toca e o
// sintetizador (manatee_synth). Nao e emulacao do ARM7: e o mesmo algoritmo de agenda, em C++.
#include "audio/manatee.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace bs::native::audio::manatee {

inline constexpr double kSequencerHz = 250.0;  // chamadas por segundo (timer A da AICA: 1000 Hz / 4)
inline constexpr int32_t kNormalStep = 0x4000;  // queda por chamada com a velocidade normal da porta
inline constexpr size_t  kMaxNotes   = 48;      // a lista de notas do driver (0x10300, 48 x 12 bytes; 0x4460-0x4480)

struct MusicEvent {
    enum class Kind : uint8_t { NoteOn, NoteOff, Control, Program, Pitch, Pressure, End } kind;
    uint64_t call = 0;  // numero da chamada do sequenciador (tempo = call / 250 s)
    uint8_t  canal = 0, a = 0, b = 0;  // nota: tecla, velocidade; controle: numero, valor; programa/pitch: valor
    uint32_t noteId = 0;  // liga a nota ao seu desliga
};

class Sequencer {
public:
    // firstNoteId: o dono continua a numeracao entre um tocar e outro na mesma porta, para que o desliga de uma nota nova
    // nunca alcance uma voz que sobrou do tocar anterior (revisao Codex 2026-10-20, MEDIA)
    explicit Sequencer(const Sequence& seq, uint32_t firstNoteId = 1);
    uint32_t next_note_id() const { return nextNote_; }
    uint64_t loops() const { return loops_; }                 // voltas do laco 0x82
    uint64_t first_loop_call() const { return firstLoop_; }   // chamada da 1a volta (0 = ainda nao voltou)
    // Diagnostico: a posicao no arquivo do proximo evento a ler (o ponteiro de leitura da trilha, [fp+0x10] no driver)
    uint32_t position() const { return idx_ < seq_.events.size() ? seq_.events[idx_].pos : 0; }  // 0 depois do fim
    int64_t  count() const { return count_; }  // a contagem da trilha ([fp+0x24]): mesma aritmetica do driver
    // Uma chamada do driver: desliga as notas cujo gate acabou, depois avanca a trilha; os eventos vao para `out`
    void call(std::vector<MusicEvent>& out);
    bool     ended() const { return ended_; }
    uint64_t calls() const { return calls_; }
    void     set_step(int32_t s) { step_ = s; }  // velocidade da porta (0x8000 normal = 0x4000)
    int32_t  tempo() const { return tempo_; }
    uint64_t stolen() const { return stolen_; }  // notas desligadas antes do gate por falta de lugar na lista

private:
    struct Note {
        uint32_t id;
        uint8_t  canal, tecla;
        int64_t  left;  // contagem ate desligar
    };
    void     events(std::vector<MusicEvent>& out);
    uint32_t index_of(uint32_t filePos) const;

    const Sequence&   seq_;
    uint64_t          stolen_ = 0;
    std::vector<Note> notes_;
    uint32_t          idx_ = 0;   // proximo evento
    int64_t           count_ = 0; // contagem da trilha ([fp+0x24])
    int32_t           tempo_ = 0, mult_ = 0, step_ = kNormalStep;
    int64_t           unit_ = 0;  // (cabecalho1 * tempo) >> 4 ([fp+0xC])
    // referencia (0x81): retorno e contador ([fp+0x1C], [fp+2])
    uint32_t refReturn_ = 0;
    int      refLeft_ = 0;
    // laco (0x82): inicio (o evento do primeiro 0x82) e contador ([fp+0x20], [fp+3]); 0x80 = sem fim
    int64_t loopAt_ = -1;
    int     loopLeft_ = 0;
    uint64_t calls_ = 0, loops_ = 0, firstLoop_ = 0;
    uint32_t nextNote_ = 1;
    bool     ended_ = false;
};

} // namespace bs::native::audio::manatee
