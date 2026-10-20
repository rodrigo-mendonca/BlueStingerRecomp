#pragma once
// N5 (docs/N5-PLANO.md, secao 4.1; decisoes D1/D4): conversao de um filme do disco (SFD: MPEG-1 +
// ADX) em MP4 (H.264 + AAC) pelo Media Foundation, e a medida da fidelidade contra a referencia.
// - video: pl_mpeg decodifica o MPEG-1; o YUV da fonte (BT.601, faixa de TV) vai direto ao H.264 em
//   NV12, um quadro de saida por quadro original, 30 fps constantes;
// - audio: o ADX decodificado (identico ao ffmpeg) vai ao AAC do Windows (192 kbit/s, o maximo dele);
// - verify_conversion decodifica os dois lados e mede PSNR (RGB) e SSIM (luma) por quadro, o atraso do
//   AAC (priming) e o SNR do audio depois de alinhado.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bs::builder::video {

struct ConvertOptions {
    // H.264 do Windows em modo qualidade 100, o maximo dele (E017: Y 45,7 dB, RGB 40,2 dB contra o
    // pl_mpeg; o modo de bitrate satura em ~2,6 MB e o teto de QP e recusado). quality 0 = bitrate.
    uint32_t videoBitrate = 8'000'000;
    uint32_t quality = 100;
    uint32_t audioBytesPerSecond = 24000;
    bool     testNoEncoder = false;  // testes: simula o Windows sem o codificador H.264 (edicoes N)
};

struct ConvertResult {
    uint32_t width = 0, height = 0, frames = 0;
    double   fps = 0;
    int64_t  videoEnd100ns = 0;  // frames / fps
    uint32_t sampleRate = 0, channels = 0, audioSamples = 0;  // amostras por canal (ADX)
    int64_t  audioEnd100ns = 0;
    std::string encoderNotes;  // controle de taxa nao aplicado etc. (vazio = tudo aplicado)
};

bool convert_sfd(const std::filesystem::path& sfd, const std::filesystem::path& mp4, const ConvertOptions& o,
                 ConvertResult& r, std::string* err = nullptr);

struct Fidelity {
    uint32_t framesRef = 0, framesMp4 = 0;
    double   psnrMin = 0, psnrAvg = 0, ssimMin = 0, ssimAvg = 0;
    uint32_t framesBelow40 = 0;  // quadros com PSNR < 40 dB (objetivo da D4, nao bloqueio)
    // so a perda do H.264: planos YUV do MP4 contra os do pl_mpeg, sem conversao para RGB
    double   psnrYMin = 0, psnrYAvg = 0, psnrUvAvg = 0;
    int32_t  audioDelaySamples = 0;  // atraso do AAC em relacao ao ADX (priming), por canal
    double   audioSnr = 0;  // dB, depois de alinhado
    int64_t  mp4AudioSamples = 0;
};

bool verify_conversion(const std::filesystem::path& sfd, const std::filesystem::path& mp4, Fidelity& f,
                       std::string* err = nullptr);

} // namespace bs::builder::video
