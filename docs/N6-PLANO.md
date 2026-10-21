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

**Ordem do roadmap mantida:** N5 → **N6** → N7 → retorno ao N2 pendente → N8 → N9. O N7 fica preparado na
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
  - **o passo do Eliot (ID 32, 51 vezes): o original dá key-off entre 6 e 15 VBlanks depois do key-on, antes do fim
    da amostra (15,2) na maioria das vezes; o nativo toca a amostra inteira (16).** O key-off da AICA inicia a soltura
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

**Substituição por mods (plano, sem ferramentas do N9).** A música de mod entra no lugar da sequência inteira, não das
amostras: `mods\<pacote>\musica\<NOME>.{wav,flac,ogg}` (NOME = M2A … M99, o nome da tabela 0x8C09EC28), lida pela
mesma `load_sound_file` dos efeitos e das falas (WAV, FLAC e OGG já validados). Laço por metadados: `smpl` no WAV,
`LOOPSTART`/`LOOPLENGTH` (ou `LOOPEND`) nos comentários Vorbis/FLAC, em amostras; sem laço, toca uma vez e a porta fica
livre como no fim da sequência original (0x83). Os comandos do gerenciador continuam valendo para a faixa: volume com
fade (0x05, na mesma lei de TL), pan (0x06), parar (0x02); o estado do jogo (bytes 0x8C1F4C52/0x8C1F4C79, consulta do
driver) não muda com a substituição. Amostras individuais do banco (por programa/camada) ficam para o N9.

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
