// Localizacao (native/localization): idiomas, pacotes de mods, textos traduzidos, legendas novas e layout.
// So dados sinteticos; nada de conteudo do jogo, nada desenhado (o apresentador e do N7).
#include "test.h"

#include "audio/mixer.h"
#include "audio/voice_player.h"
#include "localization/dialogue_bridge.h"
#include "localization/locale.h"
#include "localization/mod_packages.h"
#include "localization/subtitle_presenter.h"
#include "localization/subtitle_service.h"
#include "localization/text_localization.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace nl = bs::native::localization;

namespace {
struct Dir {
    fs::path p = fs::temp_directory_path() / ("bs-loc-" + std::to_string(std::random_device{}()));
    Dir() { fs::create_directories(p); }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
};
void put(const fs::path& f, const std::string& utf8) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary) << utf8;
}
void pkg(const fs::path& mods, const std::string& id, int prio, const std::string& extra = "idiomas = [\"pt-BR\"]\n") {
    put(mods / id / "mod.toml", "formato = 1\nid = \"" + id + "\"\nnome = \"" + id + "\"\nprioridade = " + std::to_string(prio) + "\n" + extra);
}
constexpr int64_t S = 10'000'000;  // 1 s em 100 ns
} // namespace

BS_TEST(loc_languages_ids_utf8) {
    BS_CHECK(nl::valid_language("pt") && nl::valid_language("pt-BR") && nl::valid_language("zh-Hant-TW") && nl::valid_language("es-419"));
    BS_CHECK(!nl::valid_language("") && !nl::valid_language("pt_BR") && !nl::valid_language("p") && !nl::valid_language("pt-BRA1") &&
             !nl::valid_language("pt--BR"));
    BS_CHECK(nl::canonical_language("PT-br") == "pt-BR" && nl::canonical_language("zh-hant-tw") == "zh-Hant-TW");
    const auto f = nl::language_fallback("zh-Hant-TW");
    BS_CHECK(f.size() == 3 && f[0] == "zh-Hant-TW" && f[1] == "zh-Hant" && f[2] == "zh");
    BS_CHECK(nl::language_fallback("x_y").empty());
    BS_CHECK(nl::valid_resource_id("voz/e001a") && nl::valid_resource_id("texto/menu.novo_jogo") && nl::valid_resource_id("efeito/bs_snd:sosb0:12"));
    BS_CHECK(!nl::valid_resource_id("Voz/E001A") && !nl::valid_resource_id("voz/../x") && !nl::valid_resource_id("/voz") &&
             !nl::valid_resource_id("voz/") && !nl::valid_resource_id("voz//a") && !nl::valid_resource_id(""));
    BS_CHECK(nl::valid_utf8("ação 日本語 😀") && nl::valid_utf8(""));
    BS_CHECK(!nl::valid_utf8("\xC3") && !nl::valid_utf8("\xC0\xAF") && !nl::valid_utf8("\xED\xA0\x80") && !nl::valid_utf8("\xFF"));
}

// pacotes: ordem por prioridade (empate pelo id), recusa de invalidos/incompativeis/repetidos com motivo,
// caminhos seguros, recuo de idioma (o exato em qualquer pacote vence o recuo)
BS_TEST(loc_mod_packages_priority_and_safety) {
    Dir d;
    pkg(d.p, "base-pt", 10, "idiomas = [\"pt\"]\n");
    pkg(d.p, "fino-br", 50);
    pkg(d.p, "outro-br", 50);
    put(d.p / "futuro" / "mod.toml", "formato = 9\nid = \"futuro\"\n");
    put(d.p / "quebrado" / "mod.toml", "formato = = 1\n");
    put(d.p / "idioma-ruim" / "mod.toml", "formato = 1\nid = \"idioma-ruim\"\nidiomas = [\"pt_BR\"]\n");
    put(d.p / "outra-versao" / "mod.toml", "formato = 1\nid = \"outra-versao\"\njogo = \"T99999X V9\"\n");
    put(d.p / "repetido" / "mod.toml", "formato = 1\nid = \"fino-br\"\n");
    fs::create_directories(d.p / "sem-manifesto");
    const auto m = nl::ModPackages::discover(d.p, "T13001N V1.000");
    BS_CHECK(m.packages().size() == 3);
    BS_CHECK(m.packages()[0].id == "fino-br" && m.packages()[1].id == "outro-br" && m.packages()[2].id == "base-pt");
    BS_CHECK(m.problems().size() == 5);  // futuro, quebrado, idioma-ruim, outra-versao, repetido
    bool fut = false;
    for (const auto& pr : m.problems()) fut |= pr.message.find("mais novo que o suportado") != std::string::npos;
    BS_CHECK(fut);
    put(d.p / "outro-br" / "legendas" / "E001.pt-BR.srt", "1\n00:00:01,000 --> 00:00:02,000\nOi\n");
    put(d.p / "base-pt" / "legendas" / "E001.pt.srt", "x");
    put(d.p / "base-pt" / "legendas" / "E002.pt.srt", "x");
    auto f = m.find("legendas/E001.{idioma}.srt", "pt-BR");
    BS_CHECK(f && f->package->id == "outro-br" && f->language == "pt-BR");
    f = m.find("legendas/E002.{idioma}.srt", "pt-BR");  // so o recuo existe
    BS_CHECK(f && f->package->id == "base-pt" && f->language == "pt");
    BS_CHECK(!m.find("legendas/E003.{idioma}.srt", "pt-BR"));      // ausente: nada (o jogo segue o original)
    BS_CHECK(!m.find("legendas/E001.{idioma}.srt", "en-US"));      // outro idioma nunca entra no lugar
    BS_CHECK(!nl::safe_join(d.p / "fino-br", "../outro-br/mod.toml") && !nl::safe_join(d.p / "fino-br", "C:/Windows/win.ini") &&
             !nl::safe_join(d.p / "fino-br", "/x") && nl::safe_join(d.p / "fino-br", "textos/pt-BR.toml"));
    BS_CHECK(nl::ModPackages::discover(d.p / "nao-existe").packages().empty());  // sem mods: nada muda
}

// textos: exato vence recuo, prioridade no mesmo idioma, IDs/UTF-8 invalidos recusados, codigos especiais
// preservados (traducao que muda os codigos e recusada e o original fica)
BS_TEST(loc_text_localization) {
    Dir d;
    pkg(d.p, "a", 10), pkg(d.p, "b", 20);
    put(d.p / "a" / "textos" / "pt-BR.toml", "[textos]\n\"menu.novo_jogo\" = \"NOVO JOGO (a)\"\n\"menu.carregar\" = \"CARREGAR\"\n\"Menu.Ruim\" = \"x\"\n");
    put(d.p / "b" / "textos" / "pt-BR.toml", "[textos]\n\"menu.novo_jogo\" = \"NOVO JOGO\"\n\"item.chave\" = \"Chave de {cor}azul{/cor}\"\n");
    put(d.p / "a" / "textos" / "pt.toml", "[textos]\n\"menu.opcoes\" = \"OPÇÕES\"\n\"menu.carregar\" = \"(recuo)\"\n");
    const auto m = nl::ModPackages::discover(d.p);
    const auto t = nl::TextLocalization::load(m, "pt-BR");
    BS_CHECK(t.translate("menu.novo_jogo", "NEW GAME") == "NOVO JOGO");   // b tem prioridade maior
    BS_CHECK(t.translate("menu.carregar", "LOAD") == "CARREGAR");          // exato vence o recuo
    BS_CHECK(t.translate("menu.opcoes", "OPTIONS") == "OPÇÕES");           // so no recuo
    BS_CHECK(!t.translate("menu.sair", "EXIT"));                          // sem traducao: o original
    BS_CHECK(t.problems().size() == 1);                                    // "Menu.Ruim"
    const nl::Tokenizer codes = [](std::string_view s) {
        std::vector<std::string> out;
        for (size_t i = s.find('{'); i != std::string_view::npos; i = s.find('{', i + 1)) out.emplace_back(s.substr(i, s.find('}', i) - i + 1));
        return out;
    };
    BS_CHECK(t.translate("item.chave", "Key of {cor}blue{/cor}", codes));          // mesmos codigos
    BS_CHECK(!t.translate("item.chave", "Key of {cor}blue{/cor}{fim}", codes));    // codigo perdido: original
    BS_CHECK(t.problems().size() == 2);
    BS_CHECK(nl::TextLocalization::load(nl::ModPackages::discover(d.p / "vazio"), "pt-BR").size() == 0);
}

// filmes: tempo de MIDIA (a pausa e do player); fim e pulo encerram; sem idioma ou sem arquivo, nada
BS_TEST(loc_subtitles_movie_timeline) {
    Dir d;
    pkg(d.p, "leg", 1);
    put(d.p / "leg" / "legendas" / "E001.pt-BR.srt", "1\n00:00:01,000 --> 00:00:03,000\nOlá\n\n2\n00:00:04,000 --> 00:00:05,000\nあいう\n");
    const auto m = nl::ModPackages::discover(d.p);
    nl::SubtitleService s(m, "pt-BR");
    s.movie_started("E001");
    s.movie_time(S / 2);
    BS_CHECK(s.active().empty());
    s.movie_time(2 * S);
    auto a = s.active();
    BS_CHECK(a.size() == 1 && a[0].text == L"Olá" && a[0].source == nl::ActiveSubtitle::Source::Movie && a[0].id == "E001");
    s.movie_time(2 * S);  // pausa no player: o mesmo tempo, a mesma legenda
    BS_CHECK(s.active().size() == 1);
    s.movie_time(4 * S + S / 2);
    BS_CHECK(s.active().size() == 1 && s.active()[0].text == L"あいう");
    s.movie_skipped();
    BS_CHECK(s.active().empty());
    s.movie_started("E002");  // sem arquivo: nada
    s.movie_time(2 * S);
    BS_CHECK(s.active().empty());
    s.movie_started("E001"), s.movie_time(2 * S), s.movie_ended();
    BS_CHECK(s.active().empty());
    nl::SubtitleService off(m, "pt_BR");  // idioma invalido: desligado
    off.movie_started("E001"), off.movie_time(2 * S);
    BS_CHECK(off.active().empty());
}

// falas no gameplay: tempo LOGICO; a pausa congela; repetida recomeca sem duplicar; interrupcao e troca de cena
// encerram; ID + cena vence a entrada geral; entradas invalidas recusadas com motivo
BS_TEST(loc_subtitles_dialogue_events) {
    Dir d;
    pkg(d.p, "leg", 1);
    put(d.p / "leg" / "legendas" / "falas.pt-BR.toml",
        "[[fala]]\nvoz = \"voz/e001a\"\ninicio = 0.0\nfim = 2.0\ntexto = \"Geral\"\n"
        "[[fala]]\nvoz = \"voz/e001a\"\ncena = \"e001\"\ninicio = 0.5\nfim = 1.5\ntexto = \"Na cena\"\n"
        "[[fala]]\nvoz = \"voz/e002\"\ninicio = 1.0\ntexto = \"Ate o fim\"\n"
        "[[fala]]\nvoz = \"Voz/Ruim\"\ntexto = \"x\"\n"
        "[[fala]]\nvoz = \"voz/e003\"\ninicio = 2.0\nfim = 1.0\ntexto = \"tempos ao contrario\"\n");
    const auto m = nl::ModPackages::discover(d.p);
    nl::SubtitleService s(m, "pt-BR");
    BS_CHECK(s.problems().size() == 2);
    const int64_t t0 = 100 * S;
    s.dialogue_started("voz/e001a", "e001", t0);
    s.logical_time(t0 + S);
    auto a = s.active();
    BS_CHECK(a.size() == 1 && a[0].text == L"Na cena" && a[0].source == nl::ActiveSubtitle::Source::Dialogue);
    s.paused(true, t0 + S);  // 1 s de pausa: a fala nao anda
    s.logical_time(t0 + 2 * S);
    BS_CHECK(s.active().size() == 1);
    s.paused(false, t0 + 2 * S);
    s.logical_time(t0 + 2 * S + S / 4);  // 1,25 s de fala
    BS_CHECK(s.active().size() == 1);
    s.logical_time(t0 + 3 * S);          // 2 s de fala: passou do fim (1,5 s)
    BS_CHECK(s.active().empty());
    s.dialogue_started("voz/e001a", "outra", t0 + 3 * S);  // sem a cena: a geral
    s.dialogue_started("voz/e001a", "outra", t0 + 3 * S);  // repetido: recomeca, nao duplica
    s.logical_time(t0 + 3 * S + S / 2);
    a = s.active();
    BS_CHECK(a.size() == 1 && a[0].text == L"Geral");
    s.dialogue_interrupted("voz/e001a");
    BS_CHECK(s.active().empty());
    s.dialogue_started("voz/e002", "", t0 + 4 * S);
    s.logical_time(t0 + 100 * S);  // sem fim: ate a fala acabar
    BS_CHECK(s.active().size() == 1);
    s.scene_changed("e002");
    BS_CHECK(s.active().empty());
    s.dialogue_ended("voz/inexistente");  // evento solto: ignorado
    s.dialogue_started("voz/sem-legenda", "", t0);
    BS_CHECK(s.active().empty());
}

// 2026-10-23: partes consecutivas da mesma fala (falas separadas por silencio) nunca aparecem juntas nem deixam resto; o
// personagem e opcional e vem so do pacote; cada dialogue_started e uma ocorrencia nova (a mesma voz de novo tambem)
BS_TEST(loc_subtitles_parts_speaker_instances) {
    Dir d;
    pkg(d.p, "leg", 1, "idiomas = [\"en-US\", \"pt-BR\"]\n");
    put(d.p / "leg" / "legendas" / "falas.en-US.toml",
        "[[fala]]\nvoz = \"voz/teste1\"\ninicio = 0.0\nfim = 1.78\ntexto = \"Primeira frase do teste.\"\npersonagem = \"Locutor A\"\n"
        "[[fala]]\nvoz = \"voz/teste1\"\ninicio = 2.07\nfim = 4.04\ntexto = \"Segunda frase, depois do silencio.\"\n"
        "[[fala]]\nvoz = \"voz/teste2\"\ninicio = 0.0\nfim = 1.0\ntexto = \"Sem silencio\"\n"
        "[[fala]]\nvoz = \"voz/teste2\"\ninicio = 1.0\nfim = 2.0\ntexto = \"Logo depois\"\n");
    const auto m = nl::ModPackages::discover(d.p);
    nl::SubtitleService s(m, "en-US");
    BS_CHECK(s.problems().empty() && s.has_speakers());
    const int64_t t0 = 10 * S;
    s.dialogue_started("voz/teste1", "", t0);
    s.logical_time(t0 - S / 6);  // o audivel ainda antes do inicio (a fila da saida): nem a linha que comeca em 0
    BS_CHECK(s.active().empty());
    s.logical_time(t0 + S);
    auto a = s.active();
    BS_CHECK(a.size() == 1 && a[0].text == L"Primeira frase do teste." && a[0].speaker == L"Locutor A" && a[0].line == 0);
    const uint64_t inst = a[0].instance;
    s.logical_time(t0 + 19 * S / 10);  // o silencio entre as partes: nada na tela
    BS_CHECK(s.active().empty());
    s.logical_time(t0 + 3 * S);
    a = s.active();
    BS_CHECK(a.size() == 1 && a[0].line == 1 && a[0].speaker.empty() && a[0].instance == inst);  // sem personagem: desconhecido
    s.dialogue_started("voz/teste2", "", t0 + 5 * S);  // a fala seguinte (o player corta a anterior)
    s.dialogue_interrupted("voz/teste1");
    s.logical_time(t0 + 5 * S + S / 2);
    a = s.active();
    BS_CHECK(a.size() == 1 && a[0].text == L"Sem silencio" && a[0].instance != inst);
    s.logical_time(t0 + 6 * S);  // sem intervalo: a primeira sai no mesmo instante em que a segunda entra
    a = s.active();
    BS_CHECK(a.size() == 1 && a[0].text == L"Logo depois");
    s.dialogue_started("voz/teste2", "", t0 + 7 * S);  // a mesma voz de novo: ocorrencia nova
    s.logical_time(t0 + 7 * S + S / 2);
    BS_CHECK(s.active().size() == 1 && s.active()[0].instance > a[0].instance);
    nl::SubtitleService sem(m, "pt-BR");
    BS_CHECK(!sem.has_speakers());
}

namespace {
struct FakeMeasure final : nl::ITextMeasurer {
    float width(std::u32string_view t, float px) override { return float(t.size()) * px * 0.5f; }
    float line_height(float px) override { return px * 1.2f; }
};
} // namespace

// layout: escala pela altura do conteudo, quebra por palavras, japones sem espacos por caractere, limite de
// linhas, area 4:3 numa tela 16:9, sobe acima do HUD e nunca sai da area segura
BS_TEST(loc_subtitle_layout) {
    FakeMeasure       fm;
    nl::SubtitleStyle st;
    const nl::Rect    content{240, 0, 1440, 1080};  // 4:3 centralizado numa tela 1920x1080
    auto L = nl::layout_subtitle(nl::utf8_to_u32("Olá mundo"), content, st, {}, fm);
    BS_CHECK(L.lines.size() == 1 && L.px > 48 && L.px < 49 && L.box.x >= content.x && L.box.x + L.box.w <= content.x + content.w);
    BS_CHECK(L.box.y + L.box.h <= content.y + content.h * (1 - st.safeMargin) + 0.01f);
    std::string longo;
    for (int i = 0; i < 40; ++i) longo += "palavra ";
    L = nl::layout_subtitle(nl::utf8_to_u32(longo), content, st, {}, fm);
    BS_CHECK(L.truncated && int(L.lines.size()) == st.maxLines);
    for (const auto& l : L.lines) BS_CHECK(fm.width(l.substr(0, l.size() - (l.size() > 3 && l.substr(l.size() - 3) == U"..." ? 3 : 0)), L.px) <= content.w * 0.9f + 0.01f);
    std::string jp;  // 63 caracteres sem espaco: ~1530 px na medida falsa, mais que a linha (1296 px)
    for (int i = 0; i < 3; ++i) jp += "これはスペースのない長い日本語の文章です。";
    L = nl::layout_subtitle(nl::utf8_to_u32(jp), content, st, {}, fm);
    BS_CHECK(L.lines.size() == 2 && fm.width(L.lines[0], L.px) <= content.w * 0.9f);  // quebrou por caractere
    const nl::Rect hud[] = {{240, 900, 1440, 180}};  // barra do HUD embaixo
    L = nl::layout_subtitle(nl::utf8_to_u32("Acima do HUD"), content, st, hud, fm);
    BS_CHECK(!L.box.intersects(hud[0]) && L.box.y >= content.h * st.safeMargin);
    st.scale = 2;
    BS_CHECK(nl::layout_subtitle(U"x", content, st, {}, fm).px > 2 * 48);
}

// N6 fase E: as falas do player nativo (VoicePlayer, mixer real) chegam as legendas pela ponte, no tempo logico do
// mixer. A legenda acompanha o audio: numa dublagem mais longa continua depois do fim que o jogo ve e acaba no fim
// fisico; o stop antes do fim a encerra; a fala ausente nao mostra nada.
BS_TEST(loc_dialogue_bridge_voice_player) {
    namespace na = bs::native::audio;
    Dir d;
    pkg(d.p, "leg", 1);
    put(d.p / "leg" / "legendas" / "falas.pt-BR.toml",
        "[[fala]]\nvoz = \"voz/e001a\"\ninicio = 0.0\ntexto = \"Ola\"\n"
        "[[fala]]\nvoz = \"voz/e002a\"\ninicio = 0.0\ntexto = \"Dublagem\"\n");
    const auto         m = nl::ModPackages::discover(d.p);
    nl::SubtitleService subs(m, "pt-BR");
    na::AudioMixer      mixer(44100, 8);
    nl::DialogueBridge  bridge(subs, 44100);
    int64_t             logicalEnd = -1;
    uint32_t            frames     = 4410;  // 0,1 s
    na::VoicePlayerOptions o;
    o.prepSteps = 1;
    o.endSteps  = 0;
    o.onEvent = [&](const na::DialogueEvent& e) { bridge.on_event(e); };
    na::VoicePlayer vp(
        mixer,
        [&](std::string_view n) {
            na::VoiceSource s;
            if (n == "E999.WAV") return s;
            auto d   = std::make_shared<na::SoundData>();
            d->rate  = 44100;
            d->pcm.assign(frames, 100);
            s.data = d;
            s.logicalEnd = logicalEnd;
            s.id         = n == "E001A.WAV" ? "voz/e001a" : "voz/e002a";
            return s;
        },
        o);
    auto advance = [&](size_t n) {
        std::vector<int16_t> out(2 * n);
        mixer.render(out);
        for (const auto& e : mixer.take_events(mixer.now())) vp.on_mixer_event(e);
        bridge.logical_frame(mixer.now());
    };
    vp.start("E001A.WAV");
    BS_CHECK(subs.active().empty());  // preparando: ainda nada
    vp.game_step();  // fim do preparo: Started
    advance(100);
    auto a = subs.active();
    BS_CHECK(a.size() == 1 && a[0].id == "voz/e001a" && a[0].text == L"Ola");
    advance(4310);  // fim da fala
    BS_CHECK(subs.active().empty() && vp.status() == na::kVoiceEnd);
    vp.stop();
    // dublagem 3x mais longa: o jogo ve o fim em 0,1 s, a legenda segue o audio ate 0,3 s
    frames = 13230, logicalEnd = 4410;
    vp.start("E002A.WAV");
    vp.game_step();
    advance(4410);
    BS_CHECK(vp.status() == na::kVoiceEnd && subs.active().size() == 1);
    vp.stop();  // o jogo segue; a cauda continua (PlayOut)
    advance(4410);
    BS_CHECK(subs.active().size() == 1);
    advance(4410);
    BS_CHECK(subs.active().empty());
    // interrupcao e ausente
    frames = 44100, logicalEnd = -1;
    vp.start("E001A.WAV");
    vp.game_step();
    advance(100);
    BS_CHECK(subs.active().size() == 1);
    vp.stop();
    BS_CHECK(subs.active().empty());
    vp.start("E999.WAV");
    BS_CHECK(vp.status() == na::kVoiceError && subs.active().empty());
}

// Legendas EN (docs/LEGENDAS-EN-PLANO.md): o pacote GERADO (BS_LEGENDAS=<pasta de trabalho>; fora do Git) carrega no
// parser do jogo sem problemas; a fala E002A, tocada pelo player nativo e pela ponte, mostra as linhas do pacote nos
// tempos certos (relativos ao inicio da fala, nao a sessao); o filme NEWGAME tem legendas no tempo de midia.
BS_TEST(loc_mod_legendas_en_gerado) {
    const char* trab = std::getenv("BS_LEGENDAS");
    if (!trab) return;
    namespace na = bs::native::audio;
    const auto pacotes = nl::ModPackages::discover(fs::path(trab) / "mod");
    BS_CHECK(pacotes.problems().empty());
    nl::SubtitleService subs(pacotes, "en-US");
    BS_CHECK(subs.problems().empty());
    for (const auto& p : subs.problems()) std::printf("  %s\n", p.c_str());
    na::AudioMixer         mixer(44100, 8);
    nl::DialogueBridge     bridge(subs, 44100);
    na::VoicePlayerOptions o;
    o.onEvent = [&](const na::DialogueEvent& e) { bridge.on_event(e); };
    na::VoicePlayer vp(mixer, [](std::string_view) {
        na::VoiceSource s;
        auto            d = std::make_shared<na::SoundData>();
        d->rate           = 44100;
        d->pcm.assign(size_t(20.883 * 44100), 1);  // a duracao do E002A.WAV
        s.data = d, s.id = "voz/e002a";
        return s;
    }, o);
    auto avancar = [&](double seg) {
        std::vector<int16_t> out(2 * size_t(seg * 44100));
        mixer.render(out);
        for (const auto& e : mixer.take_events(mixer.now())) vp.on_mixer_event(e);
        bridge.logical_frame(mixer.now());
    };
    avancar(37.5);  // a fala comeca num instante qualquer da sessao
    vp.start("E002A.WAV");
    for (int i = 0; i < 3; ++i) vp.game_step();
    BS_CHECK(subs.active().empty() || subs.active()[0].id == "voz/e002a");
    avancar(1.0);
    auto a = subs.active();
    BS_CHECK(a.size() == 1 && a[0].text.find(L"Eliot Ballade") != std::wstring::npos);  // a 1a linha, com o nome do jogo
    avancar(3.5);  // 4,5 s: a 2a linha
    a = subs.active();
    BS_CHECK(a.size() == 1 && a[0].text.find(L"Janine") != std::wstring::npos);
    vp.stop();  // interrompida: a legenda some
    BS_CHECK(subs.active().empty());
    subs.movie_started("NEWGAME");
    subs.movie_time(int64_t(144.0 * 10000000));  // 2:24, dentro da 1a linha do filme
    a = subs.active();
    BS_CHECK(a.size() == 1 && a[0].source == nl::ActiveSubtitle::Source::Movie);
    subs.movie_ended();
    BS_CHECK(subs.active().empty());
}

// Legendas em ingles por padrao (2026-10-21): o idioma padrao en-US acha um pacote feito para "en" (recuo) e o
// manifesto marca o conteudo nao revisado (provisorio = true: as legendas por IA); sem a chave, nao provisorio
BS_TEST(loc_mod_provisional_and_en_us_default) {
    Dir d;
    pkg(d.p, "legendas-en", 10, "idiomas = [\"en\"]\nprovisorio = true\n");
    pkg(d.p, "revisado", 5, "idiomas = [\"en\"]\n");
    put(d.p / "legendas-en" / "legendas" / "falas.en.toml", "x");
    const auto m = nl::ModPackages::discover(d.p, "T13001N V1.000");
    BS_CHECK(m.packages().size() == 2 && m.packages()[0].provisional && !m.packages()[1].provisional);
    const auto f = m.find("legendas/falas.{idioma}.toml", "en-US");
    BS_CHECK(f && f->package->id == "legendas-en" && f->language == "en");
}
