#pragma once
// N7c (docs/N7-PLANO.md secao 21): shaders de pos-processamento. Os efeitos internos e os de mods usam a MESMA
// interface de passe (um shader de fragmento de tela cheia):
//   - set 0, binding 0: sampler2D com a imagem anterior da cadeia; binding 1 (opcional): a imagem original do jogo;
//   - push constants (ate 80 bytes): vec4 tamanho (largura, altura, 1/largura, 1/altura) e vec4 p[4] (16 parametros);
//   - entrada location 0: vec2 uv (0..1); saida location 0: a cor.
// Um pacote de mod (mods/shaders/<nome>/) traz manifesto.toml e o SPIR-V ja compilado (o jogo nao compila GLSL).
// Nada de codigo nativo: o SPIR-V e validado antes de ir para a GPU (estagio, entradas, saidas, bindings, push
// constants, capacidades) e qualquer coisa fora da interface o desativa com o motivo.
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace bs::native::render {

inline constexpr uint32_t kPostParams     = 16;  // parametros por passe
inline constexpr uint32_t kPostPushBytes  = 80;  // vec4 tamanho + 4 vec4 de parametros
inline constexpr int      kPostFormat     = 1;   // versao do formato do manifesto
inline constexpr uint32_t kCrtMinSourceHeight = 1080;  // o CRT so roda sobre uma imagem de pelo menos 1080 linhas

struct PostParam {
    std::string name, label;
    float       min = 0, max = 1, def = 0, step = 0.05f;
};

struct PostShaderPackage {
    std::string            name, description;
    std::filesystem::path  dir;
    bool                   before_builtin = false;  // posicao = "antes" (dos efeitos internos); padrao "depois"
    std::vector<PostParam> params;
    std::vector<uint32_t>  spirv;
    std::string            error;  // vazio = valido (manifesto e SPIR-V); senao, o motivo (o pacote fica desativado)
};

// Valida um SPIR-V contra a interface de passe. false com o motivo em *error.
bool validate_post_spirv(std::span<const uint32_t> words, std::string* error);

// Le um pacote (manifesto.toml + o .spv que ele aponta) e valida tudo; error preenchido se algo falhar.
PostShaderPackage load_post_package(const std::filesystem::path& dir);

// Os pacotes em <pasta>/<nome>/manifesto.toml, em ordem de nome (validos e invalidos, com o motivo).
std::vector<PostShaderPackage> discover_post_shaders(const std::filesystem::path& root);

} // namespace bs::native::render
