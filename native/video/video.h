#pragma once
// Video (N5). Reproduz as FMVs como filmes nativos: o Builder converte o SFD (MPEG-1 + ADX)
// para um formato que o backend (Media Foundation, decisao do mantenedor, sujeita a
// validacao de formato e fidelidade) decodifica. O tempo segue o relogio do proprio video,
// nao a AICA. Os quadros vao para o renderizador (present_image) e o audio para o motor.
#include "core/handle.h"
#include "core/result.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace bs::native::video {

struct MovieTag;
using MovieHandle = Handle<MovieTag>;

enum class PlayState : uint8_t { Stopped, Playing, Paused, Finished, Failed };

struct MovieInfo {
    uint32_t width = 0, height = 0;
    double   frame_rate = 0;
    double   duration_s = 0;
    bool     has_audio = false;
};

class IVideoPlayer {
public:
    virtual ~IVideoPlayer() = default;
    // Nome logico do filme original (p.ex. "\BSDATA\OPENING.SFD"); o mapeamento para o arquivo
    // convertido fica no manifesto do Builder.
    virtual Result<MovieHandle> open(std::string_view logical_name) = 0;
    virtual Result<MovieInfo>   info(MovieHandle m) const = 0;
    virtual Result<void>        play(MovieHandle m) = 0;
    virtual Result<void>        pause(MovieHandle m, bool paused) = 0;
    virtual void                stop(MovieHandle m) = 0;
    virtual PlayState           state(MovieHandle m) const = 0;
    virtual double              position_s(MovieHandle m) const = 0;
    virtual void                set_volume(MovieHandle m, float v) = 0;
    // Quadro atual (ARGB), valido ate a proxima chamada; vazio se nao mudou.
    virtual std::span<const uint32_t> current_frame(MovieHandle m, uint32_t& width, uint32_t& height) = 0;
    virtual void                      close(MovieHandle m) = 0;
};

} // namespace bs::native::video
