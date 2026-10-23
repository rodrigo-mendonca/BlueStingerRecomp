#pragma once
// SubtitleService (docs/LOCALIZACAO-PLANO.md, secao 4): legendas NOVAS (o Blue Stinger original nao tem
// legendas), so de mods, sobre filmes e sobre falas do gameplay. So decide QUE texto esta ativo e QUANDO; nao
// desenha (ISubtitlePresenter, N7) e nao interfere na logica do jogo: recebe eventos e tempos, nunca os produz.
//   Filmes: tempo de MIDIA do player do N5 (100 ns, ja parado na pausa); fim ou pulo encerram as legendas.
//   Falas:  tempo LOGICO do jogo (100 ns); a pausa congela o tempo decorrido da fala; fim, interrupcao e troca de
//           cena encerram. A mesma voz pode ter legendas diferentes por cena (ID + cena; sem a cena, a geral).
// Fontes (pacotes de mods_packages.h, idioma com recuo):
//   legendas/<FILME>.{idioma}.srt|.vtt   (o nome original do filme, p.ex. E001; o parser e o do N5)
//   legendas/falas.{idioma}.toml:
//     [[fala]]
//     voz    = "voz/e001a"     # ID estavel do recurso de voz (o mesmo da dublagem)
//     cena   = "e001"          # opcional: contexto; vence a entrada sem cena
//     inicio = 0.0             # segundos desde o inicio da fala
//     fim    = 2.5             # segundos; omitido = ate o fim da fala
//     texto  = "..."
//     personagem = "Eliot"   # opcional: quem fala, so com fonte confiavel (o menu pode mostrar; nunca por suposicao)
// Portavel (o parser SRT/WebVTT e o do N5, native/video/subtitles.h).
#include "localization/mod_packages.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bs::native::localization {

struct TimedLine {
    int64_t      start100ns = 0, end100ns = -1;  // -1: ate o fim (da fala ou do filme)
    std::wstring text;
    std::wstring speaker;  // "personagem" (opcional; vazio = desconhecido)
};

struct ActiveSubtitle {
    enum class Source : uint8_t { Movie, Dialogue } source;
    std::string  id;    // o filme (nome original) ou a voz
    std::wstring text;
    std::wstring speaker;        // quem fala, quando o pacote diz (vazio = desconhecido)
    uint64_t     instance = 0;   // a ocorrencia da fala (cada dialogue_started e uma nova); 0 nos filmes
    size_t       line     = 0;   // a linha (ou parte) dentro da fala
};

class SubtitleService {
public:
    // Carrega as legendas das falas no idioma; as dos filmes sao lidas quando o filme comeca. Sem pacotes ou
    // sem idioma: nada fica ativo, nunca (o jogo segue como o original).
    SubtitleService(const ModPackages& mods, std::string language);

    // filmes (eventos do player do N5)
    void movie_started(const std::string& name);      // nome original (E001)
    void movie_time(int64_t media100ns);              // tempo de midia atual (a pausa ja o segura)
    void movie_ended();                               // fim natural
    void movie_skipped();                             // pulo pelo jogador

    // falas no gameplay (eventos do provedor de voz do N6, no tempo logico)
    void dialogue_started(const std::string& voice, const std::string& scene, int64_t logical100ns);
    void dialogue_ended(const std::string& voice);
    void dialogue_interrupted(const std::string& voice);
    void logical_time(int64_t logical100ns);
    void paused(bool on, int64_t logical100ns);       // pausa do jogo: o tempo das falas para
    void scene_changed(const std::string& scene);     // encerra as falas em curso

    std::vector<ActiveSubtitle> active() const;
    const std::vector<std::string>& problems() const { return problems_; }
    bool has_speakers() const { return speakers_; }  // alguma fala do idioma diz quem fala
    // a numeracao das ocorrencias continua num servico novo (troca de idioma)
    uint64_t instances() const { return instances_; }
    void     set_instances(uint64_t n) { instances_ = n; }

private:
    struct Dialogue {
        std::string            voice;
        const std::vector<TimedLine>* lines = nullptr;
        int64_t                started = 0, pausedAt = -1, pausedTotal = 0;
        uint64_t               instance = 0;
    };
    int64_t elapsed(const Dialogue& d) const;

    const ModPackages&                                   mods_;
    std::string                                          lang_;
    std::map<std::string, std::vector<TimedLine>>        byVoiceScene_;  // "voz|cena" e "voz|"
    std::vector<TimedLine>                               movieLines_;
    std::string                                          movie_;
    int64_t                                              movieTime_ = 0;
    bool                                                 movieOn_ = false;
    std::vector<Dialogue>                                dialogues_;
    int64_t                                              now_ = 0;
    uint64_t                                             instances_ = 0;
    bool                                                 speakers_  = false;
    std::vector<std::string>                             problems_;
};

} // namespace bs::native::localization
