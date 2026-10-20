#pragma once
// Localizacao (docs/LOCALIZACAO-PLANO.md): idiomas BCP-47 e a cadeia de recuo comum aos tres componentes
// (TextLocalization, SubtitleService, VoiceLocalization). Portavel, sem Win32.
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::localization {

// Etiqueta BCP-47 no subconjunto usado aqui: lingua (2-3 letras) + regiao (2 letras ou 3 digitos) e/ou
// escrita (4 letras), separadas por '-'. Ex.: "pt", "pt-BR", "ja", "zh-Hant-TW", "es-419". Sem "_".
bool valid_language(std::string_view tag);
// Forma canonica: lingua minuscula, escrita com inicial maiuscula, regiao maiuscula ("PT-br" -> "pt-BR").
// Invalida: vazia.
std::string canonical_language(std::string_view tag);
// Recuo determinístico: "pt-BR" -> {"pt-BR", "pt"}; "zh-Hant-TW" -> {"zh-Hant-TW", "zh-Hant", "zh"}.
// O conteudo original (sem idioma) e o ultimo recurso de quem chama, nunca outro idioma.
std::vector<std::string> language_fallback(std::string_view tag);

// ID estavel de recurso: minusculas, digitos e "._-:/" (ex.: "voz/e001a", "texto/menu.novo_jogo"),
// sem "..", sem barra no inicio ou no fim, ate 128 bytes. O mesmo ID vale para todos os idiomas.
bool valid_resource_id(std::string_view id);

// UTF-8 bem formado (sem sobrelongos nem substitutos), como os arquivos de mods exigem.
bool valid_utf8(std::string_view s);

} // namespace bs::native::localization
