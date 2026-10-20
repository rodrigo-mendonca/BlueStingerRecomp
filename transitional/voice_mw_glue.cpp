#include "voice_mw_glue.h"

namespace bs::transitional::sound {

namespace na = bs::native::audio;

VoiceOutcome VoiceGlue::create(VoiceGuest& g) {
    if (handle_) {  // o jogo cria uma fala por vez; um segundo vivo nao tem player
        ++stats_.createFailures;
        return {false, 0};
    }
    for (uint32_t i = 0; i < kWavEntries; ++i) {
        const uint32_t e   = kWavPool + i * kWavEntrySize;
        const auto     use = g.load32(e + 4);
        if (!use) break;
        if (*use != 0) continue;
        for (uint32_t o = 8; o < kWavEntrySize; o += 4) g.store32(e + o, 0);  // nada do original no resto da entrada
        g.store32(e, kWavVtable), g.store32(e + 4, 1);
        handle_ = e;
        ++stats_.creates;
        return {false, e};
    }
    ++stats_.createFailures;  // pool cheio: o original tambem devolve 0
    return {false, 0};
}

VoiceOutcome VoiceGlue::call(uint32_t entry, uint32_t r4, uint32_t r5, VoiceGuest& g) {
    if (entry == kWavCreate) return create(g);
    if (!handle_ || r4 != handle_) {  // handle de outro player (ou nenhum nativo): o original
        ++stats_.foreign;
        return {true, 0};
    }
    switch (entry) {
    case kWavStart: {
        const auto name = g.cstring(r5, 64);
        player_.start(name ? *name : std::string());
        return {false, 0};
    }
    case kWavStat: return {false, uint32_t(player_.status())};
    case kWavStop: player_.stop(); return {false, 0};
    case kWavDestroy:
        player_.stop();
        g.store32(handle_ + 4, 0), g.store32(handle_, 0);  // a entrada volta livre para o pool
        handle_ = 0;
        ++stats_.destroys;
        return {false, 0};
    case kWavMode:  // 0/1 (a funcao 8C08B8B8 que o chamaria nao tem chamador no jogo)
        ++stats_.modeCalls;
        return {false, 0};
    default: ++stats_.unexpected; return {false, 0};  // servidor por handle e metodos sem uso: tem que ficar 0
    }
}

} // namespace bs::transitional::sound
