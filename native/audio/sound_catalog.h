#pragma once
// N6: catalogo do audio de gameplay por identificador ORIGINAL estavel e por idioma, com mods
// (docs/N6-PLANO.md, secao 4). Portavel.
//
// IDs: "<tipo>/<nome>" com tipo voz, musica ou efeito, e o nome do recurso original ("voz/E001A",
// "musica/M2A", "efeito/BS_SND:SOSB0:12"). Nada de nome improvisado nem de substituir o arquivo do disco.
//
// Resolucao de resolve(id, idioma), nesta ordem (primeiro que existir):
//   1. mods\audio\<idioma>\<id>.<ext>      ("pt-br")
//   2. mods\audio\<idioma base>\<id>.<ext> ("pt")
//   3. mods\audio\<id>.<ext>               (mod sem idioma: musica remasterizada, efeito)
//   4. data\audio\manifest.toml            (o recurso do Builder, a partir do disco do usuario)
// <ext>: wav, flac, ogg (nessa ordem). Um mods\audio\manifest.toml pode declarar a sincronia por ID.
// No nome do arquivo, ':' do ID vira '#' (':' e proibido em nomes do Windows): "efeito/BS_SND:SOSB0:12"
// -> mods\audio\efeito\BS_SND#SOSB0#12.wav. '#' nao e aceito nos IDs (o escape e reversivel).
//
// Sincronia de um mod com outra duracao (dublagem):
//   original: o jogo ve a duracao ORIGINAL (o evento de fim chega quando o original acabaria); o audio
//             do mod que passar disso continua ate acabar, ou ate o jogo parar a voz (padrao para voz);
//   arquivo:  o fim e o do arquivo do mod (o jogo espera por ele) - so onde o evento de fim e do
//             proprio audio (documentado por tipo).
// Nunca acelera nem atrasa o relogio logico do jogo.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::audio {

enum class SoundKind : uint8_t { Voice, Music, Effect };
enum class SyncPolicy : uint8_t { Original, File };

struct SoundEntry {
    std::string           id;
    SoundKind             kind = SoundKind::Effect;
    std::filesystem::path file;  // caminho canonico ja validado (o que foi conferido e o que se abre)
    bool                  mod = false;
    std::string           lang;               // idioma do mod ("" = neutro ou original)
    double                originalSeconds = 0;  // duracao do recurso original (manifesto do Builder)
    SyncPolicy            sync = SyncPolicy::Original;
};

struct SoundCatalogOptions {
    std::filesystem::path dataDir;  // data\audio (manifesto do Builder)
    std::filesystem::path modsDir;  // mods\audio (opcional)
};

class SoundCatalog {
public:
    static SoundCatalog load(const SoundCatalogOptions& o);
    // vazio: nenhum recurso (nem mod nem original)
    std::optional<SoundEntry>       resolve(std::string_view id, std::string_view lang = {}) const;
    const std::vector<std::string>& problems() const { return problems_; }
    static bool                     valid_id(std::string_view id);

private:
    SoundCatalogOptions                o_;
    std::map<std::string, SoundEntry>  original_;
    std::map<std::string, SyncPolicy>  modSync_;
    std::vector<std::string>           problems_;
};

// Quadros (na taxa `rate`) ate o fim que o JOGO deve ver para este recurso: sincronia original = a
// duracao do recurso original (se conhecida); arquivo = a do proprio som. O audio em si toca ate acabar
// (ou ate o jogo pará-lo); so o evento de fim segue a politica.
int64_t logical_end_frames(const SoundEntry& e, int64_t fileFrames, uint32_t fileRate, uint32_t rate);

} // namespace bs::native::audio
