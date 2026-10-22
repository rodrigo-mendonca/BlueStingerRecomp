#include "render/frame_prep.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace bs::native::render {
namespace {

PassMode mode_of(ListType l, bool autosort) {
    switch (l) {
    case ListType::PunchThrough: return PassMode::PunchThrough;
    case ListType::Translucent: return autosort ? PassMode::TranslucentAutosort : PassMode::TranslucentOrdered;
    default: return PassMode::Opaque;
    }
}

} // namespace

void prepare_frame(const CommandList& cl, PreparedFrame& out, bool exact, float scale) {
    out = {};
    const FrameParams& f = cl.frame;
    // amostra de cobertura no canto do pixel (HALF_OFFSET bit 0 = 0): o mesmo que deslocar a geometria meio pixel
    const float shift = f.sample_center ? 0.f : 0.5f;
    const float tw = float(scaled_size(f.width, scale)), th = float(scaled_size(f.height, scale));  // alvo
    float       maxz  = std::isfinite(f.clear_depth) ? f.clear_depth : 0.f;
    float       minz  = std::isfinite(f.clear_depth) ? f.clear_depth : 0.f;
    uint32_t    seq   = 0;
    for (ListType list : {ListType::Opaque, ListType::PunchThrough, ListType::Translucent}) {
        const PassMode mode = mode_of(list, f.translucent_autosort);
        for (const Draw& d : cl.draws) {
            if (d.state.list != list) continue;
            for (uint32_t k = 0; k + 3 <= d.vertex_count; ++k) {
                ++seq;  // o software conta cada triangulo que tenta desenhar
                const Vertex* v[3] = {&cl.vertices[d.first_vertex + k], &cl.vertices[d.first_vertex + k + 1],
                                      &cl.vertices[d.first_vertex + k + 2]};
                if (k & 1) std::swap(v[0], v[1]);
                const Vertex &a = *v[0], &b = *v[1], &c = *v[2];
                const float   area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                bool          drop = area == 0 || !std::isfinite(area);
                switch (d.state.cull) {
                case Cull::Small: drop = drop || std::fabs(area) < 2 * f.small_cull; break;
                case Cull::CounterClockwise: drop = drop || area < 0; break;
                case Cull::Clockwise: drop = drop || area > 0; break;
                default: break;
                }
                if (d.state.clip == Clip::Inside && (d.state.clip_x0 > d.state.clip_x1 || d.state.clip_y0 > d.state.clip_y1))
                    drop = true;
                if (drop) {
                    ++out.culled;
                    continue;
                }
                if (!std::isfinite(a.inv_w) || !std::isfinite(b.inv_w) || !std::isfinite(c.inv_w)) {
                    ++out.nonfinite_depth;
                    continue;
                }
                const bool scissor = d.state.clip == Clip::Inside;
                int        rx0 = 0, rx1 = 0, ry0 = 0, ry1 = 0;
                if (exact) {  // o retangulo do software (Raster::draw)
                    auto fl = [](float v) { return int(std::floor(std::clamp(v, -1e6f, 1e6f))); };
                    auto ce = [](float v) { return int(std::ceil(std::clamp(v, -1e6f, 1e6f))); };
                    rx0 = std::max(0, fl(std::min({a.x, b.x, c.x}))), rx1 = std::min(int(f.width) - 1, ce(std::max({a.x, b.x, c.x})));
                    ry0 = std::max(0, fl(std::min({a.y, b.y, c.y}))), ry1 = std::min(int(f.height) - 1, ce(std::max({a.y, b.y, c.y})));
                    if (scissor) {
                        rx0 = std::max(rx0, d.state.clip_x0), rx1 = std::min(rx1, d.state.clip_x1);
                        ry0 = std::max(ry0, d.state.clip_y0), ry1 = std::min(ry1, d.state.clip_y1);
                    }
                    if (rx0 > rx1 || ry0 > ry1) {
                        ++out.culled;
                        continue;
                    }
                }
                if (out.batches.empty() || out.batches.back().mode != mode || !(out.batches.back().state == d.state)) {
                    Batch bt;
                    bt.mode = mode, bt.state = d.state, bt.first = uint32_t(out.vertices.size()), bt.scissor = scissor;
                    out.batches.push_back(bt);
                }
                for (int i = 0; i < 3; ++i) maxz = std::fmax(maxz, v[i]->inv_w), minz = std::fmin(minz, v[i]->inv_w);
                if (exact) {
                    float qx0 = float(rx0), qx1 = float(rx1 + 1), qy0 = float(ry0), qy1 = float(ry1 + 1);
                    if (scale != 1.f) {  // os pixels do alvo que cobrem o retangulo (o shader decide a cobertura)
                        qx0 = std::floor(qx0 * scale), qy0 = std::floor(qy0 * scale);
                        qx1 = std::min(tw, std::ceil(qx1 * scale)), qy1 = std::min(th, std::ceil(qy1 * scale));
                    }
                    const float q[6][2] = {{qx0, qy0}, {qx1, qy0}, {qx0, qy1}, {qx1, qy0}, {qx1, qy1}, {qx0, qy1}};
                    for (const auto& pq : q) {
                        GpuVertex g;
                        g.x = pq[0], g.y = pq[1], g.seq = seq, g.tri = out.triangles;
                        out.vertices.push_back(g);
                    }
                    out.batches.back().count += 3;  // + 3 abaixo: 6 vertices por triangulo
                } else {
                    for (int i = 0; i < 3; ++i) {
                        GpuVertex g;
                        g.x = (v[i]->x + shift) * scale, g.y = (v[i]->y + shift) * scale;
                        g.z = v[i]->inv_w, g.u = v[i]->u, g.v = v[i]->v;
                        // sombreamento plano: a cor (e o deslocamento) do ultimo vertice nos tres
                        g.color  = d.state.gouraud ? v[i]->color : c.color;
                        g.offset = d.state.gouraud ? v[i]->offset : c.offset;
                        g.seq    = seq;
                        g.tri    = out.triangles;
                        out.vertices.push_back(g);
                    }
                }
                const float inv = 1.f / area;  // como o software (Raster::draw)
                auto        w   = [](float f) { return std::bit_cast<uint32_t>(f); };
                out.tri_data.insert(out.tri_data.end(),
                                    {w(a.x), w(a.y), w(b.x), w(b.y), w(c.x), w(c.y), w(a.inv_w), w(b.inv_w), w(c.inv_w), w(inv),
                                     w(a.u), w(a.v), w(b.u), w(b.v), w(c.u), w(c.v), a.color, b.color, c.color, a.offset,
                                     b.offset, c.offset, 0u, 0u});
                out.batches.back().count += 3;
                ++out.triangles;
                out.has_autosort = out.has_autosort || mode == PassMode::TranslucentAutosort;
            }
        }
    }
    if (minz < 0) out.depth_bias = minz;
    const float range = maxz - out.depth_bias;
    while (range * out.depth_scale > 1.f) out.depth_scale *= 0.5f;  // potencia de 2: exata
}

void downsample(std::span<const uint32_t> src, uint32_t sw, uint32_t sh, std::vector<uint32_t>& dst, uint32_t dw, uint32_t dh) {
    dst.resize(size_t(dw) * dh);
    if (!sw || !sh || !dw || !dh || src.size() < size_t(sw) * sh) return;
    // limites inteiros: o destino x cobre as colunas de origem [xs[x], xs[x+1]) (nunca vazio com sw >= dw)
    std::vector<uint32_t> xs(dw + 1), ys(dh + 1);
    for (uint32_t x = 0; x <= dw; ++x) xs[x] = uint32_t((uint64_t(x) * sw + dw / 2) / dw);
    for (uint32_t y = 0; y <= dh; ++y) ys[y] = uint32_t((uint64_t(y) * sh + dh / 2) / dh);
    for (uint32_t y = 0; y < dh; ++y)
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t x0 = xs[x], x1 = std::max(xs[x + 1], x0 + 1), y0 = ys[y], y1 = std::max(ys[y + 1], y0 + 1);
            uint32_t       acc[4] = {}, n = 0;
            for (uint32_t yy = y0; yy < y1 && yy < sh; ++yy)
                for (uint32_t xx = x0; xx < x1 && xx < sw; ++xx, ++n) {
                    const uint32_t c = src[size_t(yy) * sw + xx];
                    acc[0] += c >> 24, acc[1] += (c >> 16) & 0xFF, acc[2] += (c >> 8) & 0xFF, acc[3] += c & 0xFF;
                }
            n = std::max(n, 1u);
            dst[size_t(y) * dw + x] = ((acc[0] + n / 2) / n) << 24 | ((acc[1] + n / 2) / n) << 16 | ((acc[2] + n / 2) / n) << 8 |
                                      ((acc[3] + n / 2) / n);
        }
}

} // namespace bs::native::render
