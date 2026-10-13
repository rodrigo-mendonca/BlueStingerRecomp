#pragma once
// Embaralhamento de executaveis de boot por CD (MIL-CD / self-boot).
// O 1ST_READ.BIN de um GD-ROM nao e embaralhado; alguns rips CDI sao.
#include <cstdint>
#include <vector>

namespace bs::disc {

std::vector<uint8_t> descramble(const std::vector<uint8_t>& in);
std::vector<uint8_t> scramble(const std::vector<uint8_t>& in);  // inversa (usada em testes)

} // namespace bs::disc
