#include "video/movie_build.h"

#include "util/hash.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <toml++/toml.hpp>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace bs::builder::video {

namespace fs = std::filesystem;

const char* const kMovieNames[17] = {"OPENING.SFD", "NEWGAME.SFD", "E017.SFD",  "E001.SFD",  "E107.SFD",   "E179.SFD",
                                     "E180.SFD",    "E185A.SFD",   "E185B.SFD", "E193.SFD",  "E195.SFD",   "E200A.SFD",
                                     "E200B.SFD",   "ENDING.SFD",  "E016.SFD",  "E134.SFD",  "ENDING2.SFD"};

namespace {

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

std::string fileSha(const fs::path& p) {
    std::ifstream     f(p, std::ios::binary);
    util::Sha256      sha;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        sha.update(buf.data(), size_t(f.gcount()));
    }
    return sha.hexdigest();
}

struct Entry {
    uint32_t      id = 0;
    std::string   original, file, sourceSha, mp4Sha;
    bool          absent = false, unconverted = false;
    ConvertResult c;
    Fidelity      f;
};

// manifesto anterior (para reaproveitar): original -> (fonte, mp4, conversor)
std::map<std::string, toml::table> previous(const fs::path& manifest, std::string& converter) {
    std::map<std::string, toml::table> out;
    std::error_code                    ec;
    if (!fs::exists(manifest, ec)) return out;
    try {
        const toml::table t = toml::parse_file(manifest.wstring());
        converter           = t["conversor"].value_or(std::string());
        if (const auto* arr = t["filme"].as_array())
            for (const auto& n : *arr)
                if (const auto* e = n.as_table())
                    if (auto o = (*e)["original"].value<std::string>()) out[*o] = *e;
    } catch (const toml::parse_error&) {
    }
    return out;
}

std::string q(const std::string& s) { return "\"" + s + "\""; }

// Troca atomica que substitui o destino (o std::filesystem::rename nao substitui no Windows; revisao
// Codex 2026-10-19). O destino valido so some quando o novo ja esta inteiro no lugar.
bool replaceFile(const fs::path& from, const fs::path& to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

} // namespace

bool clear_movies(const fs::path& outDir, std::string* err) {
    std::error_code ec;
    fs::remove_all(outDir, ec);  // pacotes anteriores ficam intactos: tem hard links proprios
    if (ec) return fail(err, "nao foi possivel apagar " + outDir.string() + " (" + ec.message() + ")");
    return true;
}

bool build_movies(const fs::path& bsdata, const fs::path& outDir, const MovieBuildOptions& o, MovieBuildResult& r, std::string* err) {
    r = {};
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) return fail(err, "nao foi possivel criar " + outDir.string());
    std::string prevConverter;
    const auto  prev = previous(outDir / "manifest.toml", prevConverter);
    std::vector<Entry> entries;
    for (uint32_t id = 0; id < 17; ++id) {
        if (o.cancel && o.cancel->load()) return fail(err, "cancelado");
        Entry e;
        e.id = id, e.original = kMovieNames[id];
        const fs::path src = bsdata / e.original;
        if (!fs::is_regular_file(src, ec)) {
            e.absent = true;
            r.absent.push_back(std::to_string(id) + " " + e.original);
            entries.push_back(e);
            continue;
        }
        if (o.progress) o.progress(double(id) / 17.0, e.original);
        e.sourceSha = fileSha(src);
        e.file      = fs::path(e.original).stem().string() + ".mp4";
        const fs::path mp4 = outDir / e.file;
        // reaproveita: mesma fonte, mesmo conversor e o MP4 intacto
        if (const auto it = prev.find(e.original); it != prev.end() && prevConverter == kConverterTag &&
                                                    it->second["fonte_sha256"].value_or(std::string()) == e.sourceSha &&
                                                    fs::is_regular_file(mp4, ec) &&
                                                    it->second["sha256"].value_or(std::string()) == fileSha(mp4)) {
            const toml::table& t = it->second;
            e.mp4Sha             = t["sha256"].value_or(std::string());
            e.c.frames           = uint32_t(t["quadros"].value_or(int64_t(0)));
            e.c.fps              = t["fps"].value_or(0.0);
            e.c.width            = uint32_t(t["largura"].value_or(int64_t(0)));
            e.c.height           = uint32_t(t["altura"].value_or(int64_t(0)));
            e.c.videoEnd100ns    = t["fim_video_100ns"].value_or(int64_t(0));
            e.c.audioEnd100ns    = t["fim_audio_100ns"].value_or(int64_t(0));
            e.c.audioSamples     = uint32_t(t["amostras_audio"].value_or(int64_t(0)));
            e.f.audioDelaySamples = int32_t(t["atraso_audio_amostras"].value_or(int64_t(0)));
            e.f.psnrMin = t["psnr_min"].value_or(0.0), e.f.psnrAvg = t["psnr_med"].value_or(0.0);
            e.f.ssimMin = t["ssim_min"].value_or(0.0), e.f.ssimAvg = t["ssim_med"].value_or(0.0);
            e.f.audioSnr = t["snr_audio"].value_or(0.0);
            ++r.reused;
            entries.push_back(e);
            continue;
        }
        const fs::path tmp = outDir / (e.file + ".tmp.mp4");
        fs::remove(tmp, ec);
        std::string cerr;
        if (!r.encoderUnavailable.empty() || !convert_sfd(src, tmp, o.conv, e.c, &cerr)) {
            // sem o codificador do Windows nenhum filme converte: registra uma vez e segue sem filmes
            const bool noEncoder = !r.encoderUnavailable.empty() || cerr.find("Media Foundation indisponivel") != std::string::npos ||
                                   cerr.find("codificador H.264 indisponivel") != std::string::npos ||
                                   cerr.find("codificador AAC indisponivel") != std::string::npos;
            if (!noEncoder) return fail(err, e.original + ": " + cerr);
            if (r.encoderUnavailable.empty()) {
                r.encoderUnavailable = cerr;
                if (o.log) o.log("filmes NAO convertidos: " + cerr + " (o jogo funciona sem eles; instale o Media Feature Pack e gere de novo)");
            }
            fs::remove(tmp, ec);
            e.unconverted = true;
            entries.push_back(e);
            continue;
        }
        if (!verify_conversion(src, tmp, e.f, err)) return fail(err, e.original + ": " + (err ? *err : ""));
        if (e.f.framesMp4 != e.f.framesRef) return fail(err, e.original + ": o MP4 tem " + std::to_string(e.f.framesMp4) + " quadros, a fonte " + std::to_string(e.f.framesRef));
        if (!replaceFile(tmp, mp4)) return fail(err, "nao foi possivel gravar " + mp4.string());
        e.mp4Sha = fileSha(mp4);
        if (o.log) {
            char b[256];
            std::snprintf(b, sizeof b, "filme %s: %u quadros, PSNR med %.2f min %.2f dB, SSIM med %.4f, audio atraso %d amostras SNR %.1f dB",
                          e.original.c_str(), e.c.frames, e.f.psnrAvg, e.f.psnrMin, e.f.ssimAvg, e.f.audioDelaySamples, e.f.audioSnr);
            o.log(b);
        }
        ++r.converted;
        entries.push_back(e);
    }
    // manifesto (gravado por troca: o catalogo nunca ve um manifesto pela metade)
    std::ostringstream m;
    m << "# Gerado pelo Blue Stinger Builder a partir do disco do usuario (docs/N5-PLANO.md). Nao versionar.\n"
      << "versao = 1\nconversor = " << q(kConverterTag) << "\n";
    for (const Entry& e : entries) {
        m << "\n[[filme]]\nid = " << e.id << "\noriginal = " << q(e.original) << "\n";
        if (e.absent) {
            m << "ausente = true  # nao existe no disco: o jogo original trata a ausencia\n";
            continue;
        }
        if (e.unconverted) {  // o catalogo le como ausente; um mod ainda pode fornece-lo
            m << "ausente = true  # nao convertido: sem codificador de video no Windows\nfonte_sha256 = " << q(e.sourceSha) << "\n";
            continue;
        }
        char b[1024];
        std::snprintf(b, sizeof b,
                      "arquivo = \"%s\"\nquadros = %u\nfps = %.6f\nlargura = %u\naltura = %u\nfim_video_100ns = %lld\n"
                      "fim_audio_100ns = %lld\namostras_audio = %u\natraso_audio_amostras = %d\npsnr_min = %.3f\npsnr_med = %.3f\n"
                      "ssim_min = %.5f\nssim_med = %.5f\nsnr_audio = %.2f\nsha256 = \"%s\"\nfonte_sha256 = \"%s\"\n",
                      e.file.c_str(), e.c.frames, e.c.fps, e.c.width, e.c.height, static_cast<long long>(e.c.videoEnd100ns),
                      static_cast<long long>(e.c.audioEnd100ns), e.c.audioSamples, e.f.audioDelaySamples, e.f.psnrMin,
                      e.f.psnrAvg, e.f.ssimMin, e.f.ssimAvg, e.f.audioSnr, e.mp4Sha.c_str(), e.sourceSha.c_str());
        m << b;
    }
    const fs::path man = outDir / "manifest.toml", mtmp = outDir / "manifest.toml.tmp";
    {
        std::ofstream f(mtmp, std::ios::binary | std::ios::trunc);
        f << m.str();
        if (!f) return fail(err, "nao foi possivel gravar " + mtmp.string());
    }
    if (!replaceFile(mtmp, man)) return fail(err, "nao foi possivel gravar " + man.string());
    if (o.progress) o.progress(1.0, "filmes prontos");
    return true;
}

} // namespace bs::builder::video
