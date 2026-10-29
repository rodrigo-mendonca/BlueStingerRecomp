#pragma once
// N8.7 (docs/N8-7-KAMUI.md, D-K3 e incremento 4a): o banco minimo de status de interrupcao que o tratador do Kamui
// (8C1C53E2), a Ninja e o jogo leem e limpam (005F6900-005F693F), alimentado por eventos nativos: VBlank-In/Out, fim de
// render, fim de lista, fim do DMA do canal 2 e do Maple. Nao e o controlador simulado: guarda o status e as mascaras,
// aplica os eventos entregues na ordem de hoje e diz o nivel IRL pedido ao SH-4. Quando um evento vence continua com o
// provedor de tempo do runtime (D-K4: nenhum relogio, ordem ou prazo muda).
//   ISTNRM 0x005F6900 (normal), ISTEXT 0x005F6904 (externo), ISTERR 0x005F6908 (erro): escrever 1 limpa o bit;
//   IML2NRM..IML6ERR 0x005F6910-0x005F6938: mascaras por nivel (NRM, EXT, ERR), guardadas.
#include <array>
#include <cstdint>

namespace bs::native::render::kamui {

class EventStatus {
public:
    static constexpr uint32_t kBase = 0x005F6900, kSize = 0x40;
    static constexpr uint32_t kIstNrm = 0x005F6900, kIstExt = 0x005F6904, kIstErr = 0x005F6908;
    static constexpr uint32_t kVblankIn = 1u << 3, kVblankOut = 1u << 4, kRenderDone = 0x7, kMapleDma = 1u << 12;

    // eventos agendados que venceram num ponto de avaliacao (o provedor de tempo decide quais)
    struct Due {
        bool vblank_in = false, vblank_out = false, render = false, maple = false;
        bool any() const { return vblank_in || vblank_out || render || maple; }
    };

    static bool contains(uint32_t p) { return p >= kBase && p < kBase + kSize; }

    void reset();
    // aplica os eventos vencidos, na ordem do controlador anterior: VBlank-In (com a contagem dos que chegam com o bit
    // ainda aceso), VBlank-Out, fim de render (bits 0-2), fim do DMA do Maple (bit 12)
    void deliver(const Due& d);
    // evento levantado na hora (fim de lista, fim do DMA do canal 2, fim do Maple sem prazo)
    void raise(uint32_t bits);
    // escrita do jogo (endereco fisico): IST* limpa os bits em 1; IML* guarda a mascara
    void write(uint32_t p, uint32_t v);

    uint32_t reg(uint32_t p) const { return regs_[(p - kBase) / 4]; }
    // maior nivel (6, 4, 2) cujas mascaras IMLn tem algum bit pendente em ISTNRM/ISTEXT/ISTERR; 0 se nenhum
    uint32_t irl_level() const;

    uint64_t vblanks() const { return vblanks_; }  // VBlank-In entregues desde o reset
    uint64_t fused() const { return fused_; }      // VBlank-In que chegaram com o bit ainda aceso
    uint64_t events() const { return events_; }    // eventos entregues ou levantados (mesmo mascarados)

private:
    uint32_t& at(uint32_t p) { return regs_[(p - kBase) / 4]; }

    std::array<uint32_t, kSize / 4> regs_{};
    uint64_t                        vblanks_ = 0, fused_ = 0, events_ = 0;
};

} // namespace bs::native::render::kamui
