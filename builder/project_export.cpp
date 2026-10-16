#include "project_export.h"

#include "child_process.h"

#include <ctime>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace bs::builder {

// N0: tools/dcmap (mapa de regioes, usado pela sonda do runtime), native, legacy e transitional
// sao alvos do CMake raiz e entram no kit.
const wchar_t* const kKitEntries[13] = {L"CMakeLists.txt", L"LICENSE", L"VERSION", L"app", L"runtime",
                                        L"tools/extractor", L"tools/dist/LEIA-ME.txt", L"config",
                                        L"tools/dcmap", L"native", L"legacy", L"transitional",
                                        L"tools/build"};  // N2b: dependencias do MSVC por JSON (D10)

namespace {

// Copia `from` para `to` so com o que e mais novo na origem (edicoes mais novas no destino ficam).
void syncTree(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::is_directory(from, ec)) {
        fs::create_directories(to);
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::update_existing);
    } else if (fs::is_regular_file(from, ec)) {
        fs::create_directories(to.parent_path());
        fs::copy_file(from, to, fs::copy_options::update_existing);
    }
}

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
    f.close();
    if (!f) throw std::runtime_error("nao foi possivel gravar " + path_text(p));
}

const char* const kReadme = R"(# Blue Stinger recompilado: projeto C++20 editável

Este projeto foi gerado pelo **Blue Stinger Builder** a partir da **sua** cópia do jogo.
A pasta `generated/` contém código derivado do executável do disco: use-o só localmente e
**não publique** esta pasta (nem o executável compilado a partir dela).

## O que tem aqui

| Pasta/arquivo | Conteúdo |
|---|---|
| `generated/` | Funções do jogo traduzidas do SH-4 para C++ (uma função C++ por função SH-4). |
| `mapa_funcoes.tsv` | Endereço SH-4 → função C++ (`f_XXXXXXXX`) → arquivo → como foi descoberta → tamanho. |
| `runtime/` | Hardware do Dreamcast em C++: CPU SH-4 (contexto, FPU, interrupções), memória, PowerVR2 (TA, rasterizador), GD-ROM, Maple (controle), BIOS (HLE). |
| `app/` | Executável Windows: janela e controles (SDL3), ritmo de 59,94 Hz. |
| `tools/extractor/` | Leitura dos dados do jogo (`data\` preparada pelo Builder). |
| `mods/` | **Seus mods** (C++ e arquivos). O Builder nunca sobrescreve esta pasta. |
| `third_party/SDL3/` | SDL3 (licença zlib). |

## Como recompilar

**Pelo Builder (recomendado)**: na janela, marque *Compilar a partir do projeto editável* e
escolha esta pasta; ou na linha de comando:

    bs-builder --de-projeto "<esta pasta>" --out "<pasta de saída>"

O Builder compila com os mods, verifica o executável (boot sem o CDI) e só então troca a
instalação (a anterior fica em `BlueStinger.anterior`). O CDI não é necessário se os dados
já foram preparados naquela pasta de saída.

**Direto** (só compila, não instala): `compilar.cmd` (precisa do Visual Studio Build Tools;
o caminho desta pasta deve usar só caracteres da página de código do Windows, por causa do
Ninja). O executável sai em `build\app\bluestinger.exe` e precisa da pasta `data\` de uma
instalação (`bluestinger.exe --data <instalação>\data`).

## Mods em C++ (sem editar o código gerado)

Cada arquivo `mods/*.cpp` é compilado junto com o jogo (`BS_MODS=ON`). Um mod troca uma
função do jogo pelo endereço SH-4 (veja `mapa_funcoes.tsv`):

```cpp
#include "mods/mod_api.h"

static void minhaVersao(bs::Sh4Context* c) {
    bs::mods::call_original(0x8C194580, c);  // comportamento original
    c->r[0] = 1;                              // muda o valor de retorno (r0)
}

BS_MOD(meu_mod) { bs::mods::replace(0x8C194580, minhaVersao); }
```

- `replace` vale para todas as chamadas da função: diretas (o código gerado chama pelo
  "slot" da função quando compilado com mods) e indiretas (ponteiros).
- O contexto `c` tem os registradores do SH-4 (`c->r[0..15]`, `c->fr[]` da FPU, `c->pr`,
  `c->macl`...) e a memória (`bs::sh4_read32(c, endereço)`, `bs::sh4_write32(...)`).
  A convenção do jogo: argumentos em `r4`-`r7`, retorno em `r0`, pilha em `r15`.
- Exemplos em `mods/exemplos/` (não são compilados; copie para `mods/` para usar).

## Mods de arquivos (texturas, modelos, sons)

Coloque o arquivo com o mesmo caminho do disco em `mods/assets/` (por exemplo
`mods/assets/BSDATA/CDG000.NJ`). Ao compilar pelo Builder, a pasta vai para
`<instalação>\mods\assets\` e o jogo lê o arquivo do mod no lugar do original.
O arquivo pode ter **qualquer tamanho**: o jogo lê os recursos pelo sistema de arquivos
nativo (N1). Um mod que não corresponde a nenhum recurso do jogo é recusado com uma mensagem
que diz o caminho. Com `--arquivos legado` (referência), o mod precisa caber no tamanho
original. Os recursos estão em `<instalação>\data\disc\` (mesmos nomes e pastas).

## Limitações (o que ainda não foi reconstruído)

- O código em `generated/` é a tradução **fiel** das instruções SH-4 (registradores,
  flags, memória do Dreamcast), não um código-fonte "como o original": as funções têm
  nomes de endereço e operam sobre o contexto da CPU. Por isso os mods trocam funções
  inteiras pelo endereço; estruturas e nomes de alto nível ainda não foram identificados.
- Editar `generated/` funciona, mas a próxima exportação guarda essa pasta em
  `generated.anterior-<data>` e escreve uma nova. Prefira mods.
- Sem áudio (AICA/ARM7 ainda não implementados); vídeos Sofdec dependem do relógio do
  áudio e terminam cedo.
- Sem VMU (não salva).
- Renderização por software (mais lenta em cenas com muita transparência).
)";

const char* const kModsReadme = R"(Mods do Blue Stinger recompilado
================================

*.cpp nesta pasta: compilados junto com o jogo quando o Builder compila a partir do projeto
(ou com compilar.cmd). Cada mod usa BS_MOD(nome) { ... } e bs::mods::replace(endereco, funcao).
Veja LEIA-ME-PROJETO.md e os exemplos em exemplos\ (nao compilados).

assets\<caminho do disco>: arquivos que substituem os do disco (qualquer tamanho).

Esta pasta e sua: o Builder nunca a sobrescreve.
)";

const char* const kExampleMod = R"(// Exemplo (nao compilado: copie para mods\ para usar).
// Conta quantas vezes o jogo chama a funcao 0x8C194580 (um metodo "return 0" de uma tabela
// de metodos usada no inicio da partida) e mantem o comportamento original.
#include "mods/mod_api.h"

#include <cstdio>

namespace {
unsigned g_chamadas = 0;

void contar(bs::Sh4Context* c) {
    if (++g_chamadas == 1) std::fprintf(stderr, "[mod exemplo] primeira chamada de 8C194580\n");
    bs::mods::call_original(0x8C194580, c);
}
} // namespace

BS_MOD(exemplo_contador) { bs::mods::replace(0x8C194580, contar); }
)";

const char* const kBuildCmd = R"(@echo off
rem Compila o jogo a partir deste projeto (com os mods), sem instalar. Requer o Visual Studio
rem Build Tools (C++). Para compilar, verificar e instalar, prefira o Blue Stinger Builder.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio Build Tools nao encontrado & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (echo Compilador C++ x64 nao encontrado & exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DBS_BUILD_TOOLS=OFF -DBS_BUILD_TESTS=OFF -DBS_BUILD_GAME=ON -DBS_MODS=ON "-DBS_MODS_DIR=%CD%\mods" ^
  "-DBS_GENERATED_DIR=%CD%\generated" "-DFETCHCONTENT_SOURCE_DIR_SDL3=%CD%\third_party\SDL3" -DFETCHCONTENT_FULLY_DISCONNECTED=ON || exit /b 1
cmake --build build --target bluestinger || exit /b 1
echo Pronto: build\app\bluestinger.exe (use --data ^<instalacao^>\data)
)";

} // namespace

std::string check_project(const fs::path& dir) {
    std::error_code ec;
    for (const wchar_t* f : {L"CMakeLists.txt", L"generated/generated.cmake", L"generated/funcs.h", L"runtime/CMakeLists.txt",
                             L"app/CMakeLists.txt", L"third_party/SDL3/CMakeLists.txt", L"config/bluestinger_us.toml"})
        if (!fs::is_regular_file(dir / f, ec)) return "projeto incompleto: falta " + path_text(dir / f);
    return {};
}

void export_project(const ExportInputs& in, const fs::path& dest, const std::function<void(const std::string&)>& log) {
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) throw std::runtime_error("nao foi possivel criar o projeto em " + path_text(dest) + " (" + error_text(ec) + ")");

    // fontes do runtime/app (edicoes mais novas no projeto ficam) e SDL3
    for (const wchar_t* e : kKitEntries) syncTree(in.kit / e, dest / e);
    syncTree(in.sdl3, dest / L"third_party" / L"SDL3");

    // C++ gerado: o anterior (talvez editado) e guardado, nunca perdido
    const fs::path gen = dest / L"generated";
    if (fs::exists(gen, ec)) {
        char ts[32];
        const std::time_t t = std::time(nullptr);
        std::strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", std::localtime(&t));
        const fs::path old = dest / (L"generated.anterior-" + widen(ts));
        fs::rename(gen, old);
        log("generated/ anterior guardado em " + path_text(old));
    }
    fs::copy(in.generated, gen, fs::copy_options::recursive);

    // mods/: so na primeira exportacao (e do usuario)
    const fs::path mods = dest / L"mods";
    if (!fs::exists(mods, ec)) {
        writeText(mods / L"LEIA-ME-MODS.txt", kModsReadme);
        writeText(mods / L"exemplos" / L"exemplo_contador.cpp", kExampleMod);
        writeText(mods / L"assets" / L"LEIA-ME.txt",
                  "Arquivos que substituem os do disco, no mesmo caminho (ex.: BSDATA\\CDG000.NJ).\n"
                  "Qualquer tamanho. Um arquivo que nao corresponde a nenhum do disco e recusado.\n");
    }

    // mapa endereco -> funcao C++ -> arquivo -> origem -> instrucoes
    std::map<std::string, std::string> fileOf;
    const std::regex                   fn(R"(^void (f_([0-9A-F]{8}))\(bs::Sh4Context\* c\) \{)");
    for (const auto& e : fs::directory_iterator(gen)) {
        if (e.path().extension() != L".cpp") continue;
        std::ifstream f(e.path());
        for (std::string l; std::getline(f, l);) {
            std::smatch m;
            if (l.rfind("void f_", 0) == 0 && std::regex_search(l, m, fn)) fileOf[m[2]] = path_text(e.path().filename());
        }
    }
    std::ostringstream map;
    map << "endereco\tfuncao_cpp\tarquivo\torigem\tinstrucoes\n";
    std::ifstream tsv(in.functionsTsv);
    std::string   line;
    std::getline(tsv, line);  // cabecalho
    size_t n = 0;
    while (std::getline(tsv, line)) {
        std::istringstream s(line);
        std::string        addr, origin, insns;
        std::getline(s, addr, '\t');
        std::getline(s, origin, '\t');
        std::getline(s, insns, '\t');
        const std::string hex = addr.size() > 2 ? addr.substr(2) : addr;
        map << addr << "\tf_" << hex << "\t" << (fileOf.count(hex) ? fileOf[hex] : "?") << "\t" << origin << "\t"
            << insns << "\n";
        ++n;
    }
    writeText(dest / L"mapa_funcoes.tsv", map.str());
    writeText(dest / L"LEIA-ME-PROJETO.md", kReadme);
    writeText(dest / L"compilar.cmd", kBuildCmd);
    if (!in.versionLabel.empty()) writeText(dest / L"VERSION", in.versionLabel + "\n");
    log("projeto C++ exportado em " + path_text(dest) + " (" + std::to_string(n) + " funcoes no mapa)");
}

} // namespace bs::builder
