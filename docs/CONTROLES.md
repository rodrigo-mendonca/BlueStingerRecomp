# Controles do Blue Stinger (Windows)

O jogo lê os controles do arquivo `controles.toml` na pasta do usuário: ao lado do executável ou, se a
pasta for somente leitura, em `%LOCALAPPDATA%\BlueStingerRecomp`, a mesma pasta do `bluestinger.log`.
O arquivo é criado com o mapa padrão na primeira execução. Apague-o para voltar ao padrão. Um arquivo
com erro é ignorado: o jogo avisa (linha e motivo) e usa o mapa padrão.

## Dispositivos

- **Teclado** da janela; sempre presente.
- **Controles** pelo SDL3 Gamepad: Xbox, PlayStation, Switch Pro e genéricos reconhecidos pelo banco
  de mapeamentos do SDL. Os botões têm nomes por posição (`a` é o de baixo, `b` o da direita, `x` o da
  esquerda, `y` o de cima), qualquer que seja a etiqueta do controle.
- **XInput de reserva:** `backend = "xinput"` em `[opcoes]`, ou automaticamente se o subsistema de
  controles do SDL3 não iniciar.
- **Vários controles:** o último com atividade (botão novo ou eixo passando de meio curso) vira o
  jogador 1 (`troca_automatica`), salvo `controle_preferido = "<parte do nome>"`. Conectar um controle
  sem usá-lo não muda o jogador. Desconectar solta tudo o que ele tinha apertado, e o teclado continua
  valendo.
- Perder o foco da janela solta o teclado; os controles não.

## Ações

| Ação | O que é no jogo | Teclado padrão | Controle padrão |
|---|---|---|---|
| `A` `B` `X` `Y` | botões | `Z` `X` `A` `S` | `a` `b` `x` `y` |
| `START` | início / pausa / pular vídeo | `ENTER` | `start` |
| `UP` `DOWN` `LEFT` `RIGHT` | direcional | setas | `dpup` `dpdown` `dpleft` `dpright` |
| `LT` `RT` | gatilhos (o jogo usa como botões: ativos a partir da metade do curso) | `Q` `W` | `lefttrigger` `righttrigger` |
| `ANALOG_UP` `ANALOG_DOWN` `ANALOG_LEFT` `ANALOG_RIGHT` | analógico no limite | `I` `K` `J` `L` | (botões) |
| `ANALOG_X` `ANALOG_Y` | analógico inteiro (só controle; `"-lefty"` inverte) | — | `leftx` `lefty` |
| `ANALOG2_X` `ANALOG2_Y` | segundo analógico (o jogo não usa) | — | — |
| `C` `Z` `D` | botões que o controle original suportava e o jogo não usa | — | — |

- **Teclas válidas:** letras, dígitos, `ENTER`, `SPACE`, `ESC`, `TAB`, `BACKSPACE`, `SHIFT`, `CTRL`,
  setas e `F1`–`F12`. `F11` (tela cheia) e `F12` (captura com `--capture`) são da janela.
- **Botões do controle:** `a b x y start back guide leftstick rightstick leftshoulder rightshoulder
  dpup dpdown dpleft dpright misc1 misc2 misc3 misc4 misc5 misc6 paddle1 paddle2 paddle3 paddle4
  touchpad`.
- **Eixos:** `leftx lefty rightx righty` (−1 a 1) e `lefttrigger righttrigger` (0 a 1). Um gatilho pode
  ir para um botão (`LT = "leftshoulder"`).
- **Mapa de teclado revisado no N3-3:** mantido igual ao anterior. Ele cobre todas as ações que o jogo
  usa, e mudá-lo quebraria quem já usa `--map`.

## Formato

```toml
versao = 1

[opcoes]
backend = "sdl"            # ou "xinput"
zona_morta = 0.0           # 0 a 0.9, radial; 0 = como o jogo original (o jogo tem a sua)
troca_automatica = true
controle_preferido = ""

[teclado]
A = ["Z", "SPACE"]         # texto ou lista; uma ação listada substitui o padrão dela

[controle]
A = "b"                    # as ações não listadas continuam com o padrão
B = "a"

[perfis."030000005e0400008e02000000007200"]   # GUID do controle (o log mostra ao conectar)
nome = "Xbox 360"
zona_morta = 0.15          # opcional
START = "back"             # herda o [controle] e substitui só o que lista

[perfis.xinput]            # controles do XInput de reserva
```

**Trocas:** uma ação listada substitui as ligações *dela*; as outras ações mantêm as suas. Ligar
`A = "x"` sem mudar o `X` faz o botão `x` acionar A e X ao mesmo tempo. Para trocar dois botões, liste
as duas ações (`A = "x"` e `X = "a"`).

**Zona morta:** desligada por padrão, porque o jogo aplica a dele (|v| < 20 de 128). Ligue só se o
controle tiver *drift* (o personagem anda sozinho). Valores típicos: 0,1 a 0,2.

**Perfis:** o `bluestinger.log` mostra, a cada conexão, `conectado: <nome> (GUID <32 dígitos>;
perfil <nome>)` ou `mapa [controle]`. Copie o GUID para `[perfis."<GUID>"]`. Maiúsculas não importam.

## Opção antiga

`--map <arquivo>` com linhas `ACAO = TECLA` substitui o `[teclado]` inteiro.

## Modos

`--controles nativo` (padrão desde o fim do N3) alimenta o SDK de controles do jogo direto, sem passar
pelo Maple. `--controles legado` é o fallback temporário: entrega a entrada pelo Maple emulado, como
até o N3-2. Use-o se algum controle se comportar diferente no padrão, e informe o caso. O mapa e os
perfis valem igual nos dois.
