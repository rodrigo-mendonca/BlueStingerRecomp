#pragma once
// N7b: backend Vulkan do IRenderDevice (render.h). Renderiza a CommandList num alvo OFFSCREEN e devolve a imagem na
// resolucao do jogo (read_frame), que vai para o framebuffer emulado. As regras do PVR2 seguem o rasterizador de
// referencia em software (frame_prep na CPU; pvr.frag e resolve.comp na GPU).
// O Vulkan e carregado em tempo de execucao (vulkan-1.dll / libvulkan.so.1): sem SDK nem biblioteca de importacao.
// Sem GPU ou sem os recursos exigidos (depthClamp, fragmentStoresAndAtomics), create() falha com o motivo e o jogo
// continua no software.
//
// N7c: resolucao interna e apresentacao direta.
//   - set_scale: a geometria e desenhada num alvo maior (as contas do modo fiel em cada pixel do alvo). A imagem maior
//     fica na GPU, num anel de "slots" apresentaveis (last_slot); so a reducao para a resolucao do jogo (calculada na
//     GPU) volta para a CPU, para o framebuffer emulado continuar coerente.
//   - present_*: o MESMO dispositivo desenha na janela (swapchain propria): a imagem do jogo (um slot, sem passar pela
//     CPU, ou uma imagem da CPU: software, filmes) na area 4:3 e as camadas nativas (menu, legendas) na resolucao da
//     janela. O renderizador roda na thread do jogo e a apresentacao na da janela: a fila e compartilhada com trava.
#include "render/post_shader.h"
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
    uint64_t    readback_bytes = 0;                // bytes lidos de volta pela CPU (todos os quadros)
    uint64_t    slots_written = 0, slots_skipped = 0;  // imagens maiores guardadas / sem slot livre
    bool        timestamps = false;                    // a fila mede tempo de GPU (post_timings)
    // N7c: a maior altura interna (4:3) que o dispositivo comporta: o lado maximo das imagens e ~1/4 da maior memoria
    // local da GPU para os alvos (cor, profundidade, listas por pixel, slots: ~96 bytes por pixel do alvo)
    uint32_t    max_internal_height = 480;
};

// Camada nativa sobre a saida inteira (ARGB pre-multiplicado, width x height = o tamanho da janela).
struct PresentLayer {
    const uint32_t* argb = nullptr;
    uint32_t        width = 0, height = 0;
    uint64_t        version = 0;  // muda quando o conteudo muda: so entao a camada vai para a GPU
};

// N7c (secao 21): um passe da cadeia de pos-processamento: o shader (post_builtin_color ou um carregado por post_load)
// e os 16 parametros dele (o bloco `Post` dos shaders de passe).
struct PostPass {
    int   shader = -1;
    float params[kPostParams] = {};
    // binding 1 do passe: -1 = a imagem original do jogo (antes da cadeia); k = a ENTRADA do passe k, que so pode ser o
    // imediatamente anterior (o bloom compoe o borrado sobre a imagem de antes dele)
    int   original = -1;
};

// Passes internos (secao 21), na interface dos de mods. Os ainda nao implementados devolvem -1 em post_builtin.
enum class PostBuiltin : uint8_t { Color, Tonemap, Sharpen, BloomBlurH, BloomBlurV, Crt, Count };

// a diferenca entre dois timestamps com `bits` validos (o contador volta a zero em 2^bits)
inline uint64_t timestamp_delta(uint64_t start, uint64_t end, uint32_t bits) {
    const uint64_t d = end - start;
    return bits >= 64 ? d : d & ((uint64_t(1) << bits) - 1);
}

struct PostTiming {  // tempo de GPU de um shader da cadeia (consultas de timestamp)
    int      shader = -1;
    uint64_t passes = 0;
    double   ms_total = 0;
};

struct PresentRequest {
    uint32_t        out_w = 0, out_h = 0;  // tamanho da janela em pixels
    bool            vsync = true;
    float           area[4] = {};          // area do jogo em pixels da saida (x, y, largura, altura)
    // a imagem do jogo: um slot (render na resolucao interna; slot_seq != 0) ou a da CPU (argb)
    uint64_t        slot_seq = 0;
    uint32_t        slot_y0 = 0, slot_rows = 0;  // as linhas do slot que correspondem ao exibido
    const uint32_t* argb = nullptr;              // ARGB8888, width x height
    uint32_t        width = 0, height = 0;
    uint64_t        argb_version = 0;            // muda a cada quadro novo
    bool            linear = true;               // filtragem ao ampliar (a imagem da CPU na resolucao do jogo)
    // pos-processamento: so na imagem do jogo, antes das camadas (texto nitido); vazio = a imagem como esta
    const PostPass* post = nullptr;
    uint32_t        post_count = 0;
    PresentLayer    layers[2];
    uint32_t        layer_count = 0;
    std::vector<uint32_t>* capture = nullptr;    // != nullptr: a imagem apresentada (ARGB, out_w x out_h)
};

struct PresentStats {
    uint64_t frames = 0, slot_frames = 0, cpu_frames = 0, recreated = 0;  // quadros; com slot; com imagem da CPU
    uint64_t post_frames = 0;                                             // quadros com pos-processamento
    uint64_t upload_bytes = 0;                                            // imagens da CPU e camadas enviadas
    double   cpu_ms_total = 0, cpu_ms_max = 0;  // trabalho da CPU por quadro: gravar, enviar, submeter (sem as esperas)
    double   wait_ms_total = 0;                 // esperas: quadro anterior, imagem da swapchain e vkQueuePresentKHR (V-Sync)
};

class VulkanDevice : public IRenderDevice {
public:
    // instance_extensions: as da superficie da janela (SDL_Vulkan_GetInstanceExtensions); vazias = so offscreen
    static std::unique_ptr<VulkanDevice> create(std::string* error, const std::vector<std::string>& instance_extensions = {});
    // A imagem do ultimo submit/present_image na resolucao do jogo: ARGB8888, width x height, de cima para baixo.
    virtual Result<void>       read_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) = 0;
    virtual const VulkanStats& stats() const = 0;
    // N7c: resolucao interna (1 = a do jogo: modo fiel, bit a bit igual ao software). Com escala, cada submit guarda a
    // imagem maior num slot (last_slot) e read_frame devolve a reducao dela.
    virtual void     set_scale(float scale) = 0;
    virtual float    scale() const          = 0;
    virtual uint64_t last_slot(uint32_t& width, uint32_t& height) const = 0;  // 0 = o ultimo submit nao tem slot
    // Le de volta as linhas [y0, y0 + rows) de um slot (capturas e testes; nao no caminho normal). false = o slot ja foi
    // reaproveitado.
    virtual bool     read_slot(uint64_t seq, uint32_t y0, uint32_t rows, std::vector<uint32_t>& argb, uint32_t& width) = 0;

    // Apresentacao direta (thread da janela).
    virtual uint64_t     instance_handle() const = 0;  // VkInstance, para criar a superficie (SDL_Vulkan_CreateSurface)
    virtual bool         present_attach(uint64_t surface, std::string* error) = 0;  // VkSurfaceKHR (o chamador e dono)
    virtual void         present_detach() = 0;  // antes de destruir a superficie
    virtual Result<void> present(const PresentRequest& r) = 0;
    virtual const PresentStats& present_stats() const = 0;

    // Pos-processamento (secao 21). post_load cria o modulo e o pipeline de um shader de passe JA validado
    // (validate_post_spirv); -1 com o motivo se o driver o recusar. post_builtin_color: o passe interno de correcao de
    // cor (p[0] = brilho, contraste, gamma, saturacao); -1 sem a cadeia. post_run: a cadeia sobre uma imagem da CPU,
    // fora da janela (testes); sem passes, a propria imagem.
    virtual int          post_load(std::span<const uint32_t> spirv, std::string* error) = 0;
    virtual int          post_builtin_color() = 0;
    virtual int          post_builtin(PostBuiltin which) = 0;
    // tempo de GPU medido por shader desde o inicio (vazio sem timestamps no dispositivo)
    virtual std::vector<PostTiming> post_timings() = 0;
    virtual Result<void> post_run(std::span<const uint32_t> argb, uint32_t width, uint32_t height, std::span<const PostPass> passes,
                                  std::vector<uint32_t>& out) = 0;
};

int vulkan_live_objects();  // objetos Vulkan criados e nao destruidos (0 depois de destruir o dispositivo)

// Um dispositivo por processo, compartilhado pelo renderizador (thread do jogo) e pela apresentacao (janela): a imagem
// na resolucao interna nunca sai da GPU. vulkan_shared_configure antes do primeiro vulkan_shared (as extensoes da
// superficie); vulkan_shared_lost(d) depois de uma perda do dispositivo: o proximo vulkan_shared cria outro (quem
// guarda o antigo o solta, e a geracao muda).
void                          vulkan_shared_configure(std::vector<std::string> instance_extensions);
std::shared_ptr<VulkanDevice> vulkan_shared(std::string* error);
void                          vulkan_shared_lost(const VulkanDevice* lost);
uint64_t                      vulkan_shared_generation();

} // namespace bs::native::render
