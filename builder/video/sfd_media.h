#pragma once
// N5 (docs/N5-PLANO.md): leitura dos filmes do disco para a conversao do Builder. O SFD (Sofdec) e
// um MPEG-1 Program Stream com o video MPEG-1 no stream 0xE0 e o audio ADX no stream 0xC0. Aqui
// so o demux e o decodificador ADX (formato publico: blocos de 18 bytes, preditor de 2 coeficientes);
// a decodificacao MPEG-1 de referencia e a codificacao em MP4 ficam em outras unidades. Nada disso
// vai para o executavel do jogo: o runtime so le os MP4 convertidos (native/video).
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bs::builder::video {

struct SfdStreams {
    std::vector<uint8_t> video;  // elementary stream MPEG-1 (0xE0), em ordem
    std::vector<uint8_t> audio;  // bytes do ADX (0xC0), em ordem
    uint32_t             packs = 0, videoPes = 0, audioPes = 0;
    int64_t              firstVideoPts = -1, firstAudioPts = -1;  // 90 kHz, -1 sem PTS
};

// Demux de um MPEG-1 Program Stream. Falha (com `err`) em codigo de inicio inesperado ou pacote
// truncado; streams privados, padding e o cabecalho de sistema sao ignorados.
bool demux_sfd(std::span<const uint8_t> file, SfdStreams& out, std::string* err = nullptr);

struct AdxInfo {
    uint8_t  channels = 0, version = 0, encoding = 0, blockSize = 0, bitDepth = 0;
    uint32_t sampleRate = 0, totalSamples = 0;  // amostras por canal
    uint16_t highpass = 0;
    uint32_t dataOffset = 0;  // primeiro bloco
    int32_t  coef1 = 0, coef2 = 0;  // preditor em ponto fixo (>> 12)
};

bool adx_parse_header(std::span<const uint8_t> adx, AdxInfo& info, std::string* err = nullptr);
// PCM 16 bits intercalado, totalSamples por canal (o resto do ultimo bloco e descartado).
bool adx_decode(std::span<const uint8_t> adx, std::vector<int16_t>& pcm, AdxInfo* info = nullptr, std::string* err = nullptr);
// Coeficientes do preditor pela frequencia de corte (os do codificador da CRI).
void adx_coefficients(uint32_t highpass, uint32_t sampleRate, int32_t& coef1, int32_t& coef2);

// Codificador simples (so testes e amostras sinteticas): versao 3, blocos de 18 bytes, corte 500 Hz.
std::vector<uint8_t> adx_encode(std::span<const int16_t> pcm, uint8_t channels, uint32_t sampleRate);

} // namespace bs::builder::video
