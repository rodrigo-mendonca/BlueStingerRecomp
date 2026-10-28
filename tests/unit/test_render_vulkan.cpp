// N7b: o backend Vulkan (native/render/vulkan) nas cenas de fidelidade (native/render/fidelity_scenes): cada prova
// das cenas fieis tem de sair na tolerancia da cena. Sem GPU/Vulkan na maquina: [skip] (com BS_EXIGIR_DADOS=1,
// falha). Tambem: determinismo (o mesmo quadro duas vezes, a mesma imagem) e 0 objetos vivos no fim.
#include "test.h"

#include "render/fidelity_scenes.h"
#include "render/frame_prep.h"
#include "render/vulkan/vk_device.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace nr = bs::native::render;

namespace {
bool close(uint32_t a, uint32_t b, int tol) {
    for (int sh = 0; sh <= 16; sh += 8)
        if (std::abs(int((a >> sh) & 0xFF) - int((b >> sh) & 0xFF)) > tol) return false;
    return true;
}

// renderiza a cena no dispositivo; false se algo falhar
bool run(nr::VulkanDevice& d, const nr::FidelityScene& s, std::vector<uint32_t>& img) {
    uint32_t w = 0, h = 0;
    if (s.image) return d.present_image(s.image_argb, s.cl.frame.width, s.cl.frame.height).ok() && d.read_frame(img, w, h).ok();
    std::vector<nr::TextureHandle> handles;
    for (const auto& t : s.textures) {
        auto r = d.create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb)));
        if (!r) return false;
        handles.push_back(r.value());
    }
    nr::CommandList cl = s.cl;
    for (size_t i = 0; i < cl.draws.size(); ++i)
        cl.draws[i].state.texture = s.draw_texture[i] >= 0 ? handles[size_t(s.draw_texture[i])] : nr::TextureHandle{};
    const bool ok = d.submit(cl).ok() && d.read_frame(img, w, h).ok() && w == cl.frame.width && h == cl.frame.height;
    for (auto h : handles) d.destroy_texture(h);
    return ok;
}
} // namespace

BS_TEST(render_vulkan_fidelity_scenes) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) {
            std::fprintf(stderr, "  (sem Vulkan: %s)\n", err.c_str());
            BS_SKIP("dispositivo Vulkan");
        }
        std::fprintf(stderr, "  dispositivo: %s\n", d->stats().device.c_str());
        int scenes = 0, eligible = 0, probes = 0;
        for (const auto& s : nr::fidelity_scenes()) {
            if (!s.blocked.empty() || s.mode != nr::SceneMode::Faithful) continue;
            ++eligible;
            std::vector<uint32_t> img;
            const bool            ok = run(*d, s, img);
            BS_CHECK(ok);
            if (!ok) continue;
            ++scenes;
            for (const auto& p : s.probes) {
                ++probes;
                const uint32_t got  = img[size_t(p.y) * s.cl.frame.width + p.x] & 0xFFFFFF;
                const bool     good = close(got, p.rgb, p.tolerance);
                if (!good) std::fprintf(stderr, "  cena %s (%u,%u): Vulkan %06X, esperado %06X (tol %u)\n", s.name.c_str(), p.x, p.y, got, p.rgb, p.tolerance);
                BS_CHECK(good);
            }
            // determinismo: o mesmo quadro de novo
            std::vector<uint32_t> again;
            BS_CHECK(run(*d, s, again) && again == img);
        }
        std::fprintf(stderr, "  cenas fieis %d de %d, provas %d\n", scenes, eligible, probes);
        BS_CHECK(scenes == eligible && eligible >= 7);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// N7c: resolucao interna. Com escala, o Vulkan desenha a GEOMETRIA no alvo maior (as contas do modo fiel em cada
// pixel do alvo), nao amplia a imagem do jogo: (1) as provas das cenas fieis continuam certas nos pontos
// correspondentes do alvo; (2) a borda inclinada de um triangulo tem degraus mais finos que a imagem original
// ampliada; (3) a imagem reduzida volta perto da original. Caminho B: a imagem maior fica num slot da GPU (lida aqui
// com read_slot); read_frame devolve a reducao feita na GPU, que tem de ser IGUAL a da CPU (frame_prep: downsample).
BS_TEST(render_vulkan_internal_resolution) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        for (const float sc : {1.5f, 2.25f}) {
            d->set_scale(sc);
            int probes = 0, expected = 0;
            for (const auto& s : nr::fidelity_scenes()) {
                if (!s.blocked.empty() || s.mode != nr::SceneMode::Faithful || s.image) continue;
                expected += int(s.probes.size());
                std::vector<nr::TextureHandle> handles;
                for (const auto& t : s.textures)
                    handles.push_back(d->create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb))).value());
                nr::CommandList cl = s.cl;
                for (size_t i = 0; i < cl.draws.size(); ++i)
                    cl.draws[i].state.texture = s.draw_texture[i] >= 0 ? handles[size_t(s.draw_texture[i])] : nr::TextureHandle{};
                std::vector<uint32_t> img, small, cpu;
                uint32_t              w = 0, h = 0, sw = 0, shh = 0;
                BS_CHECK(d->submit(cl).ok() && d->read_frame(small, sw, shh).ok());
                BS_CHECK(sw == cl.frame.width && shh == cl.frame.height);  // a leitura de volta: na resolucao do jogo
                const uint64_t seq = d->last_slot(w, h);
                BS_CHECK(seq && w == nr::scaled_size(cl.frame.width, sc) && h == nr::scaled_size(cl.frame.height, sc));
                uint32_t rw = 0;
                BS_CHECK(d->read_slot(seq, 0, h, img, rw) && rw == w && img.size() == size_t(w) * h);
                nr::downsample(img, w, h, cpu, sw, shh);
                BS_CHECK(cpu == small);  // a reducao da GPU e a da CPU sao a mesma conta
                for (auto hd : handles) d->destroy_texture(hd);
                if (img.size() != size_t(w) * h) continue;
                for (const auto& p : s.probes) {  // o pixel do alvo com o centro no centro do pixel da prova
                    const uint32_t x = uint32_t((float(p.x) + 0.5f) * sc), y = uint32_t((float(p.y) + 0.5f) * sc);
                    const uint32_t got = img[size_t(y) * w + x] & 0xFFFFFF;
                    ++probes;
                    if (!close(got, p.rgb, p.tolerance + 2))
                        std::fprintf(stderr, "  escala %.2f cena %s (%u,%u): %06X, esperado %06X\n", sc, s.name.c_str(), p.x, p.y, got, p.rgb);
                    BS_CHECK(close(got, p.rgb, p.tolerance + 2));
                }
            }
            BS_CHECK(probes == expected && expected >= 10);
        }
        // um triangulo branco de borda inclinada num quadro 64 x 48
        nr::CommandList cl;
        cl.frame.width = 64, cl.frame.height = 48, cl.frame.clear_color = 0xFF000000;
        auto vtx = [](float x, float y) {
            nr::Vertex v;
            v.x = x, v.y = y, v.inv_w = 1, v.color = 0xFFFFFFFF;
            return v;
        };
        cl.vertices = {vtx(3.3f, 2.1f), vtx(60.7f, 9.4f), vtx(11.2f, 45.6f)};
        nr::Draw dr;
        dr.vertex_count = 3, dr.state.depth = nr::DepthTest::Always;
        cl.draws = {dr};
        std::vector<uint32_t> lo, hi, dummy;
        uint32_t              lw = 0, lh = 0, hw = 0, hh = 0, rw = 0;
        d->set_scale(1);
        BS_CHECK(d->submit(cl).ok() && d->read_frame(lo, lw, lh).ok() && lw == 64 && lh == 48);
        BS_CHECK(d->last_slot(hw, hh) == 0);  // na escala 1 nao ha slot
        d->set_scale(3);
        BS_CHECK(d->submit(cl).ok() && d->read_frame(dummy, lw, lh).ok() && lw == 64 && lh == 48);
        const uint64_t seq = d->last_slot(hw, hh);
        BS_CHECK(seq && hw == 192 && hh == 144 && d->read_slot(seq, 0, hh, hi, rw) && rw == hw);
        if (lo.size() == 64u * 48 && hi.size() == 192u * 144) {
            // a borda esquerda (do vertice de cima ao de baixo) por linha do alvo: na imagem ampliada ela so anda de 3
            // em 3 pixels; desenhada no alvo, aparece em posicoes que nao sao multiplos de 3
            int offGrid = 0, rows = 0;
            for (uint32_t y = 12; y < 132; ++y) {
                uint32_t x = 0;
                while (x < hw && (hi[size_t(y) * hw + x] & 0xFFFFFF) == 0) ++x;
                if (x == hw) continue;
                ++rows, offGrid += x % 3 != 0;
            }
            std::fprintf(stderr, "  borda: %d de %d linhas fora da grade da imagem original\n", offGrid, rows);
            BS_CHECK(rows > 100 && offGrid > rows / 3);
            // reduzida, volta perto da original (so a cobertura parcial das bordas difere)
            std::vector<uint32_t> down;
            nr::downsample(hi, hw, hh, down, 64, 48);
            double se = 0;
            for (size_t i = 0; i < lo.size(); ++i)
                for (int s = 0; s < 24; s += 8) {
                    const double dd = double(int((lo[i] >> s) & 0xFF) - int((down[i] >> s) & 0xFF));
                    se += dd * dd;
                }
            const double psnr = 10 * std::log10(255.0 * 255.0 / std::max(se / (3.0 * double(lo.size())), 1e-9));
            std::fprintf(stderr, "  reduzida x original: PSNR %.1f dB\n", psnr);
            BS_CHECK(psnr > 20);
        }
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}

// a reducao: media exata de blocos inteiros e cobertura completa das colunas com fator fracionario
BS_TEST(render_downsample_box) {
    std::vector<uint32_t> src(4 * 2), dst;
    for (uint32_t i = 0; i < src.size(); ++i) src[i] = 0xFF000000u | (i * 10u);
    nr::downsample(src, 4, 2, dst, 2, 1);
    BS_CHECK(dst.size() == 2 && dst[0] == (0xFF000000u | ((0 + 10 + 40 + 50 + 2) / 4)) &&
             dst[1] == (0xFF000000u | ((20 + 30 + 60 + 70 + 2) / 4)));
    std::vector<uint32_t> big(9 * 9, 0xFF808080u), small;
    nr::downsample(big, 9, 9, small, 4, 4);  // 2,25 por pixel: tudo cinza continua cinza
    bool all = small.size() == 16;
    for (uint32_t c : small) all = all && c == 0xFF808080u;
    BS_CHECK(all);
}

// N8.7 (docs/N8-7-DIAGNOSTICO.md, causa da perda do dispositivo na AMD integrada): quando o buffer de triangulos cresce,
// o descritor do quadro e SEMPRE regravado. O driver pode dar ao buffer novo o mesmo handle do liberado; comparar os
// handles deixava o descritor na memoria liberada (Invalid Read). Aqui: um quadro pequeno, uma grade opaca com dezenas
// de milhares de triangulos (forca a realocacao, sem sobreposicao) e o pequeno de novo, com a mesma imagem.
namespace {
nr::CommandList grid_frame(int rows, int step) {
    nr::CommandList cl;
    cl.frame.width = 640, cl.frame.height = 480, cl.frame.clear_color = 0xFF000000;
    for (int r = 0; r < rows; ++r) {
        nr::Draw d;
        d.state.list = nr::ListType::Opaque, d.state.gouraud = false;
        d.first_vertex = uint32_t(cl.vertices.size());
        const float y0 = float(r * 480 / rows), y1 = float((r + 1) * 480 / rows);
        for (int x = 0; x <= 640; x += step)
            for (const float y : {y0, y1}) {
                nr::Vertex v;
                v.x = float(x), v.y = y, v.inv_w = 1.f, v.color = 0xFF20A040;
                cl.vertices.push_back(v);
            }
        d.vertex_count = uint32_t(cl.vertices.size()) - d.first_vertex;
        cl.draws.push_back(d);
    }
    return cl;
}
bool run_cl(nr::VulkanDevice& d, const nr::CommandList& cl, std::vector<uint32_t>& img) {
    uint32_t w = 0, h = 0;
    return d.submit(cl).ok() && d.read_frame(img, w, h).ok() && w == cl.frame.width && h == cl.frame.height;
}
} // namespace

BS_TEST(render_vulkan_triangle_buffer_growth_rewrites_descriptors) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) BS_SKIP("dispositivo Vulkan");
        const nr::CommandList small = grid_frame(4, 160), big = grid_frame(120, 2);  // 40 e 76 800 triangulos
        std::vector<uint32_t> first, again, bigImg;
        BS_CHECK(run_cl(*d, small, first));
        const uint64_t re0 = d->stats().tri_reallocs, wr0 = d->stats().frame_set_writes;
        const bool     bigOk = run_cl(*d, big, bigImg);
        if (!bigOk) std::fprintf(stderr, "  quadro grande: %s\n", d->stats().last_error.c_str());
        BS_CHECK(bigOk);
        const uint64_t re = d->stats().tri_reallocs - re0, wr = d->stats().frame_set_writes - wr0;
        std::fprintf(stderr, "  realocacoes %llu, regravacoes dos descritores %llu\n", (unsigned long long)re, (unsigned long long)wr);
        BS_CHECK(re >= 1 && wr >= re);
        // a grade cobre a tela inteira com a cor dos vertices: o shader leu os dados dos triangulos novos
        bool covered = bigImg.size() == size_t(640) * 480;
        for (size_t i = 0; covered && i < bigImg.size(); ++i) covered = (bigImg[i] & 0xFFFFFF) == 0x20A040;
        BS_CHECK(covered);
        BS_CHECK(run_cl(*d, small, again) && again == first);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}
