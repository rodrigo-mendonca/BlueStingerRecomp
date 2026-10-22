// N7c (docs/N7-PLANO.md secao 21): validacao dos shaders de pos-processamento (render/post_shader). O passe interno e
// o exemplo de mod passam; SPIR-V corrompido, estagio errado, buffer de armazenamento e push constants grandes demais
// sao recusados com o motivo; o manifesto e conferido (parametros, caminho do shader dentro do pacote).
#include "test.h"

#include "render/post_shader.h"
#include "render/vulkan/vk_device.h"
#include "render/vulkan/spirv/post_color_frag.h"
#include "render/vulkan/spirv/post_exemplo_frag.h"
#include "render/vulkan/spirv/post_teste_buffer_frag.h"
#include "render/vulkan/spirv/post_teste_push_frag.h"
#include "render/vulkan/spirv/post_teste_laco_frag.h"
#include "render/vulkan/spirv/post_teste_3d_frag.h"
#include "render/vulkan/spirv/present_vert.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace nr = bs::native::render;
namespace fs = std::filesystem;

namespace {
template <size_t N> std::vector<uint32_t> words(const uint32_t (&a)[N]) { return std::vector<uint32_t>(a, a + N); }

void write(const fs::path& p, const std::string& text) {
    std::ofstream(p, std::ios::binary) << text;
}
void write_spv(const fs::path& p, const std::vector<uint32_t>& w) {
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size() * 4));
}
const char* kManifest = "formato = 1\nnome = \"teste\"\nshader = \"shader.spv\"\n"
                        "[[parametros]]\nnome = \"a\"\nminimo = 0.0\nmaximo = 1.0\npadrao = 0.5\npasso = 0.1\n";
} // namespace

BS_TEST(post_shader_validator) {
    std::string e;
    BS_CHECK(nr::validate_post_spirv(words(kPostColorFrag), &e));
    BS_CHECK(nr::validate_post_spirv(words(kPostExemploFrag), &e));
    // corrompido e truncado
    auto bad = words(kPostColorFrag);
    bad[0] ^= 1;
    BS_CHECK(!nr::validate_post_spirv(bad, &e) && e.find("magico") != std::string::npos);
    auto cut = words(kPostColorFrag);
    cut.resize(cut.size() / 2);
    BS_CHECK(!nr::validate_post_spirv(cut, &e));
    // um shader de vertice nao e um passe
    BS_CHECK(!nr::validate_post_spirv(words(kPresentVert), &e) && e.find("fragmento") != std::string::npos);
    // escrita em buffer de armazenamento e push constants acima de 80 bytes
    BS_CHECK(!nr::validate_post_spirv(words(kPostTesteBufferFrag), &e));
    std::fprintf(stderr, "  buffer: %s\n", e.c_str());
    BS_CHECK(!nr::validate_post_spirv(words(kPostTestePushFrag), &e) && e.find("push constants") != std::string::npos);
    std::fprintf(stderr, "  push: %s\n", e.c_str());
    // revisao Codex 2026-10-22: instrucao com operandos de menos, laco (pode nao terminar), sampler que nao e 2D
    auto shortw = words(kPostColorFrag);
    shortw.push_back((1u << 16) | 17u);  // OpCapability sem o operando
    BS_CHECK(!nr::validate_post_spirv(shortw, &e) && e.find("operandos") != std::string::npos);
    BS_CHECK(!nr::validate_post_spirv(words(kPostTesteLacoFrag), &e) && e.find("lacos") != std::string::npos);
    BS_CHECK(!nr::validate_post_spirv(words(kPostTeste3dFrag), &e));
    std::fprintf(stderr, "  3d: %s\n", e.c_str());
}

BS_TEST(post_shader_manifest) {
    const fs::path root = fs::temp_directory_path() / "bs_post_teste";
    fs::remove_all(root);
    fs::create_directories(root / "ok"), fs::create_directories(root / "fora"), fs::create_directories(root / "param");
    fs::create_directories(root / "sem_spv"), fs::create_directories(root / "corrompido");
    write(root / "ok" / "manifesto.toml", kManifest), write_spv(root / "ok" / "shader.spv", words(kPostExemploFrag));
    write(root / "fora" / "manifesto.toml", "formato = 1\nshader = \"../ok/shader.spv\"\n");
    write(root / "param" / "manifesto.toml", "formato = 1\n[[parametros]]\nnome = \"x\"\nminimo = 1.0\nmaximo = 0.0\n");
    write(root / "sem_spv" / "manifesto.toml", "formato = 1\n");
    write(root / "corrompido" / "manifesto.toml", kManifest), write_spv(root / "corrompido" / "shader.spv", words(kPostTesteBufferFrag));
    // link para fora do pacote (revisao Codex 2026-10-22): so se o sistema deixar criar links
    std::error_code lk;
    fs::create_directories(root / "link");
    write(root / "link" / "manifesto.toml", kManifest);
    write_spv(fs::temp_directory_path() / "bs_post_fora.spv", words(kPostExemploFrag));
    fs::create_symlink(fs::temp_directory_path() / "bs_post_fora.spv", root / "link" / "shader.spv", lk);
    if (lk) {
        std::fprintf(stderr, "  (sem link simbolico: %s)\n", lk.message().c_str());
        fs::remove_all(root / "link");
    }
    const auto all = nr::discover_post_shaders(root);
    BS_CHECK(all.size() == (lk ? 5u : 6u));
    for (const auto& p : all) std::fprintf(stderr, "  %s: %s\n", p.dir.filename().string().c_str(), p.error.empty() ? "valido" : p.error.c_str());
    for (const auto& p : all) {
        const std::string d = p.dir.filename().string();
        if (d == "ok") BS_CHECK(p.error.empty() && p.name == "teste" && p.params.size() == 1 && p.params[0].def == 0.5f);
        else BS_CHECK(!p.error.empty());
    }
    fs::remove_all(root);
    fs::remove(fs::temp_directory_path() / "bs_post_fora.spv", lk);
}

// A cadeia na GPU (post_run, a mesma gravacao da apresentacao): sem passes a imagem nao muda; o passe interno neutro
// fica a 1 nivel; saturacao 0 da cinza; brilho sobe a media; o shader de exemplo (externo, carregado por post_load)
// esquenta a imagem. Sem Vulkan: [skip].
BS_TEST(post_chain_gpu) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const uint32_t        w = 64, h = 48;
        std::vector<uint32_t> img(size_t(w) * h), out;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                img[size_t(y) * w + x] = 0xFF000000u | ((x * 4) << 16) | ((y * 5) << 8) | ((x + y) * 2);
        auto chan = [](uint32_t c, int s) { return int((c >> s) & 0xFF); };
        auto maxdiff = [&](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
            int m = 0;
            for (size_t i = 0; i < a.size(); ++i)
                for (int s = 0; s < 24; s += 8) m = std::max(m, std::abs(chan(a[i], s) - chan(b[i], s)));
            return m;
        };
        BS_CHECK(d->post_run(img, w, h, {}, out).ok() && out == img);
        const int color = d->post_builtin_color();
        BS_CHECK(color >= 0);
        nr::PostPass p;
        p.shader = color, p.params[0] = 0, p.params[1] = 1, p.params[2] = 1, p.params[3] = 1;  // neutro
        BS_CHECK(d->post_run(img, w, h, {&p, 1}, out).ok() && out.size() == img.size());
        std::fprintf(stderr, "  neutro: maior diferenca %d\n", maxdiff(img, out));
        BS_CHECK(maxdiff(img, out) <= 1);
        p.params[3] = 0;  // saturacao 0
        BS_CHECK(d->post_run(img, w, h, {&p, 1}, out).ok());
        int gray = 0;
        for (uint32_t c : out) gray = std::max({gray, std::abs(chan(c, 16) - chan(c, 8)), std::abs(chan(c, 8) - chan(c, 0))});
        BS_CHECK(gray <= 1);
        p.params[3] = 1, p.params[0] = 0.2f;  // brilho
        BS_CHECK(d->post_run(img, w, h, {&p, 1}, out).ok());
        double before = 0, after = 0;
        for (size_t i = 0; i < img.size(); ++i) before += chan(img[i], 8), after += chan(out[i], 8);
        std::fprintf(stderr, "  brilho +0,2: media do verde %.1f -> %.1f\n", before / img.size(), after / img.size());
        BS_CHECK(after / img.size() > before / img.size() + 40);
        // o exemplo de mod: carregado como um externo (validado antes)
        const auto ex = words(kPostExemploFrag);
        BS_CHECK(nr::validate_post_spirv(ex, &err));
        const int id = d->post_load(ex, &err);
        BS_CHECK(id >= 0 && id != color);
        nr::PostPass q;
        q.shader = id, q.params[0] = 1;  // temperatura maxima, sem matiz e sem vinheta
        BS_CHECK(d->post_run(img, w, h, {&q, 1}, out).ok());
        double r0 = 0, r1 = 0, b0 = 0, b1 = 0;
        for (size_t i = 0; i < img.size(); ++i)
            r0 += chan(img[i], 16), r1 += chan(out[i], 16), b0 += chan(img[i], 0), b1 += chan(out[i], 0);
        std::fprintf(stderr, "  exemplo (temperatura 1): vermelho %.1f -> %.1f, azul %.1f -> %.1f\n", r0 / img.size(), r1 / img.size(),
                     b0 / img.size(), b1 / img.size());
        BS_CHECK(r1 > r0 && b1 < b0);
        // dois passes encadeados (o interno e o externo)
        const nr::PostPass two[2] = {p, q};
        BS_CHECK(d->post_run(img, w, h, two, out).ok() && out.size() == img.size());
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

namespace {
// imagem de teste com tons do preto ao branco (gradiente) e cores
std::vector<uint32_t> ramp(uint32_t w, uint32_t h) {
    std::vector<uint32_t> img(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t v = x * 255 / (w - 1), c = (y * 255 / (h - 1));
            img[size_t(y) * w + x] = 0xFF000000u | (v << 16) | (((v + c) / 2) << 8) | (255 - c) * v / 255;
        }
    return img;
}
double mean_luma(const std::vector<uint32_t>& img) {
    double s = 0;
    for (uint32_t c : img) s += 0.2126 * ((c >> 16) & 0xFF) + 0.7152 * ((c >> 8) & 0xFF) + 0.0722 * (c & 0xFF);
    return s / double(img.size());
}
size_t clipped(const std::vector<uint32_t>& img) {
    size_t n = 0;
    for (uint32_t c : img) n += ((c >> 16) & 0xFF) == 255 || ((c >> 8) & 0xFF) == 255;
    return n;
}
} // namespace

// N7c: tonemapping interno. Neutro = a mesma imagem (1 nivel); exposicao +1 EV clareia; com exposicao +2 a compressao
// das altas luzes corta (satura em 255) muito menos pixels que sem ela; sombras positivas levantam o escuro.
BS_TEST(post_tonemap_gpu) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const uint32_t w = 128, h = 64;
        const auto     img = ramp(w, h);
        std::vector<uint32_t> out, a, b;
        nr::PostPass t;
        t.shader = d->post_builtin(nr::PostBuiltin::Tonemap);
        BS_CHECK(t.shader >= 0);
        t.params[0] = 0, t.params[1] = 0, t.params[2] = 0;
        BS_CHECK(d->post_run(img, w, h, {&t, 1}, out).ok());
        int m = 0;
        for (size_t i = 0; i < img.size(); ++i)
            for (int s = 0; s < 24; s += 8) m = std::max(m, std::abs(int((img[i] >> s) & 0xFF) - int((out[i] >> s) & 0xFF)));
        std::fprintf(stderr, "  neutro: maior diferenca %d\n", m);
        BS_CHECK(m <= 1);
        t.params[0] = 1;  // +1 EV
        BS_CHECK(d->post_run(img, w, h, {&t, 1}, out).ok() && mean_luma(out) > mean_luma(img) + 15);
        t.params[0] = 2, t.params[1] = 0;
        BS_CHECK(d->post_run(img, w, h, {&t, 1}, a).ok());
        t.params[1] = 1;
        BS_CHECK(d->post_run(img, w, h, {&t, 1}, b).ok());
        std::fprintf(stderr, "  +2 EV: pixels cortados sem compressao %zu, com compressao %zu (de %zu)\n", clipped(a), clipped(b), img.size());
        BS_CHECK(clipped(b) * 2 < clipped(a));
        t.params[0] = 0, t.params[1] = 0, t.params[2] = 0.5f;  // sombras
        BS_CHECK(d->post_run(img, w, h, {&t, 1}, out).ok());
        const uint32_t dark = img[size_t(h / 2) * w + w / 16], lifted = out[size_t(h / 2) * w + w / 16];
        BS_CHECK(((lifted >> 16) & 0xFF) > ((dark >> 16) & 0xFF) + 10);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// Custo de GPU de cada passe interno em 4K interno (2880 x 2160), pelos timestamps da cadeia: medido e impresso (o
// criterio do plano e o custo medido por efeito; aqui so se exige que exista e fique abaixo de 4 ms por passe).
BS_TEST(post_cost_4k) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const uint32_t w = 2880, h = 2160;
        const auto     img = ramp(w, h);
        std::vector<uint32_t> out;
        const char* names[] = {"correcao de cor", "tonemapping", "nitidez", "bloom (borrao H)", "bloom (borrao V + soma)", "CRT"};
        for (int b = 0; b < int(nr::PostBuiltin::Count); ++b) {
            nr::PostPass p;
            p.shader = d->post_builtin(nr::PostBuiltin(b));
            if (p.shader < 0) continue;
            p.params[1] = p.params[2] = p.params[3] = 1;  // valores tipicos nao neutros
            p.params[0] = 0.5f;
            if (nr::PostBuiltin(b) == nr::PostBuiltin::BloomBlurV) p.original = -1;
            for (int i = 0; i < 12; ++i) BS_CHECK(d->post_run(img, w, h, {&p, 1}, out).ok());
        }
        const auto t = d->post_timings();
        if (t.empty()) std::fprintf(stderr, "  (sem timestamps neste dispositivo)\n");
        for (const auto& x : t) {
            int b = 0;
            while (b < int(nr::PostBuiltin::Count) && d->post_builtin(nr::PostBuiltin(b)) != x.shader) ++b;
            const double ms = x.ms_total / double(x.passes);
            std::fprintf(stderr, "  %-24s %.3f ms de GPU por passe (4K, %llu passes)\n", b < int(nr::PostBuiltin::Count) ? names[b] : "?", ms,
                         (unsigned long long)x.passes);
            BS_CHECK(ms < 4.0);
        }
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N7c: nitidez. Intensidade 0 = a mesma imagem; numa borda (degrau de cinza) aumenta o contraste dos dois lados sem
// estourar (a adaptacao ao contraste limita o realce); numa area lisa nada muda.
BS_TEST(post_sharpen_gpu) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const uint32_t        w = 64, h = 16;
        std::vector<uint32_t> img(size_t(w) * h), out;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) img[size_t(y) * w + x] = x < w / 2 ? 0xFF505050u : 0xFFA0A0A0u;  // degrau 80 -> 160
        nr::PostPass s;
        s.shader = d->post_builtin(nr::PostBuiltin::Sharpen);
        BS_CHECK(s.shader >= 0);
        s.params[0] = 0;
        BS_CHECK(d->post_run(img, w, h, {&s, 1}, out).ok() && out == img);
        s.params[0] = 1;
        BS_CHECK(d->post_run(img, w, h, {&s, 1}, out).ok());
        const int left = int(out[size_t(8) * w + w / 2 - 1] & 0xFF), right = int(out[size_t(8) * w + w / 2] & 0xFF);
        const int flatL = int(out[size_t(8) * w + 4] & 0xFF), flatR = int(out[size_t(8) * w + w - 4] & 0xFF);
        std::fprintf(stderr, "  borda 80|160 -> %d|%d; areas lisas %d e %d\n", left, right, flatL, flatR);
        BS_CHECK(left < 80 && right > 160 && left > 40 && right < 220);  // realce sem estourar
        BS_CHECK(flatL == 80 && flatR == 160);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}
