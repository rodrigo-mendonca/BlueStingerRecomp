# N6 (áudio nativo): plano e preparação (2026-10-20)

**Estado:** preparação técnica autorizada em paralelo ao fechamento do N5, numa worktree própria
(`../recomp-BStinger-n6`, branch `n6-audio`). **Não** inclui integrar ao gameplay, criar slots no
código gerado, mudar a AICA/CRI no runtime nem promover backend. Sem commit sem autorização.

**Escopo formal** (`docs/PLANO-PORT-NATIVO.md`, N6 e seção 4.1):
- provedor nativo da API de som do lado SH-4, com um motor que lê os bancos e sequências do
  Manatee;
- `MANATEE.DRV` só analisado estaticamente, nunca executado;
- música, efeitos e diálogos separados; mods em WAV, FLAC e OGG; dublagem em português e outros
  idiomas;
- IDs estáveis pelos identificadores originais;
- modo fiel (o contrato original) e modo modificado (duração do mod).

## 1. Inventário (`tools/dev/n6_inventario.py`, `analysis/n6/inventario.tsv`, sem conteúdo do jogo)

| Família | Recursos | Formato | Como o jogo toca |
|---|---|---|---|
| **Falas e áudio dos eventos** | 233 `E*.WAV` (`E000ENV`, `E001A`–`D`, ..., `E101G`), **47,4 min**, o mais longo com 105 s | RIFF com **format tag 0 = ADPCM de 4 bits da AICA** (Yamaha), mono, 22 050 Hz; nenhum com laço (`smpl`) | streaming pelo **player WAV da CRI** (`8C08B860` → `PlyCreateWav` `8C158FC4`; `PlyInitWav` `8C158E98` por `8C0788A0`), no mesmo pool MW do N5 |
| **Músicas** | 11 pares `M*.MSB` + `M*.MPB` (M2A, M2B, M2C, M3, M4, M5, M6, M7, M9, M10, M99) | Manatee: `SMSB` (sequências `SMSD`, MIDI do driver) e `SMPB` (programas e amostras) | sequenciador MIDI do Manatee no ARM7 com amostras do banco: **não é áudio gravado** |
| **Efeitos e bancos fixos** | `BS_SND.MLT` (1,7 MB) | contêiner `SMLT` v1.01 com 7 unidades: `SMPB#0`, `SMSB#0`, `SOSB#0`, `SOSB#1` (one-shots), `SFPB#0`, `SFOB#0`, `SFPW#0` (programas, saídas e área de trabalho do **DSP de efeitos** da AICA) | `sd_multi_unit_download` (`8C15DDA4`, chamado pelo jogo em `8C02A200`) carrega na RAM de som; tocados por chamadas `sd` (shots, sequências) |
| Áudio dos filmes | 16 SFD (ADX) | — | **N5**, fora do N6 |

O decodificador ADPCM nativo (`native/audio/aica_adpcm`) dá áudio plausível nos WAV reais: RMS
de 4 600 a 5 800, saturação rara (12 amostras em 173 mil no E001A, 0 no E000ENV). A prova contra a
AICA fica para o oráculo (seção 6).

## 2. Fronteiras de integração (`analysis/hwscan`: callgraph, fronteira, sdk_faixas)

```
jogo (69 pontos de chamada)
  └─ gerenciador de som do jogo  8C029E00–8C02B000  (18 funções; 8C02A6B8 tem 33 chamadores,
     │                                               8C02AE70 tem 8: candidatos a "tocar efeito"/"música")
     └─ camada de som do SDK     8C15D534–8C15E5BC  (~25 funções, todas só do gerenciador)
        └─ driver sd/Manatee     8C17C7E4–8C17DAB4 (38) + 8C1986D8–8C198EE8 + 8C1CB8F8–8C1CC548
           (também usado pela CRI 8C096xxx: o som do MW/ADX dos filmes e WAV)
falas: 8C08B860 → PlyCreateWav (CRI MW WAV), por vtable como os filmes do N5
```

**Fronteira proposta: o gerenciador de som do jogo** (`8C029E00`–`8C02B000`). É a API de mais alto
nível (efeito, música, volume, parada), com os IDs do próprio jogo. Abaixo dele tudo é SDK.

As falas, como no N5, ficam nas entradas MW do player WAV. A diferença é o `PlyCreateWav` no lugar
do create do Sofdec; a mesma técnica de pool e encaminhamento de handles alheios.

**Primeira análise estática (2026-10-20):** `8C02A6B8`, chamada de 33 pontos, **não toca o som:
enfileira um pedido**, `enfileirar(id = r4, a = r5, b = r6)`.
- Fila de 16 entradas de 6 bytes em `0x8C1F4CD0`, com contador em `0x8C1F4D52`. Cada entrada tem o
  ID em 16 bits, os bytes `a` e `b` e um byte da tabela por ID (`0x8C0C5B60`).
- Fila cheia: o pedido é descartado.
- O processamento (`8C02A8C0` lê `@(2)` e `@(3)` de cada entrada e chama o SDK) acontece uma vez
  por quadro.

É uma fronteira natural para o N6: um pedido por ID do jogo e uma passada por quadro. O provedor
nativo pode atender a passada da fila mantendo o enfileiramento original, e o significado de `a`
e `b` (volume e pan, ou o par do Codex em `8C02A966`/`8C02A976`) se confirma no oráculo.

**Segunda análise estática (2026-10-20, `bs-disasm` + `analysis/hwscan/callgraph.tsv`): a passada e o consumidor.**

| Peça | O que faz (endereços) |
|---|---|
| Registro da fila (6 bytes) | `+0` ID (16 bits), `+2` a, `+3` b, `+4` **prioridade** (byte com sinal, da tabela `0x8C0C5B60 + id*2`; o 2º byte da tabela não é lido aqui), `+5` livre. Índice × 6 por `shll2 + shll` |
| Passada por quadro `8C02A760` (chamada por `8C02A386`, cauda de `8C064A52`) | sai cedo se os três contadores (`4D52` fila, `4D54` ativos, `4D56` próximos) forem 0. Se houver ativos: `8C02ACB2` (para os canais anteriores pelo SDK: `8C15DF44`, `8C15E19C`, `8C15E11C`) e o consumidor `8C02A8C0` sobre o **conjunto ativo** em `0x8C1F4C7C`. Depois: cópia de segurança dos 7 ativos em `0x8C1F4CA6`; **ordenação por seleção** das 16 entradas pela prioridade (decrescente, `cmp/gt` com sinal; empate mantém a ordem de chegada); as **7 primeiras** viram o próximo conjunto ativo; contador limitado a 7; **as demais são descartadas**; a fila é zerada (ID `0xFFFF`, a = b = 0, prioridade −1) |
| Consumidor `8C02A8C0` | para cada ativo: canal `ch = [0x8C1F4D58]` em **rodízio 0..6** e `handle = [0x8C1F4C58 + ch*4]` (7 canais do SDK). `8C15E208(handle)` (reinicia o canal); `div(id, 128)` (`8C08D428` é o `div` da libc: testa divisão por zero e divide); `8C15E088(handle, id/128, id%128, 0)`, ou seja, **toca o pedido `id%128` do banco `id/128`**; `8C15E19C(handle, a, 0)` e `8C15E11C(handle, b, 0)` (forma `(canal, valor, tempo)`: volume e pan prováveis, ordem a confirmar no oráculo); grava a prioridade do canal em `0x8C1F4D43 + ch*2` |
| Prioridades (400 primeiros IDs) | mais comuns 64 (87), 0 (70), 127 (39), 9, −8 (24), 22, 40, 72, 60, 48. 127 é a máxima |

Política original, para o nativo reproduzir **sem reinventar**: até 7 efeitos novos por quadro, os de
maior prioridade, em 7 canais por rodízio (o mais antigo é reaproveitado, não o de menor prioridade),
com latência de uma passada entre o pedido e o disparo (a conferir no oráculo, junto com a ordem a/b).

**Fronteira mais segura para o N6 (efeitos):** manter recompilados o enfileiramento, a ordenação, o
limite de 7 e o rodízio (é regra do jogo) e trocar só as **5 entradas do SDK** que os dois consumidores
chamam (`8C15E208` reinicia, `8C15E088` toca banco/pedido, `8C15E19C` e `8C15E11C` parâmetros,
`8C15DF44` para), como as 8 entradas MW do N5. O provedor nativo vira um tocador de 7 canais que
resolve `(banco, pedido)` pelo catálogo (`efeito/<banco>/<pedido>`) com mods por arquivo. Música
(`8C02AE70`, 8 chamadores) e falas (`PlyCreateWav`) ficam em fronteiras próprias.

**A confirmar** (análise estática e oráculo, antes de qualquer slot):
- a assinatura e a semântica das 18 funções do gerenciador (argumentos, IDs, volume, laço,
  retorno);
- chamadas por ponteiro que o callgraph não vê;
- quem chama o DSP de efeitos e quando;
- a relação entre os eventos de história (`E###`) e os WAV (`E001A`–`D`).

## 3. Arquitetura nativa (portável; componentes em `native/audio`)

| Peça | Arquivo | Estado |
|---|---|---|
| Saída | `IAudioOutput` (N5): XAudio2 no Windows; **SDL3 Audio** no Linux futuro | existente (XAudio2) |
| Mixer | `mixer.{h,cpp}`: barramentos música/voz/efeitos com volume próprio; voz com volume, pan e laço por pontos; prioridade e roubo de voz; pausa (voz e geral), retomada, parada; eventos de conclusão, laço, parada e roubo, no quadro exato; reamostragem linear; o tempo avança só por `render(n)` (o relógio lógico decide n, sem relógio de parede) | **pronto, testado** |
| Decodificação | `aica_adpcm.{h,cpp}` (decodificador e codificador); `sound_file.{h,cpp}`: WAV PCM 8/16 e ADPCM da AICA, laço pelo `smpl` | **pronto, testado** |
| Catálogo e mods | `sound_catalog.{h,cpp}` (seção 4) | **pronto, testado** |
| Música (Manatee) | sequenciador MIDI e sampler nativos lendo `SMSB`/`SMPB` | **a fazer** (seção 6, risco 1) |
| Efeitos (`SOSB`, `SMSB` do MLT) | tabela de one-shots do banco para `SoundData`, tocados pelo mixer | **a fazer** |
| DSP de efeitos (`SFPB`/`SFOB`/`SFPW`) | reverb/eco nativo equivalente | **a fazer** (risco 2) |
| Falas | WAV ADPCM pelo catálogo; adaptador MW no padrão do N5 | **a fazer** |

Interfaces compartilhadas sem Win32. Os componentes portáveis ficam no `bs_native_audio_core`; o
XAudio2 só no `bs_native_audio` (backend). O `IAudioEngine` do N0 continua o contrato de alto nível
e será refinado quando a fronteira do gerenciador for confirmada.

## 4. Mods

**IDs estáveis** pelo recurso original: `voz/E001A`, `musica/M2A`, `efeito/BS_SND:SOSB0:12`.
Sem nomes improvisados e sem substituir o arquivo do disco.

**Resolução** (`SoundCatalog::resolve(id, idioma)`), a primeira que existir:
1. `mods\audio\<idioma>\<id>.(wav|flac|ogg)`, por exemplo `pt-br`;
2. `mods\audio\<idioma base>\...`, por exemplo `pt`;
3. `mods\audio\<id>.(...)`, mod neutro (música remasterizada, efeito);
4. o recurso do Builder (`data\audio\manifest.toml`).

Caminhos fora da pasta são recusados. Um idioma sem tradução cai no original, nunca em outro
idioma.

**Dublagem com outra duração** (`mods\audio\manifest.toml`, `sincronia` por ID):
- `original` (**padrão para voz**): o jogo vê o fim na duração ORIGINAL. O áudio do mod continua até
  acabar, ou até o jogo parar a voz, sem acelerar nem esperar.
- `arquivo`: o fim é o do arquivo, só onde o fim do áudio é o próprio evento do jogo.
- `logical_end_frames` calcula o fim que o jogo vê. O relógio lógico não muda.

**Música:** laço por pontos (`smpl` do WAV; nos FLAC/OGG, metadados a definir), volume por
barramento. As transições seguem os eventos do jogo (o gerenciador pede a troca; o mixer corta ou
encadeia).

**Efeitos:** substituição individual por ID; a prioridade e o limite de vozes continuam os do jogo.

**FLAC e OGG/Vorbis:** a interface aceita; o decodificador está **pendente de escolha**. Candidatos
de cabeçalho único e licença permissiva: `dr_flac` (domínio público/MIT-0) e `stb_vorbis` (domínio
público/MIT). Até lá, erro explícito, nunca leitura silenciosa errada.

## 5. Ferramentas e testes independentes (prontos)

- `tools/dev/n6_inventario.py`: inventário reproduzível (TSV + resumo).
- `bs_audio_tests` (ctest `audio`), **11/11**:
  - ADPCM: ida e volta com SNR > 20 dB; saturação controlada;
  - WAV: PCM, ADPCM e laço; formato sem suporte e FLAC dão erro claro;
  - mixer: fim no quadro exato com reamostragem, volume por barramento, pan, laços, pausa da voz e
    geral, parada, prioridade e roubo, recusa;
  - catálogo: idioma exato, base e neutro, original, ausente, caminho fora recusado, ID inválido,
    sincronia;
  - duração da dublagem: fiel, modificado e sem duração original.

O teste já achou um defeito: o evento de fim saía um bloco tarde quando o som acabava no último
quadro do bloco. Corrigido.

## 6. Riscos e dependências não resolvidas

1. **Música é MIDI do Manatee**, não áudio gravado. Reproduzir com fidelidade exige um sequenciador
   e sampler nativos: formato `SMSD`, programas e envelopes do `SMPB`, comportamento do driver.
   Alternativa a avaliar: o Builder **renderiza** cada sequência em áudio (como os filmes do N5).
   Isso é simples, mas perde a parte dinâmica (troca de volume e parada no meio, se o jogo usar).
   Decisão do mantenedor depois da análise do gerenciador.
2. **DSP de efeitos da AICA** (`SFPB`/`SFOB`/`SFPW`): microprograma do DSP. Reproduzir exatamente é
   praticamente emular o DSP (proibido como solução permanente); o caminho é um reverb nativo
   equivalente, medido contra o Flycast.
3. **Fidelidade do ADPCM:** o algoritmo é o conhecido da AICA; falta o oráculo (captura do áudio
   do Flycast numa fala).
4. **Chamadas por ponteiro** ao gerenciador ou ao `sd` que o callgraph não mostra: instrumentar
   antes dos slots.
5. **Dependências até o N8:** a CRI segue para o ADX do Sofdec só enquanto o legado existir (o N5
   já toca os filmes sem ela). O `sd`/Manatee e a AICA emulada saem quando os três provedores do N6
   estiverem ativos.
6. **FLAC/OGG:** escolha da biblioteca (seção 4).

## 7. Plano para começar a integração (depois do fechamento do N5)

1. **Análise estática** das 18 funções do gerenciador (`8C029E00`–`8C02B000`) e do player WAV:
   assinaturas, IDs, laços e volume (bs-disasm, Ghidra, Codex).
2. **Oráculo O6** no Flycast:
   - chamadas ao gerenciador por passo numa rota com música, efeitos e uma fala;
   - captura do áudio da AICA para o ADPCM e o DSP.
3. **Builder:** `data\audio\manifest.toml`:
   - falas convertidas de ADPCM para WAV PCM, ou mantidas em ADPCM com o decodificador nativo
     (decidir);
   - os one-shots do MLT exportados por ID.
4. **Falas primeiro:** adaptador do player WAV (o padrão do N5) com o catálogo e o mixer. Prova:
   a fala de um evento no jogo sem o WAV do disco, com o retorno ao gameplay igual.
5. **Efeitos:** slots do gerenciador para one-shots; prioridade e limite de vozes.
6. **Música:** a decisão do risco 1, depois os slots de música.
7. **DSP:** reverb nativo equivalente.
8. Testes por família, regressão, e só então a promoção (`--audio nativo`).

## 8. Revisão antecipada do Codex (`reviews/codex-20261020-010550.md`, na worktree): 7 achados, todos aceitos

| Gravidade | Achado | Correção (com teste) |
|---|---|---|
| **ALTA** | Laço com passo de reamostragem maior que o laço lia fora do buffer; a emenda interpolava com amostras depois do laço | Módulo dentro de `[loopStart, loopEnd)`; o vizinho da última amostra do laço é o `loopStart` (`audio_mixer_loop_overrun_and_seam`) |
| MÉDIA | O fim lógico da dublagem não estava ligado ao mixer | `PlayParams::logicalEnd` (posição na fonte, que a pausa não adianta) → evento `LogicalEnd` sem parar o som; se o som acaba antes, os dois no fim físico (`audio_mixer_logical_end_and_event_release`) |
| MÉDIA | Eventos gerados na renderização adiantariam o jogo pela fila da saída (~200 ms) | `take_events(até)`: só os eventos até o quadro já tocado (`IAudioOutput::played_frames`) ou alcançado pelo relógio lógico |
| MÉDIA | `:` dos IDs de efeito é proibido em nomes do Windows | `:` → `#` no nome do arquivo do mod; `#` recusado no ID (`audio_catalog_escape_and_canonical`) |
| MÉDIA | Pan e pitch não eram mutáveis (o `IAudioEngine` exige) | `set_pan`, `set_pitch`, com a posição preservada. Qual campo do gerenciador corresponde a cada um (`8C02A966`/`8C02A976`): PENDENTE da análise |
| MÉDIA | Taxa 0 ou limite de vozes 0 | Normalizados no construtor (`audio_mixer_mutable_params_and_ctor`) |
| BAIXA | Janela TOCTOU com junções | O catálogo guarda o caminho canônico já conferido |

## 9. Preparação adicional (2026-10-20)

- **Fila do jogo:** segunda análise estática na seção 2 (passada por quadro, prioridade, limite de 7,
  rodízio de 7 canais, `div(id, 128)` = banco/pedido) e a fronteira mais segura para os efeitos (as 5
  entradas do SDK).
- **Testes de lacunas reais** (`bs_audio_tests` **14/14**): fontes degeneradas (vazia, 1 quadro, laço de
  tamanho 0, 0 canais, nula); reamostragem nos extremos (8 kHz → 96 kHz, 96 kHz → 8 kHz), pitch 0,01 e o
  limite 16, pitch trocado no meio e pitch inválido (0, negativo, NaN); WAV inválidos (vazio, cabeçalho
  cortado em 5 pontos, tamanho do `data` mentiroso, 0 canais, taxa 0, 24 bits, quadro incompleto).
- **Defeitos achados e corrigidos:** WAV sem chunk `data` era aceito como som vazio (agora erro);
  quadro incompleto no fim ficava no PCM (agora descartado); mais de 8 canais estouravam o `uint8_t`
  (agora recusado); pitch NaN passava pelo `std::clamp` e deixava o índice da fonte indefinido (agora 1).
- **AddressSanitizer (MSVC, sem instalar nada):** `build\n6-asan` só com `bs_audio_tests`:
  `cmake -G Ninja -S . -B build\n6-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS=/fsanitize=address /Zi /EHsc"
  "-DCMAKE_C_FLAGS=/fsanitize=address /Zi"` (+ as fontes do SDL3/toml++ do build principal), PATH com
  `%VCToolsInstallDir%bin\Hostx64\x64` e `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`. Os 14 passam sob o
  ASan (`clang_rt.asan_dynamic-x86_64.dll` importada, conferido com `dumpbin`).
- **Ainda sem teste** (não há o componente): fila cheia do provedor nativo (a política é a do jogo,
  recompilada), perda do dispositivo (é do `IAudioOutput` do N5, XAudio2) e troca de recurso por mod em
  execução (o catálogo resolve por pedido; a troca a quente fica para a integração).

## 10. As 5 entradas do SDK e o adaptador (2026-10-20, durante a regressão do N5)

**Fonte:** o C++ gerado (`generated/funcs_005.cpp`, tradução 1:1 das instruções) e o pool literal lido do
`1ST_READ.BIN`. Nada presumido: o que não se prova estaticamente fica PENDENTE (O6).

**Correção da seção 2:** `8C15DF44` **não para** o canal: é uma **consulta de estado**. Quem para é o
`8C15E208` (comando 0x12), também usado pelo canal avulso.

| Entrada | Argumentos (r4, r5, r6, r7) | Pacote ao driver (`8C17D334`) | Retorno (r0) |
|---|---|---|---|
| `8C15E208` | handle | `8C2D8FC0`: +0 comando **0x12**, +4 porta (byte 1 do handle) | 0 ou erro |
| `8C15E088` | handle, banco, pedido, x | `8C2D8F84`: +0 **0x11**, +4 porta, +5 banco, +6 pedido, +7 x·8; x (byte com sinal) fora de 0..15 → `0x03000001` | 0 ou erro |
| `8C15E19C` | handle, valor (byte com sinal), tempo (16 bits) | `8C2D8FAC`: +0 **0x15**, +4 porta, +5 valor+128, +6/+7 tempo | 0 ou erro; **não confere a inicialização** |
| `8C15E11C` | handle, valor, tempo | `8C2D8F98`: +0 **0x16**, mesmo formato | 0 ou erro |
| `8C15DF44` | handle, saída | não envia: `out[0] = out[1] = 0` e 10 leituras do driver (`8C17D4F0`, `D578`, `D898`, `D688`, `D710`, `D798`, `D600`, `D468`, `D3E8`, `D820`) em +2..+27 | 0 ou erro |

- **Erros comuns:** flag `8C1DDB90` = 0 → `0x0D000001` (não inicializado); handle nulo → `0x07000103`;
  saída nula → `0x07000203`. Com erro, `8C17DC18(endereço da própria função, código)` registra.
- **Convenção:** SH-4 do SDK (r4–r7, r0 de retorno, r8–r14 preservados).
- **Efeitos colaterais:** só o pacote estático de cada entrada e o envio ao driver; nenhum estado do jogo.
- **Quem chama:** o consumidor `8C02A8C0` (7 canais: `E208`, `E088(h, id/128, id%128, 0)`, `E19C(h, a, 0)`,
  `E11C(h, b, 0)`); a passada `8C02ACB2` (para cada canal, `DF44`; **bit 0 da palavra +24 = 0** → o canal
  acabou: `E19C(h, 0, 0)`, `E11C(h, 0, 0)` e a prioridade do canal volta a −1); o **canal avulso** (8º handle):
  `8C02AA8C` toca (`DF44`, `E088(h, id/K, id%K, 0)`, `E19C`, `E11C` e limpa o registro) e `8C02AB40` para
  (`E208`, parâmetros 0). Os dois consumidores **ignoram o retorno**.
- **Temporização:** o jogo vê o fim só pela consulta, uma vez por passada (quadro). O tempo dos parâmetros é
  sempre 0 nas chamadas do jogo.
- **Interação com falas e música:** nenhuma por estas entradas; falas pelo player WAV da CRI e música pelo
  `8C02AE70` (fronteiras próprias, seção 2).

**PENDENTE (O6):**
- qual dos comandos 0x15/0x16 é volume e qual é pan, e a escala de cada um. Indício: `b` é a constante 0 em 46
  das 63 chamadas de `8C02A6B8`, e `a` é calculado em 37 (−8, 2, 8 e 63 quando constante), o que sugere pan em
  `a`. Não é prova;
- a unidade do tempo, o `x` do tocar, os outros campos da consulta;
- o `K` do canal avulso (`mov.w 8C02AB00`).

**Equivalência original → nativo** (`native/audio/sd_adapter.{h,cpp}`, `SdAdapter`):

| Original | Nativo |
|---|---|
| `E208(h)` | `stop(h)`: para a voz da porta (`AudioMixer::stop`); o evento dela é descartado |
| `E088(h, banco, pedido, x)` | `play(h, banco, pedido, x)`: para a voz anterior da porta (key-on recomeça), resolve `(banco, pedido)` pelo resolvedor (catálogo/mods), toca no barramento de efeitos com tag = banco·128 + pedido (o ID do jogo) e aplica os parâmetros da porta |
| `E19C` / `E11C` | `param15` / `param16`: guardam o valor da porta e aplicam na voz atual e nas próximas; o papel (pan, volume, nenhum) e a conversão são opções (`SdAdapterOptions`), provisórias até o O6; tempo ≠ 0 → `0x7F000001` (só do adaptador, recusa explícita) |
| `DF44(h, out)` | `status(h, out)`: zera os 28 bytes e escreve a palavra +24 = 1 se a porta toca |
| relógio | `sync(até)`: o canal só aparece livre quando o tempo **lógico** passa do fim (a renderização vai adiantada); eventos de outras vozes do mesmo mixer passam adiante (`take_other_events`) |

Prioridade, limite de 7 e rodízio continuam no gerenciador recompilado (não duplicados). Recurso ausente:
retorno 0, canal livre, contado (`missing`); voz recusada pelo mixer: contada (`refused`). Sem Win32 e sem o
jogo; **não ligado** às funções recompiladas (os slots dos 5 endereços já existem no `dispatch.cpp`).

**Revisão do Codex** (`reviews/codex-20261020-112136.md`, só os arquivos novos), 2 achados aceitos:

| Gravidade | Achado | Correção (com teste) |
|---|---|---|
| MÉDIA | Um mod mais longo que o original não liberava o canal no fim **lógico**: o rodízio e a prioridade do jogo mudariam | O resolvedor devolve `SdSound{dados, logicalEnd}`; no `LogicalEnd` o canal fica livre e o som segue como "cauda" (sem ocupar o canal, sem vazar eventos); tocar de novo no canal corta a cauda (`audio_sd_logical_end_frees_port`) |
| BAIXA | Eventos de outras vozes acumulavam sem limite se ninguém os drenasse | Callback `onOtherEvent` (entrega na hora); sem ele, limite `otherEventsCap` (1024) e o excedente contado (`audio_sd_other_events_bounded`) |

Itens conferidos sem problema pelo Codex (contra o pool literal e o disassembly):
- os 4 códigos de erro;
- o 0x15 sem a conferência de inicialização;
- o x de 0..15;
- a palavra +24 da consulta;
- a liberação inclusiva no tempo lógico;
- o descarte dos eventos de vozes trocadas.

**Testes** (`bs_audio_tests` **22/22**, também sob o **AddressSanitizer**; 8 novos em `tests/unit/test_audio_sd.cpp`):
- códigos de retorno, inclusive a falta de conferência do 0x15;
- ID preservado e consulta pelo tempo lógico;
- 7 canais + avulso com recomeço sem vazar eventos;
- parada, recurso ausente, voz recusada;
- papéis e conversões dos parâmetros, tempo ≠ 0;
- eventos de outras vozes;
- fim lógico antes do físico e eventos limitados (as duas correções da revisão).

Build só do alvo, `-j 2`, sem disputar com a regressão do N5.

**Recursos e manifestos (plano, sem conversão agora):**
- **Inventário:** seção 1 (`tools/dev/n6_inventario.py`): 233 falas `E*.WAV` (ADPCM da AICA), 11 pares
  `M*.MSB/MPB` (MIDI do Manatee), `BS_SND.MLT` (bancos de efeitos e DSP).
- **Builder:** `data\audio\manifest.toml`, uma entrada por recurso com:
  - `id` estável (`voz/E001A`, `musica/M2A`, `efeito/BS_SND:<unidade>:<pedido>`; o par `(banco, pedido)` do
    adaptador entra como chave secundária);
  - `tipo`, `idioma` (`ja`/`en` original; mods em `pt-br` etc.), arquivo, taxa e canais;
  - `duracao_original` (para a sincronia da dublagem) e laço (`smpl` ou pontos);
  - origem (unidade e deslocamento no MLT).
- **Formatos:** falas e efeitos convertidos de ADPCM para WAV PCM 16 (sem perda adicional). Mods em WAV,
  FLAC e OGG pela resolução por idioma (seção 4). Música depende da decisão do risco 1 (MIDI nativo ou
  renderizada).
- **Durações:** a do evento do jogo fica na original (`sincronia = original`, padrão das falas). O áudio
  substituído pode ser mais longo ou mais curto sem mudar o relógio lógico. Legendas das falas seguem o
  mesmo ID e o tempo do áudio, como no N5.
- **Pendências que precisam do jogo ou do Flycast:**
  - O6: papel e escala dos parâmetros, unidade do tempo, mapeamento `(banco, pedido)` → unidade/one-shot do MLT;
  - captura da AICA para o ADPCM e o DSP.

**Próximos passos depois do N5** (proposta para aprovação):
1. O6 no Flycast: rota curta com efeitos (registrar `E088/E19C/E11C` e o áudio da AICA por passada).
2. Builder: exportar os one-shots do MLT por `(banco, pedido)` + manifesto.
3. Slots dos 5 endereços apontando para o `SdAdapter`, atrás de `--audio-efeitos nativo` (padrão legado),
   com o mixer no relógio lógico.
4. Comparação legado × nativo por passada (canais ocupados, IDs tocados), como o `test_pd_nativo`.

## 11. Plano de integração sobre a base do N5 (proposta para aprovação, 2026-10-20)

Princípio: a lógica de áudio do jogo continua a recompilada (fila de 16, prioridade, 7 ativos, rodízio,
canal avulso); só as chamadas ao SDK trocam de dono, **uma família por vez**, cada uma atrás de uma opção com
o legado como padrão até a prova. Nada irreversível antes de as fronteiras estarem validadas no oráculo.

| Fase | O quê | Prova (nível 2) | Reversível por |
|---|---|---|---|
| A. Base | trazer a `n6-audio` para depois do commit do N5 (rebase ou merge); só componentes portáveis (`bs_native_audio_core`, `SdAdapter`), sem ligação ao jogo | `bs_audio_tests` 22/22 + ASan; build do jogo inalterado | nada ligado |
| B. Oráculo O6 (Flycast) | rota curta com efeitos (o `newgame_area1` já tem passos, portas e saltos): por passada, argumentos de `E088/E19C/E11C/E208/DF44` (porta, banco, pedido, valores) e o áudio da AICA | papel e escala de 0x15/0x16, unidade do tempo, `x`, mapeamento `(banco, pedido)` → unidade do `BS_SND.MLT` | só leitura |
| C. Builder | exportar os one-shots do `MLT` por `(banco, pedido)` (taxa, laço) e as falas `E*.WAV` (ADPCM → PCM 16) para `data\audio` + `manifest.toml` (ID, tipo, idioma, duração original) | conversão reproduzível por SHA-256; durações iguais às do recurso original | `--sem-audio` (como `--sem-filmes`) |
| D. Efeitos no jogo | cola em `transitional/`: os 5 slots (já existem no `dispatch.cpp`) leem r4 (porta = byte 1 do handle) a r7, chamam o `SdAdapter`, gravam r0 e, na consulta, os 28 bytes na RAM do jogo; mixer no **relógio lógico** (`sync` a cada VBlank); saída XAudio2 (`IAudioOutput` do N5) e nula no headless. Música e falas seguem pelo legado (AICA) ao mesmo tempo | **comparação legado × nativo por passada:** a sequência de bits "tocando" de cada canal (o que o jogo lê), os IDs tocados, quadros e estado do jogo iguais nas rotas do `test_pd_nativo`; 0 chamadas aos 5 originais no nativo (fechamento como no N3) | `--audio-efeitos legado` (padrão até a prova) |
| E. Falas | adaptador do player WAV da CRI (`PlyCreateWav`) no padrão das 8 entradas MW do N5 (pool, handles alheios ao original), com catálogo, mods por idioma e sincronia original | fala de um evento sem o WAV do disco, retorno ao gameplay igual ao legado | `--audio-falas legado` |
| F. Música | decisão do risco 1 (sequenciador nativo do Manatee **ou** renderização pelo Builder), depois os slots do `8C02AE70` | a decidir com o mantenedor | `--audio-musica legado` |
| G. DSP | reverb nativo equivalente aos `SFPB/SFOB/SFPW`, medido contra o Flycast | medição | opção |
| H. Promoção | `--audio nativo` padrão só com as 3 famílias provadas; o legado fica como fallback até o N8 | testes completos do áudio + integração dirigida (nível 2) | `--audio legado` |

**Riscos da fase D:**
- **duração dos efeitos:** o jogo libera o canal pelo bit da consulta, e o fim nativo
  (amostras/taxa, no relógio lógico) tem de cair na mesma passada que o da AICA. Por isso a comparação
  da sequência de bits por passada é o critério principal, não o som;
- **duas saídas ao mesmo tempo:** a AICA emulada (música, falas) e o mixer nativo (efeitos). O XAudio2
  aceita as duas, e a AICA deixa de receber os efeitos porque as 5 entradas não chegam mais ao driver;
- **chamadas por ponteiro** ao `sd` fora do gerenciador: o fechamento (0 chamadas aos originais) acusa.

**Ordem do roadmap mantida:** N5 → **N6** → N7 → retorno ao N2 pendente → N8 → N9 (remasterização) → N10 (mods). O N7 fica preparado na
própria worktree, sem integração ao gameplay.

## 12. Fase B: oráculo O6 no Flycast (2026-10-20)

Ferramenta: `tools/dev/n6_oraculo.py`. Registra:
- por passada: a fila do jogo, as 5 entradas do SDK (porta = byte 1 do handle) e a palavra +24 da consulta
  quando muda;
- a cada passada, os canais da AICA que mudaram (KYONB, TL, DISDL, DIPAN) e, no key-on, a amostra (SA, LSA,
  LEA, PCMS, laço, OCT/FNS);
- `--dump-sram` grava a RAM de som (2 MB);
- `--forcar` **mede** a semântica: troca, só no Flycast, o valor de um comando de parâmetro antes de ele
  executar e observa o efeito no registrador. O jogo recompilado não muda.

| Medida | Resultado | Execução |
|---|---|---|
| Latência fila → disparo | pedido na passada 593, `parar`+`tocar` na **596** (3 passadas; a hipótese era 1) | `curto.tsv` |
| Rodízio | portas 0 → 1 → 2 nos sons seguidos | `curto.tsv` |
| **8 handles** | nas trocas de área (passadas 5142 e 8192) o jogo zera os parâmetros das **portas 0–7**: os 7 canais do rodízio + o **avulso** (porta 7) | `o6.tsv` (12 000 passadas, 1 482 s) |
| **0x15 (`a`) = VOLUME** | TL = clamp(30 − 2v): v 0 → 30, −16 → 62, −64 → 158, 63 → 0; DIPAN inalterado | `forcado*.tsv` |
| **0x16 (`b`) = PAN** | DIPAN = (v<0 ? 0x10 : 0) \| ((v<0 ? ~v : v) >> 2): −64 → 31, −32 → 23, 0 → 0, 16 → 4, 63 → 15; TL inalterado | `forcado2/3.tsv` |
| Amostras (para a fase C) | ID 117 → SA 0x150D3C, 0xE25B amostras, PCMS 2 (ADPCM), OCT −2, FNS 833; ID 90 → SA 0x11FA24, 0x3080 amostras, OCT −2, FNS 0 | `forcado*.tsv` |

**O indício estático estava errado:** "`a` calculado em 37 das 63 chamadas" sugeria pan em `a`, e o `a` é o
volume (provavelmente atenuação pela distância). Por isso o adaptador deixava o papel como opção. Agora o
padrão é o medido:
- `sd_tl`, `sd_dipan`: unitário `audio_sd_measured_register_laws` com os 8 pontos;
- `sd_volume_gain` (10^(−0,375·TL/20)) e `sd_pan` (o lado oposto a −3 dB por nível; nível 15 mudo).

`bs_audio_tests` **23/23**.

**Ainda pendente na fase B:**
- os dB por passo de TL e DIPAN deste jogo (os da AICA documentada até uma captura de áudio do Flycast);
- a unidade do tempo `t` (o jogo sempre passa 0);
- o `x` do tocar (sempre 0);
- a palavra +24 com o som tocando: a rota não teve efeitos no gameplay, porque a entrada do oráculo acaba em
  "2630 UP" e o personagem fica parado depois do E001. Uma rota de oráculo com ações no gameplay, convertida
  para passadas do pd pelos deslocamentos dos filmes (NEWGAME ≈ 4 106 passadas, E001 ≈ 1 931) ou com `@ancora`,
  dá os efeitos de jogo, o bit de estado e mais amostras.

## 13. Fase C: os efeitos direto do BS_SND.MLT (2026-10-20)

Os efeitos não precisam de conversão pelo Builder. O jogo instalado já tem `data\disc\BSDATA\BS_SND.MLT` (os
arquivos do disco do próprio usuário). O provedor nativo lê o SOSB e decodifica o ADPCM da AICA na hora. Os bytes
originais continuam a fonte, e os mods trocam por ID no `SoundCatalog` (`efeito/<banco>/<pedido>`).

**Formato** (`native/audio/sosb`, `tools/dev/n6_sosb.py`), conferido contra os registradores medidos no O6:
- **SMLT:** a tabela de unidades dá o endereço de cada unidade na RAM de som:
  SMPB 0x018000, SMSB 0x08E160, **SOSB#0 0x097A60**, **SOSB#1 0x1691E0**, SFPB, SFOB e SFPW (o DSP).
- **SOSB:** "SOSB", versão 1, tamanho, **n pedidos** (128 no banco 0 e 83 no banco 1: 211 efeitos; daí o
  `id/128`), seguido da tabela de deslocamentos.
- **SOSP** (0x38 bytes):
  - SA = byte 4 << 16 | u16 em +6 (relativo à unidade);
  - LSA em +8, LEA em +10;
  - OCT/FNS em +16;
  - byte 5 = indicadores.

**O par `(banco, pedido)` do jogo é `SOSB#banco`, descritor `pedido`. Provado:**
- ID 117 → SA 0x150D3C, LEA 0xE25B, OCT −2, FNS 833;
- ID 90 → SA 0x11FA24, LEA 0x3080, OCT −2, FNS 0.

Os dois iguais aos registradores no key-on.

**Durações conferidas:** o decodificado tem exatamente a duração do canal da AICA no oráculo, a 30 passadas/s:

| ID | Decodificado | Oráculo (key-on → key-off) |
|---|---|---|
| 90 | 12 416 amostras a 11 025 Hz = 1,126 s | 34 passadas = 1,13 s |
| 117 | 57 947 amostras a 19 994 Hz = 2,898 s | 87 passadas = 2,90 s |

O fim nativo (amostras/taxa no relógio lógico) cai na passada certa, que é o que a fase D exige do bit de estado.

**Pendente:**
- os indicadores: 1 em 204 entradas, 3 em 7. A hipótese é bit 0 = ADPCM e bit 1 = laço; os 2 medidos tinham 1.
  Um efeito sem o bit 0 é recusado. O laço só com o 3 está marcado como hipótese, até o key-on de um deles
  mostrar o LPCTL;
- as 2 entradas com LSA ≠ 0.

Testes (`bs_audio_tests` **25/25**):
- `audio_sosb_synthetic`: ida e volta do ADPCM, taxa, laço, corrupções recusadas;
- `audio_sosb_real_disc_matches_o6`: o arquivo real, com `BS_GAME_DATA`: 211 efeitos, os 2 medidos exatos, RMS
  5 274 e 5 976, 7 e 0 amostras saturadas.

## 14. Fase D: os efeitos do jogo pelo provedor nativo (2026-10-20) — primeiro áudio nativo no gameplay

**Fronteira:** as 5 entradas do SDK de som (`8C15E208` parar, `8C15E088` tocar, `8C15E19C` volume, `8C15E11C` pan,
`8C15DF44` consulta) entram em `[native] slots` (só na worktree n6). O C++ é regenerado com `--out generated`
próprio da worktree, sem tocar na árvore principal.
- 14 pontos de chamada, todos pelo slot; nenhuma chamada direta aos originais.
- O gerenciador do jogo (fila de 16, prioridade, 7 ativos, rodízio, canal avulso) continua o recompilado.

**Cola** (`transitional/sound_adapter`, no padrão do `video_adapter` do N5):
- o slot lê r4 (porta = byte 1 do handle) até r7 e chama o `SdAdapter`;
- a consulta grava os 28 bytes da saída na RAM do jogo;
- o **flag de inicialização é o do original**, lido da RAM (`8C1DDB90`);
- os efeitos vêm do `BS_SND.MLT` do disco do usuário (`SosbBanks`), decodificados uma vez por ID;
- a cada VBlank o mixer anda 1/59,94 s de tempo **lógico**, e a consulta só vê os fins até ali;
- saída XAudio2 em tempo real e nula no headless.

**Opção:** `--audio-efeitos nativo|legado`, com **padrão legado** até a promoção (fase H). Música e falas
continuam no legado (AICA emulada).

**Prova** (`tools/dev/n6_efeitos.ps1`: legado × nativo no mesmo roteiro por passo, relógio fixo, `--video legado`):

| Rota | Efeitos nativos | Quadros | Estado por passo |
|---|---|---|---|
| menu (`passo_newgame_area1`, 2 400 VBlanks) | 3 tocados, 21 consultas, 0 ausentes | **2 401 iguais** | **1 100 iguais** |
| **combate** (`passo_combate`, 26 342 VBlanks, 622 s) | **301 tocados, 301 paradas, 2 394 volume, 2 394 pan, 2 071 consultas**; 0 ausentes, 0 falhas | **26 343 iguais** | **12 987 iguais** |

**Correção (2026-10-20, seção 15):** esta tabela prova só que o provedor nativo **não perturba o gameplay**. O
legado não executa o driver de som (o `runtime/aica` só modela registradores; o ARM7 nunca rodou), então ele não é
referência de tempo nem de som. A afirmação anterior, "o fim nativo cai na mesma passada do original", estava
errada: a comparação com o jogo original é a do Flycast (seção 15).

**Para ouvir** (o headless não tem dispositivo): `BS_EFEITOS_WAV=<arquivo>` grava a mistura dos efeitos nativos.
Menu: 40,0 s de WAV, som a partir de 20,5 s, cerca de 4,7 s com som (os 3 efeitos somam 5,16 s).

**Pendente na fase D:** atualizado na seção 15.

**Próximo:**
- fase E: falas, o adaptador do player WAV da CRI, com os eventos de diálogo do `docs/LOCALIZACAO-PLANO.md`;
- fase F: decisão da música;
- fase G: DSP.

## 15. Fase D contra o jogo original (Flycast, 2026-10-20)

**Por que o Flycast:** o legado não executa o driver de som (seção 14, correção). O oráculo O6 (`n6_oraculo.py`) passou
a ter, para isto:
- `--trocar k:banco:pedido`: MEDIDA, só no Flycast, troca o som do k-ésimo `tocar` (para medir efeitos que a rota
  não toca);
- `@ancora-ram <end> <valor>` (`flycast_oracle.py`): alinha a rota ao primeiro passo em que a RAM tem o valor. Usada
  com a posição x do Eliot = 500,0 (`0x8C24D668`), o início do gameplay: passo 2253 no recompilado, 8312 no Flycast
  (o Flycast carrega mais devagar);
- direcional analógico na entrada (`ANALOG_*` → x1/y1 do PDS, como o decodificador do SDK);
- `--posicao N`, `--falas` (o MW WAV) e o tempo do **TMU0** do SH-4 em cada linha (`tmu=TCNT/prescaler`; P0/64 =
  781 250 Hz). Calibrado: o efeito 117 (amostra de 2,898 s) mede 2,903 s do key-on ao key-off; o 90 (1,126 s), 1,134 s.
  O contador `0x8C1DD5B4` (callback de VBlank `8C172602`) **não serve** de relógio: fica parado depois do boot.
- Resolução: a AICA é lida uma vez por passo de jogo, e no Flycast um passo dura de 2 a 6 VBlanks (carga lenta): os
  fins têm a resolução de um passo.

Rota `tools/dev/roteiros_n6/combate_flycast.txt` (gerada de `passo_combate.txt`). Comparação
`tools/dev/n6_compara_o6.py` (tocar, fins, estado +24 por porta).

**Achados e correções (divergências reais):**

| Achado | Evidência | Correção | Teste |
|---|---|---|---|
| **Nível base por efeito.** O TL com volume 0 é o byte 0x19 do descritor SOSP, não 30 fixo (TL = base − 2v). Base de 0 a 65 nos 211 efeitos (60 em 20, 52 em 30, 24 em 40…): o nativo errava de −11 a +13 dB conforme o efeito | `--trocar 0:11,0:67,0:37`: TL 40, 20, 45 = bytes `28 14 2D`; os IDs 117 e 90 (30) também batem | `SosbEffect::baseTl`, `SdSound::baseTl`, `sd_tl(v, base)` | `audio_sd_effect_base_level`, `audio_sosb_real_disc_matches_o6` |
| **Laço.** O bit 1 dos indicadores liga o LPCTL com LSA/LEA do descritor; o canal não desliga sozinho e a consulta devolve +24 = 1 até o parar. O nativo não pedia laço ao mixer (`PlayParams::loop`): os 7 efeitos tocavam uma vez e liberavam o canal cedo | key-on dos pedidos 11, 67, 37 com `loop=1` e LSA/LEA iguais ao descritor, sem key-off em 400 a 800 passos; 90 e 117 (indicador 1) com `loop=0` | `SdAdapter::play` liga `pp.loop` quando o som tem pontos de laço | `audio_sd_loop_until_stop` |
| **Fim lógico no mixer.** O `LogicalEnd` era conferido antes de consumir a amostra: um fim no último quadro do bloco chegava um render depois (o fim físico já era conferido depois) | teste da dublagem | conferido depois de consumir, como o físico | `audio_voice_longer_dub_*` |

**Comparação no gameplay (combate, depois da âncora; a rota no Flycast parou no passo 8908):**
- **107 `tocar` iguais** (passo, porta, banco, pedido) no nativo e no original: o gameplay está alinhado;
- **estado (+24 por porta, mudanças): igual**;
- fins, **em tempo (TMU)**, 53 efeitos depois da âncora (rota até o passo 9800 do Flycast):
  - 90, 117 e 121: o key-off do original cai no fim da amostra (±1 VBlank): igual ao nativo;
  - ~~o passo do Eliot (ID 32, 51 vezes): o original dá key-off entre 6 e 15 VBlanks depois do key-on, antes do fim
    da amostra (15,2) na maioria das vezes; o nativo toca a amostra inteira (16).~~ **Corrigido na fase G (seção 19):
    medida de amostragem; com o CA do canal certo, o original toca o passo até o fim (LP no LEA) e o driver dá key-off
    em seguida, como o nativo.** O key-off da AICA inicia a soltura
    do envelope (o som decai pela taxa RR, não para): é o **envelope da AICA** (bytes 0x14–0x2F do descritor: o tempo
    de "nota" e as taxas), ainda não modelado. Não muda o jogo (o +24 e a sequência dos `tocar` são iguais); muda o
    som. **PENDENTE (fase G):** modelar o envelope pelo descritor, medido contra o Flycast (o mesmo oráculo).

**Saída nativa (combate, 439,5 s, `BS_EFEITOS_WAV`):** pico −2,0 dBFS, **0 amostras saturadas**, até 3 efeitos ao mesmo
tempo.

**Para ouvir** (não há aprovação por ouvido ainda): os WAV gravados pelo `BS_EFEITOS_WAV` (menu e combate). O que
falta comparar de ouvido ou por captura do Flycast: o envelope da AICA (ataque/decaimento/soltura, bytes 0x1A–0x2F do
descritor, ainda não modelados), os dB absolutos por passo de TL e DIPAN e o DSP (fase G).

## 16. Fase E: as falas pelo provedor nativo (2026-10-20)

**Contrato (disassembly e oráculos):**
- Player do jogo `8C08B860(id)`: ignora o pedido se `S+56` (fala ativa; S = `0x8C24D2CC`) está ligado; nome em
  `*(0x8C157754 + 4·id)` (vazio = ignora); `8C02AE70(−48, 800)` (abaixa a música); `PlyCreateWav 8C158FC4`;
  start (+0x18) com o nome; `S+56 = 1`, `S+58 = 15`.
- Atualização por quadro `8C08B900`: estado (+0x20); 2 → `S+58 = −1`; senão conta `S+58` até 0; 3 ou 4 →
  `8C08B980`: stop (+0x1C), destroy (+0x14), `S+56 = 0`, `8C02AE70(0, 800)` (devolve a música). `S+58 != 0` é lido
  pelo gameplay em `8C052BE8` e `8C083746`.
- MW WAV: pool de 32 × 56 B em `0x8C2D7D38`, tabela `8C1DD248` (+0x0C/+0x10 servidor por handle, +0x14 destroy,
  +0x18 start, +0x1C stop, +0x20 estado, +0x24…+0x34 sem uso pelo jogo); cada método valida o handle em `8C159D92`;
  nenhum laço do original percorre o pool fora disso. `8C08B8B8` (modo 0/1 pelo +0x28) não tem chamador.
- **Mapa** (`tools/dev/n6_vozes.py`): 239 IDs, 6 vazios, 233 nomes = os 233 WAV do disco; ADPCM da AICA (tag 0,
  4 bits), 22 050 Hz, 232 mono e 1 estéreo, 1 421 s. Os `*ENV.WAV` (24) são ambientes tocados pelo mesmo player.
- **Original (Flycast, `--falas`, tempo pelo TMU):** pedido → start → estado **1 por 3 passos de jogo** (0,251 s
  e 0,123 s, conforme a duração do passo: o preparo é por passo, não por tempo) → 2 → **3 no fim** → stop + destroy;
  o canal da AICA da fala: TL 0, DISDL 15, pan 0, PCMS 3, 22 050 Hz (o nativo toca com ganho 1 e na taxa do WAV).
  Na rota de combate: `E012AENV` (ambiente), `E012BENV` (ambiente) e, depois do fim do `E012BENV`, a fala **`E002A`
  (ID 4)**. Do 2 ao 3: `E012AENV` 38,80 s (arquivo 38,49), `E012BENV` 12,28 s (12,03): **o original avisa o fim
  0,25–0,31 s depois do fim nominal do áudio** (provável esvaziamento do buffer do MW).
- **Legado:** o mesmo início (3 consultas em 1), mas **nunca chega ao 3** (sem a AICA, a posição não anda):
  `E012BENV` (12,03 s) ficou em 2 por 173 s. **O legado não é referência para o fim das falas**, e um nativo certo
  diverge dele no primeiro fim natural.

**Implementação:**
- `native/audio/voice_player` (portável): estados 1/2/3/4; a consulta **só lê** (como `8C159340`); o preparo anda por
  passo de jogo (`game_step`, `prepSteps` = 3, medido); o áudio só começa quando o preparo acaba; fim lógico × fim
  físico; `TailPolicy` (PlayOut padrão, Cut); pausa e troca de cena do host; eventos `Started`, `LogicalEnd`,
  `PhysicalEnd`, `Interrupted`, `Missing`, `Paused`, `Resumed` com o quadro lógico.
- `transitional/voice_mw_glue` (portável, RAM sintética nos testes) e os slots em `transitional/sound_adapter` (as 12
  entradas em `config [native] slots`; o mesmo mixer e a mesma saída dos efeitos). Outro handle segue para o original;
  servidor e métodos sem uso contados ("inesperadas", tem de ficar 0).
- Resolução: `mods\audio\<idioma>\voz\<nome>.(wav|flac)` (dublagem, `--dublagem`) ou o WAV do disco. O jogo vê sempre
  a duração original: a dublagem mais longa avisa o fim lógico ali e continua (PlayOut); a mais curta é completada com
  silêncio até o fim original.
- FLAC: `dr_flac` (domínio público/MIT-0) em `third_party/dr_libs`, commit fixo `dfe8377` (2026-08-31, com as
  correções de limites de 2026-04), só em `native/audio/sound_flac.cpp`, entrada só da memória, limites de tamanho
  (256 MB) e duração (30 min) antes de decodificar; no kit do Builder e no projeto exportado (`kKitEntries`).
- **OGG/Vorbis: não integrado.** O `stb_vorbis` (preferência inicial) está em v1.22 (último commit em 2021-07) e tem
  os CVE-2023-45675, 45676, 45677, 45681 e 45682 (GHSL-2023: escritas e leituras fora dos limites com arquivo forjado)
  sem correção publicada; arquivos de mods são entrada não confiável. Opções para o mantenedor: `libvorbis`+`libogg`
  (Xiph, BSD-3, mantidas), `stb_vorbis` com as correções aplicadas aqui (manter um fork) ou deixar OGG de fora.
- Legendas: `native/localization/dialogue_bridge` leva os eventos ao `SubtitleService` no tempo lógico do mixer; o
  `GameHost` liga com `--audio-falas nativo --legendas <idioma>` e registra o texto ativo (`[legenda] ...`); nada é
  desenhado (N7).
- `--audio-falas nativo|legado` (padrão **legado**), `--dublagem <idioma>`.
- Fim: o estado 3 chega **2 passos de jogo depois do fim do áudio** (`endSteps`, medido no Flycast: 1,8, 2,2 e 2,3
  passos); os passos concluídos são contados antes dos eventos de cada VBlank (um fim no mesmo VBlank de um passo não
  gasta esse passo).

**Prova no jogo** (`tools/dev/n6_falas.ps1`, rota de combate, 8 000 VBlanks, legado × nativo): **11/11**.
- Falas nativas: `E012AENV` (interrompida pela cena no VBlank 4491, como no legado), `E012BENV` (fim natural),
  **`E002A`** (a fala de diálogo que o legado nunca toca, porque a anterior nunca acaba); em 12 000 VBlanks também
  `E000ENV` (2×) e `E002B`. Todas com preparo de 3 passos e estado 3 dois passos depois do áudio.
- Quadros iguais ao legado; estado igual ao legado até o passo 2514, o primeiro depois do estado 3 nativo do
  `E012BENV` (a divergência legítima). 0 chamadas inesperadas, 0 de outro handle, 0 falhas.
- Efeitos e falas nativos juntos (o mesmo mixer): estado e quadros iguais aos das falas nativas sozinhas.
- **Dublagem de teste** de 30 s para `E002A` (20,88 s), `--dublagem pt-BR`: estado **igual** ao da execução sem
  dublagem (5 821 passos); o estado 3 no mesmo passo (4045); a cauda soa até o fim físico (passo 4316) e depois
  silêncio.
- Legenda de teste (pacote em `mods\`, `--legendas pt-BR`): `[legenda] voz/e012aenv` ativa por 5 s no log.
- Áudio: som só nos intervalos das falas (silêncio no preparo e depois da interrupção); o canal da fala no original
  tem TL 0 e DISDL 15, então 406 amostras no limite vêm do próprio ADPCM decodificado (o decodificador da AICA satura
  igual), não da mistura.

**Revisão Codex** (`reviews/codex-20261020-170850.md`):
| Achado | Decisão |
|---|---|
| MÉDIA: a consulta de estado mudava o estado (o original só lê) | **aceito**: `status()` só lê; o preparo anda por passo de jogo (o Flycast mostrou que é por passo, não por tempo); teste `audio_voice_default_prep_steps` com consultas repetidas |
| MÉDIA: FLAC pequeno podia declarar ~1,38 GB de PCM | **aceito**: orçamento de PCM (64 M amostras) antes de alocar e `bad_alloc` tratado; teste `audio_flac_pcm_budget` |
| MÉDIA: "os executáveis rodam só o primeiro teste" | **rejeitado**: as saídas listam todos os testes (39 e 7 naquele momento); provável restrição do sandbox. Mesmo assim, o `main` dos unitários imprime "N testes, F falhas" no fim |
| BAIXA: pausa e cena sem caminho de produção | **aceito em parte**: `transitional::sound::pause/scene_changed` para o host (N7). O jogo original nunca pausa a fala (o +0x28 não tem chamador) e a troca de cena dele chega como stop: não há fonte comprovada no jogo, então nada do jogo as chama |

**Testes:** `bs_audio_tests` **40/40** e `bs_localization_tests` **7/7**, normal e ASan; `n6_falas.ps1` 11/11;
`n6_efeitos.ps1` (menu) 6/6 depois das mudanças; `native/` limpo (N0, 63 arquivos).

**Pendente na fase E:**
- **`E084.WAV` (ID 99) é ADPCM estéreo**, o único dos 233 (achado no inventário das legendas, `docs/LEGENDAS-EN-PLANO.md`):
  o `parse_wav` só aceita ADPCM mono, então no nativo essa fala viraria "ausente" (o jogo receberia o estado 4 e a
  encerraria). Medir a disposição dos canais no arquivo (o key-on das duas vozes da AICA no Flycast) antes de tocá-la;
- o contexto (cena) de cada fala: quais IDs cada chamador (`8C02E0C0`, `8C035E5A`) usa em cada cena;
- a interação com a música é a lógica original (abaixa −48 e devolve 0 em 800): o efeito audível é da fase F;
- a promoção (`--audio-falas nativo` como padrão) fica para a fase H, com música e DSP.

## 17. Estado do N6 por frente (2026-10-20)

| Frente | Estado | Prova | Pendente |
|---|---|---|---|
| Efeitos nativos | implementados; `--audio-efeitos nativo` (padrão legado) | combate × Flycast: 107 `tocar` e o +24 iguais; nível base e laço corrigidos pelo oráculo | envelope da AICA (o key-off antecipado do passo 32), dB absolutos; outras rotas |
| Falas nativas | implementadas; `--audio-falas nativo` (padrão legado) | `n6_falas.ps1` 11/11; contrato medido no Flycast (3 passos de preparo, fim 2 passos depois) | contexto por cena |
| Música nativa | não iniciada (fase F) | — | decisão: sequenciador nativo × render pelo Builder |
| DSP e laços | laços dos efeitos: feitos (LPCTL medido); DSP: não iniciado (fase G) | `audio_sd_loop_until_stop`, `--trocar` | envelope da AICA; reverb nativo medido contra o Flycast |
| Legendas | contratos e ponte prontos; eventos das falas ligados no `GameHost` | `loc_dialogue_bridge_voice_player`, `[legenda]` no log | **desenho só no N7** (não feito) |
| Mods de áudio | dublagem WAV/FLAC por idioma, duração original preservada | dublagem de 30 s com estado igual; `audio_flac_*` | OGG/Vorbis: decisão do mantenedor |
| Dependências legadas | música, filmes do legado, AICA emulada continuam; o legado é o padrão dos dois provedores do N6 | — | sai no N8 |

## 18. OGG/Vorbis e fase F: o caminho da música (2026-10-20, em andamento)

### 18.1 OGG/Vorbis (decisão do mantenedor: libvorbis + libogg)

- **libogg 1.3.6** (2025-06-16) e **libvorbis 1.3.7** (2020-07-04, a última publicada), tarballs oficiais conferidos
  com os `SHA256SUMS` da Xiph; só a decodificação (sem `vorbisenc`), em `third_party/xiph` com os `COPYING` (BSD-3)
  e o único arquivo escrito aqui (`config_types.h`, tipos de `<stdint.h>`). Compilado pelo nosso CMake (`bs_xiph_vorbis`),
  no kit do Builder e no projeto exportado (sem internet); licenças em `licencas\` da distribuição.
- Atrás da mesma interface: `parse_ogg` (`native/audio/sound_vorbis.cpp`), só da memória, um fluxo lógico, canais
  1–2, taxa 8–192 kHz, duração e orçamento de PCM conferidos antes de decodificar, buracos do fluxo limitados.
- Testes (`test_audio_ogg.cpp`, arquivo próprio de 4 KB embutido): decodificação (duração, nível, ~440 Hz), entradas
  ruins e **300 mutações** (bytes trocados, cortes, repetições) sem queda; com o ASan, sem acesso fora dos limites.
  `bs_audio_tests` **45/45** (normal e ASan). WAV e FLAC inalterados.

### 18.2 Fase F: o que o jogo e o driver fazem com a música (análise estática, 2026-10-20)

**Arquivos:** 11 pares `M*.MSB` (sequências `SMSB`/`SMSD`) e `M*.MPB` (programas e amostras `SMPB`) e o driver
`BSDATA\MANATEE.DRV` (ARM7, 33 KB, **carregado sem o cabeçalho de 0x20 bytes no endereço 0 da RAM de som**: as tabelas
de salto só fazem sentido nessa base). Nenhum ADX: a música é toda sequenciada.

**Gerenciador do jogo** (fica o recompilado): `8C02AE70(v, t)` é o **fade de volume** da música (limita v a no máximo
−16; o "0" que as falas pedem vira −16, o volume normal; −47 é o abaixado) e manda o comando 0x05 com o tempo. A família
do gerenciador (`8C02AE70`–`8C02B0xx`) usa 6 entradas do SDK:

| Entrada | Comando ao driver | Argumentos | Tratador no driver |
|---|---|---|---|
| `8C15DBAC` | **0x01** tocar sequência | (handle, banco, sequência, x·8) | `0x4B3C`: banco pela tabela em 0x14000 (8 B por banco), confere `SMSB`, versão 1 e o rodapé `ENDB`; prepara as trilhas (`0x7404`) |
| `8C15DD40` | **0x02** parar | (handle) | `0x4C20` |
| `8C15DCC0` | **0x05** volume com tempo (fade) | (handle, v, t) | `0x4D90` |
| `8C15DC40` | **0x06** pan com tempo | (handle, v, t) | `0x4E18` |
| `8C15D768` | **0x35** global | 2 argumentos | `0x585C` (a medir) |
| `8C15D6A0` | **0x36** global | 2 argumentos | `0x5884` (a medir) |

A tabela de comandos do driver começa em `0x49E4` (índice = código; 0x11/0x12/0x15/0x16 são os efeitos da fase D).

**Sequências (`SMSD`)**, pelo próprio driver (laço das 8 trilhas em `0x4310`, estado de 0x40 bytes por trilha em
0x10000; despacho em `0x4390`): nota (`s < 0x80`: canal = `s & 0xF`, tecla, velocidade, duração em `(s>>5)+1` bytes,
delta de 1 ou 2 bytes conforme `s & 0x10`); controle `0xA0–0xBF`; programa/pressão `0xC0–0xDF`; pitch `0xE0–0xEF`;
`0x81` referência (toca N eventos de outro ponto e volta); `0x82` laço; `0x83` fim; `0x84` tempo; o resto é ignorado
pelo driver. `tools/dev/n6_smsd.py`: **as 11 sequências são lidas até o fim exato** (0x83), sem evento desconhecido,
sobrando só o alinhamento (< 40 bytes); cada uma tem um par de laço, referências, tempo, programas e de 580 a 7 073 notas.

**Bancos (`SMPB`)** (`tools/dev/n6_smpb.py`): 4 tabelas (programas; 2 curvas de velocidade de 128 bytes; 2 pequenas);
programa = até 2 grupos que **tocam juntos** (o grupo 1 repete as amostras do 0 com outro ajuste fino ou TL: camadas);
camada de 0x30 bytes = os campos do descritor SOSP dos efeitos (amostra relativa ao banco, laço, DISDL, TL base,
envelope) + faixa de teclas, tecla base e ajuste fino. **Os 11 bancos: 0 problemas de estrutura** (amostras e laços
dentro do arquivo; todas ADPCM, indicadores 1 e 3).

**Ligação com a fase G:** o que falta para soar como o original é o mesmo nos efeitos e na música: o **envelope da
AICA** (as 5 palavras `F81F…` de cada camada/descritor, o key-off pelo "gate" das notas e a soltura). O modelo nativo
do envelope serve às duas fases.

**Próximo (fase F):**
1. o oráculo `--musica` (rodando): os comandos de música no jogo (quais sequências e bancos, fades, pan, 0x35/0x36) e
   os key-on da AICA de cada nota (amostra, tom, TL, pan): a referência nota a nota;
2. o sequenciador e o sampler nativos (C++) sobre o mixer, com o envelope da AICA; comparação nota a nota com o oráculo;
3. os 6 slots, `--audio-musica nativo|legado` (padrão legado) e a substituição por mods (`musica/<nome>` em WAV/FLAC/OGG,
   com metadados de laço; a música de mod não muda o estado do jogo).

### 18.3 Fase F: o driver lido no código e o primeiro som nativo (2026-10-20)

Tudo abaixo vem do código ARM7 do `MANATEE.DRV` (desmontagem com Capstone 5.0.7, fora do repositório) e foi conferido nos
dados do disco; nenhum valor foi ajustado de ouvido.

**Relógio da música.** O Timer A da AICA (0x802890) recebe 0xD4 com prescaler 0: uma interrupção a cada 44 amostras. O
tratador (`0x2A8`) pula 1 em 441 (44 100 / 44 × 440/441 = exatamente **1 000 Hz**, sem deriva) e a cada 4 ticks liga a
bandeira 0x22C; o laço principal (`0x194`/`0x1B8`) então chama `0x858`, `0x70A4`, **o sequenciador `0x4210` (250 Hz)** e as
rotinas de voz `0x6780`–`0x6A5C`. Não depende do VBlank do SH-4: a música anda pelo relógio de amostras da AICA, então
pausa/lentidão do jogo não a desafinam; o nativo usa o mesmo relógio (176,4 amostras por chamada, sem acumular erro).

**Tempo.** Por trilha (0x10000 + 0x40·n): `unidade = (cabeçalho1 × tempo) >> 4` (`0x7404`; cabeçalho1 = 136 nas 11);
a conta da trilha perde o passo (0x4000 normal; a porta muda pelo comando de velocidade, `0x6800`) a cada chamada; um
evento soma `delta × unidade` (`0x4874`); a duração da nota é `gate × unidade + conta atual` (`0x4430`). A primeira
chamada já desconta (`0x4354`): os testes `audio_manatee_seq_integer_timing` fixam as chamadas exatas (1, 25, gap 49).

**0x81 (referência), resolvido.** Base = início do `SMSD` (1 010/1 010 alvos caem em eventos); a contagem é **N eventos**:
o primeiro é lido direto (`0x4650→0x4380`) sem passar pela conferência do contador, que devolve ao chegar a 1. O teste
sintético separa N de N−1 (notas 60–63, referência a 12 com 3 → 60 61 62 63 60 61 62 70). Nos dados: contagens ≥ 3, sem 0.
**0x82 (laço):** o primeiro guarda o ponteiro do byte de contagem (0 = infinito, 0x80); contagem c toca o corpo c+1 vezes.

**Tom (`0x14B0`).** `acc` = ajuste fino da camada (byte 0x27 com sinal, 1/256 de semitom) [+ bend]; índice =
`(acc>>8) + tecla + 0x60 − tecla base` (8 bits); a tabela 0x3504 dá oitava (−8..+7) e semitom; FNS pela tabela 0x35E8
(1 536 entradas) = **`floor(1024·(2^(j/1536) − 1))`, igual nas 1 536** (round acertaria 764); taxa = 44 100·2^OCT·(1+FNS/1024).

**Nível (`0x0FF4`–`0x107C`, `0x1B54`–`0x1C3C`).** Canal: `c = ((T[cc7] + 1)·(T[cc11] + 1) >> 7) − 1`, com T a curva de
volume do driver (`0x3EC8`, 128 bytes, crescente; **lida do `MANATEE.DRV` do usuário**, não copiada no código, com a
versão conferida pela tabela de FNS). Nota: `nível = ((curva[c'][vel] + 1)·(c + 1)·(256 − TL da camada) − 1) >> 14`,
curva c' pelo byte 0x2A da camada; o volume da porta (comando 0x05, guardado como v + 128) soma:
`TL = 255 − limite(nível + v)`. O "0" do gerenciador é −16. Um controle 7/11 recalcula o nível das vozes do canal.

**Camadas (`0x0B18`–`0x11FC`).** Até 4 ponteiros de grupo por programa (nos bancos do disco só 2); grupo com o bit 7 é
pulado; em cada grupo, a 1ª camada com a tecla em [0x24, 0x25] e a velocidade em [0x2B, 0x2C] que conseguir voz.
Key-off (`0x0A38`): KYONB = 0 com KYONEX, a soltura é o envelope da AICA (fase G).

**Qual música e onde.** A tabela de nomes em `0x8C09EC28` (pares mpb/msb, 12 entradas: M2A, M2B, M2C, M3, M4, M4, M5, M6,
M7, M9, M10, M99), usada por `8C02A5E0`/`8C02A660(índice)` (carregar [e tocar]); índice carregado em `0x8C1F4C52`
(0xFF = nenhum), tocando em `0x8C1F4C79`. Nas rotas por passo (nativo, `BS_RAM_WATCH`): **a primeira música é a M2A
(índice 0) no passo 7 337 de `passo_combate`, na troca de sala** (posição vai a (125, 0, −400)); a área inicial não tem
música (o oráculo `--musica` do Flycast até o passo 10 600 confirma: só fades e abaixamentos −48/−16 nas falas).

**Nativo (`native/audio/manatee_seq` e `manatee_synth`).** Sequenciador (o algoritmo acima) e sintetizador (camadas,
ADPCM da AICA decodificado e em cache por amostra, tom e nível do driver, pedal 64, pan 10) sobre o mixer, no barramento
Música. Ferramenta de desenvolvimento `bs-musica <BSDATA> <nome> <s> <wav> [eventos.tsv]` (fora do kit; WAV e eventos
ficam em `%LOCALAPPDATA%`). **Primeira música nativa: M2A**, 150 s, 3 621 notas, 6 130 vozes, sem saturação.

### 18.4 Fase F contra o Dreamcast: a M2A nota a nota (Flycast, 2026-10-20)

**Oráculo** (`n6_oraculo.py --musica --ca`, LOAD do cartão `g1_entrada` pela rota `passo_load_slot1`, 4 200 passos, sem a
rota de ~50 min): no passo 1 757 (entrada da RCG02) o jogo carrega `M2A.mpb`/`M2A.msb` e manda volume −113, pan 0, parar,
volume −16, **tocar porta 0 banco 0 sequência 0** e o fade para −16 em t = 2 000. A M2A é a música da RCG02 depois do LOAD
(o `--injetar-musica` do oráculo, feito para provocar a música, não teve efeito: o índice já era 0). Os key-on da AICA
(amostra, OCT, FNS, TL, `ca`) foram lidos a cada passo; o banco fica em 0x18000 da RAM de som.

**Comparação** (`tools/dev/n6_musica_compara.py <oráculo> <vozes nativas>`). Desde a revisão Codex de 2026-10-20 o
pareamento é **um a um**: cada key-on do Dreamcast consome no máximo uma voz nativa da mesma assinatura (amostra, OCT, FNS,
TL, DIPAN, DISDL), em ordem de tempo, numa janela de 50 ms depois do deslocamento local por janela de 10 s; o relatório
dá os sem par dos dois lados e as inversões de ordem. (A primeira versão contava pertinência a um conjunto, o que não
prova multiplicidade nem ordem; os números "5 766/5 766" dela foram substituídos.)

| Medida (M2A, 150 s, `bs-musica`) | Resultado |
|---|---|
| key-on do Dreamcast pareados um a um com voz nativa de **mesma amostra, OCT, FNS, TL, DIPAN e DISDL** | **5 590/5 766 (97 %)** em 50 ms; **5 757/5 766 (99,8 %)** em 100 ms |
| inversões de ordem entre os pares | 1 (50 ms); 7 (100 ms) |
| onde ficam os sem par em 50 ms | passos 3 700–4 100 do oráculo: o trecho denso, onde o atraso do Dreamcast muda dentro da janela de 10 s |
| TL do key-on nas 10 combinações mais frequentes (teste `audio_manatee_synth_level_vs_oracle`) | 10/10 |
| tempo dos key-on, 30–90 s da música (erro mediano por janela de 10 s) | −24,7 → −21,6 ms: **sem deriva mensurável** (< 0,01 %) |
| tempo nos trechos densos (10–30 s, 90–150 s) | o Dreamcast atrasa até ~0,1 s em relação ao nativo |

O erro absoluto (~34 ms) é o da leitura uma vez por passo; os 2 key-on sem par (passo 3 173, um par estéreo) têm TL de um
recálculo por controle 7 entre o key-on e a leitura.

**Pan e envio, do driver:** no início de cada `tocar` (`0x2758`) o canal volta a cc7 = 100, **nível do canal = 100
direto** (a curva só entra no 1º controle 7/11), cc11 = 127 e pan = 0xC0 (bit 7 = sem controle 10). No key-on
(`0x1080`–`0x111C`): sem controle 10, a posição vem do DIPAN da camada (byte 0x12; as camadas `sdl14` das 11 músicas são
pares estéreo 31/15); `x = (limite(posição + pan da porta) >> 2) − 16`, DIPAN = x ou `0x10 | ~x`; DISDL = nibble do byte
0x13 + o da porta (0). No mixer, a mesma curva de DIPAN dos efeitos (`dipan_pan`, agora compartilhada).

**Hipótese para o atraso nos trechos densos: ticks perdidos pelo ARM7** (o mecanismo existe no código; a perda não foi
medida diretamente): o tratador de 1 kHz (`0x2A8`) só liga
**bandeiras de um byte** (0xFF) e a cada 4 ticks a do sequenciador (`0x22C`), além de contar os ticks reais em 0x13418; o
laço principal (`0x194`) esvazia a fila de saída 4 eventos por volta (`0x9A4` → `0x1558`, anel de 1 024 palavras em
0xB000) e roda o sequenciador quando vê a bandeira. Se uma volta demora mais de 4 ms (key-on em rajada, com leituras e
escritas nos registradores da AICA), o tick seguinte **se perderia** e a música atrasaria 4 ms. Isso casa com o perfil
medido (atraso só onde há muitas notas, nenhum entre 30 e 90 s), mas falta a prova direta: ler no oráculo o contador de
ticks reais (0x13418) junto com a posição das trilhas. Dependeria da velocidade do ARM7 (o modelo de ciclos do Flycast
não é referência de hardware); o nativo toca no tempo nominal, o de um ARM7 sem carga, **sem compensação**. O sequenciador do driver só devolve antes do fim (0x20, retomando na
mesma trilha por `[0x10202]`/`[0x10204]`) com o anel cheio, o que não ocorre nas músicas.

**Notas sem camada (reais):** M2B tem 6 notas (teclas 98 e 100) no programa 3, que cobre só 0–34 e 59–82; M3 tem 1 (tecla
96, programa 2, 0–82). O driver também não as toca (`0x0B78`); o teste das 11 músicas espera exatamente esses números.

**Testes:** ver 18.5 (61/61 depois da integração).

**Provisório (fase G):** o envelope (ataque/decaimento/soltura das 5 palavras da camada e os ajustes por canal nos
bytes 0x32–0x35; hoje soltura de ~60 ms no key-off), as curvas em dB da AICA (TL 0,375 dB, DIPAN/DISDL 3 dB por passo:
documentadas, não medidas em áudio) e o pitch bend: só a M2C tem (3 eventos no canal 0); o caminho do driver está lido
(`0xE0` → `0x13E4`: bend = (valor − 64)·128, por voz `(bend · faixa) >> 5` em 1/256 de semitom), mas a origem da faixa
(bytes +0x0A/+0x0B da voz) não está provada; fica para a validação da M2C (achado MÉDIA do Codex, aceito e adiado).

**Substituição por mods (plano, sem ferramentas do N10).** A música de mod entra no lugar da sequência inteira, não das
amostras: `mods\<pacote>\musica\<NOME>.{wav,flac,ogg}` (NOME = M2A … M99, o nome da tabela 0x8C09EC28), lida pela
mesma `load_sound_file` dos efeitos e das falas (WAV, FLAC e OGG já validados). Laço por metadados: `smpl` no WAV,
`LOOPSTART`/`LOOPLENGTH` (ou `LOOPEND`) nos comentários Vorbis/FLAC, em amostras; sem laço, toca uma vez e a porta fica
livre como no fim da sequência original (0x83). Os comandos do gerenciador continuam valendo para a faixa: volume com
fade (0x05, na mesma lei de TL), pan (0x06), parar (0x02); o estado do jogo (bytes 0x8C1F4C52/0x8C1F4C79, consulta do
driver) não muda com a substituição. Amostras individuais do banco (por programa/camada) ficam para o N10.

### 18.5 Fase F no jogo: `--audio-musica nativo` e a M2A no gameplay (2026-10-20)

**Contrato do SDK (lido no binário e no driver).** O gerenciador de música do jogo (`8C02AD80` tocar, `8C02AE20` parar
com fade, `8C02AE70` fade) e o carregador (`8C02A4FC`: abre `M*.mpb`/`M*.msb`, lê para um buffer, cria um bloco de memória
do SDK com `8C15D844`/`8C15D980` — **+8 dados, +12 tamanho** — e chama o download `8C15D534(bloco, tipo, slot)`) continuam
os recompilados. As entradas do SDK só montam um pacote de 20 bytes e o copiam para uma **fila do lado SH-4**
(`8C17D334`, entradas de 28 bytes); o ARM7 recebe o lote e o executa **inteiro no tick de 250 Hz, antes dos faders**
(`0x48E8`, 32 comandos de 16 bytes em 0x13200, chamado no ramo do tick em `0x1E0`). Pacotes:

| Entrada | Comando | Pacote / efeito no driver |
|---|---|---|
| `8C15D534(bloco, tipo, slot)` | download | SMPB e SMSB vão para os slots (0x14080/0x14000, 16 de 8 bytes); o `BS_SND.MLT` (unidades baixadas por `8C15DDA4`) também traz um SMPB e um SMSB para o slot 0 no boot. Outros tipos (SOSB, SFPB, SFOB) ficam com o original |
| `8C15DBAC(h, slot, seq, x)` | 0x01 tocar | `[2]` porta = byte 1 do handle, `[3]` slot SMSB, `[4]` sequência, `[5]` x·8; `0x4B3C` confere SMSB/versão 1/`ENDB` (erros 2/8/4 só na porta); prepara trilhas e os 16 canais (`0x2758`); **não mexe** em volume/pan nem desliga vozes |
| `8C15DD40(h)` | 0x02 parar | `0x4C20`: KYONB = 0 em todas as vozes da porta (a soltura é do envelope) e limpa as trilhas |
| `8C15DCC0(h, v, t)` / `8C15DC40` | 0x05 volume / 0x06 pan | byte = v + 0x80, t em 16 bits; fade abaixo |
| `8C15D768` / `8C15D6A0` | 0x35 / 0x36 | sem chamada nas rotas medidas; o nativo repassa ao original e conta |

Conferências do SDK iguais às dos efeitos: flag `8C1DDB90` = 0 → `0x0D000001`; handle nulo → `0x07000103`; x do tocar
fora de 0..15 → `0x03000001`; bloco sem dados/tamanho ≤ 0 → `0x7FFFFFFF`.

**Fade (o algoritmo do driver, sem fórmula inventada).** `0x4D90`/`0x4E18`: alvo = byte; t = 0 → período 0 (o valor vai
ao alvo no tick seguinte); senão período = t >> 3 e passo = |atual − alvo|, divididos juntos por 2 até passo ≤ 1 ou
período ≤ 1 (período guardado em 8 bits). `0x6640`/`0x66E0` a cada tick: espera o período, depois anda `passo` em direção
ao alvo sem passar. Um comando novo recomeça do valor **atual** (substitui o fade em curso). Unidade: tick de 4 ms; ex.:
−113 → −16 com t = 2 000 → período 3, passo 1 → 97 × 4 ticks = 1,552 s; −16 → −48 com t = 800 → 128 ticks; −16 → −47 com
t = 2 000 → período 15, passo 1 → 1,98 s. O tocar/parar não mexem nos faders. Teste `audio_manatee_fader_driver_algorithm`.

**Controles que faltavam (lidos na tabela `0x1834`, índice = número):** o banco do canal é o **controle 32** (`0x1ED0`;
o 0 é ignorado, `0x1A34`); o **121** (`0x2758`) reseta os 16 canais da porta e solta o pedal; o 64 confirma (valor & 0x40).

**Nativo.** `native/audio/manatee_player` (slots, 8 portas com sequenciador e sintetizador, faders) e
`transitional/sound_adapter` (os 7 slots, `--audio-musica nativo|legado`, padrão **legado**): o tick de 250 Hz anda no
relógio lógico do mixer (o k-ésimo em k · 176,4 quadros) e a mistura é renderizada em trechos que param em cada tick. O
envelope saiu do sintetizador para `native/audio/envelope.h` (compartilhado; fase G completa). Diagnósticos:
`BS_EFEITOS_LOG` (comandos `mus-*` no vocabulário do oráculo), `BS_EFEITOS_WAV` (a mistura), `BS_MUSICA_VOZES` (as vozes
no formato do `bs-musica`, para `n6_musica_compara.py`). Os 7 slots entram no `[native] slots` e o C++ foi regerado: só
mudaram os slots e as 5 chamadas (`8C02A57E`, `8C15DE6E`, `8C02ADD2`, `8C02A0A0`, ...) de `BSG_FN` para `BSG_SLOT`.

**A M2A no gameplay** (rotas por passo, headless, `--video legado`; nada do `dist\` alterado):

| Medida | Resultado |
|---|---|
| comandos vindos do jogo recompilado (`passo_combate`, passo 7 337, troca de sala) | `mus-carga` SMPB/SMSB slot 0 → vol −113 t0 → pan 0 → parar → vol −16 t0 → **tocar porta 0 banco 0 seq 0** → vol −16 t2000 (a mesma ordem do original no LOAD); antes, vol −113 t=133 e −16 t=800 da troca de sala |
| estado do jogo com `--audio-musica nativo` × legado (`passo_combate`, hash da região de variáveis, vida, posição) | **igual nos 8 816 passos** (1 479 depois do início da música) |
| vozes da M2A no jogo × Dreamcast (um a um, amostra/OCT/FNS/TL/DIPAN/DISDL, 50 ms) | **2 082/2 094 (99,4 %)**, 0 inversões; os 12 sem par estão no fade de entrada (~1,5 s) |
| mistura (`BS_EFEITOS_WAV`) | música a partir de 250,5 s; −22 a −34 dBFS RMS; 0 amostras saturadas |
| fades do gerenciador no gameplay (`passo_heliporto`, ~190 s de M2A) | −47 em t = 2 000: −10 dB em ~2 s; −16: volta +11 dB em ~2 s; −113 em t = 4 (saída da área): queda imediata; 7 491 vozes, 0 recusadas, 0 sem camada |

**Mesma rota que o oráculo (2026-10-20):** o save do Flycast (`g1_entrada.bin`, extraído com `n4_vmu.py extrai` e
importado com `--saves-importar`) carregado pela `passo_load_slot1` no jogo com música e falas nativas
(`BS_MUSICA_VOZES`): os comandos de música são **os mesmos do original, na mesma ordem e com os mesmos valores** (−16/−47/−16
no boot; −47 no passo 1 027 [Flycast 1 026], −16 no 1 200 [1 199], −47 no 1 351 [1 350], −16 no 1 706 [1 706]; a carga da
sala no 1 754 [1 757] com vol −113 t0, pan 0, parar, vol −16 t0, tocar, vol −16 t2000). A porta já está em −16 quando a
M2A começa (o fade do passo 1 706 acabou): **sem fade de entrada, como no original**. Vozes da M2A × Flycast, um a um com
amostra, OCT, FNS, TL, DIPAN e DISDL: **3 133/3 151 (99,4 %)**, 0 inversões; dos 18 sem par, 11 são a primeira leitura
do oráculo, que só acontece **736 ms depois do tocar** (o passo 1 757 inclui a carga da sala: as notas já em curso
aparecem juntas, com a posição e o TL da leitura), 6 são o par estéreo do recálculo por controle 7 (passo ~3 150) e 1
isolado. O fade de entrada da M2A depois da troca de sala (rota de combate) segue sem comparação na mesma rota.

**Revisão Codex da integração** (`reviews/codex-20261020-220545.md`, 4 achados, todos aceitos):
- [MÉDIA] comandos aplicados a ticks anteriores à chamada no mesmo quadro: **corrigido**. As operações de música entram
  numa fila por ordem e são entregues no VBlank seguinte, depois de renderizados os ticks até ele (no original o pacote
  vai para a fila do SH-4 e o ARM7 só o executa no tick depois da entrega). Estado do jogo e contagens conferidos de novo
  na rota de combate depois da correção: estado igual ao legado e à execução anterior nos 8 816 passos, VBlanks iguais aos
da execução anterior, e as vozes da M2A no jogo continuam 2 082/2 094 um a um.
- [MÉDIA] ids de nota reaproveitados entre um tocar e outro: **corrigido** (numeração contínua por porta; teste
  `audio_manatee_player_note_ids_across_plays`).
- [MÉDIA] envelope diferente da AICA e duração errada na documentação: **aceito**; a soltura provisória dura 43 ticks =
  172 ms (não ~60 ms; teste `audio_envelope_provisional_release`); a curva da AICA é a fase G.
- [BAIXA] alegação ampla demais no teste OGG: **corrigida** (vale para os 300 casos; segurança geral pede fuzzing).

**Tempo do download (medido no Flycast, `n6_oraculo.py --musica`, hook de entrada e volta do `8C15D534`):** o SH-4
fica **dentro** do download o tempo do DMA para a RAM de som, linear no tamanho (~177 ns/byte, ~5,66 MB/s): SMPB do
`BS_SND.MLT` (483 680 bytes) 85,5 ms, SMSB do MLT (39 168) 7,2 ms, SOSB 151,5/67,4 ms (efeitos, com o original), M2A.mpb
(483 680) 85,5 ms, M2A.msb (13 984) 2,5 ms. O nativo devolve na hora; o legado (DMA emulado) leva menos que o original.
Efeito medido no jogo: **o estado por passo é o mesmo**, mas com a música nativa os passos caem 1 VBlank antes desde o boot
e 2 depois da carga da M2A (o legado gasta ~1 VBlank por par de downloads; o original, ~5,5 campos). **Decisão do
mantenedor (2026-10-20): opção C**, adiado para o N2 (retomado depois do N7, antes do N8): nenhuma espera artificial,
avanço manual de ciclos ou mudança nas interrupções agora. Pendência explícita em `docs/N2-PLANO.md` 11.1; não é
fidelidade temporal completa.

**Divergências conhecidas.** (0) Tempo do download (acima). (1) O fade de entrada da M2A depois de uma troca de sala (−113 → −16 em ~1,55 s) vem do
algoritmo do driver e dos comandos do jogo, mas o oráculo só tem a M2A depois do LOAD (porta já em −16): falta medir a
entrada pela porta no Flycast (rota longa; fica para quando houver uma âncora mais perto). (2) Atraso do Dreamcast nos
trechos densos (18.4, hipótese de ticks perdidos). (3) Envelope mínimo (fase G). (4) Bend da M2C.

### 18.6 Fase F: validação da música (2026-10-20; integrada, ainda em validação)

**Commits:** `2d73ae2` (OGG/Vorbis) e `ab8f5ed` (marco de integração do N6-F). O que segue está na worktree, sem commit.

**Tempo do download → N2 (decisão do mantenedor, opção C).** Registrado em `docs/N2-PLANO.md` 11.1, com a consequência
audível medida na troca de música (fade de entrada do original, ausente no nativo). Não resolvido; não é fidelidade
temporal completa.

**Pitch bend (M2C), lido no driver e provado no Flycast.** Evento `0xE0` → `0x13E4`: bend = valor·128 − 8192 (só o byte
alto vem no evento); faixa da voz no key-on (`0x0BD8`–`0x0BF4`): byte 3 do canal se o bit 7 estiver ligado (só o RPN 0/0
pelo controle 6 o liga, `0x1ADC`; nenhuma música usa), senão os bytes 0x0C/0x0D do grupo da camada (2 e 2 nos 11 bancos);
`acc = fino + ((bend·faixa) >> 5)` em 1/256 de semitom (`0x1150`–`0x1164`; o driver guarda 16 bits e o índice da oitava
mascara 8 bits; |valor| < 2^15 aqui); as vozes que soam no canal mudam de tom na hora (`0x1414`–`0x1494`); notas novas
nascem com o bend; o reset 0x2758 zera. **Oráculo** (`--injetar-musica 2700:2`: o carregador do próprio jogo carrega e
toca a M2C; linhas `tom` = a mesma amostra com OCT/FNS novos entre duas leituras com KYONB): o mergulho do canal 0 no Dreamcast (passos 3 379–3 386) bate com
exatamente um valor do bend nativo em cada leitura, nas 4 vozes: 58, 37, 18, 4, 0 (−2 semitons) e a volta ao centro (64)
com as outras vozes do canal. Teste `audio_manatee_synth_pitch_bend`.

**M2C × Flycast (43 s):** 1 797/1 877 key-on um a um com todos os registradores em 50 ms; **1 859/1 877 (99,0 %)** em
100 ms; os 18 sem par são o fade de entrada do original (download, acima); as faltas em 50 ms caem no trecho denso.

**Ticks perdidos: medidos.** `n6_oraculo.py --ticks` lê a cada passo o contador de ticks reais do driver (ARM 0x13418,
incrementado pelo tratador a cada 4 interrupções de 1 kHz), o ponteiro da trilha 0 (`[fp+0x10]` − base do SMSD) e a
contagem (`[fp+0x24]`); `bs-musica` escreve o mesmo par depois de cada chamada nativa; `tools/dev/n6_ticks.py` acha, para
cada leitura, a chamada nativa com **o mesmo (ponteiro, contagem)** (pareamento exato, só para a frente). **940 de 944
leituras** do oráculo têm um par idêntico no nativo: a aritmética inteira do sequenciador é a do driver. Atraso (ticks
reais − chamadas nativas desde a 1ª leitura), M2A depois do LOAD: **0/+1** nos passos 1 757–2 199, sobe nos passos
2 200–2 400 e fica em **+6/+7** até a injeção (2 700). O driver **perdeu 6 ticks (24 ms)** no trecho denso da M2A
(~15–21 s), o que explica o desvio medido antes entre as janelas de 10 s. Separação: perda real de ticks (sim, com
pareamento exato); amostragem e alinhamento do oráculo (descartados: a chamada é identificada, não estimada);
configuração do sequenciador (descartada: os pares batem); atraso do consumidor da fila (não afeta ponteiro nem contagem;
não medido). (A primeira versão desta medida usava só o ponteiro e um script à parte, com pareamento ambíguo em
repetições: substituída depois da revisão Codex de 2026-10-20.) É o modelo de ciclos do ARM7 do **Flycast**: o hardware real pode perder outro número. O nativo toca no tempo
nominal, **sem compensação**.

**Mesma rota, transições e fades.** LOAD (18.5): comandos idênticos ao original, sem fade de entrada (porta já em −16),
3 133/3 151 um a um. Troca M2A → M2C (oráculo, carregador `8C02A660`): `vol −113 t0, pan 0, parar`, downloads, `vol −16
t0, tocar, vol −16 t2000`. Fade de saída da área (heliporto, passo 13 079): `vol −113 t4` e o jogo descarrega o índice sem
`parar` (o mesmo no legado: lógica do jogo; a sequência segue a −113, inaudível). Interrupção de um fade por outro: teste
`audio_manatee_fader_driver_algorithm`. Teste `audio_manatee_player_transitions_all_songs`: as 11 músicas em sequência
com a ordem de comandos do carregador, 20 s cada; cada `parar` silencia a anterior em até 1 s; nada recusado.

**Ducking com falas.** O jogo recompilado manda, nas falas de ambiente (`e012aenv`, `e002b`), `vol −48 t=800` no início e
`−16 t=800` no fim ou na interrupção; no diálogo `e002a`, `−47 t=2000` / `−16 t=2000`. Com a música tocando (heliporto,
~190 s de M2A) os `−47`/`−16` baixam ~10 dB e voltam em ~2 s. Teste com a M2A e os comandos reais da `e002b`
(`audio_manatee_player_ducking`): **−12,00 dB** durante a fala (a lei de TL), igual antes e depois, 128 ticks para descer e
para subir. Falas e música nativas juntas: estado igual ao de só falas em 13 312 passos, mesmos contadores das falas; a
dublagem mais longa não muda o tempo lógico (fase E). **Falta:** uma rota em que uma fala toque com a música já tocando
(nas rotas atuais as falas vêm antes da M2A).

**Cobertura das 11 músicas** (`bs-musica` 600 s, mesmo caminho do jogo; `audio_manatee_synth_real_songs` e
`audio_manatee_player_transitions_all_songs`):

| Música | Notas em 600 s | Laço (1ª volta) | Bend | Sem camada (o driver também não toca) | No jogo | Contra o Flycast |
|---|---|---|---|---|---|---|
| M2A | 14 118 | 112,9 s | — | 0 | sim (RCG02; LOAD e troca de sala) | 3 133/3 151 (mesma rota) |
| M2B | 20 104 | 121,2 s | — | 73 (programas 3 e 10) | — | — |
| M2C | 9 856 | 124,4 s | **sim** (canal 0) | 0 | — | 1 859/1 877 e o bend |
| M3 | 16 794 | 135,0 s | — | 38 (programa 2, tecla 96) | — | — |
| M4 (índices 4 e 5) | 27 643 | 142,9 s | — | 8 (programa 3, tecla 38) | — | — |
| M5 | 22 552 | 139,2 s | — | 0 | — | — |
| M6 | 24 077 | 150,4 s | — | 0 | — | — |
| M7 | 17 345 | 181,4 s | — | 12 (programa 9, tecla 55) | — | — |
| M9 | 16 642 | 163,2 s | — | 0 | — | — |
| M10 | 2 557 | 134,1 s | — | 0 | — | — |
| M99 | 24 242 | 183,2 s | — | 0 | — | — |

Todas no slot 0, sequência 0 (a tabela do carregador em 0x8C09EC28). Todas: 0 vozes recusadas, 0 tons fora da tabela, 0
falhas de amostra, sem saturação; todas laçam (0x82 sem fim). As 131 notas sem camada (600 s) têm tecla/velocidade fora
de todas as camadas do programa (conferido uma a uma). **No jogo, só a M2A** foi alcançada pelas rotas; as outras dependem
de rotas que cheguem às áreas delas.

**Revisão Codex da validação** (`reviews/codex-20261020-232634.md`, 5 achados, todos aceitos): a medida de ticks passou
a usar o par (ponteiro, contagem) dentro do próprio `n6_ticks.py` (antes: só o ponteiro, pareamento ambíguo, conclusão
tirada de um script à parte); as linhas `tom` exigem a mesma amostra com OCT/FNS diferentes e a documentação diz o que
elas não excluem (re-disparo da mesma amostra entre leituras); o teste do bend confere o tom efetivo da voz no mixer
(`AudioMixer::pitch_of`) contra 2^(semitons/12) calculado à parte, e o reset 121 com uma nota nova sem evento de pitch
(nasce sem bend; as vozes que soam mantêm o tom, como no driver); o teste de transições exige vozes novas e energia na
mistura por música (o laço fica provado só pela execução de 600 s); o `bs-musica` não escreve posição depois do fim da
sequência.

**Preparação da fase G (só investigação).** O envelope fica em `native/audio/envelope.h` (interface `key_on`/`key_off`/
`tick`/`gain`), hoje usado pela música; os efeitos (`sd_adapter`) passam a usá-lo na fase G, sem segunda implementação.
O que medir/implementar na G, só o que o jogo usa: AR, D1R, DL, D2R, RR e KRS a partir das palavras do envelope (bytes
0x16–0x1F da camada e do descritor SOSP; falta o mapeamento palavra → registrador, a ler no key-on `0x0C54`–`0x0D24`), os
ajustes do canal em 0x32–0x35 (`0x0F60`–`0x0FF0`), o key-off pelo fim do gate e pela soltura, o laço (LPCTL já tratado),
a reamostragem (o mixer interpola), pan/volume (já pela lei do driver) e o DSP que o jogo realmente liga (`0x70A4`, ainda
não lido). **Obrigatório:** o efeito de passos ID 32, que o Dreamcast interrompe antes do fim da amostra (6–15 VBlanks
contra 15,2 da amostra; seção 14). Sem emulador AICA completo.

## 19. Fase G: envelope da AICA, o passo do Eliot (ID 32) e DSP (2026-10-21, em andamento)

**Fase F (situação):** integrada e validada no que as rotas alcançam (18.5–18.6); **não concluída**. Pendências: (1) uma
rota com fala durante a música: as rotas existentes não têm, porque a `passo_heliporto` termina em morte/GAME OVER
(o `vol −113 t=4` do passo 13 079 é o game over; os `−47/−16 t=2000` dos passos 8 845/9 438 são o menu) e as falas
E000–E002 vêm antes da M2A; as seguintes (E003 em diante) dependem de avançar a história com uma rota nova; (2) as
outras 10 músicas no jogo (as rotas só chegam à M2A); (3) a transição com fade de entrada depende do tempo do download
(N2, 11.1). Todas registradas; a fase G segue em paralelo.

**Onde está o envelope (lido no key-on do driver, `0x0C58`–`0x0D24`).** Camada SMPB: **bytes 0x08–0x0B** = as imagens
dos registradores 0x10 (AR 4-0, D1R 10-6, D2R 15-11) e 0x14 (RR 4-0, DL 9-5, KRS 13-10, LPSLNK 14); o driver soma os
ajustes do canal (bytes 0x37 → AR, 0x38 → D1R e D2R, 0x39 → DL, 0x3B → RR, cada um − 0x20, limitados a 0..31; 0x20 no
reset, nenhum controle das músicas muda). A leitura anterior do parser (bytes 0x16–0x1F) estava errada e foi corrigida
(`Layer::eg10`/`eg14`). Descritor de efeito SOSP: uma imagem dos registradores a partir de +4; o envelope fica em
**+0x0C/+0x0E** (o setup `0x780C` copia o descritor para a sombra da porta).

**O que o jogo usa (os 11 bancos e os 211 descritores do `BS_SND.MLT`):**

| Parâmetro | Efeitos (211) | Músicas (374 camadas) |
|---|---|---|
| AR | 31 em todos | 31 em 368; 25–28 em 6 |
| D1R | 0 | 0 em 194; 1–28 nos outros |
| D2R | 0 | 0 em 328; 6–9 em 46 |
| DL | 0 | 0 em 279; 1–10 em 94; 31 em 1 |
| RR | 31 em todos | 16 em 177; 10–23 e 31 nos outros |
| KRS, LPSLNK | 0 | 0 |

**Efeitos:** o envelope é trivial (nível cheio até o key-off, a soltura mais rápida depois). O que falta para o ID 32 é
**quando** o key-off acontece. **Mecanismo lido no driver:** no tick (`0x70A4` → `0x72D8`), para cada porta de efeito
tocando, o driver escolhe o canal no MSLC, lê o registrador 0x2810 e testa o **LP** (bit 15): amostra sem laço → key-off
na hora (`0x7318`: KYONB = 0 com KYONEX); com laço → um contador de voltas (`+0x38`) antes. O ID 32 (amostra 0x0C58D4,
LSA 0, LEA 4 049, OCT −2, FNS 461: 15 986 Hz, **15,2 VBlanks**, sem laço) no oráculo com TMU: o key-off aparece 12 ou 15
VBlanks depois do key-on (31 e 19 vezes; leitura por passo, ~3 VBlanks de resolução) e **o jogo não manda `parar`**
entre o key-on e o key-off (passos 8 715/8 717 e seguintes). Falta saber em que posição da amostra o LP liga: o oráculo
ganhou `--seguir-sa` (LP, SGC, EG e CA por passo dos canais que tocam a amostra) para medir isso.

**Músicas:** o que muda o som é o decaimento (D1R até DL, depois D2R) e a soltura (RR, hoje a provisória de 172 ms). As
curvas de taxa da AICA (tempo por valor de taxa) serão **medidas** no oráculo (EG e SGC por passo de vozes com RR/D1R
conhecidos), não tiradas de tabela de memória.

**Ferramenta:** `flycast_oracle.connect` ganhou prazo de 20 s no primeiro pacote GDB (duas sessões ficaram presas em
"Waiting for debugger" com o Python esperando para sempre; agora reconecta).

**Correção do oráculo (2026-10-21): o MSLC estava no endereço errado.** No mapa de registradores da AICA (o mesmo do
Flycast, `CommonData`: +0x0C = MOBUF 7-0, **MSLC 13-8**, AFSEL 14; +0x10 = EG/SGC/LP; +0x14 = CA), o canal monitorado se
escolhe no byte alto de **0x280C**; o `n6_oraculo.py` escrevia em 0x2810, o MSLC não mudava e **toda leitura de monitor
(o `ca` do `--ca` e as linhas `seguir`) era do canal que o driver escolhera por último**. Pior: ler 0x2810 em 16 bits (ou
o byte 0x2811) **limpa o LP** desse canal, e o driver usa o LP para o key-off dos efeitos (`0x72D8`): a execução com
`--seguir-sa` perturbou o key-off do passo (um key-off a ~65 VBlanks). Corrigido: MSLC em 1 byte em 0x280D; nos canais de
efeito (48 em diante: o driver usa 0–47 para as músicas) só o byte baixo do EG, sem limpar o LP. **O que caiu:** a
reconstrução do key-on do ID 32 pelo `ca` (a conclusão "key-off no fim da amostra", tirada antes desta correção, foi
**retirada**) e qualquer uso antigo do `ca`. **O que fica:** as comparações de música (18.4–18.6) pareiam por registradores
de key-on e pelo tempo do TMU, sem o `ca`; o pitch bend (linhas `tom`, registradores de pitch por canal, sem MSLC); os
ticks (memória do ARM, sem MSLC).

**Lei de nível em dB: duas referências divergem.** O Flycast (a referência de comportamento deste projeto, já usado como
oráculo) faz `ganho = 2^(−TL/16)` (0,376 dB por passo de TL), DISDL/DIPAN em 8 passos de TL por nível (3 dB) e o envelope
entra como `AEG >> 2` (4 unidades de envelope = 1 de TL). As notas de Neill Corlett (texto público sobre a AICA, a partir
da especificação oficial e de experimentos; https://pastebin.com/9TbKLGPs) dizem 0x40 de envelope = 3 dB e "0x10 de TL =
3 dB" (0,1875 dB por passo), com o autor em dúvida ("I think this may be TL"). O nativo segue **0,375 dB** (o do oráculo,
contra o qual tudo foi validado); a divergência fica registrada como aberta (só uma gravação do hardware a resolve).

**Modelo do envelope (fontes):** estados (ataque a partir de 0x280 por subtração proporcional ao nível; decaimento 1 até
`DL << 5`; decaimento 2; soltura no key-off; o canal acaba quando a atenuação passa de 0x3BF) e tabelas de passo por taxa
efetiva (deslocamentos do ataque e incrementos do decaimento por taxa e passo mod 4; período por taxa: 0x02–0x05 dados,
0x06 = 0x02 com metade, 0x30 em diante a cada 2 amostras; **o intervalo 0x07–0x2F é inferido** pela regra de metade a cada 4
taxas, coerente com as duas pontas) das notas de Corlett. **Taxa efetiva com KRS = 0 e oitava negativa: as duas fontes
divergem** (Corlett: `(KRS + OCT + taxa)·2 + FNS[9]`; Flycast: `taxa·2 + FNS[9] + max(0, (KRS + OCT)·2)`), o que muda o
tempo em até 4× nas vozes das músicas (quase todas com OCT negativo). A medida no oráculo (EG e SGC por passo de vozes com
taxas conhecidas, já com o MSLC certo) decide.

**ID 32, medido com o MSLC certo (LOAD, 7 passos):** o CA do canal 48 anda ~800 amostras por leitura (501 → 1 302 → 2 100
→ 2 900 → 3 699: 15 986 Hz em ~50 ms), o EG fica em 0 (nível cheio: AR 31 sem decaimento) e o key-off vem na leitura
seguinte a 3 699, logo depois de o CA chegar ao LEA 4 049 (~22 ms) mais o tick do driver; o mesmo nos outros passos (515 →
3 713 → key-off). **O original toca o passo até o fim**: o driver dá key-off quando o LP liga no fim da amostra
(`0x72D8`). O nativo já faz o equivalente (toca a amostra inteira e libera a porta no fim). Nada a mudar no som do ID 32;
o "key-off antecipado" da fase D era artefato de amostragem (corrigido na seção 15).

**Envelope implementado (`native/audio/envelope.h`, usado pelo `manatee_synth`):** estados, taxa efetiva e tempos pela
seção anterior, com a escolha decidida pela **medida**: 18 combinações (amostra, estado, OCT, FNS[9]) de inclinação do EG
nas vozes da M2A batem com a taxa efetiva `taxa·2 + FNS[9] + max(0, (KRS + OCT)·2)` e a tabela de tempos (ex.: soltura
RR 16, OCT −1, FNS[9] 0: 1 479,2 unidades/s medidas, 1 484 pela tabela; RR 22: 12 048 × 12 047; decaimento 2 D2R 7:
60–90 × 69–81) e não com a fórmula de Corlett (metade ou menos). A soltura das músicas agora segue a RR da camada (RR 16:
~0,55–0,69 s; RR 10: até 5,5 s) em vez dos 172 ms provisórios; as camadas com D1R/DL decaem até DL e as com D2R decaem
depois. Teste `audio_envelope_release_rates_vs_oracle` (12 medidas do oráculo, ±2 %) e `audio_envelope_states`.

**48 canais da música (alocador do driver `0x1244`, INFERIDO em parte):** com a soltura real, as vozes se sobrepõem mais
e as músicas chegam ao limite. O driver percorre 48 canais (0xE800, 0x40 cada): o primeiro livre na hora; sem livre, nunca
um de prioridade maior que a do pedido e, entre os candidatos, prefere os já em soltura (`+3` = 0xFF no key-off, `0x0AA4`)
e depois compara prioridade (`+0x3E`) e idade (`+0x3F`). O nativo: 48 vozes por sintetizador, livre → em soltura com a
maior atenuação → a mais antiga (os campos `+3`/`+0x3F` exatos não estão provados; a prioridade é a mesma nas notas das
músicas do disco). O mixer do jogo passou a 80 vozes (48 + 16 dos efeitos + falas): não rouba voz por conta própria.
Nas 11 músicas (300 s): pico de 48 vozes (M10: 41), 0 recusadas, 0 saturadas; reaproveitamentos de 0 (M10) a 9 328 (M5).
**A conferir no oráculo:** quais canais o original reaproveita (o KYONB e o SA por canal já estão nas linhas `aica`).

**Efeitos:** os 211 descritores têm AR 31, D1R 0, D2R 0, RR 31: o envelope deles é cheio até o key-off e a soltura de
3,6 ms (taxa 62). O nativo corta na hora no `parar` (sem os 3,6 ms): a única diferença de envelope nos efeitos,
inaudível; a porta do adaptador de efeitos ainda não usa o `Envelope` (fica para quando houver um caso que o peça).

**Efeitos, soltura (2026-10-21):** implementada mesmo inaudível, para fechar o envelope: o `parar` é key-off e a voz
solta pela RR do descritor (+0x0C/+0x0E), linear em dB, até 0x3BF (89,9 dB); tocar no mesmo canal corta o resto. A porta
fica livre na hora, como antes (o jogo vê o fim no `parar`). Teste `audio_sd_stop_releases_with_rr`.

**Reaproveitamento dos 48 canais, medido (2026-10-21, `tools/dev/n6_canais.py`):** nos oráculos da M2A (5 766 key-on) e
da rota LOAD (2 553), 100 % dos key-on da música caem em canal sem KYONB e no máximo ~39 canais estão ligados ao mesmo
tempo: **o original nunca corta nota ligada**. O nativo também não (`stolenKeyed` = 0 nas 11 músicas em 300 s);
reaproveita só vozes em soltura (M2A: 2 810, 183 delas a menos de 30 dB; M5: 9 328 / 976). Qual canal em soltura o
original escolhe não está provado: 63 % dos key-on vão ao próximo canal livre depois do anterior (rodízio parcial); o
nativo escolhe a soltura mais atenuada. Efeito audível pequeno (caudas de soltura); fica como inferência registrada.

### 19.1 DSP de efeitos (2026-10-21)

**Uso, medido no oráculo** (rota LOAD, registrador 0x20 no key-on, `n6_oraculo.py`): música IMXL 15 (1 136), 14 (901),
13 (417), 10 (12), 0 (87) → MIXS 0; efeitos IMXL 12–15 → MIXS 0 ou 1 (passo do Eliot: 14 → MIXS 1). Os valores batem com
os dados: byte 0x10 da camada SMPB e palavra +0x14 do descritor SOSP.

**O programa, medido no oráculo** (`--dump-dsp`, passo 1 850, registradores 0x2000–0x45FF): o SFPB do `BS_SND.MLT` é a
imagem exata dos registradores 0x3000–0x3BFF **a partir do byte 0x54** (704/704 registradores iguais: COEF, MADRS, MPRO);
0x2804 = 0x6390 (RBL 3 = anel de 64 K palavras; RBP 0x390 = 0x1C8000, o SFPW, que no MLT só reserva os 128 KB); saídas
0x2000–0x200C = EFSDL 15 com EFPAN 0x1F/0x0F alternados (o SFOB: pares EFSDL, EFPAN a partir do byte 0x18). O programa:
126 passos, lê MIXS 0 e 1, escreve EFREG 0–3, 27 leituras e 12 gravações no anel, todas em passos ímpares, cada IWT dois
passos depois do MRD: uma reverberação estéreo.

**Implementação (decisão do mantenedor 2026-10-21: interpretar o programa):** `native/audio/aica_dsp` (só o bloco DSP, não
a AICA), escrito pelas notas públicas de Corlett: campos do MPRO, SHIFTED do ACC anterior (saturação em SHFT 0/1), ACC de
26 bits, Y por COEF/FRC/Y_REG, ponto flutuante de 16 bits da memória (expoente 12 = sem bit implícito), anel com MDEC_CT,
ADREB, NXADR e TABLE, ADRL/FRCL/YRL, EWT. O anel começa com o zero do formato (`pack(0)`: a palavra 0 valeria meia
escala; no console o programa roda desde o boot e cada MWT varre o anel a cada 1,49 s). O mixer soma os envios por
barramento (razão sobre o volume da voz: IMXL/DISDL, então volume, fades, ducking e soltura valem para o envio), roda o
DSP a cada quadro de 44 100 Hz (também com o jogo pausado: a cauda continua) e mistura as saídas por EFSDL/EFPAN. Mods
podem trocar o SFPB/SFOB (mesmo formato). `BS_SEM_DSP=1` desliga (comparação).

**Medidas:** resposta ao impulso do programa do disco: primeira saída em 84 amostras (1,9 ms), ~30 dB por segundo (RT60
~2 s), estéreo (L/R 1,07), resíduo de quantização ~−82 dBFS. M2A (120 s, `bs-musica`): seco −26,1 dBFS, com DSP −24,1
dBFS; a parte molhada fica 2,4 dB abaixo da seca; pico 18 008, 0 saturadas. Jogo (rota LOAD, efeitos, falas e música
nativos): estado igual em 4 481 passos; mistura sem saturação.

**Testes:** `audio_dsp_float_roundtrip`, `audio_dsp_mac_and_output`, `audio_dsp_ring_delay` (linha de atraso pelo anel e
o IWT de dois passos), `audio_dsp_game_program_reverb` (o programa do disco, conferido com os registradores do oráculo) e
`audio_dsp_mixer_send`.

**Não provado:** o nível absoluto do caminho molhado contra o console (nenhuma gravação do hardware; a escala segue a
especificação: envio 0xF = escala cheia nos dois caminhos); as falas não enviam ao DSP no nativo (o envio do canal das
falas não foi medido: nenhuma rota do oráculo com fala depois do campo novo); a latência exata de 2 ou 3 passos do MRD
(o programa só usa 2).

**Fase G: implementação completa; encerramento depois da F** (prioridade do mantenedor 2026-10-21; o programa e os registradores conferidos no Flycast; o som molhado não
foi comparado amostra a amostra), com as pendências acima registradas: envelope (medido), soltura dos
efeitos, 48 canais (sem roubo de nota ligada, como o original), DSP (programa do disco, registradores conferidos).

### 19.2 Fechamento técnico da G (2026-10-21, depois da revisão Codex)

- **Conjunto único de 48 canais** para as 8 portas (`ChannelPool`, achado Codex 2): `audio_manatee_synth_shared_pool`.
- **Envelope por amostra dentro do tick** (achado Codex 5): o nível novo de cada tick chega em rampa linear de 176 quadros
  no mixer (`set_volume_ramp`) e a voz nova só anda no tick seguinte; `audio_mixer_volume_ramp`.
- **MIXS de 20 bits sem proteção** (achado 4), medido: nas 11 músicas (300 s cada, com DSP) **nenhuma** amostra passa
  de 20 bits; a escolha volta/satura não muda o jogo.
- **11 músicas (300 s, tudo ligado):** 0 amostras saturadas (pico 23 069 = −3 dBFS na M2C), pico de 48 canais (M10: 41),
  **0 notas ligadas cortadas** (como o original, que nunca corta nota ligada), reaproveitamentos só de vozes em soltura.
- **Sem regressão no jogo:** rota LOAD (efeitos, falas e música nativos com DSP): estado igual em 4 481 passos e key-on
  3 133/3 151 um a um com o oráculo; rota `passo_heliporto` (12 815 passos, tudo nativo): estado igual às execuções
  anteriores (música nativa só / efeitos e falas do legado).
- **Testes:** `bs_audio_tests` 79/79 normal e ASan (com dados, 0 skips).
- **Limitações de fidelidade registradas:** nível absoluto do caminho molhado (sem gravação do console); envio das falas
  ao DSP não medido (sem rota do oráculo com fala depois do campo novo); qual canal em soltura o driver reaproveita
  (parte rodízio; o nativo usa a soltura mais atenuada); 0,375 dB por passo de TL (oráculo) × metade nas notas de Corlett;
  EG em rampas de 4 ms em vez de taxa exata por amostra.

**G: tecnicamente pronta para encerrar; o encerramento formal espera a F** (prioridade do mantenedor 2026-10-21).

### 18.7 F: rota com fala durante a música e outras músicas — bloqueio (2026-10-21)

**Procurado:** todas as rotas do repositório e os saves do N4 (`c1`–`c3b`, `f1`, `f2`, `g1`): todos param antes do primeiro
combate (sala RCG02/RCG03). Nas rotas `passo_heliporto`/`passo_combate`/`passo_save_rcg02`, as falas (`e012*`, `e002a/b`,
`e000env`) tocam **antes** da M2A (que começa no passo 7 337) e nenhuma outra música toca (só a M2A é carregada e tocada;
GAME OVER e menus mudam só o volume da porta). `passo_cgate` não toca música.

**A primeira cena com fala depois do início da M2A** é a do Dogs no fim do primeiro combate (`e003`: "That was close.
Thanks for helping me... Dogs", pelas transcrições). Para chegar lá é preciso **vencer o primeiro monstro**:
- as rotas gravadas não chegam (a comentário "game over no passo 12 186" é de uma linha do tempo antiga: hoje a rota fica
  presa no menu de itens, porque o Small Hassy ainda não foi pego; o estado do jogo é o mesmo em todas as combinações de
  áudio, então **não** é efeito do áudio nativo);
- 5 tentativas no gameplay assistido (fugir, socar em 8 direções, pegar e usar o item): o Eliot entra no combate com 35 de
  vida e sem arma; morre em todas (passos 12 148–13 831). Sem retorno visual contínuo, vencer o combate às cegas não é
  viável no custo de ~10 min por tentativa.

**Alternativa proposta (sem mudar a lógica do jogo nem simular eventos):** o mantenedor joga uma vez, na janela, do save
`g1` (ou de NEW GAME) até depois da cena do Dogs e salva no próximo ponto de save (save nativo, `--saves-exportar` para
VMS). Com esse save, uma rota curta de LOAD (como a `passo_load_slot1`) reproduz a fala durante a M2A e serve para:
ducking medido no WAV do jogo (o teste unitário já dá −12,00 dB), sincronia fala × legenda (eventos do `DialogueBridge`),
estado do jogo por passo nativo × legado e o oráculo do Flycast pelo mesmo save. Saves mais adiante (outras áreas) cobrem
as outras músicas com o mesmo método (uma rota de LOAD por cenário, não 11 rotas longas).

**F continua aberta** nesses dois critérios (ferramenta para o save do mantenedor: 18.8); os demais (integração, notas, bend, ticks, laços, transições nos testes, M2A no
jogo, menus e GAME OVER) estão provados (18.6). As diferenças de tempo do download e o fade de entrada continuam no N2
(N2-PLANO 11.1).

### 18.8 Gravação e reprodução de gameplay para desbloquear a F (2026-10-21)

Para substituir a exploração às cegas por jogo real do mantenedor (sem cheats e sem mudar a lógica do jogo):

- **Gravar** (`BS_GRAVAR=<arquivo>`, só na janela, `--tempo nativo|experimental`): o filtro do `InputSystem`
  (`set_filter`) recebe o estado já combinado do teclado e do controle; a amostra é **congelada dentro do passo de jogo**
  e passa pelos bytes do controle do Dreamcast (`to_pad`/`to_controller`), então o jogo vê na gravação exatamente o que
  verá na reprodução. A saída é um roteiro `indice passo` (uma linha por mudança), o formato do `BS_INPUT_SCRIPT`, do
  gameplay assistido e do oráculo do Flycast (`flycast_oracle.py --roteiro`, com `@ancora-ram` quando os passos
  divergem depois de filmes). F5 marca um checkpoint (comentário com o passo e o quadro em BMP; o quadro só ilustra e pode ser de um VBlank antes). Com `BS_GRAVAR_BASE` +
  `BS_GRAVAR_ATE`, o roteiro base controla até o passo indicado e o jogador assume (o trecho fica na gravação).
- **Reproduzir** (`tools/dev/gravacoes.py reproduzir`): headless, com o cartão do início copiado, **até o mesmo VBlank em
  que a gravação parou** (rodapé `# fim passo P vblank V`); compara o `BS_STATE_PASSO` gravado na janela (hash da região
  de variáveis, vida e posição, **todo passo**) com o da reprodução e os hashes dos saves nesse ponto. Vereditos:
  DETERMINISTICA, DIVERGENTE (primeiro passo divergente, VBlank diferente, passos além do fim, saves diferentes) e
  INCOMPLETA (gravação sem rodapé, `rc` ≠ 0 ou estado sem continuidade: o trecho existente ainda é conferido).
- A lógica da gravação fica em `transitional/input_bridge.h` (`SessionRecorder`, teste `input_native_session_recorder`);
  um roteiro base com `DESCONECTADO` é recusado (a presença do controle na janela não passa pelo filtro). O oráculo do
  Flycast aceita os valores intermediários gravados de um controle real (`AX=`, `AY=`, `LT=`, `RT=`).
- **Sessão** (`%LOCALAPPDATA%\BlueStingerRecomp\gravacoes\<nome>`): `sessao.json` (origem com SHA-256 do save,
  executável, dados, argumentos, relógio fixo), `saves_inicio\` (nunca alterado), `saves\` (os saves do jogador),
  `entrada.txt`, `estado.tsv`, checkpoints, `reproducao\`. Origens: NEW GAME, save (`--save ... --load`: o LOAD do
  slot 1 pela `passo_load_slot1` é automático e o jogador assume no passo 2131), saves de outra sessão, ou outra
  sessão/roteiro até um passo ou checkpoint (repetir o combate a partir de um ponto sem refazer o caminho).
- **Limitação:** durante filmes o passo de jogo não avança e a entrada fica congelada (os filmes não podem ser pulados
  na gravação; a reprodução é igual). O trecho reproduzido roda em tempo real (~25 passos/s).

**Testes (2026-10-21, `build
6-game`, controle virtual do SDL3 pelo caminho real dos controles da janela):**

| Sessão | Origem | Passos | Resultado da reprodução |
|---|---|---|---|
| `teste_newgame` | NEW GAME, START no título e no menu | 380 | estado igual em todos os passos, saves iguais |
| `teste_g1` | save `g1` + LOAD automático; o controle virtual move e soca a partir do passo 2131 (um A apertado durante o trecho reproduzido foi ignorado) | 3 444 | igual |
| `teste_cont` | `teste_g1` até o passo 2800, depois controle ao vivo | 4 010 | igual; prefixo idêntico ao da origem |
| `teste_cp` | F5 real (tecla enviada à janela) | 934 | checkpoint no passo 680 com o quadro |
| negativo | `teste_g1` com uma entrada atrasada 5 passos | — | **acusado** no passo 2247 (hash do estado) |

**Depois da revisão Codex (`reviews/codex-20261021-112540.md`, 7 achados; STATUS.md):** sessões refeitas com o rodapé novo.
LOAD do `g1` com analógico e gatilho intermediários (`AX=50`, `LT=93`) e F5 real: 3 094 passos; continuação do `cp1`
(passo 2660): 3 762 passos, prefixo idêntico; as duas DETERMINISTICA, saves iguais, parada no VBlank do rodapé, 0 passos
além do fim. Negativo (ANALOG_UP atrasado 4 passos): acusado no passo 2224 (uma mutação no gatilho em 93 não muda o jogo:
abaixo de 128 não é botão). Sessão sem rodapé: INCOMPLETA. Base com `DESCONECTADO`: recusado.

### 18.9 F com a gravação do mantenedor: fala durante a música (2026-10-21)

**Sessão** `sessao-20261021-140145` (`tools/dev/gravacoes.py`, controle Xbox): LOAD do `g1` → combate vencido → cena do
Dogs → SAVE no slot 2 (`BLUESTIN.001`); 7 915 passos, 3 checkpoints, gravação completa (rc 0, rodapé).
**Reprodução:** DETERMINISTICA, estado igual nos 7 915 passos e saves iguais (a comparação ignora a data do envelope
`.bsv`, bytes 16–23, a hora de gravação pelo relógio do host; a imagem do save, o tamanho e o CRC são comparados).

**Medidas na mesma entrada, headless, `--audio nativo` × `--audio legado`** (`%LOCALAPPDATA%\BlueStingerRecomp\n6f\dogs`):
- **Fala durante a música:** `fala-start voz/e003` no passo 3825 (VBlank 7697) com a M2A tocando; início do áudio 3
  passos depois (o contrato do Flycast); fim físico no VBlank 11 723, estado 3 no passo 5840.
- **Ducking:** no mesmo passo do início da fala o jogo pede a porta da música em −48 com rampa de 800 e, no passo do
  estado 3, de volta a −16 com rampa de 800; o player nativo aplica a lei medida (degrau −16 → −48 = −12,00 dB,
  `audio_manatee_player_ducking`). Uma medida acústica no WAV não isola a música (a cena tem outros sons durante a fala)
  e não foi usada como prova.
- **Sincronia fala × legenda (pacote en-US provisório):** as 16 linhas da `e003` começam no jogo de 0,1 a 1,0 VBlank
  depois do tempo do pacote a partir do início do áudio (a resolução de um VBlank).
- **Estado × legado:** igual do passo 1 ao 5840; a 1ª diferença no 5841, logo depois do 1º fim natural da fala nativa
  (a divergência legítima: no legado a fala nunca acaba). Depois dela a posição do Eliot é igual em todos os passos menos
  um, o legado também chega ao SAVE e **o save gravado é idêntico** ao do nativo.
- Três provedores sem falha (efeitos 288 tocados, música 4 604 notas, a fala `e003` inteira).
- **Achado para a G (nível):** 7 795 amostras saturadas e 5 estouros do MIXS no trecho de combate (VBlank 5 936–6 553,
  ~5 500 amostras, golpes e o monstro) e em picos curtos durante a fala com a música. As rotas anteriores não tinham
  combate com fala. Falta saber se o original também satura ali (comparação com o Flycast no mesmo trecho).

**F:** o critério "fala durante a música" está **provado** (ducking pelo jogo, sincronia, estado e save). **Continua
aberto** o critério "outras músicas no jogo": nesta sessão só a M2A toca (cargas 4, tocar 1). Próximo passo: o
mantenedor grava a partir do save do slot 2 (opção "saves da sessão selecionada", LOAD manual) até uma área com outra
música e salva.

**Áudio em tempo real (achado na gravação):** o mantenedor não ouviu som. Causa: o adaptador enviava ~4 blocos por VBlank;
com o jogo atrasado (picos de 981 ms) a fila do XAudio2 (64 blocos) estourava, o envio falhava e a saída era dada como
perdida (saída nula até o fim); e a saída ficava presa ao dispositivo padrão da abertura. **Corrigido** no áudio do jogo
(a saída dos filmes, N5, não muda): um bloco por VBlank; modo "seguir a saída padrão" (`make_xaudio2_output(200, true)`):
confere a saída padrão do Windows a cada segundo e se reabre nela quando muda ou cai, e com a fila cheia descarta o
bloco em vez de perder o dispositivo; testes `audio_output_xaudio2_follow_burst` e `audio_output_xaudio2_stats`. O
desempenho do gameplay (~17–18 ms por campo, legado e nativo iguais na mesma rota) é o do N2: abaixo do tempo real a
saída fica sem dados (faltas de dados), qualquer que seja o áudio.

### 18.10 F encerrada: outra música no jogo e a análise automática (2026-10-21)

**Segunda gravação do mantenedor** `sessao-20261021-145006` (dos saves da 1ª sessão, LOAD do slot 2): 7 496 passos, rodapé,
checkpoint F5 no passo 7420 (`checkpoint_1.bmp`), **SAVE real no slot 3** (`BLUESTIN.002`; slot 1 = `.000`).
Analisada por `tools/dev/n6_f_sessao.py` (reprodução, a mesma entrada com `--audio nativo` e `--audio legado`, critérios):

| Critério | Sessão 1 (Dogs) | Sessão 2 |
|---|---|---|
| reprodução determinística (estado por passo, saves) | ok (7 915 passos) | ok (7 496) |
| músicas identificadas (slot da sequência pelo tamanho; os M*.MSB têm tamanhos únicos) | M2A | M2A ×3, **M3** (passo 6548, troca de área) |
| outra música além da M2A | — | **ok** |
| fala durante a música com ducking (descida no início, volta à base no estado 3) | `e003` | `e025a`, `e025b`, `e005b` (esta no passo da carga da sala: −113, −16, −16, −48 → −16); `e005env` e `e005` interrompidas pelo jogo |
| legendas en-US (o arquivo que o jogo escolhe; todas as linhas, ±2 VBlanks) | 16 linhas, 0,05–0,96 VBlank | 17 linhas, 0,0–1,0 VBlank |
| estado × legado até o 1º fim natural de fala; saves iguais | ok (1ª diferença no 5841, fim no 5840) | ok (3048 / 3047) |
| saída sem erro nem perda; provedores sem falha | ok | ok ("sem camada 1": a nota tecla 96 do programa 2 da M3, fora das camadas; o driver também não a toca, 18.5) |

**M3 × Flycast** (`n6_oraculo.py --injetar-musica 2700:3`, o carregador do próprio jogo toca a M3; × `bs-musica M3`):
3 164/3 255 key-on com a mesma amostra e tom (97,2 %), 3 120/3 255 com TL, DIPAN e DISDL (95,9 %), 6 inversões; tempo +20 ms
no início a −25 ms nos trechos densos (como a M2A).
**Resíduo analisado (2026-10-21):** é deriva de tempo e o fade de entrada, não erro do sintetizador. (1) Com janela de
100 ms, **3 255/3 255** pareiam em amostra e tom (os 91 sem par a 50 ms são a deriva dos trechos densos; a M2A dá
5 757/5 766 a 100 ms). (2) No critério estrito sobram **48, todos no 1º 1,4 s**, com o TL do original **94 acima** do
nativo: a porta ainda em −113 antes do −16 do gerenciador (o tempo do download, N2-PLANO 11.1; o `bs-musica` toca sem esse
fade); fora disso **3 207/3 255 (98,5 %)** com todos os registradores. (3) As 6–8 inversões são acordes no trecho denso: o
original lê as duas notas na mesma passada e o nativo as tem a ~80 ms (a deriva documentada de até ~0,1 s), não ordem
errada.

**F concluída** (2026-10-21): os critérios da seção 18 estão provados no jogo, com gravações reais do mantenedor: fala
durante a música (ducking, sincronia, estado, save), outra música no gameplay (M3), integração, notas, bend, ticks, laços e
transições. Diferenças de tempo reservadas ao N2 (11.1): o download da música e o fade de entrada que depende dele; o mesmo
passo 1–2 VBlanks antes no nativo.

**Ferramentas desta etapa:** `n6_f_sessao.py` (revisão Codex `reviews/codex-20261021-145415.md`); `gravacoes.py` com a
reprodução na janela em pasta própria (não substitui o veredito headless), saves comparados sem a data do envelope `.bsv` v1
válido, padrão `--audio nativo` (os sons do menu são efeitos; com só música e falas nativas, o menu ficava mudo na janela:
os efeitos legados não tocam na saída do PC); `flycast_oracle.py` acha o CDI do repositório principal a partir do worktree.

### 19.3 G encerrada: a saturação caracterizada (2026-10-21)

**Achado:** saturação da mistura nativa no combate e durante falas (sessão 1: 7 795 amostras e 5 estouros do MIXS; sessão 2:
13 016 amostras). Os picos coincidem com **as 7 portas de efeito disparadas juntas** (o golpe final: pedidos 63, 20, 21 ×3,
54, 16/14) sobre a música, e com a fala sobre a música.

**Caracterização (comportamento do original, não divergência):**
- os níveis por canal são os do original: música (key-on com TL, DIPAN e DISDL iguais ao Flycast, 18), efeitos (leis de TL
  por volume e nível-base do descritor medidas no O6, pan e DISDL), falas (canal da AICA TL 0, DISDL 15, pan 0 no Flycast =
  ganho 1 no nativo);
- a soma é a do original: os canais somados num acumulador largo, o volume mestre MVOL **15** (0 dB; registrador 0x2800
  lido no Flycast, sem saída de 18 bits nem mono) e um clamp de 16 bits no fim (referência de comportamento: o mixer do
  Flycast);
- o filtro por canal do original não reduz os picos: LPOFF 0 em todos os 211 efeitos, mas FLV0–FLV4 = 0x1FF8 (corte
  máximo) e Q 0 (descritores do disco, byte +0x18 = registrador 0x28);
- o MIXS sem proteção de estouro é o do manual da AICA (o nativo dá a volta como o hardware).
Logo o original satura nos mesmos picos; **nada foi corrigido** (baixar o volume global seria mascarar).

**Confirmação acústica com o Flycast: inviável com o oráculo atual.** A saída do Flycast capturada pelo driver "disk" do
SDL (`SDL_AUDIODRIVER=disk`, sem mudar o Flycast) grava só o 1º bloco quando o oráculo para a emulação a cada passo; e a
entrada gravada não se reproduz exatamente no Flycast (depois da âncora de RAM no passo 1758, a posição se afasta ~32
unidades no passo 2210), então o combate tomaria outro rumo. Registrado como limitação.

**G concluída** (2026-10-21) com as limitações: nível molhado do DSP contra o console, envio das falas ao DSP (não medido;
o original teria mais energia, não menos), canal reaproveitado em soltura (parcialmente inferido), TL 0,375 dB × as notas
de Corlett, EG em rampas de 4 ms, o resíduo da M3 × Flycast (18.10).

## 20. Fase H: promoção do áudio nativo (2026-10-21, promovida condicionalmente, 20.5)

Objetivo (seção 11): o áudio nativo (efeitos, falas e música) como padrão, com o legado como fallback até o N8.
**Não promover antes de:** (1) a F fechar (rota com fala durante a música e outras músicas no jogo: gravação do
mantenedor, 18.8); (2) o fechamento formal da G (depois da F); (3) a matriz 20.2 aprovada numa execução completa (sem
`-Parcial`); (4) autorização do mantenedor.

### 20.1 Infraestrutura (feita, sem mudar o padrão)

- `--audio nativo|legado`: os três provedores de uma vez; `--audio-efeitos/-falas/-musica` valem sobre ela. Precedência:
  opção específica > `--audio` > `BS_AUDIO` (ambiente, baterias) > `kAudioNativeDefault` (`app/main_sdl.cpp`, **false**).
  Promover = trocar a constante e o texto de ajuda; `--audio legado` é o fallback de uma palavra. Valor inválido (opção
  ou ambiente): o jogo recusa e mostra o uso.
- Estabilidade no resumo do jogo (`[audio] mistura:`): pico, amostras saturadas, máximo de vozes no mixer, vozes no fim,
  MIXS acima de 20 bits; e da saída (`IAudioOutput::Stats`): blocos, **faltas de dados** (bloco que chegou com a fila do
  XAudio2 vazia, fora de pausa e de descarte), maior fila, erros de escrita e perdas do dispositivo. Com o dispositivo
  perdido, o adaptador troca pela saída nula com log (o contrato da `IAudioOutput`); a sessão da saída recomeça do zero
  a cada `open`, e o descarte durante a pausa não religa a voz (teste `audio_output_xaudio2_stats`).
- Bateria nível 2 `tools/dev/n6_h.ps1` (H1–H9), reaproveitando `n6_efeitos.ps1`, `n6_falas.ps1`, `bs-musica`,
  `n6_musica_compara.py` (oráculo do Flycast da LOAD, `n6_o6\musica_m2a.tsv`), `test_pd_nativo.ps1`, `n5_e001.ps1` e
  `test_saves.ps1` (com o build com mods `build\n6-mods`). Pré-requisito ausente reprova; ambiente `BS_*` isolado.
  Revisão Codex `reviews/codex-20261021-121034.md` (7 achados, STATUS.md).

### 20.2 Matriz de aprovação (2026-10-21, casos um por vez)

Reaproveitados os casos cujo código não mudou depois da aprovação (a saída em tempo real não afeta as execuções headless,
que usam a saída nula; a seleção do áudio em `app/main_sdl.cpp` não mudou desde o H5).

| Caso | Situação | Evidência |
|---|---|---|
| H1 unitários | **PASSOU** | áudio 79/79 e vídeo 45/45 (com os testes novos da saída) normal e ASan depois das últimas mudanças; localização 9/9 e interface 25/25 normal e ASan (código sem mudança desde então) |
| H2 efeitos × legado | **PASSOU** | combate e heliporto: quadros e estado por passo iguais (`n6_efeitos.ps1`) |
| H3 falas × legado | **PASSOU** | `n6_falas.ps1` 11/11 (contrato do Flycast, eventos, legenda) |
| H4 tudo nativo × legado | **PASSOU** (critério de saturação revisto pelo modelo da AICA, abaixo) | LOAD: estado igual nos 4 480 passos comuns (o nativo 1–2 VBlanks antes por passo, N2 11.1), 0 fontes acima de 16 bits, 0 saturadas, MIXS 0, provedores sem falha, key-on 3 133/3 151 × Flycast, 0 inversões. Heliporto: estado igual até o 1º fim natural de fala (2513), provedores sem falha, **0 fontes acima de 16 bits**, 460 amostras de **clipping de soma** (o maior trecho durante o ambiente `e012aenv`, voz de ganho 1 como no original, e efeitos) |
| H5 seleção e fallback | **PASSOU** | padrão legado; `--audio nativo/legado`; `BS_AUDIO`; precedência da opção específica; valores inválidos recusados (uso no stderr) |
| H6 dublagens WAV/FLAC/OGG + legenda | **PASSOU** | as três tocadas, 0 falhas de leitura, legenda ativa; `mods\audio` e o pacote de teste removidos no fim |
| H7 11 músicas × 300 s | **PASSOU** (critério completo) | inventário das 11; notas e vozes > 0; recusadas, tom saturado, falhas de amostra, saturadas, nota ligada roubada, MIXS = 0; "sem camada" só M2B 36, M3 18, M4 4, M7 4 (dado da música, 18.5) |
| H8 outras frentes com áudio nativo | **PASSOU** | controles `test_pd_nativo` P1/P2: 30 casos, 0 falhas; filme `n5_e001` 10/10; **`test_saves` 25/0 com `BS_AUDIO=nativo`** (as 17 execuções do jogo com os três provedores nativos) e 25/0 com o áudio padrão, depois de corrigido o orçamento da rota de SAVE para os filmes nativos (abaixo); S12 pulado como sempre (sem a VMU do Flycast) |
| H9 tempo real (janela, XAudio2) | **BLOQUEADO** pelo desempenho do N2 (reprovado no critério de faltas) | 0 erros de escrita, 0 perdas, 0 descartes, 0 saturadas; faltas de dados 1 399 → **345** em ~7 200 blocos com a reserva de 66 ms; o jogo a ~18,4 ms por campo em média (42 % acima de 16,68 ms), igual com áudio legado (N2-PLANO 11.2). Limite mantido |

**H4, critério de saturação pelo modelo da AICA (2026-10-21).** No original a saída de cada canal é de 16 bits (o
Flycast confere isso por canal) e só a soma dos canais passa da faixa, cortada uma vez no fim (MVOL 15, 19.3). O mixer
nativo conta agora as amostras em que **uma fonte sozinha** passa de 16 bits (uma voz com o seu ganho; uma saída EFREG do
DSP com o seu ganho; faixa assimétrica de PCM16, `-32768` é válido): isso seria clipping **introduzido pelo backend**
(ganho acima de 0 dB: o `play()` e o `set_dsp` não limitam o ganho). O H4 reprova fonte acima de 16 bits e mistura
inaudível; o clipping de soma é **informado**, não reprovado. Limite do critério (revisão Codex): ele não detecta um nível
por canal mais alto que o original abaixo de 16 bits; a igualdade de nível está provada **nos registradores** (música:
TL, DIPAN e DISDL por key-on × Flycast; efeitos: as leis de TL por volume e nível-base medidas no O6; falas: TL 0 e DISDL
15 no Flycast), e a lei registrador → ganho (0,375 dB por passo de TL) é a do Flycast (as notas de Corlett dão metade,
divergência aberta da G). **Não há prova de equivalência acústica**: a captura do áudio do Flycast sob o depurador falha
(19.3). Teste `audio_mixer_source_over_vs_sum`.

**H8, `test_saves` com os filmes nativos (2026-10-21).** O roteiro `passo_save_rcg02` (S1, S2, S17) é indexado por passo;
com o vídeo nativo (padrão desde o N5) o passo fica parado nos filmes, que duram o tempo real: NEWGAME 452,8 s (27 146
VBlanks, passo 1038) e E001 13,7 s (828, passo 2146). O orçamento de 17 100 VBlanks (do Sofdec) não chegava ao SAVE nem com
o áudio legado (falha anterior, do N5, não do áudio); agora 46 000 (17 100 + 27 146 + 828, com margem), o roteiro igual. O
detalhe do S2 não quebra mais sem VMS.

### 20.3 O que falta para ativar o áudio nativo por padrão

**Saída do jogo (2026-10-21):** um bloco por VBlank; modo "seguir a saída padrão" (reabre na saída padrão do Windows
quando muda ou cai; fila cheia descarta o bloco; a fila nunca passa do teto de 200 ms, reserva incluída); reserva de silêncio
de 66 ms com a fila vazia (absorve campos atrasados); testes `audio_output_xaudio2_follow_reopen` (saída padrão, relógio e
perda injetados), `_follow_burst` (fila ≤ 200 ms), `_stats`. **Latência conhecida:** com a reserva, o áudio do jogo sai ~66 ms
(mais a fila) depois do relógio lógico; as legendas das falas no gameplay (desenhadas só no N7) terão de compensar a fila
da saída; a sincronia medida na F vale para o relógio lógico.

1. **F**: a gravação do mantenedor até depois da cena do Dogs (18.8) e as rotas de LOAD dela: ducking, sincronia fala ×
   legenda, estado nativo × legado e o oráculo pelo mesmo save; outras músicas por saves mais adiante.
2. **G**: o fechamento formal, depois da F (19.2), com as limitações documentadas.
3. **Bateria H completa aprovada** numa execução sem `-Parcial`, de preferência sem outra carga na máquina (o H9 mede
   tempo real): falta rodar H4 (heliporto e a confirmação da LOAD), H6, H7 com o critério completo, H8 e H9.
4. **Escuta do mantenedor** na janela (qualidade audível, volumes, reverberação): os testes não julgam o som.
5. **Autorização** para a promoção: trocar `kAudioNativeDefault` e o texto de ajuda; `--audio legado` continua como
   fallback até o N8. Builder e Preview não mudam (o áudio nativo lê o disco do usuário).
6. **Fora da H** (registrado): mods de música e de efeitos não estão ligados no jogo (o catálogo resolve, mas só as
   dublagens são usadas); o tempo do download (o mesmo passo 1 a 2 VBlanks antes no nativo) fica para o N2 depois do N7.

### 20.4 Proposta de encerramento condicional da H (2026-10-21)

**Fechado por evidência (não depende de mais nada):** H1, H2, H3, H4 (critério pelo modelo da AICA), H5, H6, H7, H8; a
saída do jogo (um bloco por VBlank, seguir a saída padrão, reabertura, teto de 200 ms, reserva de 66 ms); F e G concluídas.

**Condição 1 — desempenho do N2 (H9):** o H9 (0 faltas de dados na janela) só passa quando o gameplay rodar dentro do tempo
real (hoje ~18,4 ms por campo, 42 % dos campos acima de 16,68 ms, igual com áudio legado; N2-PLANO 11.2). Medições
preservadas: faltas 1 399 → 345 em ~7 200 blocos com a reserva, 0 erros de escrita, 0 perdas, 0 descartes, fila máxima
166 ms, latência adicional de 66 ms + fila. O limite não muda.

**Condição 2 — avaliação auditiva do mantenedor:** os testes não julgam o som. A avaliar na janela: volume relativo de
música, efeitos e falas; reverberação (nível molhado do DSP, não medido contra o console); o clipping de soma nos picos
de combate e falas (do modelo do original, sem confirmação acústica); a latência de 66 ms com as cenas; o picote quando
o jogo cai abaixo do tempo real (N2).

**Proposta:** encerrar a H **condicionalmente**: o áudio nativo pode virar o padrão depois da avaliação auditiva
favorável e da autorização do mantenedor, com o H9 registrado como **pendente do N2** (não aprovado) e reavaliado quando o
N2 for retomado (depois do N7, antes do N8). Até lá o legado continua como fallback (`--audio legado`). A promoção é a troca
de `kAudioNativeDefault` e do texto de ajuda, com o H5 refeito.

### 20.5 Promoção condicional (2026-10-21, autorizada pelo mantenedor)

**O áudio nativo é o padrão:** `kAudioNativeDefault = true` (`app/main_sdl.cpp`): sem opção, efeitos, falas e música são
nativos (com o DSP da AICA). **Fallback até o N8:** `--audio legado` ou `BS_AUDIO=legado` (o SDK de som e o driver Manatee
originais na AICA emulada); `--audio-efeitos/-falas/-musica` valem sobre os dois; precedência: opção específica >
`--audio` > `BS_AUDIO` > padrão. Texto de ajuda atualizado. Legendas en-US continuam ativas por padrão (sem mudança).

**Condicional:** H1–H8 aprovados (20.2); **H9 bloqueado pelo desempenho do N2** (última medição: **345 faltas de dados** em
~7 200 blocos com a reserva de 66 ms; 0 erros de escrita, 0 perdas, 0 descartes; o jogo a ~18,4 ms por campo, igual com o
áudio legado; N2-PLANO 11.2), a reavaliar quando o N2 for retomado (depois do N7). As faltas continuam contadas e informadas
no resumo do jogo (`[audio] mistura: ... faltas de dados N`); nada foi mascarado.

**Verificação dirigida da promoção** (sem repetir H1–H8, por orientação do mantenedor): H5 refeito com o padrão novo (12
verificações: padrão = os três nativos; `--audio legado` e `BS_AUDIO=legado` sem nenhum nativo; misturas; opção sobre o
ambiente; valores inválidos recusados); `test_gameplay` com o padrão nativo 4/4 (NEW GAME até a área jogável, HUD,
movimento, parado sem entrada) e com `BS_AUDIO=legado` 3/3 até ser interrompido pelo mantenedor (roda, HUD, movimento);
unitários de áudio 80, vídeo 45, localização 9, interface 25, normal e ASan.

**Scripts ajustados:** `n6_efeitos.ps1` e `n6_falas.ps1` fixam `--audio legado` antes da opção do provedor testado (com o
padrão nativo, a execução "legado" deles teria os outros provedores nativos e a comparação deixaria de isolar o provedor).

### 20.6 H9 aprovado (2026-10-23, retomada do N2)

Com o **Vulkan como renderizador padrão** (fechamento do N7, `docs/N7-PLANO.md` seção 25), o H9 passou: o gargalo do
gameplay era o rasterizador em software, não o áudio nem o modelo de tempo (`docs/N2-PLANO.md` seção 12).

| Execução (`n6_h.ps1 -Casos H9`, Preview `465b2ed`, RTX 5080) | Trabalho por campo | Campos > 16,68 ms | Faltas de dados |
|---|---|---|---|
| Vulkan (padrão), rodada 1 | 7,18 ms | 10 | **0** em 8 978 blocos |
| Vulkan (padrão), rodada 2 | 7,14 ms | 11 | **0** em 8 977 |
| Vulkan (padrão), rodada 3 | 7,22 ms | 11 | **0** em 8 977 |
| Software (`graficos.toml`), mesmo executável e rota | 17,11 ms | 3 249 | 287 em 7 600 (reprova, como antes) |

Nas três rodadas aprovadas: 0 erros de escrita, 0 perdas do dispositivo, 0 descartes, 0 saturadas; fila máxima 132–149 ms.
O limite não mudou.

**Decisão:** o critério H9 está cumprido na configuração padrão do produto; com H1–H8 já aprovados (20.2), a fase H deixa
de ser condicional e o **N6 fica concluído (100 %)**. O legado continua como fallback (`--audio legado`) até o N8.

Limites registrados (não fazem parte do critério, que é o padrão):
- **recuo para o software** (sem Vulkan): as faltas continuam (287 em 150 s), porque o rasterizador em software custa
  ~15 ms por campo;
- **4K com bloom e tonemapping:** 1 falta em 150 s (9,68 ms por campo, 910 campos acima do orçamento). A thread do jogo
  espera cada render na GPU (5,4–5,8 ms a 4K); ver `docs/N2-PLANO.md` 12.3.
