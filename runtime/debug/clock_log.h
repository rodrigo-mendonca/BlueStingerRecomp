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

// Aud (N2b-0): escritas da CPU na AICA (registradores e RAM de som; endereco e valor).
// AudRd (oraculo de audio): leituras da CPU na RAM de som (endereco e valor), p.ex. o estado do
// driver que a biblioteca de som consulta antes de mandar comandos.
enum class ClockRead : uint8_t { Tmu, Spg, Mdst, Rtc, Ist, Aud, AudRd, Count };

void clock_log_read(ClockRead kind, uint32_t value);
void clock_log_vblank(uint64_t vblank);  // fecha a linha do VBlank
void clock_log_init(const std::string& path);  // vazio desliga

} // namespace bs
