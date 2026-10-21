// N6: OGG/Vorbis das dublagens e do audio de mods (third_party/xiph/README.md). O unico arquivo que inclui o libvorbis:
// o resto do audio so ve parse_ogg (sound_file.h), e trocar a biblioteca nao muda a interface.
// Entrada nao confiavel: so da memoria (callbacks proprios, com limites), canais/taxa/duracao/PCM conferidos antes de
// decodificar, os mesmos limites do FLAC.
#include "audio/sound_file.h"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace bs::native::audio {

namespace {
struct Memoria {
    const uint8_t* p;
    size_t         n, pos = 0;
};
size_t ler(void* dst, size_t tam, size_t cnt, void* src) {
    auto&        m = *static_cast<Memoria*>(src);
    const size_t q = tam && cnt ? std::min(tam * cnt, m.n - m.pos) : 0;
    std::memcpy(dst, m.p + m.pos, q);
    m.pos += q;
    return tam ? q / tam : 0;
}
int buscar(void* src, ogg_int64_t off, int de) {
    auto&       m    = *static_cast<Memoria*>(src);
    const int64_t base = de == SEEK_SET ? 0 : de == SEEK_CUR ? int64_t(m.pos) : int64_t(m.n);
    const int64_t novo = base + off;
    if (novo < 0 || novo > int64_t(m.n)) return -1;
    m.pos = size_t(novo);
    return 0;
}
long onde(void* src) { return long(static_cast<Memoria*>(src)->pos); }
} // namespace

std::shared_ptr<SoundData> parse_ogg(std::span<const uint8_t> bytes, std::string* err) {
    auto fail = [&](const std::string& m) -> std::shared_ptr<SoundData> {
        if (err) *err = m;
        return nullptr;
    };
    if (bytes.size() > kMaxSoundFileBytes) return fail("OGG grande demais");
    Memoria          mem{bytes.data(), bytes.size()};
    OggVorbis_File   vf;
    const ov_callbacks cb{&ler, &buscar, nullptr, &onde};
    if (ov_open_callbacks(&mem, &vf, nullptr, 0, cb) != 0) return fail("OGG/Vorbis invalido");
    std::unique_ptr<OggVorbis_File, decltype(&ov_clear)> guard(&vf, &ov_clear);
    if (ov_streams(&vf) != 1) return fail("OGG com mais de um fluxo logico (encadeado): sem suporte");
    const vorbis_info* vi = ov_info(&vf, -1);
    if (!vi || vi->channels < 1 || vi->channels > 2) return fail("OGG com canais fora de 1..2");
    if (vi->rate < 8000 || vi->rate > 192000) return fail("OGG com taxa fora de 8000..192000 Hz");
    const ogg_int64_t frames = ov_pcm_total(&vf, -1);
    if (frames <= 0) return fail("OGG sem duracao conhecida ou vazio");
    if (uint64_t(frames) > uint64_t(kMaxSoundSeconds) * uint64_t(vi->rate)) return fail("OGG longo demais");
    if (uint64_t(frames) > kMaxSoundPcmSamples / uint64_t(vi->channels)) return fail("OGG grande demais depois de decodificado");
    auto s      = std::make_shared<SoundData>();
    s->rate     = uint32_t(vi->rate);
    s->channels = uint8_t(vi->channels);
    try {
        s->pcm.resize(size_t(frames) * size_t(vi->channels));
    } catch (const std::bad_alloc&) {
        return fail("OGG: memoria insuficiente");
    }
    size_t lidos = 0;  // em amostras (todos os canais)
    int    sec   = 0, buracos = 0;
    while (lidos < s->pcm.size()) {
        const size_t falta = (s->pcm.size() - lidos) * sizeof(int16_t);
        const long   r     = ov_read(&vf, reinterpret_cast<char*>(s->pcm.data() + lidos), int(std::min<size_t>(falta, 1 << 16)), 0, 2, 1, &sec);
        if (r == 0) break;      // fim do fluxo
        if (r < 0) {            // buraco ou erro no fluxo: o que veio antes vale, o resto nao e inventado
            if (r == OV_HOLE && ++buracos < 64) continue;  // um arquivo forjado nao prende o laco
            break;
        }
        lidos += size_t(r) / sizeof(int16_t);
    }
    if (lidos == 0) return fail("OGG sem amostras decodificaveis");
    s->pcm.resize(lidos - lidos % s->channels);  // arquivo truncado: o que decodificou
    return s;
}

} // namespace bs::native::audio
