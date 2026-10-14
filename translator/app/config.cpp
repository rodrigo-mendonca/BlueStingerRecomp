#include "app/config.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace bs {
namespace {

uint32_t u32(const toml::node* n, const char* what) {
    auto v = n ? n->value<int64_t>() : std::nullopt;
    if (!v || *v < 0 || *v > 0xFFFFFFFFll) throw std::runtime_error(std::string("config: valor invalido em ") + what);
    return uint32_t(*v);
}

} // namespace

GameConfig loadConfig(const std::filesystem::path& path) {
    // le pelo path (UTF-16 no Windows): parse_file(path.string()) falha com caracteres
    // fora do ANSI no caminho
    const std::u8string u8name = path.u8string();
    const std::string   name(u8name.begin(), u8name.end());
    std::ifstream       in(path, std::ios::binary);
    if (!in) throw std::runtime_error("config: nao foi possivel abrir " + name);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    toml::table       t;
    try {
        t = toml::parse(text, name);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error("config " + name + ": " + std::string(e.description()));
    }
    GameConfig c;
    c.base           = u32(t["load"]["base"].node(), "load.base");
    c.analysis.entry = u32(t["load"]["entry"].node(), "load.entry");
    if (auto e = t["input"]["executable"].value<std::string>()) c.executable = *e;
    if (auto v = t["analysis"]["pointer_candidates"].value<bool>()) c.analysis.pointer_candidates = *v;
    if (auto v = t["analysis"]["data_pointers"].value<bool>()) c.analysis.data_pointers = *v;
    if (auto v = t["codegen"]["inline_pool_constants"].value<bool>()) c.inline_pool_constants = *v;
    if (auto v = t["codegen"]["funcs_per_file"].value<int64_t>()) c.funcs_per_file = uint32_t(*v);

    if (auto arr = t["functions"].as_array())
        for (auto& el : *arr) {
            const auto& tb = *el.as_table();
            c.analysis.extra_functions.push_back(u32(tb["address"].node(), "functions.address"));
        }
    if (auto arr = t["jump_tables"].as_array())
        for (auto& el : *arr) {
            const auto&                 tb = *el.as_table();
            analysis::JumpTableOverride o;
            o.insn  = u32(tb["insn"].node(), "jump_tables.insn");
            o.table = u32(tb["table"].node(), "jump_tables.table");
            o.count = u32(tb["count"].node(), "jump_tables.count");
            c.analysis.jump_tables.push_back(o);
        }
    if (auto arr = t["relocated"].as_array())
        for (auto& el : *arr) {
            const auto&         tb = *el.as_table();
            analysis::Relocated r;
            r.dest   = u32(tb["dest"].node(), "relocated.dest");
            r.source = u32(tb["source"].node(), "relocated.source");
            r.size   = u32(tb["size"].node(), "relocated.size");
            if ((r.dest | r.source | r.size) & 1) throw std::runtime_error("config: relocated desalinhado");
            c.relocated.push_back(r);
            c.analysis.extra_functions.push_back(r.dest);  // o jogo executa a copia a partir do inicio
        }
    if (auto arr = t["data_ranges"].as_array())
        for (auto& el : *arr) {
            const auto& tb = *el.as_table();
            c.analysis.data_ranges.push_back({u32(tb["start"].node(), "data_ranges.start"),
                                              u32(tb["end"].node(), "data_ranges.end")});
        }
    // [threads]: funcoes setjmp/longjmp da biblioteca de threads cooperativa do jogo
    for (const char* k : {"setjmp", "longjmp"})
        if (auto arr = t["threads"][k].as_array())
            for (auto& el : *arr) {
                const uint32_t a = u32(&el, "threads");
                (std::string(k) == "setjmp" ? c.setjmp_funcs : c.longjmp_funcs).insert(a);
                c.analysis.extra_functions.push_back(a);
            }
    return c;
}

} // namespace bs
