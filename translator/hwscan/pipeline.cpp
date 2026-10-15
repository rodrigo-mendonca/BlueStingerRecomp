#include "pipeline.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace bs::hwscan {

std::unique_ptr<Report> run(const std::filesystem::path& config, const std::filesystem::path& executable,
                            const std::filesystem::path& probeTsv) {
    auto r      = std::make_unique<Report>();
    r->cfg      = loadConfig(config);
    r->img.base = r->cfg.base;
    std::ifstream f(executable, std::ios::binary);
    if (!f) throw std::runtime_error("nao foi possivel abrir o executavel (rode bs-extract antes)");
    r->img.bytes.assign(std::istreambuf_iterator<char>(f), {});
    for (const auto& rel : r->cfg.relocated) {
        if (!r->img.inMain(rel.source, rel.size)) throw std::runtime_error("relocated: origem fora do executavel");
        r->img.relocated.push_back(rel);
    }
    r->analysis = analysis::analyze(r->img, r->cfg.analysis);
    r->scan     = scan(r->img, r->analysis);
    r->sdk      = identify(r->img, r->analysis, r->scan);
    if (!probeTsv.empty()) {
        std::ifstream p(probeTsv, std::ios::binary);
        if (!p) throw std::runtime_error("nao foi possivel abrir o TSV da sonda");
        const std::string text((std::istreambuf_iterator<char>(p)), {});
        r->probe    = merge_probe(r->scan, r->img, text);
        r->hasProbe = true;
    }
    r->boundary = derive_boundary(r->scan, r->sdk, r->cfg.native_slots);
    return r;
}

} // namespace bs::hwscan
