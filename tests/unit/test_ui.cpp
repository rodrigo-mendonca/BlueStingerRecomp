// Interface do recompilador: catalogo de textos (builder/ui/i18n.h) e configuracao da instalacao
// (native/platform/install_config.h). Sem janela: a tela usa so estas funcoes.
#include "test.h"

#include "builder_core.h"
#include "platform/install_config.h"
#include "ui/i18n.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace ui = bs::builder::ui;
namespace ni = bs::native::install;

namespace {
struct Dir {
    fs::path p = fs::temp_directory_path() / ("bs-ui-" + std::to_string(std::random_device{}()));
    Dir() { fs::create_directories(p); }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
};
std::set<std::string> keys(const ui::Language& l) {
    std::set<std::string> k;
    for (const auto& e : l.entries) k.insert(e.key);
    return k;
}
} // namespace

// pt-BR e en-US presentes; o seletor lista os dois com o nome no proprio idioma
BS_TEST(ui_languages_present) {
    BS_CHECK(ui::languages().size() >= 2);
    const ui::Language* pt = ui::find_language("pt-BR");
    const ui::Language* en = ui::find_language("en-US");
    BS_CHECK(pt && en && std::wstring(pt->name) == L"Português (Brasil)" && std::wstring(en->name) == L"English");
    BS_CHECK(ui::find_language("PT-br") == pt);  // codigo sem diferenciar maiusculas
}

// toda chave existe em todos os idiomas (nenhuma sem traducao, nenhuma sobrando), sem texto vazio nem
// repetida, e com os mesmos parametros {n}
BS_TEST(ui_catalog_complete) {
    const ui::Language& ref = *ui::languages()[0];
    const auto          refKeys = keys(ref);
    BS_CHECK(refKeys.size() == ref.entries.size());  // sem chave repetida
    for (const ui::Language* l : ui::languages()) {
        const auto k = keys(*l);
        BS_CHECK(k.size() == l->entries.size());
        std::string missing, extra;
        for (const auto& r : refKeys)
            if (!k.count(r)) missing += " " + r;
        for (const auto& x : k)
            if (!refKeys.count(x)) extra += " " + x;
        if (!missing.empty()) std::fprintf(stderr, "  %s sem traducao:%s\n", l->code, missing.c_str());
        if (!extra.empty()) std::fprintf(stderr, "  %s com chaves fora da referencia:%s\n", l->code, extra.c_str());
        BS_CHECK(missing.empty() && extra.empty());
        for (const auto& e : l->entries) {
            BS_CHECK(e.text && *e.text);
            for (const auto& r : ref.entries)
                if (std::string(r.key) == e.key) BS_CHECK(ui::placeholders(r.text) == ui::placeholders(e.text));
        }
    }
    // as chaves que o pipeline devolve existem no catalogo
    using bs::builder::ErrorKind;
    for (ErrorKind k : {ErrorKind::Cancelled, ErrorKind::MissingInput, ErrorKind::DiscNotFound, ErrorKind::InvalidDisc,
                        ErrorKind::NoSpace, ErrorKind::NoPermission, ErrorKind::PathTooLong, ErrorKind::Busy,
                        ErrorKind::Toolchain, ErrorKind::Data, ErrorKind::Build, ErrorKind::Verify, ErrorKind::Install,
                        ErrorKind::Other})
        BS_CHECK(refKeys.count(bs::builder::error_key(k)) == 1);
}

// troca de idioma imediata, parametros e fallback
BS_TEST(ui_translate_and_switch) {
    ui::set_language("pt-BR");
    BS_CHECK(ui::tr("btn.build") == L"Recompilar" && ui::tr("portable.check") == L"Modo portátil");
    ui::set_language("en-US");
    BS_CHECK(ui::tr("btn.build") == L"Build" && ui::tr("portable.check") == L"Portable mode");
    BS_CHECK(ui::tr("status.progress", {L"42", L"Compiling"}) == L"42% - Compiling");
    BS_CHECK(ui::tr("chave.inexistente") == L"chave.inexistente");  // aparece, nao some
    ui::set_language("xx-YY");  // desconhecido: ingles
    BS_CHECK(std::string(ui::current_language().code) == "en-US");
}

// idioma inicial: preferencia salva; senao o do Windows (pt-BR); senao ingles
BS_TEST(ui_initial_language) {
    BS_CHECK(ui::initial_language("", {L"pt-BR", L"en-US"}) == "pt-BR");
    BS_CHECK(ui::initial_language("", {L"en-GB"}) == "en-US");
    BS_CHECK(ui::initial_language("", {L"pt-PT"}) == "en-US");  // sem tabela propria: ingles
    BS_CHECK(ui::initial_language("", {L"de-DE", L"pt-BR"}) == "en-US");  // vale o primeiro preferido
    BS_CHECK(ui::initial_language("", {}) == "en-US");
    BS_CHECK(ui::initial_language("en-US", {L"pt-BR"}) == "en-US");  // a escolha do usuario vence
    BS_CHECK(ui::initial_language("pt-BR", {L"en-US"}) == "pt-BR");
    BS_CHECK(ui::initial_language("klingon", {L"pt-BR"}) == "pt-BR");  // preferencia invalida: ignora
}

// instalacao.toml: padrao Documentos, portatil, gravacao, valor desconhecido
BS_TEST(ui_install_config_modes) {
    Dir d;
    BS_CHECK(ni::read_config(d.p).saves == ni::SavesLocation::Documents);  // sem arquivo
    BS_CHECK(ni::write_config(d.p, {ni::SavesLocation::Portable}));
    BS_CHECK(ni::read_config(d.p).saves == ni::SavesLocation::Portable);
    BS_CHECK(ni::write_config(d.p, {ni::SavesLocation::Documents}));
    BS_CHECK(ni::read_config(d.p).saves == ni::SavesLocation::Documents && !fs::exists(d.p / L"instalacao.toml.tmp"));
    std::ofstream(d.p / ni::kConfigFile) << "[saves]\nlocal = \"nuvem\"\n";
    std::string w;
    BS_CHECK(ni::read_config(d.p, &w).saves == ni::SavesLocation::Documents && !w.empty());
    std::ofstream(d.p / ni::kConfigFile) << "isto nao e toml [[[";
    w.clear();
    BS_CHECK(ni::read_config(d.p, &w).saves == ni::SavesLocation::Documents && !w.empty());
}

// revisao Codex 2026-10-19: arquivo inacessivel avisa (nao vira Documentos em silencio); a escrita devolve
// o codigo do sistema; espaco e permissao sao classificados para a mensagem traduzida
BS_TEST(ui_install_config_errors_and_kinds) {
    Dir d;
    BS_CHECK(ni::write_config(d.p, {ni::SavesLocation::Portable}));
    const std::wstring f = (d.p / ni::kConfigFile).wstring();
    HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);  // sem compartilhar
    BS_CHECK(h != INVALID_HANDLE_VALUE);
    std::string w;
    BS_CHECK(ni::read_config(d.p, &w).saves == ni::SavesLocation::Documents && !w.empty());
    CloseHandle(h);
    w.clear();
    BS_CHECK(ni::read_config(d.p, &w).saves == ni::SavesLocation::Portable && w.empty());  // de novo legivel
    std::string     err;
    std::error_code code;
    BS_CHECK(!ni::write_config(d.p / "nao" / "existe", {}, &err, &code) && code && !err.empty());
    using bs::builder::ErrorKind;
    using bs::builder::error_kind_of;
    const auto sys = [](int e) { return std::error_code(e, std::system_category()); };
    BS_CHECK(error_kind_of(sys(ERROR_DISK_FULL), ErrorKind::Other) == ErrorKind::NoSpace);
    BS_CHECK(error_kind_of(sys(ERROR_HANDLE_DISK_FULL), ErrorKind::Other) == ErrorKind::NoSpace);
    BS_CHECK(error_kind_of(sys(ERROR_ACCESS_DENIED), ErrorKind::Install) == ErrorKind::NoPermission);
    BS_CHECK(error_kind_of(std::make_error_code(std::errc::no_space_on_device), ErrorKind::Other) == ErrorKind::NoSpace);
    BS_CHECK(error_kind_of(sys(ERROR_SHARING_VIOLATION), ErrorKind::Install) == ErrorKind::Install);
}

// pasta dos saves: --saves-pasta > --saves-portatil > instalacao.toml > Documentos; relativa ao
// executavel (a pasta inteira pode mudar de lugar)
BS_TEST(ui_install_saves_root) {
    const fs::path exe = L"E:\\Jogos\\BlueStinger", docs = L"C:\\Users\\x\\OneDrive\\Documentos", fb = L"C:\\fb";
    const ni::InstallConfig doc{}, port{ni::SavesLocation::Portable};
    BS_CHECK(ni::saves_root({}, false, exe, doc, docs, fb) == docs / L"DC-Recomp" / L"Blue Stinger" / L"saves");
    BS_CHECK(ni::saves_root({}, false, exe, port, docs, fb) == exe / L"saves");
    BS_CHECK(ni::saves_root({}, true, exe, doc, docs, fb) == exe / L"saves");
    BS_CHECK(ni::saves_root(L"D:\\s", true, exe, port, docs, fb) == fs::path(L"D:\\s"));
    BS_CHECK(ni::saves_root({}, false, exe, doc, {}, fb) == fb);
    // a mesma instalacao em outra unidade: os saves vao junto
    BS_CHECK(ni::saves_root({}, false, L"F:\\Pendrive\\BlueStinger", port, docs, fb) == fs::path(L"F:\\Pendrive\\BlueStinger\\saves"));
}

// Legendas em ingles por padrao (2026-10-21): sem arquivo, en-US; "nenhuma" desliga; outro idioma vale; valor invalido
// avisa e fica no padrao; o Recompiler grava a escolha junto com o modo dos saves e a releitura a preserva
BS_TEST(ui_install_config_subtitles) {
    Dir d;
    BS_CHECK(ni::read_config(d.p).subtitles == "en-US");
    std::ofstream(d.p / ni::kConfigFile) << "[legendas]\nidioma = \"nenhuma\"\n";
    BS_CHECK(ni::read_config(d.p).subtitles.empty());
    std::ofstream(d.p / ni::kConfigFile) << "[legendas]\nidioma = \"pt-BR\"\n";
    BS_CHECK(ni::read_config(d.p).subtitles == "pt-BR");
    std::ofstream(d.p / ni::kConfigFile) << "[legendas]\nidioma = \"../x\"\n";
    std::string w;
    BS_CHECK(ni::read_config(d.p, &w).subtitles == "en-US" && !w.empty());
    ni::InstallConfig c{ni::SavesLocation::Portable, ""};
    BS_CHECK(ni::write_config(d.p, c));
    const auto r = ni::read_config(d.p);
    BS_CHECK(r.saves == ni::SavesLocation::Portable && r.subtitles.empty());
    BS_CHECK(ni::write_config(d.p, {ni::SavesLocation::Documents}));
    BS_CHECK(ni::read_config(d.p).subtitles == "en-US");
    std::string out = "x";
    BS_CHECK(ni::parse_subtitles("off", &out) && out.empty());
    BS_CHECK(ni::parse_subtitles("en", &out) && out == "en");
    BS_CHECK(!ni::parse_subtitles("", &out) && !ni::parse_subtitles("en_US", &out) && !ni::parse_subtitles("1en", &out));
}
