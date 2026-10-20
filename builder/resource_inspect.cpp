#include "resource_inspect.h"

#include "platform/install_config.h"
#include "runtime_data.h"
#include "video/movie_catalog.h"

#include <toml++/toml.hpp>

#include <cstdio>
#include <fstream>
#include <map>
#include <system_error>

namespace bs::builder {

namespace fs = std::filesystem;

namespace {
bool is_file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
std::string utf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}
} // namespace

void movie_rows(ResourceReport& r, const fs::path& videoDir, const fs::path& modDir) {
    if (!is_dir(videoDir) && !is_dir(modDir)) {
        ResourceRow m;
        m.kind     = ResourceKind::Movie;
        m.state    = ResourceState::NotInstalled;  // instalacao sem filmes: o jogo pula os filmes
        m.manifest = videoDir / L"manifest.toml";
        m.moddable = true;  // mods\video ainda vale
        r.rows.push_back(m);
        return;
    }
    const auto cat = native::video::MovieCatalog::load({videoDir, modDir});
    for (const auto& p : cat.problems()) r.issues.push_back({InspectProblem::ManifestError, p});
    for (const auto& [id, e] : cat.all()) {
        ResourceRow m;
        m.kind     = ResourceKind::Movie;
        m.id       = std::to_string(id);
        m.name     = e.original;
        m.file     = e.file;
        m.moddable = true;
        m.origin   = e.absent ? ResourceOrigin::None : e.mod ? ResourceOrigin::Mod : ResourceOrigin::Builder;
        m.manifest = e.mod ? modDir : videoDir / L"manifest.toml";
        m.state    = e.absent ? ResourceState::Absent : is_file(e.file) ? ResourceState::Ok : ResourceState::FileMissing;
        char buf[96] = {};
        if (e.width) std::snprintf(buf, sizeof buf, "%ux%u %.2f fps %u quadros", e.width, e.height, e.fps, e.frames);
        m.detail = buf;
        for (const auto& [lang, path] : e.subtitles) m.detail += (m.detail.empty() ? "" : "; ") + std::string("legenda ") + lang;
        r.rows.push_back(m);
    }
}

ResourceReport inspect_project(const fs::path& dir) {
    ResourceReport r;
    r.project    = true;
    r.installDir = dir;
    std::error_code ec;
    {  // mapa de funcoes: quantas e como foram descobertas
        ResourceRow f;
        f.kind     = ResourceKind::Functions;
        f.name     = "mapa_funcoes.tsv";
        f.manifest = dir / L"mapa_funcoes.tsv";
        std::ifstream in(f.manifest);
        if (in) {
            std::map<std::string, size_t> byOrigin;
            size_t                        n = 0;
            std::string                   line;
            std::getline(in, line);  // cabecalho: endereco, funcao_cpp, arquivo, origem, instrucoes
            while (std::getline(in, line)) {
                size_t pos[4], at = 0;
                bool   ok = true;
                for (size_t& q : pos) {
                    q = line.find('\t', at);
                    if (q == std::string::npos) { ok = false; break; }
                    at = q + 1;
                }
                if (!ok) continue;
                ++n, ++byOrigin[line.substr(pos[2] + 1, pos[3] - pos[2] - 1)];
            }
            f.state  = n ? ResourceState::Ok : ResourceState::FileMissing;
            f.origin = ResourceOrigin::Builder;
            f.file   = f.manifest;
            f.detail = std::to_string(n) + " funcoes";
            for (const auto& [o, k] : byOrigin) f.detail += "; " + o + " " + std::to_string(k);
        } else {
            f.state = ResourceState::FileMissing;
        }
        r.rows.push_back(f);
    }
    {  // C++ gerado
        ResourceRow g;
        g.kind     = ResourceKind::Generated;
        g.name     = "generated";
        g.manifest = dir / L"generated" / L"generated.cmake";
        size_t cpp = 0;
        for (const auto& e : fs::directory_iterator(dir / L"generated", ec)) cpp += e.path().extension() == L".cpp";
        g.state  = is_file(g.manifest) && cpp ? ResourceState::Ok : ResourceState::FileMissing;
        g.origin = ResourceOrigin::Builder;
        g.file   = dir / L"generated";
        g.detail = std::to_string(cpp) + " arquivos .cpp";
        r.rows.push_back(g);
    }
    {  // mods do projeto (C++, recursos do disco, filmes)
        const fs::path mods = dir / L"mods";
        size_t         cpp = 0, assets = 0, video = 0;
        for (const auto& e : fs::directory_iterator(mods, ec)) cpp += e.is_regular_file() && e.path().extension() == L".cpp";
        for (auto it = fs::recursive_directory_iterator(mods / L"assets", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            assets += it->is_regular_file() && it->path().filename() != L"LEIA-ME.txt";
        for (const auto& e : fs::directory_iterator(mods / L"video", ec)) video += e.is_regular_file();
        ResourceRow m;
        m.kind     = ResourceKind::Mod;
        m.name     = "mods";
        m.state    = is_dir(mods) ? ResourceState::Ok : ResourceState::Absent;
        m.origin   = ResourceOrigin::Mod;
        m.file     = mods;
        m.moddable = true;
        m.detail   = std::to_string(cpp) + " mods C++; " + std::to_string(assets) + " recursos em assets; " + std::to_string(video) +
                   " arquivos em video";
        r.rows.push_back(m);
    }
    {  // presets da IDE
        ResourceRow p;
        p.kind     = ResourceKind::Project;
        p.name     = "CMakePresets.json";
        p.file     = dir / L"CMakePresets.json";
        p.manifest = p.file;
        p.state    = is_file(p.file) ? ResourceState::Ok : ResourceState::FileMissing;
        p.origin   = ResourceOrigin::Builder;
        p.detail   = "release, debug (MSVC x64)";
        r.rows.push_back(p);
    }
    // de onde veio: a pasta dos recursos (dados conferidos: compilar sem o CDI)
    fs::path resources;
    {
        ResourceRow p;
        p.kind     = ResourceKind::Project;
        p.name     = "bs-projeto.toml";
        p.manifest = dir / L"bs-projeto.toml";
        if (is_file(p.manifest)) {
            try {
                const auto t = toml::parse_file(utf8(p.manifest));
                if (const auto v = t["recursos"].value<std::string>()) resources = fs::path(std::u8string(v->begin(), v->end()));
                p.detail = "versao " + t["versao"].value_or(std::string("?"));
            } catch (const toml::parse_error& e) {
                r.issues.push_back({InspectProblem::ManifestError, std::string("bs-projeto.toml: ") + std::string(e.description())});
            }
            // o mesmo contrato do Builder (builder_core: marca com o prefixo do formato atual; os hashes de cada
            // arquivo sao conferidos ao compilar, caro demais para a janela): sem marca ou formato antigo = pede o CDI
            std::string mark;
            {
                std::ifstream f(resources / L".bs-build" / L"data.ok", std::ios::binary);
                std::getline(f, mark);
            }
            const std::string prefix = std::to_string(disc::kRuntimeDataFormat) + " ";
            const bool        marked = !resources.empty() && mark.rfind(prefix, 0) == 0 && is_dir(resources / L".bs-build" / L"data");
            p.state  = marked ? ResourceState::Unverified : ResourceState::NotInstalled;
            p.origin = ResourceOrigin::Builder;
            p.file   = resources;
            p.detail += marked ? "; recursos preparados (formato atual; conferidos ao compilar, sem o CDI)"
                               : mark.empty() ? "; recursos ausentes (informe o CDI ao compilar)"
                                              : "; recursos de outro formato (informe o CDI ao compilar)";
        } else {
            p.state = ResourceState::Absent;
        }
        r.rows.push_back(p);
    }
    // filmes como o jogo os resolveria: os do Builder (na pasta dos recursos) + os mods do projeto
    if (!resources.empty()) movie_rows(r, resources / L".bs-build" / L"data" / L"video", dir / L"mods" / L"video");
    if (!is_file(dir / L"CMakeLists.txt")) r.issues.push_back({InspectProblem::NoData, "CMakeLists.txt"});
    return r;
}

ResourceReport inspect_resources(const fs::path& dir, const std::wstring& installName) {
    if (is_file(dir / L"mapa_funcoes.tsv") || is_file(dir / L"generated" / L"funcs.h")) return inspect_project(dir);
    ResourceReport r;
    // a pasta da instalacao ou a de saida que a contem (o que o usuario escolheu no Builder)
    r.installDir = is_file(dir / L"BlueStinger.exe") || !is_dir(dir / installName) ? dir : dir / installName;
    if (!is_dir(r.installDir)) {
        r.issues.push_back({InspectProblem::NoInstallation, utf8(r.installDir)});
        return r;
    }
    if (!is_file(r.installDir / L"BlueStinger.exe")) r.issues.push_back({InspectProblem::NoExecutable, "BlueStinger.exe"});
    if (!is_dir(r.installDir / L"data")) r.issues.push_back({InspectProblem::NoData, "data"});

    // configuracao da instalacao (a mesma leitura do jogo)
    {
        const fs::path cfg = r.installDir / L"instalacao.toml";
        ResourceRow    c;
        c.kind     = ResourceKind::Config;
        c.name     = "instalacao.toml";
        c.manifest = cfg;
        if (is_file(cfg)) {
            std::string warn;
            const auto  ic = native::install::read_config(r.installDir, &warn);
            c.state  = ResourceState::Ok;
            c.origin = ResourceOrigin::Installation;
            c.file   = cfg;
            c.detail = ic.saves == native::install::SavesLocation::Portable ? "saves=portatil" : "saves=documentos";
            if (!warn.empty()) r.issues.push_back({InspectProblem::ManifestError, warn});
        } else {
            c.state  = ResourceState::Absent;  // o padrao: saves em Documentos
            c.detail = "saves=documentos (padrao)";
        }
        r.rows.push_back(c);
    }

    // filmes: o catalogo do jogo, com os mods
    movie_rows(r, r.installDir / L"data" / L"video", r.installDir / L"mods" / L"video");

    // audio: so com o manifesto do N6; sem ele, a familia aparece como pendente (nada inventado)
    {
        ResourceRow a;
        a.kind     = ResourceKind::Audio;
        a.manifest = r.installDir / L"data" / L"audio" / L"manifest.toml";
        a.state    = ResourceState::Pending;
        r.rows.push_back(a);
    }
    return r;
}

} // namespace bs::builder
