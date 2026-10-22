#pragma once
// Camera livre (N7c, docs/N7-PLANO.md secao 23): o analogico direito gira a vista em volta do ponto que a camera do jogo
// olha, sem escrever nas variaveis da camera do jogo (0x8C24CD50: posicao, pitch, yaw). So os argumentos de seis chamadas
// ao SDK feitas pela montagem da camera de gameplay (8C052240) mudam, filtradas pelo endereco de retorno (PR):
//
//   8C174148 pitch do objeto camera (CC38)       PR 8C052892  r5 + dpitch
//   8C174180 yaw do objeto camera                PR 8C05289A  r5 + dyaw
//   8C174308 posicao do objeto camera            PR 8C0528AC  fr4..fr6 = posicao orbitada
//   8C1713A4 translacao do desenho (0x8C2D0CC0)  PR 8C0528DA  (x, -y, -z) da posicao orbitada
//   8C170DC8 giro em Y do desenho por ~yaw       PR 8C0528E4  r5 - dyaw  (~(yaw + d) = ~yaw - d)
//   8C170D34 giro em X do desenho por pitch      PR 8C05290C  r5 + dpitch
//
// As chamadas sao diretas no codigo gerado (alvo resolvido): por isso as seis funcoes estao em [native] slots
// (config/bluestinger_us.toml) e a troca usa o registro de fronteiras. Qualquer outro chamador (sao 52 so de 8C1713A4)
// recebe a funcao anterior sem mudanca nenhuma. Modo Original: nada instalado.
//
// Suspensao: com 0x8C24CD68 != 0 (cenas roteirizadas, secao 23.1) ou sem montagem de gameplay ha 30 VBlanks, o
// deslocamento e zero e o rig recomeca do centro. Corte de camera (salto de posicao ou de yaw entre montagens) tambem
// zera. Os sistemas do jogo que leem a camera (posicao CD50, yaw/pitch CD5C/CD60) seguem vendo a original.
#include "boundary_registry.h"
#include "platform/camera_settings.h"
#include "platform/input.h"

#include <cstdint>
#include <functional>
#include <string>

namespace bs::transitional::free_camera {

namespace st = bs::native::settings;

// Rig puro (sem SH-4): um passo por VBlank. Saida: deslocamento em graus (yaw para a direita, pitch para cima).
struct RigInput {
    float x = 0, y = 0;  // analogico -1..1 (y para baixo, como o SDL)
    bool  recenter = false;
};

class CameraRig {
public:
    static constexpr float kStep = 1.0f / 59.94f;  // um VBlank
    void  configure(const st::CameraSettings& s) { cfg_ = st::clamp_camera(s); }
    void  step(const RigInput& in, bool suspended);
    void  reset();  // corte de camera, troca de modo: volta ao centro na hora
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    bool  returning() const { return returning_; }

private:
    st::CameraSettings cfg_;
    float              yaw_ = 0, pitch_ = 0, vx_ = 0, vy_ = 0, idle_ = 0;
    bool               returning_ = false;
};

// A geometria da orbita (exposta para os testes). Angulos do SDK: 0x10000 = 360 graus.
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
Vec3 forward(int32_t yaw, int32_t pitch);  // o vetor de visao do objeto camera do SDK: (0,0,-1) girado em X e depois em Y
// Posicao orbitada: gira a camera em volta do ponto da linha de visao mais perto do jogador (no maximo kMaxOrbit).
constexpr float kMaxOrbit = 600.0f;
Vec3 orbit(const Vec3& pos, int32_t yaw, int32_t pitch, const Vec3& player, int32_t dyaw, int32_t dpitch);
int32_t units(float degrees);

using InputFn = std::function<native::input::ControllerState()>;
struct Options {
    st::CameraSettings cfg;
    InputFn            input;   // o analogico da camera (InputSystem::peek); vazio = nenhum
    std::string        script;  // BS_CAMERA_ROTEIRO: "vblank x y [r3]" por linha (testes; substitui `input`)
    std::string        log;     // BS_CAMERA_LOG: uma linha por montagem interceptada
    bool               fixed = false;  // BS_CAMERA_FIXA=yaw,pitch: deslocamento constante em graus (testes)
    float              fixedYaw = 0, fixedPitch = 0;
};
// Roteiro, log e deslocamento fixo so valem quando a sessao comeca fora do Original.

// Na thread do jogo. Modo Original: desinstala (nada fica); outro modo: instala os seis slots.
void configure(BoundaryRegistry& registry, Options o);
void set_settings(BoundaryRegistry& registry, const st::CameraSettings& s);  // menu: mantem entrada, roteiro e log
void uninstall(BoundaryRegistry& registry);
bool installed();
void vblank(uint64_t n);  // um passo do rig (nada sem instalacao)

struct Stats {
    uint64_t builds = 0, applied = 0, suspended = 0, cuts = 0, foreign = 0;  // foreign: chamadas de outros pontos
};
Stats       stats();
std::string summary();

} // namespace bs::transitional::free_camera
