# Mods de shaders de pós-processamento (N7)

O Blue Stinger Native PC aplica **efeitos de imagem** sobre a imagem do jogo antes de desenhar o menu e as legendas,
que ficam sempre nítidos. Há efeitos internos (correção de cor) e **shaders de mod**. Os shaders de mod são pacotes em
`mods\shaders\<nome>\` ao lado do `BlueStinger.exe`, ligados e ajustados pelo menu do jogo, sem recompilar nada e sem
tocar nos recursos originais.

- Os efeitos só existem com a **apresentação Vulkan** (o padrão quando há GPU com Vulkan).
- A predefinição **Original Dreamcast** não aplica nenhum efeito: a imagem é exatamente a do jogo.

## Onde ficam e como se escolhem

```
BlueStinger.exe
mods\
  shaders\
    cor-quente\
      manifesto.toml
      shader.spv      (o SPIR-V compilado)
      shader.frag     (o fonte GLSL; opcional, mas recomendado)
```

No jogo: **Esc** ou **Back** do controle > **Gráficos** > **Efeitos de imagem** > **Shader de mod**.
- Cada parâmetro declarado no manifesto aparece logo abaixo, com a faixa e o passo dele.
- A escolha e os parâmetros ficam no `graficos.toml`: `[imagem] shader` e `[shader_parametros]`.
- A ordem na cadeia é a do manifesto: `posicao = "antes"` ou `"depois"` (o padrão) da correção de cor interna.

## O manifesto (`manifesto.toml`)

```toml
formato = 1                 # obrigatorio: a versao deste formato
nome = "cor-quente"         # o nome no menu (padrao: o nome da pasta)
descricao = "Temperatura de cor, matiz e vinheta"
posicao = "depois"          # "antes" ou "depois" dos efeitos internos
shader = "shader.spv"       # o arquivo SPIR-V, dentro da pasta do pacote

[[parametros]]              # ate 16, na ordem em que o shader os le
nome = "temperatura"        # a chave gravada em graficos.toml
rotulo = "Temperatura"      # o texto no menu
minimo = -1.0
maximo = 1.0
padrao = 0.5
passo = 0.1
```

## A interface do shader

Cada pacote é **um passe**: um shader de fragmento de tela cheia, compilado para SPIR-V 1.3 ou anterior (Vulkan 1.1).

```glsl
#version 450
layout(set = 0, binding = 0) uniform sampler2D prev;      // a imagem que chega neste passe
layout(set = 0, binding = 1) uniform sampler2D original;  // opcional: a imagem do jogo antes de qualquer efeito
layout(push_constant) uniform Post {
    vec4 size;  // largura, altura, 1/largura, 1/altura (pixels da imagem do jogo, na resolucao interna)
    vec4 p[4];  // os 16 parametros do manifesto: p[0].x e o primeiro, p[0].y o segundo, ... p[3].w o 16o
} pc;
layout(location = 0) in vec2 vUv;        // 0..1 sobre a imagem
layout(location = 0) out vec4 outColor;  // a cor deste passe (o alfa nao e usado)
void main() { outColor = texture(prev, vUv); }
```

Compilar: `glslangValidator -V --target-env vulkan1.1 shader.frag -o shader.spv` (Vulkan SDK). O exemplo completo
está em `mods-exemplos/shaders/cor-quente`. Para instalá-lo num jogo sem o SDK:
`python tools/dev/n7_shader_exemplo.py <pasta do jogo>`.

## Validação (segurança)

O jogo **não executa código nativo** de mods: só SPIR-V, que roda na GPU e só lê as imagens que a cadeia entrega.
Antes de ir para a GPU, cada pacote é conferido:

1. **Manifesto:**
   - `formato = 1`;
   - no máximo 16 parâmetros, cada um com nome e com `minimo < maximo` e o padrão dentro da faixa;
   - o `shader` dentro da pasta do pacote, também depois de resolver links e junções (sem `..` nem caminho absoluto).
2. **SPIR-V:**
   - número mágico e versão até 1.3;
   - cada instrução com os operandos completos;
   - um único ponto de entrada, de **fragmento**, chamado `main`;
   - só as capacidades `Shader`, `Matrix`, `ImageQuery` e `DerivativeControl`;
   - só o conjunto estendido `GLSL.std.450`;
   - **sem laços**: um passe tem de terminar. Use somas desenroladas; laços com orçamento de execução ficam para o
     N9.
3. **Interface:**
   - entradas só em `location 0` (o uv) e `gl_FragCoord`;
   - saída só em `location 0`;
   - recursos só `sampler2D` simples (2D, sem array, multiamostra ou shadow) em `set 0, binding 0` ou `1`;
   - push constants de até 80 bytes, só escalares e vetores de 32 bits, structs e arrays com offset e stride;
   - **sem** buffers, imagens de armazenamento, escrita em imagem, atômicos ou memória compartilhada.
4. **Driver:** o módulo e o pipeline têm de ser criados sem erro.

Um pacote que falha em qualquer ponto fica **desativado**:
- o motivo vai para o log (`[pos] shader de mod <nome>: INVALIDO: ...`);
- o menu mostra "Shaders inválidos: N";
- se ele estava escolhido, a escolha volta para "nenhum" e é gravada assim;
- uma perda do dispositivo com o shader ativo, ou o driver recusando-o num dispositivo recriado, também o desativa;
- o jogo continua com os outros efeitos.

## Limites desta versão (N7)

- Um shader de mod por vez, de um passe.
- Os efeitos internos desta etapa são de imagem (correção de cor; tonemapping, nitidez e bloom em seguida). Não há
  dados de luz inventados.
- SSAO só virá se a profundidade do renderizador servir (`docs/N7-PLANO.md`, seção 21).
- Pacotes com vários passes, texturas próprias (LUTs) e as ferramentas de criação ficam para o N9.
