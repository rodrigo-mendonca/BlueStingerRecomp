#pragma once
// Localiza o compilador na maquina do usuario: Visual Studio (ou Build Tools) com o
// componente C++ x64, pelo vswhere; ambiente do vcvars64; CMake e Ninja que vem com ele;
// runtime do MSVC para copiar ao lado do jogo.
#include "child_process.h"

#include <filesystem>
#include <functional>
#include <string>

namespace bs::builder {

struct Toolchain {
    std::filesystem::path installRoot;  // raiz do Visual Studio / Build Tools
    std::filesystem::path vcvars;       // VC\Auxiliary\Build\vcvars64.bat
    std::filesystem::path cmake, ninja;
    std::filesystem::path crtDir;       // ...\redist\MSVC\<v>\x64\Microsoft.VC*.CRT
    std::string           msvcVersion;  // VCToolsVersion
    std::string           sdkVersion;   // WindowsSDKVersion
    Env                   env;          // ambiente completo depois do vcvars
};

struct ToolchainOptions {
    std::filesystem::path vswhere;  // vazio = local padrao do instalador
    std::filesystem::path vcvars;   // definido: pula o vswhere (instalacao fora do padrao)
};

// Lanca std::runtime_error com mensagem para o usuario (o que falta e como instalar).
Toolchain find_toolchain(const ToolchainOptions& opt, const std::function<void(const std::string&)>& log);

// Texto de ajuda para quando o compilador nao e encontrado.
extern const char* const kToolchainHelp;

} // namespace bs::builder
