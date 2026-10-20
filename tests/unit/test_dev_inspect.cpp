// BlueStinger-Developer: inspecao de recursos de uma instalacao ja gerada (builder/resource_inspect), sem CDI.
#include "test.h"

#include "platform/install_config.h"
#include "resource_inspect.h"
#include "runtime_data.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace bb = bs::builder;
namespace ni = bs::native::install;

namespace {
struct Dir {
    fs::path p = fs::temp_directory_path() / ("bs-dev-inspect-" + std::to_string(std::random_device{}()));
    Dir() { fs::create_directories(p); }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
};
const bb::ResourceRow* row(const bb::ResourceReport& r, bb::ResourceKind k, const std::string& id = "") {
    for (const auto& x : r.rows)
        if (x.kind == k && x.id == id) return &x;
    return nullptr;
}
bool has(const bb::ResourceReport& r, bb::InspectProblem k) {
    for (const auto& i : r.issues)
        if (i.kind == k) return true;
    return false;
}
} // namespace

// Sem instalacao; instalacao incompleta (sem executavel nem data): as familias aparecem pelo estado, sem inventar
BS_TEST(dev_inspect_missing_and_incomplete) {
    Dir d;
    auto r = bb::inspect_resources(d.p / "nao-existe");
    BS_CHECK(has(r, bb::InspectProblem::NoInstallation) && r.rows.empty());
    fs::create_directories(d.p / "BlueStinger");
    r = bb::inspect_resources(d.p);  // a pasta de saida do Builder: entra na subpasta BlueStinger
    BS_CHECK(r.installDir == d.p / "BlueStinger");
    BS_CHECK(has(r, bb::InspectProblem::NoExecutable) && has(r, bb::InspectProblem::NoData));
    const auto* m = row(r, bb::ResourceKind::Movie);
    BS_CHECK(m && m->state == bb::ResourceState::NotInstalled && m->moddable);  // instalacao sem filmes
    const auto* a = row(r, bb::ResourceKind::Audio);
    BS_CHECK(a && a->state == bb::ResourceState::Pending && !a->moddable);  // N6: sem manifesto, nada listado
    const auto* c = row(r, bb::ResourceKind::Config);
    BS_CHECK(c && c->state == bb::ResourceState::Absent && c->detail.find("documentos") != std::string::npos);
}

// Filmes pelo catalogo do jogo: do Builder, ausente no disco original, arquivo faltando e mod por ID; modo
// portatil lido do instalacao.toml como o jogo le
BS_TEST(dev_inspect_movies_mods_and_config) {
    Dir        d;
    const auto inst = d.p / "BlueStinger";
    fs::create_directories(inst / "data" / "video");
    fs::create_directories(inst / "mods" / "video");
    std::ofstream(inst / "BlueStinger.exe") << "x";
    std::ofstream(inst / "data" / "video" / "manifest.toml")
        << "[[filme]]\nid = 1\noriginal = \"NEWGAME.SFD\"\nausente = true\n"
           "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n"
           "[[filme]]\nid = 5\noriginal = \"E003.SFD\"\narquivo = \"E003.mp4\"\n";
    std::ofstream(inst / "data" / "video" / "E001.mp4") << "mp4";
    // E003.mp4 nao existe: o manifesto aponta um arquivo que sumiu
    std::ofstream(inst / "mods" / "video" / "manifest.toml") << "[[filme]]\nid = 3\narquivo = \"E001b.mp4\"\n";
    std::ofstream(inst / "mods" / "video" / "E001b.mp4") << "mod";
    BS_CHECK(ni::write_config(inst, {ni::SavesLocation::Portable}));
    const auto r = bb::inspect_resources(inst);
    BS_CHECK(!has(r, bb::InspectProblem::NoExecutable) && !has(r, bb::InspectProblem::NoData));
    const auto* ng = row(r, bb::ResourceKind::Movie, "1");
    BS_CHECK(ng && ng->state == bb::ResourceState::Absent && ng->origin == bb::ResourceOrigin::None && ng->name == "NEWGAME.SFD");
    const auto* e1 = row(r, bb::ResourceKind::Movie, "3");
    BS_CHECK(e1 && e1->state == bb::ResourceState::Ok && e1->origin == bb::ResourceOrigin::Mod && e1->file.filename() == "E001b.mp4");
    const auto* e3 = row(r, bb::ResourceKind::Movie, "5");
    BS_CHECK(e3 && e3->state == bb::ResourceState::FileMissing && e3->origin == bb::ResourceOrigin::Builder && e3->moddable);
    const auto* c = row(r, bb::ResourceKind::Config);
    BS_CHECK(c && c->state == bb::ResourceState::Ok && c->detail == "saves=portatil");
}

// Manifesto com erro: aparece como problema (com a mensagem do leitor), a inspecao continua
BS_TEST(dev_inspect_manifest_errors) {
    Dir        d;
    const auto inst = d.p / "BlueStinger";
    fs::create_directories(inst / "data" / "video");
    std::ofstream(inst / "BlueStinger.exe") << "x";
    std::ofstream(inst / "data" / "video" / "manifest.toml") << "[[filme]\nid = = 3\n";
    std::ofstream(inst / "instalacao.toml") << "[saves]\nlocal = \"marte\"\n";  // valor desconhecido
    const auto r = bb::inspect_resources(inst);
    int errors = 0;
    for (const auto& i : r.issues) errors += i.kind == bb::InspectProblem::ManifestError && !i.detail.empty();
    BS_CHECK(errors >= 2);  // o do filme e o do instalacao.toml
    BS_CHECK(row(r, bb::ResourceKind::Audio) && row(r, bb::ResourceKind::Config));
}

// ---- exportacao do projeto (project_export) e inspecao do projeto exportado, com entradas pequenas ----
#include "project_export.h"

#include <sstream>
#include <toml++/toml.hpp>

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
// kit, SDL3, toml++, C++ gerado e analise minimos: so o que o export copia e le
bb::ExportInputs fakeInputs(const fs::path& root) {
    bb::ExportInputs in;
    in.kit = root / "kit", in.sdl3 = root / "sdl3", in.toml = root / "toml", in.generated = root / "gen";
    in.functionsTsv = root / "functions.tsv", in.versionLabel = "teste-1", in.resources = root / "recursos";
    fs::create_directories(in.kit / "runtime"), fs::create_directories(in.kit / "native"), fs::create_directories(in.sdl3);
    fs::create_directories(in.toml), fs::create_directories(in.generated);
    std::ofstream(in.kit / "CMakeLists.txt") << "cmake_minimum_required(VERSION 3.25)\n";
    std::ofstream(in.kit / "runtime" / "r.cpp") << "// runtime\n";
    std::ofstream(in.sdl3 / "CMakeLists.txt") << "# sdl3\n";
    std::ofstream(in.toml / "toml.hpp") << "// toml\n";
    std::ofstream(in.generated / "funcs_000.cpp") << "void f_8C010000(bs::Sh4Context* c) {\n}\nvoid f_8C010010(bs::Sh4Context* c) {\n}\n";
    std::ofstream(in.generated / "funcs.h") << "// h\n";
    std::ofstream(in.generated / "generated.cmake") << "# gen\n";
    std::ofstream(in.functionsTsv) << "endereco\torigem\tinstrucoes\n0x8C010000\tentrada\t8\n0x8C010010\tchamada\t4\n";
    return in;
}
} // namespace

// O projeto exportado: README que diferencia recompilacao estatica de codigo-fonte original, presets da IDE
// iguais ao compilar.cmd (release e debug, mods, gerado e dependencias offline), bs-projeto.toml com a pasta
// dos recursos, mapa de funcoes e mods iniciais; reexportar preserva mods/ e guarda o generated/ editado
BS_TEST(dev_export_project_files) {
    Dir        d;
    const auto in   = fakeInputs(d.p);
    const auto proj = d.p / "projeto";
    bb::export_project(in, proj, [](const std::string&) {});
    const std::string readme = slurp(proj / "LEIA-ME-PROJETO.md");
    BS_CHECK(readme.find("recompilação estática") != std::string::npos && readme.find("Não é o código-fonte original") != std::string::npos);
    for (const char* dir : {"`generated/`", "`runtime/`", "`native/`", "`transitional/`", "`legacy/`", "`app/`", "`mods/`"})
        BS_CHECK(readme.find(dir) != std::string::npos);
    BS_CHECK(readme.find("não publique") != std::string::npos);  // aviso legal
    const std::string presets = slurp(proj / "CMakePresets.json"), cmd = slurp(proj / "compilar.cmd");
    for (const char* k : {"\"release\"", "\"debug\"", "BS_GENERATED_DIR", "BS_MODS_DIR", "FETCHCONTENT_SOURCE_DIR_SDL3",
                          "FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS", "FETCHCONTENT_FULLY_DISCONNECTED", "BS_BUILD_GAME", "\"bluestinger\""})
        BS_CHECK(presets.find(k) != std::string::npos);
    BS_CHECK(cmd.find("cmake --preset %PRESET%") != std::string::npos && cmd.find("--build --preset %PRESET%") != std::string::npos);
    const auto meta = toml::parse_file((proj / "bs-projeto.toml").u8string());
    BS_CHECK(meta["versao"].value_or(std::string()) == "teste-1");
    const std::string rec = meta["recursos"].value_or(std::string());
    BS_CHECK(fs::path(std::u8string(rec.begin(), rec.end())) == in.resources);
    const std::string map = slurp(proj / "mapa_funcoes.tsv");
    BS_CHECK(map.find("0x8C010000\tf_8C010000\tfuncs_000.cpp\tentrada\t8") != std::string::npos);
    BS_CHECK(fs::exists(proj / "mods" / "exemplos" / "exemplo_contador.cpp") && fs::exists(proj / "runtime" / "r.cpp"));
    BS_CHECK(bb::check_project(proj).find("generated/generated.cmake") == std::string::npos);  // o gerado entrou
    // reexportar: mods do usuario ficam; o gerado editado e guardado
    std::ofstream(proj / "mods" / "meu_mod.cpp") << "// meu\n";
    std::ofstream(proj / "generated" / "funcs_000.cpp", std::ios::app) << "// editado\n";
    bb::export_project(in, proj, [](const std::string&) {});
    BS_CHECK(slurp(proj / "mods" / "meu_mod.cpp").rfind("// meu", 0) == 0);  // o mod do usuario ficou intacto
    int saved = 0;
    for (const auto& e : fs::directory_iterator(proj))
        if (e.path().filename().native().rfind(L"generated.anterior-", 0) == 0)
            saved += slurp(e.path() / "funcs_000.cpp").find("// editado") != std::string::npos;
    BS_CHECK(saved == 1 && slurp(proj / "generated" / "funcs_000.cpp").find("// editado") == std::string::npos);
}

// Inspecao de um projeto exportado, sem o CDI: mapa de funcoes por origem, C++ gerado, mods, presets, a pasta
// dos recursos (pronta = compilar sem o CDI) e os filmes como o jogo os resolveria com os mods do projeto
BS_TEST(dev_inspect_exported_project) {
    Dir        d;
    const auto in   = fakeInputs(d.p);
    const auto proj = d.p / "projeto";
    bb::export_project(in, proj, [](const std::string&) {});
    std::ofstream(proj / "mods" / "a.cpp") << "//\n";
    fs::create_directories(proj / "mods" / "assets" / "BSDATA"), fs::create_directories(proj / "mods" / "video");
    std::ofstream(proj / "mods" / "assets" / "BSDATA" / "X.NT") << "x";
    std::ofstream(proj / "mods" / "video" / "E001.mp4") << "mod";
    auto r = bb::inspect_resources(proj);
    BS_CHECK(r.project);
    const auto* f = row(r, bb::ResourceKind::Functions);
    BS_CHECK(f && f->state == bb::ResourceState::Ok && f->detail.find("2 funcoes") != std::string::npos &&
             f->detail.find("entrada 1") != std::string::npos && f->detail.find("chamada 1") != std::string::npos);
    const auto* gen = row(r, bb::ResourceKind::Generated);
    BS_CHECK(gen && gen->state == bb::ResourceState::Ok && gen->detail.find("1 arquivos .cpp") != std::string::npos);
    const auto* m = row(r, bb::ResourceKind::Mod);
    BS_CHECK(m && m->detail.find("1 mods C++") != std::string::npos && m->detail.find("1 recursos em assets") != std::string::npos &&
             m->detail.find("1 arquivos em video") != std::string::npos);
    int projRows = 0;
    for (const auto& x : r.rows) projRows += x.kind == bb::ResourceKind::Project;
    BS_CHECK(projRows == 2);  // presets e bs-projeto.toml
    // recursos ainda nao preparados: compilar vai pedir o CDI
    bool notReady = false;
    for (const auto& x : r.rows) notReady |= x.name == "bs-projeto.toml" && x.state == bb::ResourceState::NotInstalled;
    BS_CHECK(notReady);
    // recursos prontos (marca do Builder) com os filmes do Builder; o mod do projeto troca o E001 por nome
    fs::create_directories(in.resources / ".bs-build" / "data" / "video");
    std::ofstream(in.resources / ".bs-build" / "data.ok") << "1 x";  // formato antigo: o Builder recusaria
    r = bb::inspect_resources(proj);
    bool old = false;
    for (const auto& x : r.rows) old |= x.name == "bs-projeto.toml" && x.state == bb::ResourceState::NotInstalled;
    BS_CHECK(old);
    std::ofstream(in.resources / ".bs-build" / "data.ok") << std::to_string(bs::disc::kRuntimeDataFormat) + " x";
    std::ofstream(in.resources / ".bs-build" / "data" / "video" / "manifest.toml")
        << "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n";
    std::ofstream(in.resources / ".bs-build" / "data" / "video" / "E001.mp4") << "mp4";
    r = bb::inspect_resources(proj);
    bool ready = false;
    for (const auto& x : r.rows) ready |= x.name == "bs-projeto.toml" && x.state == bb::ResourceState::Unverified;
    const auto* e1 = row(r, bb::ResourceKind::Movie, "3");
    BS_CHECK(ready && e1 && e1->origin == bb::ResourceOrigin::Mod && e1->state == bb::ResourceState::Ok);
}

// Revisao Codex 2026-10-20 (ALTA): nenhum projeto (de origem ou exportado) pode ficar dentro do que o Builder troca
// (a instalacao) ou apaga (.bs-build: os dados preparados sao refeitos com remove_all). A validacao recusa antes
// de tocar em qualquer coisa: o arquivo do usuario dentro do projeto continua la.
#include "builder_core.h"

BS_TEST(dev_project_inside_builder_folders_refused) {
    Dir        d;
    const auto out = d.p / "saida";
    std::ofstream(d.p / "imagem.cdi") << "nao e um CDI";  // so precisa existir: a recusa vem antes de ler
    for (const fs::path& inside : {out / ".bs-build" / "data" / "proj", out / ".bs-build", out / "BlueStinger" / "proj", out / "BlueStinger"}) {
        fs::create_directories(inside);
        std::ofstream(inside / "meu.cpp") << "// do usuario\n";
        for (int mode = 0; mode < 2; ++mode) {  // 0: compilar a partir do projeto; 1: so exportar para ele
            bb::Request req;
            req.outDir = out;
            req.kitDir = d.p / "kit-inexistente";
            if (mode == 0) req.fromProject = inside;
            else req.exportProject = inside, req.exportOnly = true, req.disc = d.p / "imagem.cdi";
            const bb::Result r = bb::build(req, {});
            BS_CHECK(!r.ok && r.failedStep == bb::Step::Validate && r.error.find("nao pode ficar dentro de") != std::string::npos);
            BS_CHECK(fs::exists(inside / "meu.cpp"));
        }
    }
    // controle: fora das pastas do Builder, a validacao passa desse ponto (falha depois, pelo kit inexistente)
    bb::Request ok;
    ok.outDir      = out;
    ok.kitDir      = d.p / "kit-inexistente";
    ok.fromProject = d.p / "projeto-fora";
    fs::create_directories(ok.fromProject);
    const bb::Result r = bb::build(ok, {});
    BS_CHECK(!r.ok && r.error.find("nao pode ficar dentro de") == std::string::npos);
}

// Revisao Codex 2026-10-20: o preset leva a versao do Builder (BS_VERSION_OVERRIDE, com escape JSON) e o
// cancelamento durante a exportacao interrompe as copias (nunca vira "exportado")
BS_TEST(dev_export_version_and_cancel) {
    Dir  d;
    auto in         = fakeInputs(d.p);
    in.versionLabel = "v1 \"teste\"";
    bb::export_project(in, d.p / "p1", [](const std::string&) {});
    BS_CHECK(slurp(d.p / "p1" / "CMakePresets.json").find("\"BS_VERSION_OVERRIDE\": \"v1 \\\"teste\\\"\"") != std::string::npos);
    std::atomic<bool> cancel{true};
    bool              cancelled = false;
    try {
        bb::export_project(in, d.p / "p2", [](const std::string&) {}, &cancel);
    } catch (const bb::ExportCancelled&) {
        cancelled = true;
    }
    BS_CHECK(cancelled && !fs::exists(d.p / "p2" / "LEIA-ME-PROJETO.md") && !fs::exists(d.p / "p2" / "generated"));
}
