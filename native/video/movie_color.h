#pragma once
// N5: cor do sprite do filme, portavel (sem Windows, DirectWrite ou API grafica): aplicada ao quadro
// ARGB em memoria antes da legenda; no N7 o mesmo modelo vira parametro do shader do renderer.
//
// O jogo desenha o filme como um sprite do PVR com uma cor base (8C158D92) e uma cor de offset
// (8C158DAE). O modelo e parametrizado; os parametros vem da evidencia do oraculo O1b
// (tools/dev/n5_cor.py, analysis/n5/o1b) e a identidade e o padrao enquanto nao forem confirmados.
//   v' = clamp( (faixaTV ? 16 + v*219/255 : v) * (usaBase ? base/255 : 1) + (offsetAditivo ? offset : 0) )
// por canal; o alfa fica como esta.
#include <array>
#include <cstdint>
#include <span>

namespace bs::native::video {

struct MovieColorModel {
    bool tvRange       = false;  // RGB do decodificador original na faixa de TV (16..235)
    bool modulateBase  = false;  // multiplica pela cor base do sprite (base/255)
    bool additiveOffset = false;  // soma a cor de offset do sprite
    static MovieColorModel identity() { return {}; }
    bool operator==(const MovieColorModel&) const = default;
};

class MovieColor {
public:
    // base e offset: o byte que o jogo passa (replicado em R, G e B pelo original)
    void set(const MovieColorModel& m, uint32_t base, uint32_t offset);
    bool is_identity() const { return identity_; }
    uint8_t map(uint8_t v) const { return lut_[v]; }
    void apply(std::span<uint32_t> argb) const;

private:
    std::array<uint8_t, 256> lut_{};
    bool                     identity_ = true;
};

} // namespace bs::native::video
