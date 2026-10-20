#pragma once
// Textos da interface do recompilador (BlueStinger-Recompiler.exe) por chave. Cada idioma e uma tabela
// num arquivo proprio (lang_<codigo>.cpp) e uma linha em kLanguages (i18n.cpp): o seletor e a tela leem
// a lista, entao um idioma novo nao muda nenhum dos dois. So a apresentacao e traduzida: nomes de
// arquivo, parametros de linha de comando, caminhos e identificadores ficam como sao.
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bs::builder::ui {

struct Entry {
    const char*    key;
    const wchar_t* text;  // {0}, {1}... = parametros
};

struct Language {
    const char*            code;  // BCP 47: "pt-BR", "en-US"
    const wchar_t*         name;  // como aparece no seletor, no proprio idioma
    std::span<const Entry> entries;
};

// Idioma de referencia (todas as chaves): o primeiro da lista.
std::span<const Language* const> languages();
const Language*                  find_language(std::string_view code);

// Idioma inicial: a preferencia salva, se for um idioma conhecido; senao o primeiro idioma preferencial
// do Windows, se houver tabela para ele (pt-BR); senao en-US.
std::string initial_language(std::string_view saved, const std::vector<std::wstring>& windowsPreferred);

void            set_language(std::string_view code);  // desconhecido: en-US
const Language& current_language();

// Texto da chave no idioma atual (falta: en-US; falta tambem la: a propria chave, para aparecer no teste).
std::wstring tr(std::string_view key);
std::wstring tr(std::string_view key, std::span<const std::wstring> args);
inline std::wstring tr(std::string_view key, std::initializer_list<std::wstring> args) {
    return tr(key, std::span<const std::wstring>(args.begin(), args.size()));
}

// Parametros {n} usados num texto (os testes conferem que cada idioma usa os mesmos).
std::vector<int> placeholders(std::wstring_view text);

} // namespace bs::builder::ui
