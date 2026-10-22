#include "platform/camera_settings.h"
#include "platform/user_settings.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace bs::native::settings {
namespace fs = std::filesystem;

const char* camera_mode_name(CameraMode m) {
    static constexpr const char* n[] = {"original", "hibrida", "livre"};
    return n[size_t(m)];
}

CameraSettings clamp_camera(CameraSettings s) {
    const CameraSettings d;
    auto f = [](float& v, float def, float lo, float hi) { v = std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    f(s.sens_x, d.sens_x, 10, 360), f(s.sens_y, d.sens_y, 10, 360), f(s.deadzone, d.deadzone, 0, 0.6f);
    f(s.smoothing, d.smoothing, 0, 0.9f), f(s.return_s, d.return_s, 1, 15), f(s.pitch_up, d.pitch_up, 0, 45);
    f(s.pitch_down, d.pitch_down, 0, 45);
    if (uint8_t(s.mode) > uint8_t(CameraMode::Free)) s.mode = CameraMode::Original;
    return s;
}

std::string camera_to_toml(const CameraSettings& s) {
    std::ostringstream o;
    o << "[camera]  # camera livre (N7c): modo \"original\" (a do jogo), \"hibrida\" (volta sozinha) ou \"livre\"\n"
      << "modo = \"" << camera_mode_name(s.mode) << "\"\n"
      << "sensibilidade_x = " << s.sens_x << "   # graus por segundo\n"
      << "sensibilidade_y = " << s.sens_y << "\n"
      << "zona_morta = " << s.deadzone << "\n"
      << "inverter_y = " << (s.invert_y ? "true" : "false") << "\n"
      << "suavizacao = " << s.smoothing << "\n"
      << "retorno_s = " << s.return_s << "   # hibrida: segundos sem usar ate voltar\n"
      << "r3_recentraliza = " << (s.r3_recenter ? "true" : "false") << "\n"
      << "pitch_cima = " << s.pitch_up << "   # graus\n"
      << "pitch_baixo = " << s.pitch_down << "\n";
    return o.str();
}

CameraSettings camera_from_toml(const std::string& text, std::string* warning) {
    CameraSettings s;
    toml::table    doc;
    try {
        doc = toml::parse(text);
    } catch (const toml::parse_error& e) {
        if (warning) *warning = std::string("controles.toml ilegivel para a camera (") + std::string(e.description()) + ")";
        return s;
    }
    const toml::table* t = doc["camera"].as_table();
    if (!t) return s;
    auto flt = [&](const char* k, float& v) {
        if (auto x = (*t)[k].value<double>(); x && std::isfinite(*x)) v = float(*x);
    };
    auto bln = [&](const char* k, bool& v) {
        if (auto x = (*t)[k].value<bool>()) v = *x;
    };
    if (auto m = (*t)["modo"].value<std::string>()) {
        if (*m == "original") s.mode = CameraMode::Original;
        else if (*m == "hibrida") s.mode = CameraMode::Hybrid;
        else if (*m == "livre") s.mode = CameraMode::Free;
        else if (warning) *warning = "controles.toml: [camera] modo \"" + *m + "\" desconhecido (original)";
    }
    flt("sensibilidade_x", s.sens_x), flt("sensibilidade_y", s.sens_y), flt("zona_morta", s.deadzone);
    bln("inverter_y", s.invert_y), flt("suavizacao", s.smoothing), flt("retorno_s", s.return_s);
    bln("r3_recentraliza", s.r3_recenter), flt("pitch_cima", s.pitch_up), flt("pitch_baixo", s.pitch_down);
    return clamp_camera(s);
}

CameraSettings read_camera(const fs::path& dir, std::string* warning) {
    std::ifstream in(dir / L"controles.toml", std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return camera_from_toml(ss.str(), warning);
}

// A secao [camera] vai do cabecalho "[camera]" (no inicio da linha) ate a proxima linha que comeca com "[" (outra
// secao) ou o fim; o resto do texto fica byte a byte.
std::string replace_camera_section(const std::string& text, const CameraSettings& s) {
    std::string       sec = camera_to_toml(s);
    const std::string nl  = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    if (nl.size() == 2) {  // o arquivo usa CRLF (Bloco de Notas): a secao nova tambem
        std::string crlf;
        for (char ch : sec) crlf += ch == '\n' ? std::string("\r\n") : std::string(1, ch);
        sec = std::move(crlf);
    }
    size_t            begin = std::string::npos;
    for (size_t pos = 0; pos < text.size();) {
        const size_t eol  = text.find('\n', pos);
        const size_t next = eol == std::string::npos ? text.size() : eol + 1;
        size_t       a = pos;
        while (a < next && (text[a] == ' ' || text[a] == '\t')) ++a;
        if (text.compare(a, 8, "[camera]") == 0) {
            begin = pos;
            break;
        }
        pos = next;
    }
    if (begin == std::string::npos) {  // sem secao: no fim do arquivo
        std::string out = text;
        if (!out.empty() && out.back() != '\n') out += nl;
        if (!out.empty()) out += nl;
        return out + sec;
    }
    size_t end = text.size();
    for (size_t pos = text.find('\n', begin); pos != std::string::npos && pos + 1 < text.size(); pos = text.find('\n', pos + 1)) {
        size_t a = pos + 1;
        while (a < text.size() && (text[a] == ' ' || text[a] == '\t')) ++a;
        if (a < text.size() && text[a] == '[') {
            end = pos + 1;
            break;
        }
    }
    std::string tail = text.substr(end);
    return text.substr(0, begin) + sec + (tail.empty() ? "" : nl + tail);
}

bool write_camera(const fs::path& dir, const CameraSettings& s, std::string* err) {
    std::string       text;
    {
        std::ifstream in(dir / L"controles.toml", std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
    }
    return write_text_atomic(dir / L"controles.toml", replace_camera_section(text, s), err);
}

} // namespace bs::native::settings
