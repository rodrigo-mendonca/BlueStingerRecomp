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
};

// Fonte de entrada: devolve o estado do controle `port` na leitura numero `seq` (contagem
// de pedidos de condicao feitos pelo jogo a essa porta, a partir de 0).
class InputSource {
public:
    virtual ~InputSource() = default;
    virtual PadState read(int port, uint64_t seq) = 0;
};

// Nome de botao <-> bit ("START", "A", "UP", ...); 0 se desconhecido.
uint16_t pad_button_from_name(const std::string& name);

// Roteiro deterministico: linhas "<seq> <botoes|->" (p.ex. "600 START", "610 -"); o
// estado vale a partir daquela leitura ate a proxima linha. Botoes separados por '+'.
// Comentarios com '#'. Lanca std::runtime_error com a linha em caso de erro.
std::unique_ptr<InputSource> make_scripted_input(const std::string& text);

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
