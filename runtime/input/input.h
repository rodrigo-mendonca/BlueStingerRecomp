#pragma once
// Entrada do jogador, independente do Maple: estado logico de um controle do Dreamcast
// (botoes, direcional, analogico, gatilhos), vindo de fontes do host (teclado, XInput)
// ou de um roteiro deterministico (testes). O Maple so traduz este estado para o formato
// do barramento (runtime/maple/maple.cpp).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bs {

// Botoes do controle do Dreamcast (mesmos bits da condicao do Maple, em logica positiva:
// 1 = apertado; o Maple inverte para o barramento, que e ativo em 0).
enum PadButton : uint16_t {
    kPadC     = 1u << 0,
    kPadB     = 1u << 1,
    kPadA     = 1u << 2,
    kPadStart = 1u << 3,
    kPadUp    = 1u << 4,
    kPadDown  = 1u << 5,
    kPadLeft  = 1u << 6,
    kPadRight = 1u << 7,
    kPadZ     = 1u << 8,
    kPadY     = 1u << 9,
    kPadX     = 1u << 10,
    kPadD     = 1u << 11,
};

struct PadState {
    uint16_t buttons = 0;              // PadButton, 1 = apertado
    uint8_t  ltrig = 0, rtrig = 0;     // 0 solto .. 255 fundo
    uint8_t  joyx = 0x80, joyy = 0x80; // 0x80 = centro; 0 esquerda/cima, 255 direita/baixo
    uint8_t  joy2x = 0x80, joy2y = 0x80; // segundo analogico (bytes 6-7 da condicao; o controle padrao nao tem)
    bool     connected = true;         // false: a porta fica vazia no Maple (testes de conexao, N3-0)
};

// Fonte de entrada: devolve o estado do controle `port` na leitura numero `seq` (contagem
// de pedidos de condicao feitos pelo jogo a essa porta, a partir de 0).
class InputSource {
public:
    virtual ~InputSource() = default;
    virtual PadState read(int port, uint64_t seq) = 0;
    // Presenca na leitura `seq`, sem tirar amostra: o Maple pergunta isto nos comandos que nao leem
    // o estado (DEVICE_INFO, ALL_STATUS...); so o GET_CONDITION chama read (N3-1). Padrao: o de read.
    virtual bool connected(int port, uint64_t seq) { return read(port, seq).connected; }
};

// Nome de botao <-> bit ("START", "A", "UP", ...); 0 se desconhecido.
uint16_t pad_button_from_name(const std::string& name);

// Roteiro deterministico: linhas "<seq> <botoes|->" (p.ex. "600 START", "610 -"); o
// estado vale a partir daquela leitura ate a proxima linha. Botoes separados por '+'; alem
// dos botoes, LT, RT (gatilhos no fundo) e ANALOG_UP/DOWN/LEFT/RIGHT (analogico no limite).
// Comentarios com '#'. Lanca std::runtime_error com a linha em caso de erro.
// Indice: por padrao, o numero da leitura do controle. A linha "indice passo" (antes dos
// passos) troca para o passo de jogo (input_step): um roteiro por passo vale igual nos modos
// de tempo, porque as cargas nao contam passos (o estavel le o controle mais vezes nelas).
// "indice transicao" (testes do ponto de amostragem, N3-0): o indice e a contagem de
// input_transition_tick(), que o Maple chama no primeiro acesso ao MDST depois de cada VBlank-In:
// dentro do tratador do SDK (8C08EFF8 -> 8C08F332), depois da entrega do VBlank-In e antes do disparo do
// DMA (8C0975C0). E a transicao injetada entre os dois pontos de amostragem possiveis.
std::unique_ptr<InputSource> make_scripted_input(const std::string& text);

// Estado <-> texto do roteiro ("UP+A+RT", "-"). Lanca std::runtime_error para nome invalido.
// Valores exatos (N3-0): AX=n, AY=n (analogico, 0..255, 128 = centro), A2X=n, A2Y=n (segundo
// analogico), LT=n, RT=n (gatilhos, 0..255). "DESCONECTADO" (sozinho): a porta fica vazia. No texto, os extremos continuam com os
// nomes digitais (LT, ANALOG_LEFT...) e os valores intermediarios saem como AX=n etc.
PadState    pad_state_from_text(const std::string& text);
std::string pad_state_text(const PadState& s);

// Transicao injetada (N3-0): avanca o indice dos roteiros "indice transicao" (ver acima).
void     input_transition_tick();
uint64_t input_transition_index();
// Passo de jogo ou `fallback` sem contador (diagnosticos que nao podem lancar).
uint64_t input_step_or(uint64_t fallback);

// Roteiro alimentado durante a execucao (gameplay assistido, so desenvolvimento): o estado
// vale a partir do passo de jogo `step`. Mesma semantica de um roteiro "indice passo", entao a
// sessao gravada se reproduz exatamente.
class LiveScriptInput : public InputSource {
public:
    virtual void push(uint64_t step, const PadState& s) = 0;
};
std::unique_ptr<LiveScriptInput> make_live_script_input();

// Passo de jogo (voltas do laco principal; o host informa o contador do provedor de tempo).
// Sem contador, input_step() lanca: roteiros por passo exigem --tempo nativo ou experimental.
void     input_set_step_counter(uint64_t (*fn)());
uint64_t input_step();

// Teclado do Windows (GetAsyncKeyState) com mapeamento "BOTAO = TECLA" por linha (teclas:
// letras, digitos, ENTER, SPACE, ESC, TAB, SHIFT, CTRL, UP, DOWN, LEFT, RIGHT, F1..F12).
// Texto vazio: mapeamento padrao (ver input.cpp). So responde pela porta 0.
std::unique_ptr<InputSource> make_keyboard_input(const std::string& mapping = "");

// Teclado alimentado por eventos da janela (porta 0): o host informa tecla apertada ou
// solta pelo nome (os mesmos nomes do mapa: letras, digitos, ENTER, SPACE, ESC, TAB, SHIFT,
// CTRL, UP, DOWN, LEFT, RIGHT, BACKSPACE, F1..F12). Ao contrario de make_keyboard_input,
// so ve o que chega a janela (nada com o foco em outro programa). Seguro entre threads:
// eventos na thread da janela, leitura na thread do jogo.
class KeyEventInput : public InputSource {
public:
    virtual void key(const std::string& name, bool down) = 0;
    virtual void releaseAll() = 0;  // p.ex. ao perder o foco
};
std::unique_ptr<KeyEventInput> make_key_event_input(const std::string& mapping = "");

// Primeiro controle XInput conectado (porta 0); sem controle, estado neutro.
std::unique_ptr<InputSource> make_xinput_input();

// Combina fontes: botoes por OU; analogico e gatilhos da primeira fonte fora do neutro.
std::unique_ptr<InputSource> make_combined_input(std::vector<std::unique_ptr<InputSource>> sources);

// Fonte usada pelo Maple (nullptr = nenhum botao apertado). O runtime e dono da fonte.
void          input_set_source(std::unique_ptr<InputSource> src);
InputSource*  input_source();

// Configura a fonte a partir do ambiente: BS_INPUT_SCRIPT=<arquivo> (roteiro, so ele);
// senao teclado (BS_INPUT_MAP=<arquivo> opcional) + XInput, se BS_INPUT=host.
// Sem nada disso, nenhuma fonte (testes diferenciais ficam deterministicos).
void input_configure_from_env();

} // namespace bs
