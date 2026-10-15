#pragma once
// Projeto C++20 editavel (opcional no Builder): tudo o que e preciso para modificar e
// recompilar o jogo, numa pasta privada do dono do CDI (nunca na distribuicao do Builder nem
// no repositorio).
//
//   <projeto>/
//     LEIA-ME-PROJETO.md   estrutura, como compilar, API de mods, limitacoes
//     mapa_funcoes.tsv     endereco SH-4 -> funcao C++ -> arquivo gerado -> origem -> tamanho
//     compilar.cmd         compilacao direta (CMake + Ninja do Visual Studio), sem instalar
//     CMakeLists.txt, app/, runtime/, tools/extractor/, config/, LICENSE, VERSION
//     third_party/SDL3/    fontes do SDL3 (zlib)
//     generated/           C++ gerado a partir do 1ST_READ.BIN (derivado da copia do usuario)
//     mods/                mods em C++ (*.cpp, compilados com BS_MODS=ON) e mods/assets/
//                          (arquivos que substituem os do disco); nunca sobrescrita
//
// Reexportar atualiza as fontes (sem apagar edicoes mais novas em runtime/ e app/), guarda o
// generated/ anterior em generated.anterior-<data> e nao toca em mods/.
#include <filesystem>
#include <functional>
#include <string>

namespace bs::builder {

struct ExportInputs {
    std::filesystem::path kit, sdl3;    // fontes (kit do Builder ou o proprio repositorio)
    std::filesystem::path generated;    // C++ gerado (cache do Builder)
    std::filesystem::path functionsTsv; // analysis/functions.tsv (origem e tamanho)
    std::string           versionLabel; // identificacao da versao do Builder/kit
};

// Lanca std::runtime_error com mensagem para o usuario.
void export_project(const ExportInputs& in, const std::filesystem::path& dest,
                    const std::function<void(const std::string&)>& log);

// Pasta de projeto valida para --de-projeto? Vazio = sim; senao, o que falta.
std::string check_project(const std::filesystem::path& dir);

// Partes do kit que entram na compilacao (mesma lista usada para copiar o kit ao cache).
extern const wchar_t* const kKitEntries[8];

} // namespace bs::builder
