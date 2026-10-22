#pragma once
// N7: contrato das texturas substituiveis (versoes HD, 2K, 4K e interface por mods). Decisao do mantenedor de
// 2026-10-21: o N7 so estabelece os contratos e os pontos de extensao; nenhuma textura externa e carregada, nenhum
// upscale e feito, e as ferramentas e pacotes ficam no N9 (docs/N7-PLANO.md, secao 13).
//
// Identidade: pelo CONTEUDO decodificado (os texels ARGB8888 do maior nivel, de cima para baixo) e pelas dimensoes;
// nunca pelo endereco na VRAM (o mesmo endereco recebe outras texturas e a mesma textura muda de endereco entre
// cenas), nem pelo formato de armazenamento ou pelo banco de paleta. Texto estavel: "<largura>x<altura>-<16 hex>".
//
// Regras de uma substituicao (o backend as aplica quando houver provedor):
//   - mesma proporcao e fator inteiro (1 a 16) nas duas dimensoes: as UVs do jogo (normalizadas) nao mudam;
//   - o canal alfa vale como o original (punch-through, translucidos, ignore texture alpha do desenho);
//   - os mipmaps da substituicao sao gerados a partir dela (os do original nao servem);
//   - sem substituicao (ou incompativel), o original, sempre (fallback automatico);
//   - a preferencia (original, substituicao) e configuravel; o padrao e o original ate o N9.
// Formatos de arquivo (avaliados para o N9): PNG (sem perda, simples) e KTX2 (mipmaps prontos e compressao de GPU
// BC7/BC1/ASTC via Basis), com o consumo de VRAM medido; o cache do backend e por TextureId.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bs::native::render {

struct TextureId {
    uint32_t width = 0, height = 0;
    uint64_t content = 0;  // FNV-1a de 64 bits sobre os texels ARGB8888
    bool     operator==(const TextureId&) const = default;
    std::string text() const;  // "<largura>x<altura>-<16 hex>"
};

// O id de uma textura decodificada (w x h texels ARGB8888, de cima para baixo).
TextureId texture_id(uint32_t width, uint32_t height, std::span<const uint32_t> argb);
std::optional<TextureId> parse_texture_id(const std::string& text);

// Uma substituicao pronta (ja decodificada do arquivo do mod): o maior nivel em ARGB8888.
struct ReplacementTexture {
    uint32_t              width = 0, height = 0;
    std::vector<uint32_t> argb;
};

// true se `r` pode substituir a textura `id` (proporcao igual e fator inteiro de 1 a 16, dados do tamanho certo).
bool replacement_compatible(const TextureId& id, const ReplacementTexture& r);

// Ponto de extensao: quem fornece as substituicoes (o carregador de mods do N9). nullptr = so os originais.
class ITextureReplacements {
public:
    virtual ~ITextureReplacements() = default;
    virtual const ReplacementTexture* find(const TextureId& id) = 0;
};

} // namespace bs::native::render
