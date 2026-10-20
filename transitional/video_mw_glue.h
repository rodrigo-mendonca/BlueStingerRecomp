#pragma once
// N5 (docs/N5-PLANO.md, secao 3.1): a logica da cola entre o guest e o player nativo, sem Sh4Context
// nem runtime. As funcoes de slot (video_adapter.cpp) so traduzem registradores e memoria do guest
// para esta interface; os testes usam uma RAM sintetica.
//
// Contrato com o guest (confirmado no binario e no oraculo O1):
//   create(r4 = parametros): pool estatico do MW, 32 entradas de 120 B a partir de [8C1580B8]+4; a
//     primeira com +4 == 0 e usada: +0 = tabela 8C1DD168, +4 = 1 (em uso). Devolve o endereco, ou 0
//     com o pool cheio (o original tambem). So um player nativo vive por vez (o jogo toca um filme
//     por vez); um segundo create com o primeiro vivo falha (0) e fica registrado.
//   parametro(h, v) / cor base(h, v) / offset(h, v): guardam; nao tocam a memoria do guest.
//   metodos da tabela (+0x14 destroy, +0x18 start, +0x1C stop, +0x20 estado): so para o handle criado
//     aqui; outro handle (o WAV do N6 usa o mesmo pool e a mesma biblioteca) segue para o original.
//   r6/r7 nunca sao lidos (as entradas tem 1 ou 2 argumentos).
// Propriedade: o player (MP4, audio, legendas) pertence ao MwPlayerCore e vive entre start e
// stop/destroy; nada do guest aponta para memoria do host.
#include "video_mw_core.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace bs::transitional::video {

constexpr uint32_t kEntCreate = 0x8C157FC4, kEntParam = 0x8C158D42, kEntBase = 0x8C158D92, kEntOffset = 0x8C158DAE;
constexpr uint32_t kEntDestroy = 0x8C1580E4, kEntStart = 0x8C15814A, kEntStop = 0x8C158250, kEntStat = 0x8C1582C0;
constexpr uint32_t kMwPoolPtr = 0x8C1580B8, kMwVtable = 0x8C1DD168, kMwEntries = 32, kMwEntrySize = 120;

class GuestPort {
public:
    virtual ~GuestPort()                                                    = default;
    virtual std::optional<uint32_t>    load32(uint32_t addr)                = 0;
    virtual bool                       store32(uint32_t addr, uint32_t v)   = 0;
    virtual std::optional<std::string> cstring(uint32_t addr, size_t max)   = 0;
    virtual int64_t                    now100ns()                           = 0;  // relogio logico
};

struct MwArgs {
    uint32_t r4 = 0, r5 = 0;  // as entradas so usam r4 e r5
};
struct MwOutcome {
    bool     forward = false;  // handle de outro player: chamar o original
    uint32_t r0      = 0;
};

using FrameSink = std::function<void(const native::video::VideoFrame&)>;

struct MwAdapterStats {
    uint64_t creates = 0, createFailures = 0, foreign = 0, destroys = 0;
    // +64 (subordinado do decodificador) diferente de zero na entrada nativa: o servidor por handle do MW
    // (8C1589F0 -> 8C1588D8, chamado pelos despachantes 8C1745E6/8C174472 durante o filme) a
    // processaria. Tem que ficar 0 (o create zera a entrada).
    uint64_t serverExposed = 0;
};

class MwAdapter {
public:
    MwAdapter(MwCoreOptions o, FrameSink sink);
    MwOutcome call(uint32_t entry, const MwArgs& a, GuestPort& g);
    bool      playing() const;  // o host mostra o quadro do filme enquanto true
    uint32_t  handle() const { return handle_; }
    const MwPlayerCore&    core() const { return core_; }
    const MwAdapterStats&  stats() const { return stats_; }

private:
    MwOutcome create(GuestPort& g);
    MwPlayerCore   core_;
    FrameSink      sink_;
    uint32_t       handle_ = 0;
    MwAdapterStats stats_;
};

} // namespace bs::transitional::video
