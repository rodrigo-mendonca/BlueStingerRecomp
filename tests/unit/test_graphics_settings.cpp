// N7 (docs/N7-PLANO.md secao 17): modelo das configuracoes graficas, sem interface nem renderizador: presets,
// classificacao, capacidades da GPU (recuo seguro), confirmacao com reversao, persistencia.
#include "test.h"

#include <cmath>

#include "platform/graphics_settings.h"
#include "platform/user_settings.h"

#include <filesystem>
#include <fstream>

namespace st = bs::native::settings;
namespace fs = std::filesystem;

BS_TEST(graphics_presets_and_classification) {
    BS_CHECK(st::preset(st::Preset::Original) == st::GraphicsSettings{});
    for (st::Preset p : {st::Preset::Original, st::Preset::Balanced, st::Preset::High}) BS_CHECK(st::classify(st::preset(p)) == p);
    st::GraphicsSettings s = st::preset(st::Preset::Balanced);
    s.window = st::WindowMode::Fullscreen, s.vsync = false;  // do jogador: o preset continua
    BS_CHECK(st::classify(s) == st::Preset::Balanced);
    s.bloom = true;  // uma opcao isolada: personalizado
    BS_CHECK(st::classify(s) == st::Preset::Custom);
    BS_CHECK(st::internal_height(st::GraphicsSettings{}) == 480);
    s.resolution = st::Resolution::P2160, s.scale_pct = 50;
    BS_CHECK(st::internal_height(s) == 1080);
    // o Original nao tem nenhum efeito
    const st::GraphicsSettings o = st::preset(st::Preset::Original);
    BS_CHECK(!o.bloom && !o.ssao && !o.tonemap && !o.crt && o.sharpen == 0 && !o.lighting && o.msaa == 1 &&
             o.filter == st::Filter::Original && o.shadows == st::Shadows::Original && o.shader.empty());
}

BS_TEST(graphics_clamp_to_capabilities) {
    st::GraphicsSettings s = st::preset(st::Preset::High);
    s.aspect = st::Aspect::Wide16x9;
    std::vector<std::string> notes;
    st::Capabilities none;  // sem Vulkan: so o original
    st::GraphicsSettings r = st::clamp_to(s, none, &notes);
    BS_CHECK(st::classify(r) == st::Preset::Original && r.aspect == st::Aspect::Original4x3 && !notes.empty());
    // fechamento do N7: Vulkan e o padrao; sem dispositivo, o recuo e o software
    BS_CHECK(st::GraphicsSettings{}.vulkan && !r.vulkan && !st::clamp_to(st::GraphicsSettings{}, none).vulkan);
    BS_CHECK(st::from_toml("[tela]\nrenderizador = \"software\"\n").vulkan == false && st::from_toml("").vulkan);
    st::Capabilities gpu;
    gpu.vulkan = true, gpu.max_msaa = 2, gpu.max_anisotropy = 8, gpu.max_height = 1080, gpu.features = st::Features::all();
    notes.clear();
    r = st::clamp_to(s, gpu, &notes);
    BS_CHECK(r.msaa == 2 && r.anisotropy == 8 && st::internal_height(r) <= 1080 && r.resolution == st::Resolution::P1080);
    BS_CHECK(r.aspect == st::Aspect::Original4x3 && notes.size() >= 3);
}

// O que o renderizador ainda nao implementa volta ao original (o menu o mostra indisponivel), com o motivo
BS_TEST(graphics_unimplemented_features_fall_back) {
    st::GraphicsSettings s = st::preset(st::Preset::High);
    s.bloom = true, s.window = st::WindowMode::Fullscreen;
    st::Capabilities gpu;
    gpu.vulkan = true, gpu.max_msaa = 8, gpu.max_anisotropy = 16, gpu.max_height = 2160;  // so o modo fiel existe
    std::vector<std::string> notes;
    st::GraphicsSettings r = st::clamp_to(s, gpu, &notes);
    BS_CHECK(st::classify(r) == st::Preset::Original && r.window == st::WindowMode::Windowed && !r.bloom);
    BS_CHECK(notes.size() >= 5);
    gpu.features.window_modes = true;  // implementado: fica
    r = st::clamp_to(s, gpu);
    BS_CHECK(r.window == st::WindowMode::Fullscreen);
}

BS_TEST(graphics_confirmation_rule) {
    st::GraphicsSettings a, b = a;
    b.filter = st::Filter::Bilinear, b.bloom = true;  // aplicaveis na hora
    BS_CHECK(!st::requires_confirmation(a, b));
    b.resolution = st::Resolution::P1080;
    BS_CHECK(st::requires_confirmation(a, b));
    b = a, b.window = st::WindowMode::Borderless;
    BS_CHECK(st::requires_confirmation(a, b) && st::kConfirmSeconds == 15);
    // aplicacao: efeitos no proximo quadro; tamanho, modo de tela, MSAA e V-Sync recriam alvos/swapchain
    b = a, b.filter = st::Filter::Bilinear, b.bloom = true;
    BS_CHECK(st::apply_kind(a, a) == st::Apply::None && st::apply_kind(a, b) == st::Apply::Immediate);
    b = a, b.msaa = 4;
    BS_CHECK(st::apply_kind(a, b) == st::Apply::Recreate && !st::requires_confirmation(a, b));
    b = a, b.window_w = 1920, b.window_h = 1080;
    BS_CHECK(st::apply_kind(a, b) == st::Apply::Recreate && st::requires_confirmation(a, b));
}

BS_TEST(graphics_toml_round_trip_and_invalid_values) {
    st::GraphicsSettings s = st::preset(st::Preset::High);
    s.window = st::WindowMode::Borderless, s.vsync = false, s.gamma = 1.25f, s.shader = "teste";
    s.aspect = st::Aspect::Auto, s.window_w = 1280, s.window_h = 960, s.vulkan = true;
    std::string warn;
    BS_CHECK(st::from_toml(st::to_toml(s), &warn) == s && warn.empty());
    // valor desconhecido: o padrao no campo, com aviso; fora da faixa: para a faixa
    const st::GraphicsSettings r =
        st::from_toml("[imagem]\nresolucao = \"8k\"\nescala = 900\nmsaa = 3\n[tela]\nmodo = \"janela\"\n", &warn);
    BS_CHECK(r.resolution == st::Resolution::Original && r.scale_pct == 200 && r.msaa == 1 && !warn.empty());
    warn.clear();
    BS_CHECK(st::from_toml("isto nao e toml [", &warn) == st::GraphicsSettings{} && !warn.empty());
    // preset nomeado como base
    BS_CHECK(st::from_toml("preset = \"equilibrado\"\n") == st::preset(st::Preset::Balanced));
    // arquivo: gravacao atomica e leitura; ausente = Original
    const fs::path dir = fs::temp_directory_path() / "bs_graficos_teste";
    fs::remove_all(dir);
    fs::create_directories(dir);
    BS_CHECK(st::read_graphics(dir) == st::GraphicsSettings{});
    BS_CHECK(st::write_graphics(dir, s));
    BS_CHECK(st::read_graphics(dir) == s && !fs::exists(dir / L"graficos.toml.tmp"));
    fs::remove_all(dir);
}

// Audio e legendas (preferencias do menu): padroes, faixas, idioma invalido, legibilidade, arquivo
BS_TEST(user_settings_audio_and_subtitles) {
    std::string warn;
    st::AudioSettings a;
    a.master = 80, a.music = 50, a.effects = 100, a.voices = 0;
    BS_CHECK(st::audio_from_toml(st::to_toml(a), &warn) == a && warn.empty());
    BS_CHECK(a.gain_music() == 0.4f && a.gain_voices() == 0.0f);
    const st::AudioSettings b = st::audio_from_toml("geral = 300\nmusica = -5\n");
    BS_CHECK(b.master == 100 && b.music == 0 && b.effects == 100);
    st::SubtitleSettings s;
    BS_CHECK(s.enabled && s.language == "en-US" && s.effective_language() == "en-US");  // ingles por padrao
    s.enabled = false, s.language = "pt-BR", s.size_pct = 150, s.box = true, s.speaker = true;
    BS_CHECK(st::subtitles_from_toml(st::to_toml(s), &warn) == s && warn.empty() && s.effective_language().empty());
    const st::SubtitleSettings r = st::subtitles_from_toml("idioma = \"../x\"\ntamanho = 999\ncontorno = false\n", &warn);
    BS_CHECK(r.language == "en-US" && r.size_pct == 200 && r.outline && !warn.empty());  // sem contorno nem fundo: contorno
    BS_CHECK(!r.speaker && !st::SubtitleSettings{}.speaker);  // identificacao do personagem: desligada por padrao
    const fs::path dir = fs::temp_directory_path() / "bs_prefs_teste";
    fs::remove_all(dir);
    fs::create_directories(dir);
    bool present = true;
    BS_CHECK(st::read_subtitles(dir, nullptr, &present) == st::SubtitleSettings{} && !present);
    BS_CHECK(st::write_subtitles(dir, s) && st::write_audio(dir, a));
    BS_CHECK(st::read_subtitles(dir, nullptr, &present) == s && present && st::read_audio(dir) == a);
    fs::remove_all(dir);
}

// N7c (secao 21): efeitos de imagem. O Original nao tem nenhum; os aprimorados ligam a correcao de cor; brilho,
// contraste e os parametros do shader de mod vao e voltam pelo arquivo; sem o recurso no renderizador, tudo volta ao
// neutro (e os parametros do shader somem).
BS_TEST(graphics_post_effects) {
    const st::GraphicsSettings o = st::preset(st::Preset::Original);
    BS_CHECK(!o.color && o.shader.empty() && o.brightness == 0 && o.contrast == 1.0f && o.gamma == 1.0f && o.saturation == 1.0f);
    BS_CHECK(st::preset(st::Preset::Balanced).color && st::preset(st::Preset::High).color);
    st::GraphicsSettings s;
    s.color = true, s.brightness = 0.1f, s.contrast = 1.2f, s.saturation = 0.5f, s.shader = "cor-quente";
    s.shader_params = {{"temperatura", 0.75f}, {"vinheta", 0.25f}};
    s.tonemap = true, s.exposure = 0.5f, s.highlights = 0.8f, s.tone_shadows = -0.2f;
    BS_CHECK(st::from_toml(st::to_toml(s)) == s);
    BS_CHECK(st::classify(s) == st::Preset::Custom);
    st::Capabilities none;
    none.vulkan = true;  // sem Features: os efeitos nao existem
    const st::GraphicsSettings c = st::clamp_to(s, none);
    BS_CHECK(!c.color && c.brightness == 0 && c.contrast == 1.0f && c.saturation == 1.0f && c.shader_params.empty());
    BS_CHECK(!c.tonemap && c.exposure == 0 && c.highlights == 0.5f && c.tone_shadows == 0);
    // faixas
    BS_CHECK(st::from_toml("[efeitos]\ncor = true\nbrilho = 9.0\ncontraste = 0.1\n").brightness == 0.5f);
    // CRT: so com resolucao interna de 1080p ou mais (desligado com o motivo)
    st::Capabilities all;
    all.vulkan = true, all.max_height = 2160, all.features = st::Features::all();
    st::GraphicsSettings crt;
    crt.crt = true;
    std::vector<std::string> notes;
    BS_CHECK(!st::clamp_to(crt, all, &notes).crt && !notes.empty() && notes.back().find("CRT") != std::string::npos);
    crt.resolution = st::Resolution::P1080;
    BS_CHECK(st::clamp_to(crt, all).crt);
    // nan e inf (validos no TOML) nao chegam aos shaders: o padrao do campo (revisao Codex 2026-10-22)
    const st::GraphicsSettings n = st::from_toml(
        "[efeitos]\nexposicao = nan\nbrilho = inf\nbloom_raio = -nan\ncrt_mascara = nan\n[shader_parametros]\nx = nan\ny = 0.5\n");
    const st::GraphicsSettings def;
    BS_CHECK(n.exposure == def.exposure && n.brightness == def.brightness && n.bloom_radius == def.bloom_radius &&
             n.crt_mask == def.crt_mask && !n.shader_params.count("x") && n.shader_params.at("y") == 0.5f);
    st::GraphicsSettings m;
    m.gamma = std::nanf(""), m.bloom_strength = INFINITY;
    const st::GraphicsSettings mc = st::clamp_to(m, all);
    BS_CHECK(mc.gamma == def.gamma && mc.bloom_strength == def.bloom_strength);
}
