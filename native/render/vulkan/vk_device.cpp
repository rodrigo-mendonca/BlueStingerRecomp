#include "render/vulkan/vk_device.h"

#include "render/frame_prep.h"
#include "render/vulkan/spirv/pvr_frag.h"
#include "render/vulkan/spirv/pvr_frag_autosort.h"
#include "render/vulkan/spirv/pvr_frag_autosort_fc.h"
#include "render/vulkan/spirv/pvr_frag_autosort_fcd.h"
#include "render/vulkan/spirv/pvr_frag_fc.h"
#include "render/vulkan/spirv/pvr_frag_fcd.h"
#include "render/vulkan/spirv/pvr_vert.h"
#include "render/vulkan/spirv/resolve_comp.h"
#include "render/vulkan/spirv/resolve_comp_fc.h"
#include "render/vulkan/spirv/resolve_comp_fcd.h"
#include "render/vulkan/spirv/downsample_comp.h"

#include "render/vulkan/vk_internal.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  undef DrawState  // WinUser.h: DrawStateW (colide com render::DrawState)
#else
#  include <dlfcn.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>

namespace bs::native::render {
namespace {


// Constantes de envio de um lote: o layout de `Batch` em pvr.vert/pvr.frag
struct Push {
    float    size[2];
    float    depthScale;
    uint32_t flags;
    int32_t  clip[4];
    uint32_t texSize[2];
    uint32_t ptRef, blend, clampMin, clampMax;
    float    depthBias;  // subtraido de 1/w antes da escala (so com 1/w negativo no quadro)
    float    invScale;   // 1 / escala da resolucao interna (N7c)
};
static_assert(sizeof(Push) == 64);

constexpr uint32_t F_TEXTURED = 1, F_GOURAUD = 2, F_OFFSET = 4, F_USE_ALPHA = 8, F_IGNORE_TEX_ALPHA = 16, F_CLAMP = 32,
                   F_CLIP_OUTSIDE = 64, F_CLAMP_U = 4096, F_CLAMP_V = 8192, F_FLIP_U = 16384, F_FLIP_V = 32768,
                   F_TEXEL_HALF = 65536, F_ATTR_PLUS = 1u << 19, F_ATTR_MINUS = 1u << 20, F_EXACT = 1u << 21,
                   F_SAMPLE_CORNER = 1u << 22, F_ATTR_CORNER = 1u << 23, F_RAW_DEPTH = 1u << 24;
constexpr uint32_t kFragBytes = 32;  // struct Frag (std430)
constexpr uint32_t kHeader    = 32;  // cabecalho das listas por pixel (Heads: 8 palavras)

struct Tex {
    TextureDesc     desc;
    Image           image;
    VkDescriptorSet set = VK_NULL_HANDLE;
    bool            ready = false;  // ja recebeu dados (layout de leitura)
};

class VkDev final : public VulkanDevice, public VkBase, public SlotSource {
public:
    ~VkDev() override { destroy(); }

    bool init(std::string* error, const std::vector<std::string>& instanceExtensions) {
        if (!load(error)) return false;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Blue Stinger Native PC", app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        // N7c: as extensoes da superficie da janela (apresentacao direta); sem elas, so offscreen
        std::vector<const char*> iext;
        for (const std::string& e : instanceExtensions) iext.push_back(e.c_str());
        ici.enabledExtensionCount = uint32_t(iext.size()), ici.ppEnabledExtensionNames = iext.data();
        surfaceExt_ = !iext.empty();
        if (!ok(f_.vkCreateInstance(&ici, nullptr, &inst_), "vkCreateInstance", error)) return false;
        ++g_live;
#define LOADI(n) f_.n = reinterpret_cast<PFN_##n>(f_.gipa(inst_, #n));
        VK_IFUNCS(LOADI)
        VK_SFUNCS(LOADI)
#undef LOADI
        if (!pick(error)) return false;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        const float prios[2] = {1.0f, 1.0f};
        qi.queueFamilyIndex = qfam_, qi.queueCount = std::min(queueCount_, 2u), qi.pQueuePriorities = prios;
        VkPhysicalDeviceFeatures feat{};
        feat.depthClamp = VK_TRUE, feat.fragmentStoresAndAtomics = VK_TRUE;
        // acessos fora dos limites dos buffers devolvem valores definidos em vez de falha de pagina (obrigatorio no
        // Vulkan 1.0; perda do dispositivo na GPU AMD integrada, 2026-10-22)
        feat.robustBufferAccess = VK_TRUE;
        // Garantias do modo fiel (opcionais; sem elas o backend roda, mas a igualdade bit a bit com o software deixa
        // de ser garantida e o resumo diz isso):
        //   VK_KHR_shader_float_controls com RTE e preservacao de subnormais em float32: o ambiente FP do software;
        //   VK_EXT_depth_range_unrestricted: a profundidade guarda o proprio 1/w (inclusive negativo), sem transformacao.
        uint32_t ne = 0;
        f_.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &ne, nullptr);
        std::vector<VkExtensionProperties> exts(ne);
        f_.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &ne, exts.data());
        auto has = [&](const char* n) {
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, n) == 0) return true;
            return false;
        };
        std::vector<const char*> ext;
        if (has(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME)) {
            VkPhysicalDeviceFloatControlsProperties fc{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
            VkPhysicalDeviceProperties2             p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            p2.pNext = &fc;
            f_.vkGetPhysicalDeviceProperties2(phys_, &p2);
            if (fc.shaderRoundingModeRTEFloat32) {
                floatControls_  = true, ext.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
                denormPreserve_ = fc.shaderDenormPreserveFloat32;
            }
        }
        if (has(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME))
            rawDepth_ = true, ext.push_back(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
        if (surfaceExt_ && has(VK_KHR_SWAPCHAIN_EXTENSION_NAME))  // apresentacao direta (N7c)
            swapchainExt_ = true, ext.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1, dci.pQueueCreateInfos = &qi, dci.pEnabledFeatures = &feat;
        dci.enabledExtensionCount = uint32_t(ext.size()), dci.ppEnabledExtensionNames = ext.data();
        st_.float_controls = floatControls_, st_.denorm_preserve = denormPreserve_, st_.raw_depth = rawDepth_;
        if (!ok(f_.vkCreateDevice(phys_, &dci, nullptr, &dev_), "vkCreateDevice", error)) return false;
        ++g_live;
#define LOADD(n) f_.n = reinterpret_cast<PFN_##n>(f_.vkGetDeviceProcAddr(dev_, #n));
        VK_DFUNCS(LOADD)
        VK_PFUNCS(LOADD)
#undef LOADD
        f_.vkGetDeviceQueue(dev_, qfam_, 0, &queue_);
        presentQueue_ = queue_;
        if (qi.queueCount > 1) f_.vkGetDeviceQueue(dev_, qfam_, 1, &presentQueue_);
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, pci.queueFamilyIndex = qfam_;
        if (!ok(f_.vkCreateCommandPool(dev_, &pci, nullptr, &pool_), "vkCreateCommandPool", error)) return false;
        ++g_live;
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool_, cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, cai.commandBufferCount = 1;
        if (!ok(f_.vkAllocateCommandBuffers(dev_, &cai, &cmd_), "vkAllocateCommandBuffers", error)) return false;
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!ok(f_.vkCreateFence(dev_, &fci, nullptr, &fence_), "vkCreateFence", error)) return false;
        ++g_live;
        return layouts(error);
    }

    // ---- IRenderDevice ----
    Result<TextureHandle> create_texture(const TextureDesc& desc, std::span<const std::byte> data) override {
        if (desc.format != PixelFormat::ARGB8888 || !desc.width || !desc.height) return Error::Unsupported;
        Tex t;
        t.desc = desc, t.desc.mip_levels = 1;  // o software amostra so o maior nivel (fase de fidelidade)
        t.image = makeImage(desc.width, desc.height, VK_FORMAT_B8G8R8A8_UNORM,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!t.image.img) return Error::Io;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = texPool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &texLayout_;
        if (f_.vkAllocateDescriptorSets(dev_, &ai, &t.set) != VK_SUCCESS) {
            freeImage(t.image);
            return Error::Io;
        }
        VkDescriptorImageInfo ii{sampler_, t.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet  w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = t.set, w.descriptorCount = 1, w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w.pImageInfo = &ii;
        f_.vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
        const uint32_t id = ++nextTex_;
        texs_[id]         = t;
        TextureHandle h;
        h.value = id;
        if (auto r = update_texture(h, data); !r) return r.error();
        return h;
    }

    Result<void> update_texture(TextureHandle h, std::span<const std::byte> data) override {
        auto it = texs_.find(h.value);
        if (it == texs_.end()) return Error::InvalidHandle;
        const size_t bytes = size_t(it->second.desc.width) * it->second.desc.height * 4;
        if (data.size() < bytes) return Error::OutOfRange;
        // a copia vai no proximo quadro (antes do passe de render); o dado fica na area de envio
        const size_t off = uploadBytes_.size();
        uploadBytes_.insert(uploadBytes_.end(), data.begin(), data.begin() + bytes);
        uploads_.push_back({h.value, off});
        ++st_.uploads;
        return {};
    }

    void set_palette(std::span<const uint32_t>) override {}  // as texturas chegam ja convertidas (ARGB8888)

    void destroy_texture(TextureHandle h) override {
        auto it = texs_.find(h.value);
        if (it == texs_.end()) return;
        // sem quadro em voo (cada submit espera o seu): pode destruir ja
        f_.vkFreeDescriptorSets(dev_, texPool_, 1, &it->second.set);
        freeImage(it->second.image);
        uploads_.erase(std::remove_if(uploads_.begin(), uploads_.end(), [&](const Upload& u) { return u.tex == h.value; }),
                       uploads_.end());
        texs_.erase(it);
    }

    Result<void> submit(const CommandList& cl) override {
        // diagnostico BS_VK_LENTOS=<ms>: cada render mais lento que isso, com o que ele fez (o tempo de GPU, se foi refeito
        // por falta de espaco nas listas por pixel, pipelines criados, textura enfileirada para este render) - os picos dos
        // campos (N2, 2026-10-23). Desligado, so chama submitFrame.
        static const double lentos = [] {
            const char* v = std::getenv("BS_VK_LENTOS");
            return v ? std::atof(v) : 0.0;
        }();
        if (lentos <= 0) return submitFrame(cl);
        const auto     t0   = std::chrono::steady_clock::now();
        const uint64_t ret0 = st_.fragment_retries, pip0 = pipelinesCreated_;
        const double   gpu0 = st_.gpu_ms_total;
        const size_t   up0  = uploadBytes_.size();
        auto           r    = submitFrame(cl);
        const double   ms   = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms >= lentos)
            std::fprintf(stderr, "[render-vk] render lento %llu: %.1f ms (GPU %.1f ms; refeito %llu; pipelines novos %llu; textura %zu bytes; %ux%u; camadas %u, fragmentos %u)\n",
                         static_cast<unsigned long long>(st_.frames), ms, st_.gpu_ms_total - gpu0,
                         static_cast<unsigned long long>(st_.fragment_retries - ret0),
                         static_cast<unsigned long long>(pipelinesCreated_ - pip0), up0, w_, h_, lastLayers_, lastFrags_);
        return r;
    }

    Result<void> submitFrame(const CommandList& cl) {
        prepare_frame(cl, prep_, true, scale_);  // modo fiel: retangulos envolventes, a cobertura e a do shader
        if (!target(scaled_size(cl.frame.width, scale_), scaled_size(cl.frame.height, scale_), cl.frame.width, cl.frame.height))
            return fail("alvo do quadro (imagens/passe)");
        // N7c: com a escala, a imagem maior vai para um slot livre (nenhuma apresentacao usando); sem slot livre, o quadro
        // sai so na resolucao do jogo
        writeSlot_ = -1;
        if (scale_ != 1.f) {
            std::lock_guard<std::mutex> lock(slotMutex_);
            for (int i = 0; i < kSlots; ++i)
                if (!slots_[i].pins && (writeSlot_ < 0 || slots_[i].seq < slots_[writeSlot_].seq)) writeSlot_ = i;
            if (writeSlot_ >= 0) {
                Slot& sl = slots_[writeSlot_];
                sl.seq   = 0;  // em escrita: nao pode ser fixado pela apresentacao
                // com duas filas, um semaforo novo a cada escrita: o uso anterior (sinal e, se houve, a espera da
                // apresentacao) ja terminou, porque o slot so e reaproveitado sem fixacao (revisao Codex 2026-10-22).
                // Com uma fila so, nenhum semaforo: a ordem de submissao da a dependencia.
                if (presentQueue_ != queue_ && !remakeReady(writeSlot_)) writeSlot_ = -1;
                if (sl.w != w_ || sl.h != h_) {
                    freeImage(sl.image);
                    sl.image = makeImage(w_, h_, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                         VK_IMAGE_ASPECT_COLOR_BIT);
                    sl.w = sl.image.img ? w_ : 0, sl.h = sl.image.img ? h_ : 0, sl.shaderRead = false, ++sl.version;
                    if (!sl.image.img) writeSlot_ = -1;
                }
            }
            if (writeSlot_ < 0) ++st_.slots_skipped;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (auto r = record(cl); !r) return r;
            const uint32_t* head = static_cast<const uint32_t*>(headerRead_.map);
            const uint32_t  used = head[0], overflow = head[2];
            st_.max_layers = std::max<uint64_t>(st_.max_layers, head[4]);
            lastLayers_ = head[4], lastFrags_ = used;
            st_.fragments += std::min(used, fragCapacity_);
            if (!overflow) break;
            // a lista do pixel nao coube: cresce e refaz o MESMO quadro (sem perder fragmentos); o semaforo do slot ja foi
            // sinalizado pela tentativa anterior (concluida: cerca) e e refeito para a nova sinalizar
            ++st_.fragment_retries;
            if (writeSlot_ >= 0 && presentQueue_ != queue_ && !remakeReady(writeSlot_)) writeSlot_ = -1;
            if (!fragments(used + used / 2 + 1024)) return fail("listas por pixel (memoria)");
        }
        st_.frames++, st_.triangles += prep_.triangles, st_.culled += prep_.culled;
        st_.nonfinite_depth += prep_.nonfinite_depth, st_.batches += prep_.batches.size();
        st_.negative_depth_frames += prep_.depth_bias != 0;  // so importa sem profundidade crua
        uploads_.clear(), uploadBytes_.clear();
        imageW_ = gameW_, imageH_ = gameH_;  // a leitura de volta e sempre na resolucao do jogo
        {
            std::lock_guard<std::mutex> lock(slotMutex_);
            lastSlot_ = 0;
            if (writeSlot_ >= 0) slots_[writeSlot_].seq = lastSlot_ = ++slotSeq_, slots_[writeSlot_].readyPending = presentQueue_ != queue_, ++st_.slots_written;
        }
        return {};
    }

    void  set_scale(float s) override { scale_ = s > 0 ? s : 1.f; }
    float scale() const override { return scale_; }

    bool remakeReady(int i) {
        auto& sl = slots_[i];
        if (sl.ready) f_.vkDestroySemaphore(dev_, sl.ready, nullptr), --g_live, sl.ready = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (f_.vkCreateSemaphore(dev_, &sci, nullptr, &sl.ready) != VK_SUCCESS) return sl.ready = VK_NULL_HANDLE, false;
        ++g_live, sl.readyPending = false;
        return true;
    }

    uint64_t last_slot(uint32_t& width, uint32_t& height) const override {
        width = w_, height = h_;
        return lastSlot_;
    }

    // capturas e testes (thread do jogo, a mesma do submit: usa o buffer de comandos dele)
    bool read_slot(uint64_t seq, uint32_t y0, uint32_t rows, std::vector<uint32_t>& argb, uint32_t& width) override {
        VkImage  img = VK_NULL_HANDLE;
        VkImageView view;
        uint32_t w = 0, h = 0;
        int      idx = -1;
        VkSemaphore unused = VK_NULL_HANDLE;
        if (!seq || !pinShared(seq, img, view, w, h, idx, nullptr, unused)) return false;
        rows = std::min(rows, h > y0 ? h - y0 : 0u);
        bool ok = rows > 0;
        Buffer buf;
        if (ok) buf = makeBuffer(VkDeviceSize(w) * rows * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true), ok = buf.map != nullptr;
        if (ok) {
            f_.vkResetCommandBuffer(cmd_, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            f_.vkBeginCommandBuffer(cmd_, &bi);
            // o slot fica em GENERAL: a copia so le (a apresentacao pode amostrar ao mesmo tempo); a escrita dele foi na
            // mesma fila (a do renderizador), antes
            barrier(f_, cmd_, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy c{};
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, c.imageOffset = {0, int32_t(y0), 0}, c.imageExtent = {w, rows, 1};
            f_.vkCmdCopyImageToBuffer(cmd_, img, VK_IMAGE_LAYOUT_GENERAL, buf.buf, 1, &c);
            VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
            f_.vkEndCommandBuffer(cmd_);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1, si.pCommandBuffers = &cmd_;
            f_.vkResetFences(dev_, 1, &fence_);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                ok = f_.vkQueueSubmit(queue_, 1, &si, fence_) == VK_SUCCESS;
            }
            ok = ok && f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
        }
        if (ok) {
            const uint8_t* p = static_cast<const uint8_t*>(buf.map);
            argb.resize(size_t(w) * rows);
            for (size_t i = 0; i < argb.size(); ++i, p += 4)
                argb[i] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
            width = w, st_.readback_bytes += argb.size() * 4;
        }
        freeBuffer(buf);
        unpin(idx);
        return ok;
    }

    // ---- apresentacao direta (vk_present.cpp) ----
    uint64_t instance_handle() const override { return reinterpret_cast<uint64_t>(inst_); }
    bool     present_attach(uint64_t surface, std::string* error) override {
        if (!swapchainExt_) {
            if (error) *error = surfaceExt_ ? "o dispositivo nao tem VK_KHR_swapchain" : "instancia criada sem as extensoes da superficie";
            return false;
        }
        if (!presenter_) presenter_ = make_presenter(*this, *this);
        return presenter_->attach(reinterpret_cast<VkSurfaceKHR>(surface), error);
    }
    void present_detach() override {
        if (presenter_) presenter_->detach();
    }
    Result<void> present(const PresentRequest& r) override { return presenter_ ? presenter_->present(r) : Result<void>(Error::NotFound); }
    const PresentStats& present_stats() const override {
        static const PresentStats none;
        return presenter_ ? presenter_->stats() : none;
    }

    // ---- pos-processamento (vk_post.cpp) ----
    PostChain* post_chain() override {
        std::lock_guard<std::mutex> lock(postMutex_);
        if (!post_ && !postFailed_) {
            std::string err;
            post_ = make_post_chain(*this, &err);
            if (!post_) postFailed_ = true, std::fprintf(stderr, "[pos] cadeia indisponivel: %s\n", err.c_str());
        }
        return post_.get();
    }
    int post_load(std::span<const uint32_t> spirv, std::string* error) override {
        PostChain* c = post_chain();
        if (!c) {
            if (error) *error = "cadeia de pos-processamento indisponivel";
            return -1;
        }
        return c->load(spirv, error);
    }
    int post_builtin_color() override {
        PostChain* c = post_chain();
        return c ? c->builtin_color() : -1;
    }
    int post_builtin(PostBuiltin which) override {
        PostChain* c = post_chain();
        return c ? c->builtin(which) : -1;
    }
    std::vector<PostTiming> post_timings() override {
        PostChain* c = post_chain();
        return c ? c->timings() : std::vector<PostTiming>{};
    }
    // testes: a cadeia fora da janela, com o buffer de comandos do renderizador (a mesma thread dele)
    Result<void> post_run(std::span<const uint32_t> argb, uint32_t w, uint32_t h, std::span<const PostPass> passes,
                          std::vector<uint32_t>& out) override {
        PostChain* c = post_chain();
        if (!c || argb.size() < size_t(w) * h) return Error::Unsupported;
        Image  src = makeImage(w, h, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT);
        Buffer up = makeBuffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        Buffer rd = makeBuffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        bool   ok = src.img && up.map && rd.map;
        if (ok) {
            std::memcpy(up.map, argb.data(), size_t(w) * h * 4);
            f_.vkResetCommandBuffer(cmd_, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            f_.vkBeginCommandBuffer(cmd_, &bi);
            barrier(f_, cmd_, src.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy c1{};
            c1.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, c1.imageExtent = {w, h, 1};
            f_.vkCmdCopyBufferToImage(cmd_, up.buf, src.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c1);
            barrier(f_, cmd_, src.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
            const VkImageView v   = c->record(cmd_, src.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, w, h, passes);
            const VkImage     res = v ? c->image_of(v) : src.img;
            barrier(f_, cmd_, res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
            f_.vkCmdCopyImageToBuffer(cmd_, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rd.buf, 1, &c1);
            VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
            f_.vkEndCommandBuffer(cmd_);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1, si.pCommandBuffers = &cmd_;
            f_.vkResetFences(dev_, 1, &fence_);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                ok = f_.vkQueueSubmit(queue_, 1, &si, fence_) == VK_SUCCESS;
            }
            ok = ok && f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
            if (ok) {
                out.resize(size_t(w) * h);
                const uint8_t* p = static_cast<const uint8_t*>(rd.map);
                if (!v) std::memcpy(out.data(), p, out.size() * 4);  // a propria imagem (B8G8R8A8 = ARGB)
                else
                    for (size_t i = 0; i < out.size(); ++i, p += 4)  // RGBA -> ARGB
                        out[i] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
            }
        }
        freeImage(src), freeBuffer(up), freeBuffer(rd);
        return ok ? Result<void>() : Result<void>(Error::Io);
    }

    // SlotSource: a apresentacao fixa o slot enquanto o desenha (o renderizador nao o reaproveita)
    bool pin(uint64_t seq, VkImage& img, VkImageView& view, uint32_t& w, uint32_t& h, int& idx, VkSemaphore& wait) override {
        bool consume = true;
        return pinShared(seq, img, view, w, h, idx, &consume, wait);
    }
    // consume != nullptr: a apresentacao (outra fila) recebe o semaforo da escrita na primeira fixacao
    bool pinShared(uint64_t seq, VkImage& img, VkImageView& view, uint32_t& w, uint32_t& h, int& idx, bool* consume,
                   VkSemaphore& wait) {
        std::lock_guard<std::mutex> lock(slotMutex_);
        wait = VK_NULL_HANDLE;
        for (int i = 0; i < kSlots; ++i)
            if (seq && slots_[i].seq == seq && slots_[i].shaderRead) {
                ++slots_[i].pins;
                img = slots_[i].image.img, view = slots_[i].image.view, w = slots_[i].w, h = slots_[i].h, idx = i;
                if (consume && slots_[i].readyPending && presentQueue_ != queue_) wait = slots_[i].ready;
                if (consume) slots_[i].readyPending = false;  // a mesma fila: a ordem de submissao basta
                return true;
            }
        return false;
    }
    void unpin(int idx) override {
        std::lock_guard<std::mutex> lock(slotMutex_);
        if (idx >= 0 && idx < kSlots && slots_[idx].pins > 0) --slots_[idx].pins;
    }

    Result<void> present_image(std::span<const uint32_t> argb, uint32_t width, uint32_t height) override {
        if (argb.size() < size_t(width) * height) return Error::OutOfRange;
        frameImage_.assign(argb.begin(), argb.begin() + size_t(width) * height);
        imageW_ = width, imageH_ = height, fromImage_ = true;
        return {};
    }
    Result<void> present() override { return {}; }  // offscreen: o jogo apresenta
    void         resize(uint32_t, uint32_t) override {}

    Result<void> read_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) override {
        width = imageW_, height = imageH_;
        if (fromImage_) {
            argb = frameImage_;
            fromImage_ = false;
            return {};
        }
        if (!readback_.map) return Error::NotFound;
        const uint8_t* p = static_cast<const uint8_t*>(readback_.map);
        argb.resize(size_t(width) * height);
        for (size_t i = 0; i < argb.size(); ++i, p += 4)  // RGBA -> ARGB
            argb[i] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        return {};
    }

    const VulkanStats& stats() const override { return st_; }

private:
    VkCommandPool    pool_ = VK_NULL_HANDLE;
    VkCommandBuffer  cmd_ = VK_NULL_HANDLE;
    VkFence          fence_ = VK_NULL_HANDLE;
    uint64_t         pipelinesCreated_ = 0;  // diagnostico BS_VK_LENTOS
    uint32_t         lastLayers_ = 0, lastFrags_ = 0;  // BS_VK_LENTOS: maior lista e fragmentos do ultimo render
    VulkanStats      st_;
    bool             floatControls_ = false, denormPreserve_ = false, rawDepth_ = false;
    float            scale_ = 1.f;  // resolucao interna (N7c)
    bool             surfaceExt_ = false, swapchainExt_ = false;  // apresentacao direta possivel (N7c)
    std::unique_ptr<PresenterImpl> presenter_;
    std::unique_ptr<PostChain>     post_;
    std::mutex                     postMutex_;
    bool                           postFailed_ = false;

    // N7c: imagens na resolucao interna para a apresentacao (anel; a apresentacao fixa a que esta desenhando)
    struct Slot {
        Image    image;
        uint32_t w = 0, h = 0;
        uint64_t seq = 0;      // render guardado (0 = vazio ou em escrita)
        int      pins = 0;     // apresentacoes em curso
        bool     shaderRead = false;  // ja escrito (GENERAL)
        uint32_t version = 0;  // muda quando a imagem e recriada
        VkSemaphore ready = VK_NULL_HANDLE;  // sinalizado pela escrita, esperado pela primeira apresentacao
        bool        readyPending = false;    // sinalizado e ainda nao esperado
    };
    static constexpr int kSlots = 4;
    Slot                 slots_[kSlots];
    std::mutex           slotMutex_;
    uint64_t             slotSeq_ = 0, lastSlot_ = 0;
    int                  writeSlot_ = -1;
    uint32_t             gameW_ = 0, gameH_ = 0;  // resolucao do jogo do quadro atual
    Buffer               down_;                   // a reducao na GPU (gameW_ x gameH_)
    VkDescriptorSetLayout downLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout     downPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule       downMod_ = VK_NULL_HANDLE;
    VkPipeline           downPipe_ = VK_NULL_HANDLE;
    VkDescriptorSet      downSet_ = VK_NULL_HANDLE;

    // layouts, pipelines e amostrador (fixos)
    VkDescriptorSetLayout texLayout_ = VK_NULL_HANDLE, fragLayout_ = VK_NULL_HANDLE, resolveLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      texPool_ = VK_NULL_HANDLE, framePool_ = VK_NULL_HANDLE;
    VkPipelineLayout      gfxLayout_ = VK_NULL_HANDLE, compLayout_ = VK_NULL_HANDLE;
    VkShaderModule        vert_ = VK_NULL_HANDLE, frag_ = VK_NULL_HANDLE, fragList_ = VK_NULL_HANDLE, comp_ = VK_NULL_HANDLE;
    VkPipeline            resolve_ = VK_NULL_HANDLE;
    VkSampler             sampler_ = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, VkPipeline> pipes_;
    Tex                   dummy_;  // 1x1 para desenhos sem textura

    // alvo do quadro (recriado quando o tamanho muda)
    uint32_t        w_ = 0, h_ = 0;
    Image           color_, depth_;
    VkRenderPass    pass_ = VK_NULL_HANDLE;
    VkFramebuffer   fb_ = VK_NULL_HANDLE;
    Buffer          heads_, frags_, readback_, headerRead_, verts_, staging_, tris_;
    uint32_t        fragCapacity_ = 0;
    VkDescriptorSet fragSet_ = VK_NULL_HANDLE, resolveSet_ = VK_NULL_HANDLE;

    std::unordered_map<uint32_t, Tex> texs_;
    uint32_t                          nextTex_ = 0;
    struct Upload {
        uint32_t tex;
        size_t   offset;
    };
    std::vector<Upload>   uploads_;
    std::vector<std::byte> uploadBytes_;
    PreparedFrame         prep_;
    uint32_t              imageW_ = 0, imageH_ = 0;
    std::vector<uint32_t> frameImage_;
    bool                  fromImage_ = false;

    Error fail(const char* what, VkResult r = VK_SUCCESS) {  // motivo da falha para o diagnostico (stats().last_error)
        st_.last_error = std::string(what) + (r != VK_SUCCESS ? " (VkResult " + std::to_string(int(r)) + ")" : "");
        return Error::Io;
    }


    bool load(std::string* error) {
#ifdef _WIN32
        HMODULE m = LoadLibraryA("vulkan-1.dll");
        lib_      = m;
        if (m) f_.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(m, "vkGetInstanceProcAddr"));
#else
        lib_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (lib_) f_.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib_, "vkGetInstanceProcAddr"));
#endif
        if (!f_.gipa) {
            if (error) *error = "carregador do Vulkan nao encontrado (vulkan-1.dll)";
            return false;
        }
        f_.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(f_.gipa(VK_NULL_HANDLE, "vkCreateInstance"));
        return f_.vkCreateInstance != nullptr;
    }

    bool pick(std::string* error) {
        uint32_t n = 0;
        f_.vkEnumeratePhysicalDevices(inst_, &n, nullptr);
        std::vector<VkPhysicalDevice> devs(n);
        f_.vkEnumeratePhysicalDevices(inst_, &n, devs.data());
        std::string why = "nenhum dispositivo Vulkan";
        const char* want = std::getenv("BS_VULKAN_GPU");
        if (want && !*want) want = nullptr;
        for (int pass = 0; pass < 2 && !phys_; ++pass)
            for (VkPhysicalDevice d : devs) {
                VkPhysicalDeviceProperties p;
                f_.vkGetPhysicalDeviceProperties(d, &p);
                // BS_VULKAN_GPU=<parte do nome>: escolhe o dispositivo (testes por GPU); senao, discreta primeiro
                if (want && !std::strstr(p.deviceName, want)) continue;
                if (!want && pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;
                VkPhysicalDeviceFeatures ft;
                f_.vkGetPhysicalDeviceFeatures(d, &ft);
                if (!ft.depthClamp || !ft.fragmentStoresAndAtomics) {
                    why = std::string(p.deviceName) + " sem depthClamp/fragmentStoresAndAtomics";
                    continue;
                }
                VkFormatProperties fp;
                f_.vkGetPhysicalDeviceFormatProperties(d, VK_FORMAT_R8G8B8A8_UNORM, &fp);
                if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
                    why = std::string(p.deviceName) + " sem RGBA8 como imagem de armazenamento";
                    continue;
                }
                uint32_t nq = 0;
                f_.vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
                std::vector<VkQueueFamilyProperties> qs(nq);
                f_.vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qs.data());
                for (uint32_t q = 0; q < nq; ++q)
                    if ((qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qs[q].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                        phys_ = d, qfam_ = q, queueCount_ = qs[q].queueCount, st_.device = p.deviceName;
                        tsPeriod_ = qs[q].timestampValidBits ? p.limits.timestampPeriod : 0.f;
                        tsBits_   = qs[q].timestampValidBits;
                        st_.timestamps = tsPeriod_ > 0;
                        break;
                    }
                if (phys_) break;
            }
        if (!phys_) {
            if (error) *error = why;
            return false;
        }
        f_.vkGetPhysicalDeviceMemoryProperties(phys_, &mem_);
        {  // N7c: limite da resolucao interna
            VkPhysicalDeviceProperties p;
            f_.vkGetPhysicalDeviceProperties(phys_, &p);
            VkDeviceSize heap = 0;
            for (uint32_t i = 0; i < mem_.memoryHeapCount; ++i)
                if (mem_.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap = std::max(heap, mem_.memoryHeaps[i].size);
            const double pixels = double(heap) / 4.0 / 96.0;                       // pixels do alvo que cabem
            const double byMem  = std::sqrt(pixels * 3.0 / 4.0);                    // altura 4:3
            const double byDim  = double(p.limits.maxImageDimension2D) * 3.0 / 4.0;  // a largura e 4/3 da altura
            st_.max_internal_height = uint32_t(std::max(480.0, std::min(byMem, byDim)));
        }
        return true;
    }


    bool layouts(std::string* error) {
        VkDescriptorSetLayoutBinding tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 1, li.pBindings = &tb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &texLayout_), "layout da textura", error)) return false;
        ++g_live;
        VkDescriptorSetLayoutBinding fb[4] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        li.bindingCount = 4, li.pBindings = fb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &fragLayout_), "layout das listas", error)) return false;
        ++g_live;
        VkDescriptorSetLayoutBinding rb[3] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        li.bindingCount = 3, li.pBindings = rb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &resolveLayout_), "layout do resolve", error)) return false;
        ++g_live;

        VkDescriptorPoolSize       tps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, pi.maxSets = 8192, pi.poolSizeCount = 1, pi.pPoolSizes = &tps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &pi, nullptr, &texPool_), "pool das texturas", error)) return false;
        ++g_live;
        VkDescriptorPoolSize fps[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
                                       {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1}};
        pi.flags = 0, pi.maxSets = 3, pi.poolSizeCount = 3, pi.pPoolSizes = fps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &pi, nullptr, &framePool_), "pool do quadro", error)) return false;
        ++g_live;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = framePool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &fragLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &fragSet_), "conjunto das listas", error)) return false;
        ai.pSetLayouts = &resolveLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &resolveSet_), "conjunto do resolve", error)) return false;
        // N7c: reducao da resolucao interna (downsample.comp): imagem de armazenamento -> buffer
        VkDescriptorSetLayoutBinding db[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        li.bindingCount = 2, li.pBindings = db;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &downLayout_), "layout da reducao", error)) return false;
        ++g_live;
        ai.pSetLayouts = &downLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &downSet_), "conjunto da reducao", error)) return false;

        VkPushConstantRange        pr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};  // 64 <= 128
        VkDescriptorSetLayout      sets[2] = {texLayout_, fragLayout_};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 2, pl.pSetLayouts = sets, pl.pushConstantRangeCount = 1, pl.pPushConstantRanges = &pr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &pl, nullptr, &gfxLayout_), "layout grafico", error)) return false;
        ++g_live;
        VkPushConstantRange cpr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
        pl.setLayoutCount = 1, pl.pSetLayouts = &resolveLayout_, pl.pPushConstantRanges = &cpr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &pl, nullptr, &compLayout_), "layout do resolve", error)) return false;
        ++g_live;

        vert_     = module(kPvrVert, sizeof kPvrVert);
        // a variante com o ambiente FP que o dispositivo garante: RTE + subnormais, so RTE, ou o padrao
        if (floatControls_ && denormPreserve_) {
            frag_     = module(kPvrFragFcd, sizeof kPvrFragFcd);
            fragList_ = module(kPvrFragAutosortFcd, sizeof kPvrFragAutosortFcd);
            comp_     = module(kResolveCompFcd, sizeof kResolveCompFcd);
        } else if (floatControls_) {
            frag_     = module(kPvrFragFc, sizeof kPvrFragFc);
            fragList_ = module(kPvrFragAutosortFc, sizeof kPvrFragAutosortFc);
            comp_     = module(kResolveCompFc, sizeof kResolveCompFc);
        } else {
            frag_     = module(kPvrFrag, sizeof kPvrFrag);
            fragList_ = module(kPvrFragAutosort, sizeof kPvrFragAutosort);
            comp_     = module(kResolveComp, sizeof kResolveComp);
        }
        if (!vert_ || !frag_ || !fragList_ || !comp_) {
            if (error) *error = "shaders";
            return false;
        }
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, comp_, "main", nullptr};
        cp.layout = compLayout_;
        if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cp, nullptr, &resolve_), "pipeline do resolve", error))
            return false;
        ++g_live;
        {
            VkPushConstantRange        dpr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
            VkPipelineLayoutCreateInfo dl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            dl.setLayoutCount = 1, dl.pSetLayouts = &downLayout_, dl.pushConstantRangeCount = 1, dl.pPushConstantRanges = &dpr;
            if (!ok(f_.vkCreatePipelineLayout(dev_, &dl, nullptr, &downPipeLayout_), "layout da reducao", error)) return false;
            ++g_live;
            downMod_ = module(kDownsampleComp, sizeof kDownsampleComp);
            if (!downMod_) {
                if (error) *error = "shader da reducao";
                return false;
            }
            VkComputePipelineCreateInfo dc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            dc.stage  = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, downMod_, "main", nullptr};
            dc.layout = downPipeLayout_;
            if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &dc, nullptr, &downPipe_), "pipeline da reducao", error))
                return false;
            ++g_live;
        }

        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;  // texelFetch: a amostragem e a do shader
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (!ok(f_.vkCreateSampler(dev_, &si, nullptr, &sampler_), "amostrador", error)) return false;
        ++g_live;

        TextureDesc d;
        d.width = d.height = 1;
        const uint32_t white = 0xFFFFFFFF;
        auto r = create_texture(d, std::as_bytes(std::span<const uint32_t>(&white, 1)));
        if (!r) {
            if (error) *error = "textura vazia";
            return false;
        }
        dummy_ = texs_[r.value().value];
        texs_.erase(r.value().value);
        dummyId_ = r.value().value;
        return true;
    }
    uint32_t dummyId_ = 0;

    bool fragments(uint32_t capacity) {
        freeBuffer(frags_);
        frags_ = makeBuffer(VkDeviceSize(capacity) * kFragBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
        if (!frags_.buf) return false;
        fragCapacity_ = capacity;
        writeFrameSets();
        return true;
    }

    void writeFrameSets() {
        VkDescriptorBufferInfo fbi{frags_.buf, 0, VK_WHOLE_SIZE}, hbi{heads_.buf, 0, VK_WHOLE_SIZE}, tbi{tris_.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo  ii{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo  di{VK_NULL_HANDLE, depth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet   w[7] = {};
        for (int i = 0; i < 7; ++i) w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, w[i].descriptorCount = 1;
        w[6].dstSet = fragSet_, w[6].dstBinding = 3, w[6].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, w[6].pImageInfo = &di;
        w[5].dstSet = fragSet_, w[5].dstBinding = 2, w[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[5].pBufferInfo = &tbi;
        w[0].dstSet = fragSet_, w[0].dstBinding = 0, w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[0].pBufferInfo = &fbi;
        w[1].dstSet = fragSet_, w[1].dstBinding = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[1].pBufferInfo = &hbi;
        w[2].dstSet = resolveSet_, w[2].dstBinding = 0, w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[2].pBufferInfo = &fbi;
        w[3].dstSet = resolveSet_, w[3].dstBinding = 1, w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[3].pBufferInfo = &hbi;
        w[4].dstSet = resolveSet_, w[4].dstBinding = 2, w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, w[4].pImageInfo = &ii;
        // a posicao 5 (triangulos) so existe depois do primeiro quadro: ate la, sem ela
        VkWriteDescriptorSet list[7];
        uint32_t             n = 0;
        for (int i = 0; i < 7; ++i)
            if (i != 5 || tris_.buf) list[n++] = w[i];
        f_.vkUpdateDescriptorSets(dev_, n, list, 0, nullptr);
        ++st_.frame_set_writes;
    }

    // alvo w x h: cor RGBA8 (anexo, armazenamento do resolve, origem da leitura), profundidade D32, cabecas das
    // listas por pixel, leitura de volta
    bool target(uint32_t w, uint32_t h, uint32_t gw, uint32_t gh) {
        gameW_ = gw, gameH_ = gh;
        if (w == w_ && h == h_ && gw == downW_ && gh == downH_ && pass_) return true;
        waitIdle();
        freeTarget();
        w_ = w, h_ = h, downW_ = gw, downH_ = gh;
        color_ = makeImage(w, h, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT);
        depth_ = makeImage(w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT);
        if (!color_.img || !depth_.img) return false;
        VkAttachmentDescription at[2] = {};
        at[0].format = VK_FORMAT_R8G8B8A8_UNORM, at[0].samples = VK_SAMPLE_COUNT_1_BIT;
        at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, at[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        at[1]               = at[0];
        at[1].format = VK_FORMAT_D32_SFLOAT, at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        // subpasso 0: opacas, punch-through, translucidas na ordem; 1: translucidas em autosort (a profundidade como
        // entrada, o >= feito no shader com o 1/w do software)
        VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference ir{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkSubpassDescription  sps[2] = {};
        sps[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sps[0].colorAttachmentCount = 1, sps[0].pColorAttachments = &cr;
        sps[0].pDepthStencilAttachment = &dr;
        sps[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sps[1].colorAttachmentCount = 1, sps[1].pColorAttachments = &cr;
        sps[1].inputAttachmentCount = 1, sps[1].pInputAttachments = &ir;
        VkSubpassDependency dep[3] = {};
        dep[0].srcSubpass = VK_SUBPASS_EXTERNAL, dep[0].dstSubpass = 0;
        dep[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        dep[2].srcSubpass = 0, dep[2].dstSubpass = 1, dep[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dep[2].srcStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        // o store da profundidade no fim do subpasso 1 (mesmo DONT_CARE) e escrita em LATE_FRAGMENT_TESTS: entra no
        // destino para ficar ordenado depois da transicao de layout desta dependencia (SYNC-HAZARD-WRITE-AFTER-WRITE)
        dep[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep[2].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_SHADER_WRITE_BIT;
        dep[2].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep[1].srcSubpass = 1, dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dep[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep[1].dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 2, rp.pAttachments = at, rp.subpassCount = 2, rp.pSubpasses = sps;
        rp.dependencyCount = 3, rp.pDependencies = dep;
        if (f_.vkCreateRenderPass(dev_, &rp, nullptr, &pass_) != VK_SUCCESS) return false;
        ++g_live;
        VkImageView             views[2] = {color_.view, depth_.view};
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = pass_, fi.attachmentCount = 2, fi.pAttachments = views, fi.width = w, fi.height = h, fi.layers = 1;
        if (f_.vkCreateFramebuffer(dev_, &fi, nullptr, &fb_) != VK_SUCCESS) return false;
        ++g_live;
        heads_      = makeBuffer(kHeader + VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
        // a leitura de volta e sempre na resolucao do jogo: com a escala, a reducao feita na GPU (down_)
        readback_   = makeBuffer(VkDeviceSize(gw) * gh * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        if (w != gw || h != gh) {
            down_ = makeBuffer(VkDeviceSize(gw) * gh * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
            if (!down_.buf) return false;
            VkDescriptorImageInfo  ii{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo bi{down_.buf, 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet   dw[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
            dw[0].dstSet = downSet_, dw[0].dstBinding = 0, dw[0].descriptorCount = 1, dw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            dw[0].pImageInfo = &ii;
            dw[1].dstSet = downSet_, dw[1].dstBinding = 1, dw[1].descriptorCount = 1, dw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            dw[1].pBufferInfo = &bi;
            f_.vkUpdateDescriptorSets(dev_, 2, dw, 0, nullptr);
        }
        headerRead_ = makeBuffer(kHeader, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        if (!heads_.buf || !readback_.buf || !headerRead_.buf) return false;
        for (auto& [k, p] : pipes_) f_.vkDestroyPipeline(dev_, p, nullptr), --g_live;  // o passe mudou
        pipes_.clear();
        // 4 fragmentos por pixel na resolucao do jogo; com a escala, 2 (o quadro cresce sozinho se faltar)
        return fragments(std::max(fragCapacity_, w * h * (scale_ == 1.f ? 4u : 2u)));
    }

    uint32_t downW_ = 0, downH_ = 0;  // tamanho do jogo do alvo atual
    void freeTarget() {
        freeBuffer(down_);
        for (auto& [k, p] : pipes_) f_.vkDestroyPipeline(dev_, p, nullptr), --g_live;
        pipes_.clear();
        if (fb_) f_.vkDestroyFramebuffer(dev_, fb_, nullptr), --g_live, fb_ = VK_NULL_HANDLE;
        if (pass_) f_.vkDestroyRenderPass(dev_, pass_, nullptr), --g_live, pass_ = VK_NULL_HANDLE;
        freeImage(color_), freeImage(depth_);
        freeBuffer(heads_), freeBuffer(frags_), freeBuffer(readback_), freeBuffer(headerRead_);
    }

    static VkBlendFactor blendFactor(Blend b, bool src) {
        switch (b) {
        case Blend::Zero: return VK_BLEND_FACTOR_ZERO;
        case Blend::One: return VK_BLEND_FACTOR_ONE;
        case Blend::OtherColor: return src ? VK_BLEND_FACTOR_DST_COLOR : VK_BLEND_FACTOR_SRC_COLOR;
        case Blend::InvOtherColor: return src ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case Blend::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case Blend::InvSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case Blend::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        default: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        }
    }

    // pipeline pela parte fixa do estado: modo, comparacao e escrita de profundidade, fatores de mistura
    VkPipeline pipeline(PassMode mode, const DrawState& s) {
        uint32_t cmp = uint32_t(s.depth), zw = s.depth_write ? 1 : 0, src = 0, dst = 0;
        if (mode == PassMode::PunchThrough) cmp = 6, zw = 1;  // >= forcado; z-write disable ignorado
        if (mode == PassMode::TranslucentAutosort) cmp = 6, zw = 0;
        if (mode == PassMode::TranslucentOrdered) src = uint32_t(s.src), dst = uint32_t(s.dst);
        const uint32_t key = uint32_t(mode) | (cmp << 2) | (zw << 5) | (src << 6) | (dst << 9);
        if (auto it = pipes_.find(key); it != pipes_.end()) return it->second;

        VkPipelineShaderStageCreateInfo st[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
             mode == PassMode::TranslucentAutosort ? fragList_ : frag_, "main", nullptr}};
        VkVertexInputBindingDescription   vb{0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription va[7] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},  {1, 0, VK_FORMAT_R32_SFLOAT, 8},
                                                   {2, 0, VK_FORMAT_R32G32_SFLOAT, 12}, {3, 0, VK_FORMAT_R32_UINT, 20},
                                                   {4, 0, VK_FORMAT_R32_UINT, 24},      {5, 0, VK_FORMAT_R32_UINT, 28},
                                                   {6, 0, VK_FORMAT_R32_UINT, 32}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1, vi.pVertexBindingDescriptions = &vb;
        vi.vertexAttributeDescriptionCount = 7, vi.pVertexAttributeDescriptions = va;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1, vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        // sem recorte por z (o PVR nao recorta por profundidade): depthClamp; culling ja feito na CPU
        rs.depthClampEnable = VK_TRUE, rs.polygonMode = VK_POLYGON_MODE_FILL, rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE, rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable  = mode == PassMode::TranslucentAutosort ? VK_FALSE : VK_TRUE;  // no autosort: no shader
        ds.depthWriteEnable = zw ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = VkCompareOp(cmp);  // a mesma numeracao do ISP (0 nunca ... 7 sempre)
        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = mode == PassMode::TranslucentAutosort ? 0u : 0xFu;
        if (mode == PassMode::TranslucentOrdered) {
            cb.blendEnable         = VK_TRUE;
            cb.srcColorBlendFactor = cb.srcAlphaBlendFactor = blendFactor(s.src, true);
            cb.dstColorBlendFactor = cb.dstAlphaBlendFactor = blendFactor(s.dst, false);
            cb.colorBlendOp = cb.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cbs.attachmentCount = 1, cbs.pAttachments = &cb;
        VkDynamicState                   dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2, dy.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2, gp.pStages = st, gp.pVertexInputState = &vi, gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp, gp.pRasterizationState = &rs, gp.pMultisampleState = &ms, gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cbs, gp.pDynamicState = &dy, gp.layout = gfxLayout_, gp.renderPass = pass_;
        gp.subpass = mode == PassMode::TranslucentAutosort ? 1 : 0;
        VkPipeline p = VK_NULL_HANDLE;
        if (f_.vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
        ++pipelinesCreated_;
        ++g_live, ++st_.pipelines;
        pipes_[key] = p;
        return p;
    }

    void imageBarrier(VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags ss, VkAccessFlags sa,
                      VkPipelineStageFlags ds, VkAccessFlags da) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from, b.newLayout = to, b.srcAccessMask = sa, b.dstAccessMask = da;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img, b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        f_.vkCmdPipelineBarrier(cmd_, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void bufferBarrier(VkBuffer buf, VkPipelineStageFlags ss, VkAccessFlags sa, VkPipelineStageFlags ds, VkAccessFlags da) {
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        b.srcAccessMask = sa, b.dstAccessMask = da, b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = buf, b.offset = 0, b.size = VK_WHOLE_SIZE;
        f_.vkCmdPipelineBarrier(cmd_, ss, ds, 0, 0, nullptr, 1, &b, 0, nullptr);
    }

    Push push(const Batch& b, const FrameParams& fr, const Tex* tex) const {
        const DrawState& s = b.state;
        Push             p{};
        p.size[0] = float(w_), p.size[1] = float(h_), p.depthScale = prep_.depth_scale;
        uint32_t fl = 0;
        if (tex) {
            fl |= F_TEXTURED;
            p.texSize[0] = tex->desc.width, p.texSize[1] = tex->desc.height;
        }
        if (s.gouraud) fl |= F_GOURAUD;
        if (s.offset_color) fl |= F_OFFSET;
        if (s.use_alpha) fl |= F_USE_ALPHA;
        if (s.ignore_tex_alpha) fl |= F_IGNORE_TEX_ALPHA;
        if (s.color_clamp) fl |= F_CLAMP;
        if (s.clip == Clip::Outside) fl |= F_CLIP_OUTSIDE;
        fl |= uint32_t(s.env) << 8;
        fl |= uint32_t(s.filter == Filter::Point ? 0 : 1) << 10;
        if (s.wrap_u == Wrap::Clamp) fl |= F_CLAMP_U;
        if (s.wrap_v == Wrap::Clamp) fl |= F_CLAMP_V;
        if (s.wrap_u == Wrap::Mirror) fl |= F_FLIP_U;
        if (s.wrap_v == Wrap::Mirror) fl |= F_FLIP_V;
        if (fr.texel_center) fl |= F_TEXEL_HALF;
        fl |= F_EXACT;  // modo fiel: a cobertura do software
        if (!fr.sample_center) fl |= F_SAMPLE_CORNER;
        if (!fr.attribute_center) fl |= F_ATTR_CORNER;
        if (rawDepth_) fl |= F_RAW_DEPTH;
        // atributos (TSP) numa amostra diferente da cobertura (FPU): HALF_OFFSET bits 1 e 0
        if (fr.attribute_center && !fr.sample_center) fl |= F_ATTR_PLUS;
        if (!fr.attribute_center && fr.sample_center) fl |= F_ATTR_MINUS;
        fl |= uint32_t(b.mode) << 17;
        p.flags   = fl;
        p.clip[0] = s.clip_x0, p.clip[1] = s.clip_y0, p.clip[2] = s.clip_x1, p.clip[3] = s.clip_y1;
        p.ptRef   = uint32_t(std::lround(fr.punch_through_ref * 255));
        p.blend   = (uint32_t(s.src) << 3) | uint32_t(s.dst);
        p.clampMin = fr.clamp_min, p.clampMax = fr.clamp_max;
        p.depthBias = prep_.depth_bias;
        p.invScale  = 1.f / scale_;
        return p;
    }

    Result<void> record(const CommandList& cl) {
        const FrameParams& fr = cl.frame;
        const size_t       vbytes = std::max<size_t>(prep_.vertices.size() * sizeof(GpuVertex), 64);
        if (!ensureHost(verts_, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return fail("buffer de vertices");
        if (!prep_.vertices.empty()) std::memcpy(verts_.map, prep_.vertices.data(), prep_.vertices.size() * sizeof(GpuVertex));
        const size_t tbytes = std::max<size_t>(prep_.tri_data.size() * sizeof(uint32_t), 64);
        if (tris_.size < tbytes) {
            // o buffer novo pode receber o MESMO handle do que acabou de ser liberado (o driver reaproveita): o descritor
            // e sempre regravado. Comparar com o handle antigo deixava o descritor na memoria liberada e o shader lia uma
            // pagina sem mapeamento (VK_ERROR_DEVICE_LOST com "Invalid Read" na AMD integrada; docs/N8-7-DIAGNOSTICO.md)
            if (!ensureHost(tris_, tbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return fail("dados dos triangulos");
            ++st_.tri_reallocs;
            if (std::getenv("BS_VK_ENVIOS"))
                std::fprintf(stderr, "[render-vk] triangulos realocados no render %llu: %zu bytes\n",
                             static_cast<unsigned long long>(st_.frames + 1), size_t(tris_.size));
            writeFrameSets();
        }
        if (!prep_.tri_data.empty()) std::memcpy(tris_.map, prep_.tri_data.data(), prep_.tri_data.size() * sizeof(uint32_t));
        if (!uploadBytes_.empty() && !ensureHost(staging_, uploadBytes_.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
            return Error::Io;
        if (!uploadBytes_.empty()) std::memcpy(staging_.map, uploadBytes_.data(), uploadBytes_.size());

        f_.vkResetCommandBuffer(cmd_, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        f_.vkBeginCommandBuffer(cmd_, &bi);
        // envios de textura pendentes
        // diagnostico BS_VK_ENVIOS=1: cada envio (render, textura, tamanho, nova ou atualizada, deslocamento na area de envio)
        static const bool envios = std::getenv("BS_VK_ENVIOS") != nullptr;
        for (const Upload& u : uploads_) {
            Tex* t = u.tex == dummyId_ ? &dummy_ : &texs_[u.tex];
            if (envios)
                std::fprintf(stderr, "[render-vk] envio render %llu: textura %u %ux%u %s, deslocamento %zu de %zu\n",
                             static_cast<unsigned long long>(st_.frames + 1), u.tex, t->desc.width, t->desc.height,
                             t->ready ? "atualizada" : "nova", u.offset, uploadBytes_.size());
            imageBarrier(t->image.img, t->ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy c{};
            c.bufferOffset = u.offset, c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.imageExtent = {t->desc.width, t->desc.height, 1};
            f_.vkCmdCopyBufferToImage(cmd_, staging_.buf, t->image.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            imageBarrier(t->image.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_SHADER_READ_BIT);
            t->ready = true;
        }
        // listas por pixel vazias: contador 0, capacidade, transbordo 0, largura; cabecas = nenhuma
        const uint32_t hdr[8] = {0, fragCapacity_, 0, w_, 0, 0, 0, 0};  // contador, capacidade, transbordo, largura, maior lista
        f_.vkCmdUpdateBuffer(cmd_, heads_.buf, 0, kHeader, hdr);
        f_.vkCmdFillBuffer(cmd_, heads_.buf, kHeader, VkDeviceSize(w_) * h_ * 4, kNone);
        bufferBarrier(heads_.buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        VkClearValue clear[2] = {};
        const uint32_t c = fr.clear_color;
        clear[0].color = {{float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float(c & 0xFF) / 255.f,
                           float(c >> 24) / 255.f}};
        const float cz = !std::isfinite(fr.clear_depth) ? 0.f
                         : rawDepth_ ? fr.clear_depth
                                     : std::clamp((fr.clear_depth - prep_.depth_bias) * prep_.depth_scale, 0.f, 1.f);
        clear[1].depthStencil = {cz, 0};
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = pass_, rb.framebuffer = fb_, rb.renderArea = {{0, 0}, {w_, h_}}, rb.clearValueCount = 2, rb.pClearValues = clear;
        f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
        // com a profundidade crua, o clamp do depthClamp vai a +-1e30 (o 1/w do jogo cabe; o shader escreve gl_FragDepth)
        VkViewport vp{0, 0, float(w_), float(h_), rawDepth_ ? -1e30f : 0.f, rawDepth_ ? 1e30f : 1.f};
        f_.vkCmdSetViewport(cmd_, 0, 1, &vp);
        const VkDeviceSize zero = 0;
        f_.vkCmdBindVertexBuffers(cmd_, 0, 1, &verts_.buf, &zero);
        f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxLayout_, 1, 1, &fragSet_, 0, nullptr);
        VkPipeline bound = VK_NULL_HANDLE;
        bool       second = false;
        for (const Batch& b : prep_.batches) {
            if (b.mode == PassMode::TranslucentAutosort && !second) {
                f_.vkCmdNextSubpass(cmd_, VK_SUBPASS_CONTENTS_INLINE), second = true, bound = VK_NULL_HANDLE;
            }
            const Tex* tex = nullptr;
            if (b.state.texture) {
                auto it = texs_.find(b.state.texture.value);
                if (it != texs_.end() && it->second.ready) tex = &it->second;
            }
            VkPipeline p = pipeline(b.mode, b.state);
            if (!p) return fail("pipeline");  // o buffer de comandos e recomecado (reset) no proximo quadro
            if (p != bound) f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, p), bound = p;
            VkRect2D sc{{0, 0}, {w_, h_}};
            if (b.scissor) {  // o recorte "dentro" em pixels do jogo -> os pixels do alvo que ele cobre
                auto      lo = [&](int v) { return scale_ == 1.f ? v : int(std::floor(float(v) * scale_)); };
                auto      hi = [&](int v) { return scale_ == 1.f ? v : int(std::ceil(float(v + 1) * scale_)) - 1; };
                const int x0 = std::max(0, lo(b.state.clip_x0)), y0 = std::max(0, lo(b.state.clip_y0));
                const int x1 = std::min(int(w_) - 1, hi(b.state.clip_x1)), y1 = std::min(int(h_) - 1, hi(b.state.clip_y1));
                if (x0 > x1 || y0 > y1) continue;
                sc = {{x0, y0}, {uint32_t(x1 - x0 + 1), uint32_t(y1 - y0 + 1)}};
            }
            f_.vkCmdSetScissor(cmd_, 0, 1, &sc);
            const VkDescriptorSet set = tex ? tex->set : dummy_.set;
            f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxLayout_, 0, 1, &set, 0, nullptr);
            const Push pc = push(b, fr, tex);
            f_.vkCmdPushConstants(cmd_, gfxLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
            f_.vkCmdDraw(cmd_, b.count, 1, b.first, 0);
        }
        if (!second) f_.vkCmdNextSubpass(cmd_, VK_SUBPASS_CONTENTS_INLINE);
        f_.vkCmdEndRenderPass(cmd_);  // cor em GENERAL (dependencia externa: compute e transferencia)
        if (prep_.has_autosort) {
            f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, resolve_);
            f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, compLayout_, 0, 1, &resolveSet_, 0, nullptr);
            const uint32_t sz[2] = {w_, h_};
            f_.vkCmdPushConstants(cmd_, compLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, sz);
            f_.vkCmdDispatch(cmd_, (w_ + 7) / 8, (h_ + 7) / 8, 1);
            imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        }
        bufferBarrier(heads_.buf, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
        if (w_ == downW_ && h_ == downH_) {  // resolucao do jogo: a imagem inteira volta (o modo fiel)
            VkBufferImageCopy rc{};
            rc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, rc.imageExtent = {w_, h_, 1};
            f_.vkCmdCopyImageToBuffer(cmd_, color_.img, VK_IMAGE_LAYOUT_GENERAL, readback_.buf, 1, &rc);
        } else {
            // N7c: a imagem maior fica na GPU (slot, para a apresentacao) e so a reducao volta para a CPU
            imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
            if (writeSlot_ >= 0) {
                Slot& sl = slots_[writeSlot_];
                // GENERAL sempre (amostrado pela apresentacao e copiado pelas capturas sem transicoes); a leitura
                // anterior terminou (a fixacao so cai depois da cerca da apresentacao)
                imageBarrier(sl.image.img, sl.shaderRead ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                VkImageCopy ic{};
                ic.srcSubresource = ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, ic.extent = {w_, h_, 1};
                f_.vkCmdCopyImage(cmd_, color_.img, VK_IMAGE_LAYOUT_GENERAL, sl.image.img, VK_IMAGE_LAYOUT_GENERAL, 1, &ic);
                imageBarrier(sl.image.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
                sl.shaderRead = true;
            }
            f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, downPipe_);
            f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, downPipeLayout_, 0, 1, &downSet_, 0, nullptr);
            const uint32_t sizes[4] = {w_, h_, downW_, downH_};
            f_.vkCmdPushConstants(cmd_, downPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, sizes);
            f_.vkCmdDispatch(cmd_, (downW_ + 7) / 8, (downH_ + 7) / 8, 1);
            bufferBarrier(down_.buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferCopy dc{0, 0, VkDeviceSize(downW_) * downH_ * 4};
            f_.vkCmdCopyBuffer(cmd_, down_.buf, readback_.buf, 1, &dc);
        }
        st_.readback_bytes += uint64_t(downW_) * downH_ * 4;
        // a maior lista e escrita pelo resolve (compute): a copia espera por ele (barreira de compute para transferencia)
        bufferBarrier(heads_.buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferCopy hc{0, 0, kHeader};
        f_.vkCmdCopyBuffer(cmd_, heads_.buf, headerRead_.buf, 1, &hc);
        VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
        f_.vkEndCommandBuffer(cmd_);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1, si.pCommandBuffers = &cmd_;
        if (writeSlot_ >= 0 && w_ != downW_ && presentQueue_ != queue_ && slots_[writeSlot_].ready) {
            // a apresentacao (outra fila) espera a escrita do slot por este semaforo
            si.signalSemaphoreCount = 1, si.pSignalSemaphores = &slots_[writeSlot_].ready;
        }
        f_.vkResetFences(dev_, 1, &fence_);
        // diagnostico: BS_VK_ATRASO_MS=<ms> espera antes de cada submissao (a GPU ociosa entre quadros, como no modo
        // comparar, em que o software ocupa a CPU)
        static const int idleMs = [] {
            const char* v = std::getenv("BS_VK_ATRASO_MS");
            return v ? std::atoi(v) : 0;
        }();
        if (idleMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(idleMs));
        const auto t0 = std::chrono::steady_clock::now();
        {  // a fila e compartilhada com a apresentacao (thread da janela)
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (VkResult r = f_.vkQueueSubmit(queue_, 1, &si, fence_); r != VK_SUCCESS) return fail("vkQueueSubmit", r);
        }
        if (VkResult r = f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX); r != VK_SUCCESS)
            return fail("vkWaitForFences", r);
        // a perda do dispositivo pode sinalizar a cerca sem o trabalho feito (AMD integrada, 2026-10-22: o quadro saia
        // com a imagem anterior e so o envio seguinte acusava): a fila ociosa confirma que o quadro terminou de fato
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (VkResult r = f_.vkQueueWaitIdle(queue_); r != VK_SUCCESS) return fail("vkQueueWaitIdle", r);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        st_.gpu_ms_total += ms, st_.gpu_ms_max = std::max(st_.gpu_ms_max, ms);
        uploads_.clear();  // ja copiados (uma nova tentativa do quadro nao os repete)
        return {};
    }

    void destroy() {
        presenter_.reset();  // a apresentacao primeiro (usa o dispositivo e a cadeia)
        post_.reset();
        if (dev_) {
            f_.vkDeviceWaitIdle(dev_);
            for (Slot& sl : slots_) {
                freeImage(sl.image);
                if (sl.ready) f_.vkDestroySemaphore(dev_, sl.ready, nullptr), --g_live, sl.ready = VK_NULL_HANDLE;
            }
            if (downPipe_) f_.vkDestroyPipeline(dev_, downPipe_, nullptr), --g_live, downPipe_ = VK_NULL_HANDLE;
            if (downMod_) f_.vkDestroyShaderModule(dev_, downMod_, nullptr), --g_live, downMod_ = VK_NULL_HANDLE;
            if (downPipeLayout_) f_.vkDestroyPipelineLayout(dev_, downPipeLayout_, nullptr), --g_live, downPipeLayout_ = VK_NULL_HANDLE;
            if (downLayout_) f_.vkDestroyDescriptorSetLayout(dev_, downLayout_, nullptr), --g_live, downLayout_ = VK_NULL_HANDLE;
        }
        if (dev_) {
            f_.vkDeviceWaitIdle(dev_);
            freeTarget();
            for (auto& [id, t] : texs_) freeImage(t.image);
            texs_.clear();
            freeImage(dummy_.image);
            freeBuffer(verts_), freeBuffer(staging_), freeBuffer(tris_);
            if (resolve_) f_.vkDestroyPipeline(dev_, resolve_, nullptr), --g_live;
            for (VkShaderModule m : {vert_, frag_, fragList_, comp_})
                if (m) f_.vkDestroyShaderModule(dev_, m, nullptr), --g_live;
            if (sampler_) f_.vkDestroySampler(dev_, sampler_, nullptr), --g_live;
            if (gfxLayout_) f_.vkDestroyPipelineLayout(dev_, gfxLayout_, nullptr), --g_live;
            if (compLayout_) f_.vkDestroyPipelineLayout(dev_, compLayout_, nullptr), --g_live;
            if (texPool_) f_.vkDestroyDescriptorPool(dev_, texPool_, nullptr), --g_live;
            if (framePool_) f_.vkDestroyDescriptorPool(dev_, framePool_, nullptr), --g_live;
            for (VkDescriptorSetLayout l : {texLayout_, fragLayout_, resolveLayout_})
                if (l) f_.vkDestroyDescriptorSetLayout(dev_, l, nullptr), --g_live;
            if (fence_) f_.vkDestroyFence(dev_, fence_, nullptr), --g_live;
            if (pool_) f_.vkDestroyCommandPool(dev_, pool_, nullptr), --g_live;
            f_.vkDestroyDevice(dev_, nullptr), --g_live;
            dev_ = VK_NULL_HANDLE;
        }
        if (inst_) f_.vkDestroyInstance(inst_, nullptr), --g_live, inst_ = VK_NULL_HANDLE;
#ifdef _WIN32
        if (lib_) FreeLibrary(static_cast<HMODULE>(lib_));
#else
        if (lib_) dlclose(lib_);
#endif
        lib_ = nullptr;
    }
};

} // namespace

std::unique_ptr<VulkanDevice> VulkanDevice::create(std::string* error, const std::vector<std::string>& instance_extensions) {
    auto d = std::make_unique<VkDev>();
    if (!d->init(error, instance_extensions)) return nullptr;
    return d;
}

namespace {
struct Shared {
    std::mutex                    m;
    std::vector<std::string>      ext;
    std::weak_ptr<VulkanDevice>   dev;
    std::shared_ptr<VulkanDevice> keep;  // ate alguem perder o dispositivo, ele fica vivo (os dois lados o reusam)
    uint64_t                      generation = 0;
    std::string                   error;
    bool                          failed = false;
};
Shared& shared() {
    static Shared s;
    return s;
}
} // namespace

void vulkan_shared_configure(std::vector<std::string> instance_extensions) {
    std::lock_guard<std::mutex> lock(shared().m);
    shared().ext = std::move(instance_extensions);
}

std::shared_ptr<VulkanDevice> vulkan_shared(std::string* error) {
    Shared&                     s = shared();
    std::lock_guard<std::mutex> lock(s.m);
    if (auto d = s.dev.lock()) return d;
    if (s.failed) {  // sem Vulkan: nao tenta de novo a cada chamada
        if (error) *error = s.error;
        return nullptr;
    }
    std::string err;
    std::shared_ptr<VulkanDevice> d(VulkanDevice::create(&err, s.ext).release());
    if (!d) {
        s.failed = true, s.error = err;
        if (error) *error = err;
        return nullptr;
    }
    s.dev = d, s.keep = d, ++s.generation;
    return d;
}

void vulkan_shared_lost(const VulkanDevice* lost) {
    Shared&                     s = shared();
    std::lock_guard<std::mutex> lock(s.m);
    if (s.keep.get() == lost) s.keep.reset();
    if (auto d = s.dev.lock(); d.get() == lost) s.dev.reset();
    s.failed = false;
}

uint64_t vulkan_shared_generation() {
    std::lock_guard<std::mutex> lock(shared().m);
    return shared().generation;
}

int vulkan_live_objects() { return detail::g_live.load(); }

} // namespace bs::native::render
