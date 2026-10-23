#pragma once
// N7c (docs/N7-PLANO.md secao 17): preferencias do jogador que o menu interno grava, alem das graficas
// (graphics_settings.h) e dos controles (controles.toml, N3). Cada grupo num arquivo da pasta do usuario, o mesmo padrao:
// valor desconhecido volta ao padrao com aviso, fora da faixa vai para a faixa, gravacao atomica.
//   audio.toml    volumes (0 a 100): geral, musica, efeitos e vozes, aplicados por cima do mixer do N6 (o jogo
//                 continua mandando os volumes dele; estes sao a preferencia do jogador, multiplicada no fim).
//   legendas.toml legendas das falas e dos filmes: ligadas (padrao), idioma (padrao en-US; os pacotes de mods
//                 dizem quais existem), tamanho do texto e legibilidade (contorno e/ou fundo).
// Precedencia do idioma: --legendas na linha de comando > legendas.toml > [legendas] do instalacao.toml > en-US.
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace bs::native::settings {

inline constexpr const wchar_t* kAudioFile     = L"audio.toml";
inline constexpr const wchar_t* kSubtitlesFile = L"legendas.toml";

struct AudioSettings {
    uint32_t master = 100, music = 100, effects = 100, voices = 100;  // por cento
    bool     operator==(const AudioSettings&) const = default;
    float    gain_music() const { return master * music / 10000.0f; }
    float    gain_effects() const { return master * effects / 10000.0f; }
    float    gain_voices() const { return master * voices / 10000.0f; }
};

struct SubtitleSettings {
    bool        enabled  = true;
    std::string language = "en-US";  // BCP-47 (o pacote escolhe o arquivo, com recuo en-US -> en)
    uint32_t    size_pct = 100;      // 50 a 200
    bool        outline  = true;     // contorno escuro
    bool        box      = false;    // fundo escuro translucido
    bool        speaker  = false;    // identificacao do personagem ("Nome: texto"), so nas falas em que o pacote diz quem fala
    bool        operator==(const SubtitleSettings&) const = default;
    // o idioma efetivo ("" = desligadas)
    std::string effective_language() const { return enabled ? language : std::string(); }
};

std::string      to_toml(const AudioSettings& s);
AudioSettings    audio_from_toml(const std::string& text, std::string* warning = nullptr);
std::string      to_toml(const SubtitleSettings& s);
SubtitleSettings subtitles_from_toml(const std::string& text, std::string* warning = nullptr);

// Ausente: o padrao (e `present` = false, para a precedencia saber se o jogador ja escolheu).
AudioSettings    read_audio(const std::filesystem::path& dir, std::string* warning = nullptr, bool* present = nullptr);
SubtitleSettings read_subtitles(const std::filesystem::path& dir, std::string* warning = nullptr, bool* present = nullptr);
bool write_audio(const std::filesystem::path& dir, const AudioSettings& s, std::string* err = nullptr);
bool write_subtitles(const std::filesystem::path& dir, const SubtitleSettings& s, std::string* err = nullptr);

// Gravacao atomica (temporario + troca), comum aos arquivos de preferencias.
bool write_text_atomic(const std::filesystem::path& file, const std::string& text, std::string* err = nullptr,
                       std::error_code* code = nullptr);

} // namespace bs::native::settings
