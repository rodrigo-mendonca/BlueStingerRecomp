#pragma once
// N2a (criterio A5): valores que o jogo le dos relogios consultaveis (TCNT do TMU, SPG_STATUS,
// MDST do Maple, RTC) e dos registradores de estado de interrupcao do Holly (ISTNRM/EXT/ERR,
// onde aparece um evento perdido mesmo sem interrupcao), resumidos por VBlank: quantidade e
// hash de cada tipo. Com o mesmo roteiro, --tempo legado e --tempo nativo tem de dar o mesmo
// arquivo.
// BS_CLOCK_LOG=<arquivo> liga; desligado custa um teste de bool por leitura.
#include <cstdint>
#include <string>

namespace bs {

extern bool g_clock_log;

enum class ClockRead : uint8_t { Tmu, Spg, Mdst, Rtc, Ist, Count };

void clock_log_read(ClockRead kind, uint32_t value);
void clock_log_vblank(uint64_t vblank);  // fecha a linha do VBlank
void clock_log_init(const std::string& path);  // vazio desliga

} // namespace bs
