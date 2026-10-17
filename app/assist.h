#pragma once
// Gameplay assistido (so desenvolvimento): BS_ASSISTIDO=<pasta> com --headless. O jogo pausa entre
// VBlanks e obedece a comandos acrescentados em <pasta>/comandos.txt (uma linha cada); responde em
// <pasta>/respostas.txt e, ao pausar, grava <pasta>/estado.txt ("pronto", comandos atendidos,
// VBlank, passo de jogo, leituras do controle). A entrada vem so dos comandos (roteiro ao vivo
// indexado por passo de jogo) e a sessao fica em <pasta>/sessao.txt, um roteiro "indice passo"
// que reproduz a mesma partida com BS_INPUT_SCRIPT, em qualquer modo de tempo.
// Nao emula nada: so entrega o estado do controle, captura o quadro exibido e le a RAM do guest.
// Cliente: tools/dev/assistido.py. Comandos:
//   pad <estado>         botoes a partir do proximo passo ("UP+A", "RT", "-" = solto); recusado
//                        enquanto um roteiro carregado tiver entradas futuras
//   passos <n>           roda n passos de jogo e pausa;  vblanks <n>: n VBlanks
//   ate_passo <n>        roda ate o passo n
//   roteiro <arquivo>    acrescenta as linhas de um roteiro "indice passo" (passos futuros)
//   quadro <nome>        grava o quadro exibido em <pasta>/<nome>.png
//   ram <endereco> <n>   n bytes da RAM (hex; 0x8C000000.. ou deslocamento), ate 4096
//   dump <nome>          os 16 MB da RAM em <pasta>/<nome>.bin
//   marca <texto>        comentario na sessao
//   fim                  encerra
#include "game_host.h"

#include <filesystem>
#include <memory>

namespace bs {

class LiveScriptInput;

class Assist {
public:
    // `input`: a fonte do jogo (o runtime e dono dela; tem de viver ate o fim da execucao).
    Assist(std::filesystem::path dir, LiveScriptInput* input);
    ~Assist();
    bool onVblank(const GameOptions::VblankView& v);  // GameOptions::vblankHook

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bs
