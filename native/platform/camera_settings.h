#pragma once
// N7c (docs/N7-PLANO.md secao 23): opcoes da camera livre, na secao [camera] do controles.toml (o mesmo arquivo dos
// controles do N3). Gravar so troca a secao [camera]: o resto do arquivo (mapas, perfis, comentarios do jogador) fica
// como estava.
#include <cstdint>
#include <filesystem>
#include <string>

namespace bs::native::settings {

// Original: a camera do jogo, sem nada instalado. Livre: o analogico direito gira a vista, ancorada no mundo (so ele e o R3 a
// mudam; a camera do jogo nao a arrasta), e o esquerdo anda para onde ela olha. O antigo modo "hibrida" (que voltava
// sozinho) foi retirado em 2026-10-23: o controles.toml antigo com ele vira Livre.
enum class CameraMode : uint8_t { Original, Free };
// Movimentacao com a camera Livre: Camera = o analogico esquerdo anda para onde a vista olha (o
// deslocamento de yaw da camera livre e somado a entrada); Original = relativo a camera do jogo, como no console.
// No modo Original da camera nao ha deslocamento: o movimento e sempre o original.
enum class MoveMode : uint8_t { Original, Camera };

struct CameraSettings {
    CameraMode mode        = CameraMode::Original;
    float      sens_x      = 120.0f;  // graus por segundo com o analogico no fim (horizontal)
    float      sens_y      = 80.0f;   // vertical
    float      deadzone    = 0.15f;   // 0 a 0.6
    bool       invert_y    = false;
    float      smoothing   = 0.5f;    // 0 (nenhuma) a 0.9
    bool       r3_recenter = true;
    float      pitch_up    = 25.0f;   // limites do pitch em graus (0 a 45)
    float      pitch_down  = 25.0f;
    MoveMode   movement    = MoveMode::Camera;
    bool       operator==(const CameraSettings&) const = default;
};

const char*    camera_mode_name(CameraMode m);  // "original", "livre"
const char*    move_mode_name(MoveMode m);      // "original", "camera"
CameraSettings clamp_camera(CameraSettings s);   // faixas (e nao finitos voltam ao padrao)

// A secao [camera] (sem o resto do arquivo); invalida: padrao + aviso.
std::string    camera_to_toml(const CameraSettings& s);
CameraSettings camera_from_toml(const std::string& controls_text, std::string* warning = nullptr);
CameraSettings read_camera(const std::filesystem::path& dir, std::string* warning = nullptr);  // dir/controles.toml
// Grava so a secao [camera] em dir/controles.toml (cria o arquivo se faltar; gravacao atomica).
bool           write_camera(const std::filesystem::path& dir, const CameraSettings& s, std::string* err = nullptr);
// O texto de um controles.toml com a secao [camera] trocada (exposto para os testes).
std::string    replace_camera_section(const std::string& controls_text, const CameraSettings& s);

} // namespace bs::native::settings
