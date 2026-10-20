#pragma once
// N5 (docs/N5-PLANO.md, secao 3.1): provedor nativo das entradas MW (Sofdec) que a funcao de video do
// jogo (8C08B544) chama. A funcao do jogo continua a original (estados, START, flags, retornos por
// construcao); os slots abaixo atendem o player:
//   8C157FC4 create  (mesmo pool estatico do MW: 32 x 120 B em [8C1580B8]+4, +0 = tabela 8C1DD168)
//   8C158D42 parametro, 8C158D92 cor base, 8C158DAE cor de offset (fade)
//   metodos da tabela: +0x14 destroy 8C1580E4, +0x18 start 8C15814A, +0x1C stop 8C158250,
//                      +0x20 estado 8C1582C0
// Os metodos sao compartilhados com outros players da biblioteca (o WAV do N6 usa o mesmo pool): um
// handle que nao foi criado aqui segue para o original. Nucleo sem Sh4Context: video_mw_core.h.
#include "boundary_registry.h"
#include "video_mw_glue.h"

#include <functional>
#include <string>

namespace bs::transitional::video {

// FrameSink (video_mw_glue.h): recebe o quadro do filme (com legendas) a cada consulta de estado do
// jogo; o host o publica no lugar do quadro do PVR enquanto playing().
void        install(BoundaryRegistry& registry, MwCoreOptions o, FrameSink sink);
void        uninstall(BoundaryRegistry& registry);
bool        installed();
bool        playing();  // um filme nativo em andamento (o host mostra o quadro dele)
std::string summary();  // linha de estatisticas para o log de saida

} // namespace bs::transitional::video
