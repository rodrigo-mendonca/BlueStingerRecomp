# N3-1: núcleo nativo de entrada

Data: 2026-10-18. Autorização do mantenedor: **N3-1 apenas** (plano em `docs/N3-PLANO.md`, seção 4;
N3-0 em `docs/N3-0-RELATORIO.md`). Base: `3a3fc92` mais o N3-0 (sem commit). O N3-2 (provedor `pd`
nativo) não foi iniciado. O modo padrão dos controles não mudou: o jogo continua lendo pelo Maple
legado, que agora tira a amostra do núcleo nativo e só traduz o estado.

## 1. O que entrou

| Peça | Onde | Papel |
|---|---|---|
| API nativa | `native/platform/input.h` | `ControllerState` (12 botões próprios, 6 eixos em float, `connected`), `ActionMap` (ligações de teclado e controle, opções), `InputSystem` (dispositivos, jogador 1, latch, eventos de conexão), conversões dos backends e zona morta radial. Nada conhece o protocolo do console. |
| Mapa de ações | `native/platform/input_map.cpp` | Mapa padrão (o mesmo teclado de antes; controle por posição Xbox), arquivo TOML do usuário via toml++ (`versao`, `[opcoes]`, `[teclado]`, `[controle]`; ação listada substitui o padrão dela; erro com linha e motivo; BOM e CRLF aceitos), texto padrão comentado, formato antigo do `--map`. |
| Núcleo | `native/platform/input_system.cpp` | Teclado da janela, controles pelo SDL3 Gamepad (nomes do SDL), XInput de reserva (leitor injetável para testes), escolha do controle (preferido por nome, troca automática pelo último com atividade, primeiro conectado), conexão/desconexão (solta tudo do controle), junção teclado + controle, latch sob mutex (thread da janela × thread do jogo), fonte de teste exclusiva. |
| Ponte da fase híbrida | `transitional/input_bridge.{h,cpp}` | Fonte do runtime que tira a amostra do `InputSystem` no GET_CONDITION do Maple (ponto do disparo do DMA, plano 3.1); conversões exatas eixo/gatilho float ↔ byte (ida e volta iguais nos 256 valores, neutro = 0x80); bits `PadButton` ↔ `Button`; roteiros e gameplay assistido viram fonte de teste do núcleo. |
| Presença sem amostra | `runtime/input` (`InputSource::connected`), `runtime/maple`, ponte | Os comandos do Maple que não leem o estado (DEVICE_INFO, ALL_STATUS...) só perguntam a presença; a amostra é uma por GET_CONDITION (revisão do Codex). Para roteiros, o padrão é o de `read`: mesmo resultado de antes. |
| Integração no app | `app/main_sdl.cpp` | Toda entrada passa pelo núcleo: janela (teclado + SDL3 Gamepad; `backend = "xinput"` ou falha do subsistema de controles → XInput), roteiro (`BS_INPUT_SCRIPT`), gameplay assistido, `BS_INPUT=host` (diagnóstico headless: teclado do Windows + XInput, como antes). `controles.toml` na pasta do usuário, criado na primeira execução; arquivo inválido → aviso e mapa padrão. Perder o foco solta só o teclado. Eventos de conexão no console. |
| Diagnóstico | `BS_CONTROLE_VIRTUAL=<roteiro>` (`app/main_sdl.cpp`) | Controle virtual do SDL3 (`SDL_AttachVirtualJoystick`) que conecta, desconecta e aciona botões/eixos em instantes fixos: o caminho real SDL → núcleo → ponte → Maple sem hardware. Desligado por padrão. |
| toml++ no produto | `CMakeLists.txt` raiz (antes só `translator/`), `native/CMakeLists.txt` | O jogo lê o arquivo de controles; `bs_native` liga `tomlplusplus::tomlplusplus` e `xinput`. |
| toml++ no Builder | `builder/builder_core.{h,cpp}`, `tools/dist/make_builder.ps1` | `toml_dir` (kit: `third_party/tomlplusplus`; repositório: ao lado das fontes do SDL3), `check_kit` exige as fontes, cópia para o cache quando o caminho não é ANSI ou é longo, `-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS`, carimbo do build incremental, licença `tomlplusplus-MIT.txt`. O kit leva o toml++ sem testes, documentação e exemplos (1,3 MB). |
| toml++ no projeto exportado | `builder/project_export.{h,cpp}` | `third_party/tomlplusplus/` no projeto, `compilar.cmd` com o diretório local (offline), `check_project` exige as fontes, LEIA-ME atualizado. |
| Testes | `tests/unit/test_input_native.cpp` (8), `test_builder.cpp`, `tests/n0/native_api_check.cpp` | Seção 3. |

## 2. Decisões e diferenças de comportamento

- **Caminho legado = referência.** Roteiros e gameplay assistido passam pelo núcleo (fonte de teste
  → `ControllerState` → bytes do Maple). As conversões são exatas nos 256 valores de cada eixo e
  gatilho e preservam o segundo analógico e `connected`, então o Maple recebe os mesmos bytes de
  antes. Isso é verificado pelo K13 do `test_controles` contra o executável de `3a3fc92`.
- **Janela:** antes, `KeyEventInput` + XInput direto; agora o núcleo. O mapa padrão do teclado é o
  mesmo (setas, ENTER, Z/X/A/S, Q/W, I/J/K/L), e com os dois lados apertados prevalecem direita e
  baixo, como antes. Diferenças intencionais:
  - controles pelo SDL3 (Xbox, PlayStation, Switch e genéricos), com XInput só de reserva;
  - conversão linear documentada de eixos (antes `(v + 32768) >> 8`);
  - **sem zona morta do host por padrão**. Antes o XInput aplicava a sua (7849/32767 ≈ 0,24 por
    eixo); o jogo tem a própria (|v| < 20 de 128 ≈ 0,16). Fica configurável (`zona_morta`, radial)
    para controles com *drift*. É a decisão "modo fiel" do plano (3.1, decisão 3).
- **Headless sem roteiro:** antes havia uma fonte (teclado da janela, sempre vazio no headless, mais
  XInput ao vivo); agora nenhuma, e o Maple responde com o estado fixo neutro. O jogo vê o mesmo
  que antes sem controle conectado; só o contador `maple_cond_seq` (diagnóstico) deixa de avançar
  nesse modo. Testes diferenciais deixam de depender de um controle conectado na máquina.
- **Troca de dispositivo:** conectar não é atividade (um controle neutro que chega não toma o
  jogador 1). Atividade = botão novo ou eixo que passa de meio curso, igual no SDL e no XInput.
  O controle desconectado solta tudo; um reconectado entra como novo.
- **Foco:** perder o foco solta o teclado; os controles não, porque o SDL não reenvia o botão
  mantido e sem foco já ignora apertos novos. O caminho anterior (XInput) também não soltava.
- **Gatilho em botão:** `LT = "leftshoulder"` vira gatilho no fundo (255); o gatilho físico deixa
  de valer para o LT.

## 3. Testes

| Teste | O que prova |
|---|---|
| `input_native_conversions_are_exact` | 256 valores de eixo e gatilho ida e volta; neutro 0x80; saturação; 12 botões nos dois sentidos; `connected` |
| `input_native_radial_deadzone` | desligada por padrão; zera dentro; reescala fora |
| `input_native_action_map_and_toml` | padrão; arquivo padrão = mapa padrão; remapeamento (lista, substituição por ação, eixo invertido); erros com linha; BOM/CRLF; `--map` antigo |
| `input_native_keyboard` | botões, gatilhos, analógico no limite, direita/baixo prevalecem; perder o foco solta o teclado e não o controle |
| `input_native_gamepads_and_connection` | botões, eixos, gatilhos; conexão neutra não troca; troca automática; desconexão do ativo; id desconectado ignorado; reconexão; preferido por nome; zona morta; teclado + controle |
| `input_native_xinput_fallback` | leitor injetado: botões, y invertido, gatilhos de 8 bits, conexão/desconexão por slot, troca por botão e por eixo; gatilho no ombro; desconectar com botão apertado |
| `input_native_bridge_and_test_source` | roteiro → núcleo → Maple sem perda; só a porta A; desinstalação |
| `input_native_latch_only_on_get_condition` | DMA com DEVICE_INFO + GET_CONDITION: uma amostra só; presença sem amostra; nomes `misc2`..`misc6` do SDL3 |
| janela, `BS_CONTROLE_VIRTUAL` + `SendInput` | ver 4.3 |

## 4. Resultados

Política de testes do N3 (mantenedor, 2026-10-18): unitários e testes dos componentes durante a
implementação; integração relacionada ao fim do N3-1. A regressão completa fica para o fim do N3.

### 4.1 Builds e unitários
- Builds limpos (`--clean-first`) debug, release e com mods, mais o build de investigação: sem
  erros. O único aviso, C4456 em `native/platform/event_queue.cpp`, é anterior ao N3.
- **229 unitários** (221 do N3-0 + 8 do N3-1); `ctest` sem disco e com disco: 100% (o
  `boot_trace` é pulado por projeto, depende do oráculo do Flycast).

### 4.2 Integração (com o código final, em sequência)
| Suíte | Resultado |
|---|---|
| `make_builder`, `make_preview` | ok; kit com `third_party/tomlplusplus`; Preview com `licencas/tomlplusplus-MIT.txt` |
| `test_builder` | 18/0 (inclusive kit copiado para o cache com caminho fora do ANSI) |
| `test_projeto` | 13/0 (exporta, recompila do projeto sem o CDI, mods, reexportação) |
| `test_preview` | 9/0 (janela abre, roda e fecha; pasta somente leitura → arquivos em `%LOCALAPPDATA%`) |
| `test_gameplay` | 4/0 (NEW GAME até a área 1 pela Preview) |
| `test_controles` | 39/0: contrato, negativos, limiares, conexão, determinismo e paridade com mods; **K13: quadros e leituras iguais ao executável de `3a3fc92`** em `passo_cgate`, `passo_heliporto` e `newgame_area1` |
| K13 nas outras rotas assistidas (`-Casos K13 -RotasK13 passo_combate,passo_load_opcoes`) | 3/0: quadros e leituras iguais a `3a3fc92` em `passo_combate` e `passo_load_opcoes`; traço ligado não muda os quadros |
| `test_assistido` | 28/0 (quadros, leituras, cargas, áudio e estado por passo iguais entre `--tempo nativo` e `experimental`; negativos acusados) |

**Projeto exportado, offline:** projeto gerado pelo `bs-builder --projeto` e compilado só com o
`compilar.cmd` (CMake + Ninja do VS, sem o Builder), com `HTTP(S)_PROXY`/`ALL_PROXY` inválidos e
`GIT_ALLOW_PROTOCOL=none`. Resultado: código 0, `FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS` =
`third_party/tomlplusplus` do projeto, `FETCHCONTENT_FULLY_DISCONNECTED=ON`, nenhum *subbuild* de
download, e o executável gerado roda 300 VBlanks com os dados do Builder.

### 4.3 Janela, controle virtual e teclado real
Executável release em modo janela, `BS_PAD_LEITURAS` registrando o estado servido em cada
GET_CONDITION. Controle virtual do SDL3 (`BS_CONTROLE_VIRTUAL`) com conexão, botões, eixos e
gatilhos, mais teclas reais injetadas com `keybd_event`, só com a janela do jogo em primeiro plano.
Sequência de estados distintos que o jogo recebeu:

```
- | A | - | ANALOG_LEFT | - | AY=190 | - | RT | RT=127 | - | UP | - | START | - | A+LT+ANALOG_LEFT | -
```

- `a` → A; `leftx −32768` → ANALOG_LEFT (byte 0); `lefty 16000` → AY=190 (0,4883 → 189,76 →
  190); gatilho cheio → RT (255); meio gatilho → 127 (o SDL entrega 16383, e 0,49998 → 127,5 →
  127); `dpup` → UP.
- **Desconectar com `dpup` apertado solta**; reconectar e START funcionam.
- Teclado: Z+Q+J → A+LT+ANALOG_LEFT.
- Com `controles.toml` remapeado (A↔B no controle, `A = "SPACE"` no teclado,
  `zona_morta = 0.5`):
  `- | B | - | ANALOG_LEFT | - | RT | RT=127 | - | UP | - | START | - | LT+ANALOG_LEFT | -`. O `a`
  virou B, o AY=190 caiu na zona morta e Z deixou de ser A.
- O `controles.toml` padrão é criado na primeira execução e lido de volta igual ao mapa padrão.
- Achado do teste: sem foco, o SDL3 descarta eventos de controle (comportamento padrão desejável
  no produto). O diagnóstico liga `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` só para si.

## 5. Revisão do Codex (`reviews/codex-20261018-120344.md`)

Uma rodada; os quatro achados conferidos no código e no SDL3 fixado, todos aceitos e corrigidos:

| Achado | Decisão |
|---|---|
| [MÉDIA] um controle recém-conectado e neutro tomava o jogador 1 (`added()` contava como atividade; os testes tinham cristalizado isso) | aceito: conexão não é atividade; testes reescritos (SDL e XInput) para provar que conexão neutra não troca |
| [MÉDIA] perder o foco limpava o estado dos controles SDL; o SDL não reenvia o botão mantido, que ficava solto até novo aperto | aceito: `release_all` → `release_keyboard` (só o teclado); teste |
| [BAIXA] o latch rodava também em DEVICE_INFO/ALL_STATUS (duas amostras por DMA) | aceito: `InputSource::connected` (presença sem amostra; padrão = `read`, então roteiros dão o mesmo de antes); o Maple só chama `read` no GET_CONDITION; teste com DMA de dois quadros (uma consulta ao XInput) |
| [BAIXA] `misc2`..`misc5` do SDL3 rejeitados no TOML | aceito, com `misc6` (também existe no SDL3 fixado e o Codex não citou); teste |

Nenhum rejeitado. Dos itens que o Codex declarou não verificados, a execução, a compilação offline,
o hotplug e o `BS_CONTROLE_VIRTUAL` foram exercitados na seção 4. A perda de foco (Alt+Tab) só tem
teste unitário.

## 6. Riscos, dependências e recomendação

**Riscos e pendências:**
- **Controles físicos não testados nesta máquina** (nenhum controle conectado; XInput sem
  dispositivo nos 4 slots). O caminho SDL3 foi exercitado com o controle virtual do próprio SDL3, e
  o XInput com o leitor injetado. Falta o teste manual na Preview com um controle Xbox e um
  PlayStation real, que é critério de aceitação do plano.
- **Sem zona morta do host por padrão:** um controle com *drift* acima de ≈ 0,16 move o
  personagem (antes, a zona morta axial do XInput, ≈ 0,24, escondia isso). O usuário tem
  `zona_morta`. Rever no N3-3 com hardware real.
- A curva dos eixos do XInput mudou de `(v + 32768) >> 8` para a regra linear do plano; só o
  caminho com janela é afetado. Roteiros não passam por ela.
- A perda de foco com botão de controle mantido só tem teste unitário.
- Achado lateral, anterior ao N3-1: rodar o `bs-builder` no pwsh com `> arquivo 2>&1` faz a
  verificação `--version` do executável compilado falhar (saída vazia, código 0). Via `cmd /c` e
  pela janela do Builder funciona. Não afeta o usuário da janela. Fica registrado para o Builder.
- O latch continua amarrado ao GET_CONDITION do Maple (marcador temporário até o N2c, plano 3.1).
  O N3-2 o leva para o slot do servidor `pd`.

**Dependências para o N3-2:** a ponte (`transitional/input_bridge`) e as conversões exatas são a
base do `pad_adapter`; `pad_contract` (N3-0) define o que escrever no `PDS_PERIPHERAL`;
`InputSystem::latch` e `connected` são as duas entradas que o adaptador precisa.

**Recomendação:** autorizar o **N3-2** (provedor nativo do `pd` com `--controles nativo`, opcional:
slots em `8C15C4E6`/`8C15C668`, `8C15C10C` original preservado, Maple sem controle nesse modo,
prova de fechamento estática e dinâmica, C1–C5 legado × nativo). Antes ou junto, um teste manual
rápido da Preview com um controle real fecha o critério manual do N3-1.
