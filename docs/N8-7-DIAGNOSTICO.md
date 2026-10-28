# N8.7: diagnóstico técnico dos bloqueios (somente leitura, 2026-10-28)

Feito durante a regressão nível 3 do N8.8, sem build, teste de GPU, alteração de arquivo nem disputa de recursos.
Fontes: código (`native/render`, `runtime/pvr`, `transitional/render_capture`), `docs/N7-PLANO.md` (seções 12–16 e
24–25), `STATUS.md`, a sonda de hardware (`analysis/hwscan_sonda/hw_manifest.tsv`) e o C++ gerado.

Legenda: **[C]** comprovado por evidência registrada; **[H]** hipótese; **[N]** não verificado.

## 1. `VK_ERROR_DEVICE_LOST` na AMD (APU: GPU integrada ao processador)

### Fatos
- [C] Falha de página real da GPU: `VK_EXT_device_fault` relata `GPU fault`, faixa `Invalid Read`, fila
  gráfica/compute/transfer com a submissão incompleta (2026-10-24).
- [C] Intermitente: 1 em 10 rodadas A1 sem camadas, em renders diferentes (370, 371, 469→470).
- [C] O render da falha sai com a **imagem anterior**, e a cerca e a fila ociosa acusam sucesso (`vk_device.cpp:1208`).
  A falha é desse render; só o envio seguinte acusa a perda.
- [C] Só no modo `comparar`. Não aparece:
  - no modo nativo (2 rotas completas);
  - com a GPU ociosa entre quadros (`BS_VK_ATRASO_MS`);
  - com o software sequencial (`BS_RENDER_SEQ=1`);
  - reproduzindo sozinho o render gravado (50 vezes, imagem igual à da NVIDIA);
  - com as Validation Layers completas mais a sincronização (0 mensagens).
- [C] O `robustBufferAccess` está ligado desde 2026-10-22 (`vk_device.cpp:98`), antes da captura de 2026-10-24.
- [C] O dispositivo é usado por uma única thread, com submissão síncrona (cerca e `vkQueueWaitIdle` a cada quadro).
  Não há quadro em voo quando texturas e buffers são destruídos (`destroy_texture`, `ensureHost`, `fragments`).
- [C] No `comparar`, o software rasteriza **depois** do Vulkan de cada render, com todas as threads
  (`std::execution::par`, `pvr_render.cpp:351`; ordem em `pvr_render.cpp:760-761`). A GPU parte de um estado ocioso
  logo depois de uma rajada de carga em todos os núcleos.

### O que isso exclui ou enfraquece
- **Leitura fora dos limites** em storage buffer ou vértice (frags, heads, tris, verts): o `robustBufferAccess` define
  o resultado. Fica excluída como causa da falha de página dentro do intervalo do descritor.
- **Erro de dados do render (shader):** a reprodução isolada é idêntica 50 vezes. Ressalva: ela recria todas as
  texturas de uma vez, e a sessão real só atualiza as que mudaram, então o histórico de texturas não é reproduzido.
- **Uso incorreto da API e hazards de sincronização:** as camadas não acusam nada. Um caminho raro ainda pode escapar,
  porque as camadas mudam o tempo de execução.
- **Concorrência de CPU sobre objetos Vulkan:** não há outra thread no headless.

### Hipóteses, em ordem de probabilidade
- **H1 [H] Defeito do driver ou do firmware da APU, ligado a energia, relógio ou residência sob carga da CPU.**
  - A favor: só falha com todos os núcleos ocupados logo antes do envio. Não falha com a GPU ociosa sem carga nem com o
    software sequencial. Nenhum acesso do backend está fora das regras. A NVIDIA dedicada nunca falha.
  - Na APU, CPU e GPU dividem memória, controlador e orçamento de energia; o carve-out de "VRAM" é pequeno e o WDDM
    gerencia a residência.
  - Contra: "Invalid Read" é endereço inválido, não lentidão. Precisa de um mecanismo (página despejada ou mapeamento
    de residência) que a hipótese ainda não mostra.
- **H2 [H] Acesso não coberto pela robustez, dependente do histórico.** O `robustBufferAccess` não protege:
  - cópias (`vkCmdCopyBufferToImage` das texturas, cópias de leitura);
  - imagens (`texelFetch` fora da imagem é comportamento indefinido);
  - descritores de recursos já liberados.

  O ciclo de texturas da sessão real (atualizar em lugar, despejar a cada 300 renders, recriar ao mudar de tamanho:
  `render_capture.cpp:395-420`) não aparece na reprodução isolada. Contra: nenhum caminho incorreto foi encontrado na
  leitura, e as camadas validam as regiões das cópias.
- **H3 [H] Página de memória do driver** (fila, descritores, scratch do `resolve.comp`, que usa 3×128 palavras por
  invocação). A favor: em 2026-10-22, uma variante "várias passadas de K em K" do resolve dava imagem errada só na AMD.
  Contra: a variante atual passou nas cenas de 6, 70 e 140 camadas e nas 9 rodadas sem falha.

### Experimentos que decidem (quando autorizados; só diagnóstico)
1. **Separar carga e código.** Modo nativo com carga artificial em todos os núcleos entre os envios.
   - Se falhar: carga ou energia, H1.
   - Se não falhar: o caminho do `comparar`.

   Ao mesmo tempo, rodar o `comparar` com `BS_RENDER_SEQ=1`. Com 10% de falha por rodada, 30 rodadas por configuração
   dão cerca de 96% de chance de ver pelo menos uma falha.
2. **Radeon GPU Detective (RGD)**, o analisador de falha de página da AMD no Windows (se a geração da APU for
   suportada, a confirmar).
   - Mostra o recurso e o shader que geraram o endereço.
   - Pede nomes nos objetos (`VK_EXT_debug_utils`), uma mudança só de diagnóstico.
   - Alternativa: `VK_EXT_device_address_binding_report`, para mapear o endereço da falha nos buffers e imagens vivos.
3. **Localizar o comando:** `vkCmdWriteBufferMarkerAMD` entre os lotes, ou o render pass quebrado por fase, para o
   `crash_diagnostic` ter checkpoints.
4. **Isolar o ciclo de texturas:** sem despejo e sem atualização em lugar (sempre uma textura nova), 30 rodadas.
5. **Isolar o resolve:** K=32 e um caminho sem autosort (só diagnóstico), 30 rodadas cada.
6. Testar o driver Adrenalin mais recente da APU e registrar a versão.

### Correções possíveis (dependem do experimento)
- H1: contornar sem esconder (prender a GPU num estado de energia é inviável no produto) e reportar à AMD com o
  reprodutor. O recuo atual (recriar o dispositivo e depois ir para o software) continua como mitigação.
- H2/H3: corrigir o acesso apontado pelo RGD.

**Risco:** médio. **Complexidade:** baixa a média para diagnosticar; a correção depende da causa.

**Relação com o N8.7: independente.** O corte gráfico não exige resolver a AMD, mas o N7 só sai de 95% com a causa
eliminada. O produto (modo nativo, sem o software em paralelo) tem exposição menor, ainda não medida: a apresentação
direta nunca foi testada na AMD.

## 2. Fog

### Fatos
- [C] O modo de fog do TSP (bits 23-22) é "tabela" em 2 758 908 desenhos da rota de combate, a partir do render 1127.
  Zero desenhos em "por vértice" ou "Table2".
- [C] O jogo programa a fog: a função `8C1C59A4`, que grava registradores do PVR por índice e é chamada por
  `8C18ADBC`, `8C18B200` e `8C18B3FA`, grava:
  - `FOG_TABLE` (as 128 entradas, 3 vezes cada);
  - `FOG_DENSITY` 3 vezes, `FOG_COL_RAM` 4 vezes, `FOG_COL_VERT` 2 vezes;
  - `FOG_CLAMP_MAX/MIN` 1 vez cada.
- [C] A IR já captura a tabela, a cor e a densidade (`render_capture.cpp:676-679`, `render.h:97-100`). Nem o software
  (`pvr_render.cpp:15`: "sem fog") nem o Vulkan (`pvr.frag`) aplicam.
- [N] **O efeito visível.** A contagem é de bits do TSP, e o modo 0 também é o valor quando ninguém o escolhe. Se a
  tabela ou a densidade forem quase nulas no render 1127, a fog é invisível. Ninguém leu os valores daquele render.
  Isso não contradiz o bloqueio, mas o dimensiona.

### Causa do bloqueio
[C] Falta implementação e falta oráculo. Não há defeito, a fog só nunca foi feita. Pela documentação pública, o
hardware:
1. calcula um índice pseudo-logarítmico de `FOG_DENSITY × 1/w`;
2. interpola os dois valores de 8 bits da entrada da tabela;
3. mistura a cor final do pixel com `FOG_COL_RAM` por esse alfa.

A fórmula exata do índice e da interpolação **[H]** tem de ser confirmada contra o oráculo. O Flycast serve só como
referência de comportamento (GPL, sem cópia de código).

### Alternativas
- **(a)** Fog na IR, nos dois backends: um passe por pixel no fragmento. O Vulkan e um software-IR fazem a mesma conta,
  porque é função só do 1/w e de constantes do quadro.
- **(b)** Fog só no Vulkan: descartada. O software deixaria de ser a referência.

**Risco:** baixo a médio, porque a fórmula é pequena e determinística. **Complexidade:** média, por causa do oráculo e
dos arredondamentos.

### Testes
- Primeiro, sem oráculo: ler a tabela e a densidade do render 1127 (`BS_RENDER_IR_GUARDAR=1127:<arq>`, já existe).
  Isso diz quanto a fog muda a imagem.
- Cenas sintéticas com a tabela em rampa e em degrau e 1/w conhecidos, com a igualdade exata Vulkan × software-IR.
- Quadro pareado com o Flycast no render 1127 e em mais 2 ou 3 cenas com fog.

## 3. Volumes modificadores, stencil, profundidade e ordem

### Fatos
- [C] 76 287 volumes, 2 109 830 triângulos de volume e 1 170 volumes sem fechamento, a partir do render 1127.
- [C] O jogo **não usa polígonos de dois volumes**: nenhum, 0 parâmetros omitidos (`N7-PLANO` seção 12). Então os
  volumes agem pelo **modo de sombra de um volume** ("cheap shadow"): dentro do volume, o polígono marcado tem a cor
  escalada por `FPU_SHAD_SCALE`.
- [C] O jogo grava `FPU_SHAD_SCALE` (`0x5F8074`, 3 vezes, pela mesma `8C1C59A4`).
- [C] A IR tem a geometria, a lista (1 ou 3) e a inclusão ou exclusão de cada volume. **Faltam:**
  - o bit de sombra do polígono (PCW bit 7);
  - `FPU_SHAD_SCALE`.

  Sem os dois, nenhum backend sabe o que escurecer. O software descarta as listas 1/3 (`pvr_render.cpp:129, 161`).
- [N] Quantos desenhos têm o bit de sombra, e se a lista 3 (volumes sobre translúcidos) é usada de fato.

### Semântica que o backend precisa reproduzir [H, a confirmar no oráculo]
- O PVR é diferido por tile:
  1. resolve a profundidade dos opacos e do punch-through;
  2. cada triângulo de volume com teste de profundidade contra essa profundidade inverte um bit do pixel (paridade);
  3. a instrução "último polígono" fecha o volume: inclusão faz OU, exclusão faz E;
  4. só então sombreia: os pixels dentro, cujo polígono tem o bit de sombra, são escalados.
- Na lista 3, o teste é contra a profundidade de **cada fragmento translúcido**, não a final. Isso exige avaliar os
  volumes por fragmento dentro do autosort.

### Mapeamento possível
- **Opacos (lista 1):**
  - profundidade D32 → D32S8;
  - passe de stencil por volume, com incremento ou inversão e teste de profundidade igual ao do hardware;
  - um anexo com o bit de sombra por pixel;
  - um passe final que escala a cor onde stencil e bit coincidem.

  O modo fiel atual desenha os opacos à frente com a profundidade já final por pixel, o que serve: a sombra vira um
  pós-passe sobre a cor quantizada, antes das translúcidas.
- **Translúcidos (lista 3):** guardar no fragmento da lista por pixel o bit "dentro", calculado contra o z do próprio
  fragmento. Isso é contar os triângulos de volume que cobrem o pixel à frente dele: caro e complexo.
- **Volumes sem fechamento (1 170):** a regra do hardware para um volume aberto **[N]**.

**Risco:** alto, pelas regras de borda (empates de profundidade, top-left na geometria do volume, volumes abertos) e
pela lista 3. **Complexidade:** alta.

### Testes
- Cenas sintéticas: um volume fechado sobre opaco; inclusão × exclusão; um volume aberto; empate de profundidade.
  Igualdade exata entre os dois backends.
- Contagem real: desenhos com o bit de sombra e uso da lista 3, nas rotas.
- Quadros pareados com o Flycast nas cenas com sombra (a partir do render 1127).

## 4. Dependências de TA, PVR e VRAM no produto

[C] Lidas no código e medidas pela sonda (`newgame_area1`, 6 000 VBlanks):

| Dependência | Onde | Medida |
|---|---|---|
| Listas de vértices pelas store queues e pelo DMAC do canal 2 até a FIFO do TA | `memory/sh4_sq`, `sh4_dmac`, `pvr/ta` | SQ 79 M acessos, TA 2,69 M, DMAC 35,8 mil |
| Decodificação das listas (o único parser) | `pvr_render.cpp` `Parser::feed`, observado pelo `PvrCaptureSink` | todos os renders |
| Parâmetros do quadro | `render_capture.cpp:672-686` lê os registradores do PVR (fundo, `HALF_OFFSET`, fog, recorte, `ISP_FEED_CFG`) | 376 mil acessos aos registradores |
| Texturas | `texture_source` lê os bytes da VRAM pelo endereço do TCW a cada render e compara com o cache (`render_capture.cpp:374-410`) | VRAM 32/64 bits: 2,68 M |
| Fim de render e VBlank | `holly_intc`; o jogo espera as interrupções | todo quadro |
| Framebuffer de volta | `writeBack` grava em `FB_W_SOF1` | todo render |
| Texturas montadas em tempo de execução (render-to-texture) | **[N]**: a varredura estática não as vê (`N7-PLANO` seção 12) | — |

## 5. Viabilidade de uma representação gráfica nativa

[H] É viável em etapas, com uma distinção que é decisão do mantenedor.

- **O que sai:** o comportamento de hardware simulado.
  - **FIFO do TA e store queues como transporte:** o produtor nativo intercepta o buffer de vértices que o Kamui
    entrega (D-N8-2 b) e o decodifica sem passar pela FIFO.
  - **DMAC do canal 2.**
  - **Registradores do PVR com efeito colateral:** viram um banco de "estado do quadro" gravado pela `8C1C59A4`, a
    função que concentra as gravações e é um ponto natural de interceptação.
  - **Rasterizador sobre o TA:** vira o software-IR.
- **O que não sai sem o N10:** os bytes das texturas no endereço que o jogo usa no TCW. O jogo referencia texturas pelo
  endereço de VRAM. Tirar isso exige elevar as APIs de textura do Kamui para handles, e isso é N10 pela D-N8-2.
  - Um **heap de texturas** endereçado como a VRAM é uma área de memória, como a RAM do guest. Não é hardware simulado.
  - Se isso conta como "VRAM simulada" para o critério 7.1 é a decisão: recomendo aceitar o heap como memória do
    guest.
- **Interrupções de fim de render:** continuam como eventos do tempo lógico (N2), sem o PVR.
- **Software-IR:**
  - o modo fiel do Vulkan já reproduz bit a bit as contas do software sobre a `CommandList` (`frame_prep`,
    `pvr.frag`);
  - um rasterizador em C++ sobre a mesma IR é sobretudo reorganização do `pvr_render` (o `Raster` passa a consumir
    `nr::CommandList` em vez de `PvrTri`);
  - é o passo de maior alavanca: dá à fog e aos volumes uma referência em software fora do legado.

**Risco:**
- software-IR: médio;
- produtor nativo da IR: alto, por ser o caminho quente com 79 M acessos, pela invalidação de texturas e pelo
  render-to-texture desconhecido;
- corte: médio, se a comparação tripla estiver verde.

## 6. Ordem recomendada

1. **[sem tocar no gráfico] Medidas.** Valores de fog e de `FPU_SHAD_SCALE` no render 1127; contagem de desenhos com o
   bit de sombra e uso da lista 3, depois de capturar o bit na IR (só captura, sem efeito na imagem); varredura de
   render-to-texture.
2. **Oráculo gráfico do Flycast.** O `flycast_oracle.py` hoje só lê memória e registradores pelo GDB. Para pixels:
   - framebuffer emulado na VRAM, lido pelo GDB depois do render;
   - ou captura de tela no mesmo passo do roteiro (`passo_combate`, por passo).

   **[N]** confirmar a opção do Flycast que faz isso.
3. **AMD em paralelo:** os experimentos 1 a 6 da seção 1. Independente do resto.
4. **Software-IR,** bit a bit igual ao software atual: 16 referências, cenas de fidelidade e rota de combate. Ainda
   sem fog e sem volumes.
5. **Fog por tabela** no software-IR e no Vulkan; cenas sintéticas e oráculo.
6. **Sombras dos volumes da lista 1**, nos dois backends; cenas e oráculo.
7. **Lista 3**, só se a medida do passo 1 mostrar uso.
8. **Produtor nativo da IR** (listas, estado do quadro, heap de texturas), com comparação tripla: software atual,
   software-IR e Vulkan. Validation Layers.
9. **Corte:** SQ/DMAC/TA/PVR/`pvr_render` vão para a validação; a catraca os exclui do produto.

Os passos 5 a 7 reabrem trabalho do N7 (fidelidade). Pela regra do mantenedor, pedem autorização explícita.

## 7. Dependências entre N7 e N8.7

| Item | N7 (fidelidade, 95%) | N8.7 (corte) |
|---|---|---|
| Fog e volumes | obrigatório para o N7 fechar | pré-requisito do plano para o corte (não se retira a referência sem eles) |
| Software-IR | — | pré-requisito do corte; também é a base certa para fazer fog e volumes |
| AMD | obrigatório para o N7 fechar | não bloqueia o corte |
| Produtor nativo da IR | — | o próprio N8.7 |

Consequência: o N8.7 só fica desbloqueado com os passos 4 a 6. Os passos 5 e 6 são do N7. Sem autorização para eles, a
única saída compatível com o plano é a terceira opção do `docs/N8-7-GRAFICOS.md`: aceitar o N8 sem o corte gráfico.

## 8. Testes para comprovar as correções

- **AMD:** 30 rodadas A1 por configuração com 0 falhas, depois da correção. RGD ou endereço da falha apontando o
  recurso. Validation Layers com 0 mensagens. Apresentação direta na AMD.
- **Software-IR:** 16/16 referências com as 179 exceções e nenhuma nova; cenas de fidelidade; rota de combate render a
  render igual ao software atual.
- **Fog e volumes:** cenas sintéticas com igualdade exata entre os dois backends; quadros pareados com o Flycast
  (critério de pixels a definir com o mantenedor); o preset Original sem regressão fora das cenas com fog e sombra.
- **Produtor nativo:** comparação tripla em todo render das rotas; o modo `conferir` da IR do N7a; os 16 negativos; a
  catraca.
- **Corte:** regressão nível 3, a campanha e o mapa e imports sem o PVR/TA no produto.

N7 permanece em 95%. N8.7 continua bloqueado. Nenhuma implementação gráfica sem autorização.
