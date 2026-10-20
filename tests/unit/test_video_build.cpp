// N5: passo do Builder sem o codificador de video do Windows (edicoes N sem o Media Feature Pack): os
// filmes ficam "nao convertidos" (o catalogo os le como ausentes), a compilacao segue, e um mod ainda
// pode fornecer o filme.
#include "test.h"

#include "video/movie_build.h"
#include "video/movie_catalog.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace bv = bs::builder::video;
namespace nv = bs::native::video;

BS_TEST(video_build_without_encoder) {
    const fs::path d = fs::temp_directory_path() / ("bs-vbuild-" + std::to_string(std::random_device{}()));
    fs::create_directories(d / "BSDATA");
    std::ofstream(d / "BSDATA" / "E001.SFD") << "nao e um SFD: o codificador falta antes de ler";
    std::ofstream(d / "BSDATA" / "E017.SFD") << "idem";
    bv::MovieBuildOptions o;
    o.conv.testNoEncoder = true;
    std::string log;
    o.log = [&](const std::string& m) { log += m + "\n"; };
    bv::MovieBuildResult r;
    std::string          err;
    BS_CHECK(bv::build_movies(d / "BSDATA", d / "video", o, r, &err) && r.converted == 0);
    BS_CHECK(!r.encoderUnavailable.empty() && log.find("NAO convertidos") != std::string::npos);
    BS_CHECK(log.find("NAO convertidos") == log.rfind("NAO convertidos"));  // avisado uma vez
    auto c = nv::MovieCatalog::load({d / "video", d / "mods"});
    BS_CHECK(c.find(3) && c.find(3)->absent && c.find(2) && c.find(2)->absent && c.problems().empty());
    fs::create_directories(d / "mods");
    std::ofstream(d / "mods" / "E001.mp4") << "x";
    auto c2 = nv::MovieCatalog::load({d / "video", d / "mods"});
    BS_CHECK(c2.find(3) && !c2.find(3)->absent && c2.find(3)->mod);
    // --sem-filmes depois de uma compilacao com filmes: o manifesto anterior some, nada vai ao pacote
    BS_CHECK(fs::exists(d / "video" / "manifest.toml") && bv::clear_movies(d / "video") && !fs::exists(d / "video"));
    auto c3 = nv::MovieCatalog::load({d / "video", {}});
    BS_CHECK(!c3.find(3) || c3.find(3)->absent);
    BS_CHECK(bv::clear_movies(d / "video"));  // ja ausente: tudo bem
    std::error_code ec;
    fs::remove_all(d, ec);
}
