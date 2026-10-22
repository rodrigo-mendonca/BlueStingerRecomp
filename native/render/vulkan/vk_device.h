#pragma once
// N7b: backend Vulkan do IRenderDevice (render.h). Fase de fidelidade: renderiza a CommandList num alvo
// OFFSCREEN na resolucao do quadro e devolve a imagem (read_frame); a apresentacao continua a do jogo. As regras
// do PVR2 seguem o rasterizador de referencia em software (frame_prep na CPU; pvr.frag e resolve.comp na GPU).
// O Vulkan e carregado em tempo de execucao (vulkan-1.dll / libvulkan.so.1): sem SDK nem biblioteca de importacao.
// Sem GPU ou sem os recursos exigidos (depthClamp, fragmentStoresAndAtomics), create() falha com o motivo e o jogo
// continua no software.
#include "render/render.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bs::native::render {

struct VulkanStats {
    std::string device;
    uint64_t    frames = 0, triangles = 0, culled = 0, nonfinite_depth = 0, batches = 0, pipelines = 0;
    uint64_t    fragments = 0, fragment_retries = 0, uploads = 0;
    uint64_t    negative_depth_frames = 0;  // quadros com 1/w negativo (profundidade deslocada, ver frame_prep)
    // garantias do modo fiel (igualdade bit a bit com o software): ambiente FP do software nos shaders
    // (VK_KHR_shader_float_controls) e profundidade crua (VK_EXT_depth_range_unrestricted)
    bool        float_controls = false, denorm_preserve = false, raw_depth = false;
    std::string last_error;  // motivo da ultima falha de um quadro (vazio = nenhuma)
    double      gpu_ms_total = 0, gpu_ms_max = 0;  // submissao ate o fim de cada quadro (inclui a leitura de volta)
    uint64_t    max_layers = 0;                    // maior lista de fragmentos translucidos num pixel
};

class VulkanDevice : public IRenderDevice {
public:
    static std::unique_ptr<VulkanDevice> create(std::string* error);
    // A imagem do ultimo submit/present_image: ARGB8888, width x height, de cima para baixo.
    virtual Result<void>       read_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) = 0;
    virtual const VulkanStats& stats() const = 0;
    // N7c: resolucao interna. Os quadros seguintes sao desenhados em round(w x scale) x round(h x scale) (a geometria
    // de verdade, com as contas do modo fiel em cada pixel do alvo; frame_prep.h) e read_frame devolve esse tamanho.
    // 1 = a resolucao do jogo (modo fiel, bit a bit igual ao software).
    virtual void               set_scale(float scale) = 0;
    virtual float              scale() const          = 0;
};

int vulkan_live_objects();  // objetos Vulkan criados e nao destruidos (0 depois de destruir o dispositivo)

} // namespace bs::native::render
