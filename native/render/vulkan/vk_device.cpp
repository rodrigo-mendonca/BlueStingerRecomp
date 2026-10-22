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

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

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

std::atomic<int> g_live{0};

#define VK_GFUNCS(X) X(vkCreateInstance)
#define VK_IFUNCS(X)                                                                                           \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                        \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties)                          \
    X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) \
    X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceProperties2)
#define VK_DFUNCS(X)                                                                                           \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateCommandPool) X(vkDestroyCommandPool)                      \
    X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer)           \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdCopyBuffer)              \
    X(vkCmdFillBuffer) X(vkCmdUpdateBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkDeviceWaitIdle) X(vkCreateFence)               \
    X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkCreateBuffer) X(vkDestroyBuffer)                  \
    X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory)   \
    X(vkUnmapMemory) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory)    \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateRenderPass) X(vkDestroyRenderPass)                     \
    X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateShaderModule) X(vkDestroyShaderModule)             \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines)                           \
    X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass)              \
    X(vkCmdBindPipeline) X(vkCmdNextSubpass) X(vkCmdBindVertexBuffers) X(vkCmdDraw) X(vkCmdDispatch) X(vkCmdPushConstants)          \
    X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout)       \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets)    \
    X(vkUpdateDescriptorSets) X(vkCmdBindDescriptorSets) X(vkCreateSampler) X(vkDestroySampler)

struct Fn {
    PFN_vkGetInstanceProcAddr gipa = nullptr;
#define DECL(n) PFN_##n n = nullptr;
    VK_GFUNCS(DECL) VK_IFUNCS(DECL) VK_DFUNCS(DECL)
#undef DECL
};

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
constexpr uint32_t kNone      = 0xFFFFFFFFu;

struct Buffer {
    VkBuffer       buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    void*          map = nullptr;
    VkDeviceSize   size = 0;
};
struct Image {
    VkImage        img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView    view = VK_NULL_HANDLE;
};
struct Tex {
    TextureDesc     desc;
    Image           image;
    VkDescriptorSet set = VK_NULL_HANDLE;
    bool            ready = false;  // ja recebeu dados (layout de leitura)
};

class VkDev final : public VulkanDevice {
public:
    ~VkDev() override { destroy(); }

    bool init(std::string* error) {
        if (!load(error)) return false;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Blue Stinger Native PC", app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        if (!ok(f_.vkCreateInstance(&ici, nullptr, &inst_), "vkCreateInstance", error)) return false;
        ++g_live;
#define LOADI(n) f_.n = reinterpret_cast<PFN_##n>(f_.gipa(inst_, #n));
        VK_IFUNCS(LOADI)
#undef LOADI
        if (!pick(error)) return false;
        const float             prio = 1.0f;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = qfam_, qi.queueCount = 1, qi.pQueuePriorities = &prio;
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
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1, dci.pQueueCreateInfos = &qi, dci.pEnabledFeatures = &feat;
        dci.enabledExtensionCount = uint32_t(ext.size()), dci.ppEnabledExtensionNames = ext.data();
        st_.float_controls = floatControls_, st_.denorm_preserve = denormPreserve_, st_.raw_depth = rawDepth_;
        if (!ok(f_.vkCreateDevice(phys_, &dci, nullptr, &dev_), "vkCreateDevice", error)) return false;
        ++g_live;
#define LOADD(n) f_.n = reinterpret_cast<PFN_##n>(f_.vkGetDeviceProcAddr(dev_, #n));
        VK_DFUNCS(LOADD)
#undef LOADD
        f_.vkGetDeviceQueue(dev_, qfam_, 0, &queue_);
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
        prepare_frame(cl, prep_, true, scale_);  // modo fiel: retangulos envolventes, a cobertura e a do shader
        if (!target(scaled_size(cl.frame.width, scale_), scaled_size(cl.frame.height, scale_)))
            return fail("alvo do quadro (imagens/passe)");
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (auto r = record(cl); !r) return r;
            const uint32_t* head = static_cast<const uint32_t*>(headerRead_.map);
            const uint32_t  used = head[0], overflow = head[2];
            st_.max_layers = std::max<uint64_t>(st_.max_layers, head[4]);
            st_.fragments += std::min(used, fragCapacity_);
            if (!overflow) break;
            // a lista do pixel nao coube: cresce e refaz o MESMO quadro (sem perder fragmentos)
            ++st_.fragment_retries;
            if (!fragments(used + used / 2 + 1024)) return fail("listas por pixel (memoria)");
        }
        st_.frames++, st_.triangles += prep_.triangles, st_.culled += prep_.culled;
        st_.nonfinite_depth += prep_.nonfinite_depth, st_.batches += prep_.batches.size();
        st_.negative_depth_frames += prep_.depth_bias != 0;  // so importa sem profundidade crua
        uploads_.clear(), uploadBytes_.clear();
        imageW_ = w_, imageH_ = h_;
        return {};
    }

    void  set_scale(float s) override { scale_ = s > 0 ? s : 1.f; }
    float scale() const override { return scale_; }

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
    Fn               f_;
    void*            lib_ = nullptr;
    VkInstance       inst_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_{};
    VkDevice         dev_ = VK_NULL_HANDLE;
    VkQueue          queue_ = VK_NULL_HANDLE;
    uint32_t         qfam_ = 0;
    VkCommandPool    pool_ = VK_NULL_HANDLE;
    VkCommandBuffer  cmd_ = VK_NULL_HANDLE;
    VkFence          fence_ = VK_NULL_HANDLE;
    VulkanStats      st_;
    bool             floatControls_ = false, denormPreserve_ = false, rawDepth_ = false;
    float            scale_ = 1.f;  // resolucao interna (N7c)

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

    static bool ok(VkResult r, const char* what, std::string* error) {
        if (r == VK_SUCCESS) return true;
        if (error) *error = std::string(what) + " falhou (VkResult " + std::to_string(int(r)) + ")";
        return false;
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
                        phys_ = d, qfam_ = q, st_.device = p.deviceName;
                        break;
                    }
                if (phys_) break;
            }
        if (!phys_) {
            if (error) *error = why;
            return false;
        }
        f_.vkGetPhysicalDeviceMemoryProperties(phys_, &mem_);
        return true;
    }

    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) const {
        for (uint32_t i = 0; i < mem_.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mem_.memoryTypes[i].propertyFlags & want) == want) return i;
        return kNone;
    }

    // host: mapeado (envio); readback: mapeado e de preferencia em memoria com cache da CPU (ler da memoria
    // combinada para escrita, a sem cache, custa dezenas de ms por quadro de 640x480)
    Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host, bool readback = false) {
        Buffer             b;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size, bi.usage = usage, bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (f_.vkCreateBuffer(dev_, &bi, nullptr, &b.buf) != VK_SUCCESS) return {};
        ++g_live;
        VkMemoryRequirements mr;
        f_.vkGetBufferMemoryRequirements(dev_, b.buf, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize  = mr.size;
        const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        ai.memoryTypeIndex             = kNone;
        if (readback) ai.memoryTypeIndex = memType(mr.memoryTypeBits, hv | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (ai.memoryTypeIndex == kNone)
            ai.memoryTypeIndex = memType(mr.memoryTypeBits, host ? hv : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == kNone || f_.vkAllocateMemory(dev_, &ai, nullptr, &b.mem) != VK_SUCCESS) {
            f_.vkDestroyBuffer(dev_, b.buf, nullptr), --g_live;
            return {};
        }
        ++g_live;
        b.size = size;
        if (f_.vkBindBufferMemory(dev_, b.buf, b.mem, 0) != VK_SUCCESS ||
            (host && (f_.vkMapMemory(dev_, b.mem, 0, size, 0, &b.map) != VK_SUCCESS || !b.map))) {
            b.map = nullptr;
            freeBuffer(b);
            return {};
        }
        return b;
    }
    void freeBuffer(Buffer& b) {
        if (!b.buf) return;
        if (b.map) f_.vkUnmapMemory(dev_, b.mem);
        f_.vkDestroyBuffer(dev_, b.buf, nullptr), --g_live;
        f_.vkFreeMemory(dev_, b.mem, nullptr), --g_live;
        b = {};
    }
    bool ensureHost(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage) {
        if (b.size >= size) return true;
        freeBuffer(b);
        b = makeBuffer(std::max<VkDeviceSize>(size, 64 * 1024) * 3 / 2, usage, true);
        return b.buf != VK_NULL_HANDLE && b.map;
    }

    Image makeImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
        Image             im;
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D, ii.format = fmt, ii.extent = {w, h, 1};
        ii.mipLevels = 1, ii.arrayLayers = 1, ii.samples = VK_SAMPLE_COUNT_1_BIT, ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage, ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE, ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (f_.vkCreateImage(dev_, &ii, nullptr, &im.img) != VK_SUCCESS) return {};
        ++g_live;
        VkMemoryRequirements mr;
        f_.vkGetImageMemoryRequirements(dev_, im.img, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size, ai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == kNone || f_.vkAllocateMemory(dev_, &ai, nullptr, &im.mem) != VK_SUCCESS) {
            f_.vkDestroyImage(dev_, im.img, nullptr), --g_live;
            return {};
        }
        ++g_live;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = im.img, vi.viewType = VK_IMAGE_VIEW_TYPE_2D, vi.format = fmt, vi.subresourceRange = {aspect, 0, 1, 0, 1};
        if (f_.vkBindImageMemory(dev_, im.img, im.mem, 0) != VK_SUCCESS ||
            f_.vkCreateImageView(dev_, &vi, nullptr, &im.view) != VK_SUCCESS) {
            im.view = VK_NULL_HANDLE;
            f_.vkDestroyImage(dev_, im.img, nullptr), --g_live;
            f_.vkFreeMemory(dev_, im.mem, nullptr), --g_live;
            return {};
        }
        ++g_live;
        return im;
    }
    void freeImage(Image& im) {
        if (!im.img) return;
        f_.vkDestroyImageView(dev_, im.view, nullptr), --g_live;
        f_.vkDestroyImage(dev_, im.img, nullptr), --g_live;
        f_.vkFreeMemory(dev_, im.mem, nullptr), --g_live;
        im = {};
    }

    VkShaderModule module(const uint32_t* code, size_t bytes) {
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize = bytes, si.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        if (f_.vkCreateShaderModule(dev_, &si, nullptr, &m) == VK_SUCCESS) ++g_live;
        return m;
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
        VkDescriptorPoolSize fps[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                                       {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1}};
        pi.flags = 0, pi.maxSets = 2, pi.poolSizeCount = 3, pi.pPoolSizes = fps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &pi, nullptr, &framePool_), "pool do quadro", error)) return false;
        ++g_live;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = framePool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &fragLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &fragSet_), "conjunto das listas", error)) return false;
        ai.pSetLayouts = &resolveLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &resolveSet_), "conjunto do resolve", error)) return false;

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
    }

    // alvo w x h: cor RGBA8 (anexo, armazenamento do resolve, origem da leitura), profundidade D32, cabecas das
    // listas por pixel, leitura de volta
    bool target(uint32_t w, uint32_t h) {
        if (w == w_ && h == h_ && pass_) return true;
        f_.vkDeviceWaitIdle(dev_);
        freeTarget();
        w_ = w, h_ = h;
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
        dep[2].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep[2].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_SHADER_WRITE_BIT;
        dep[2].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
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
        readback_   = makeBuffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        headerRead_ = makeBuffer(kHeader, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        if (!heads_.buf || !readback_.buf || !headerRead_.buf) return false;
        for (auto& [k, p] : pipes_) f_.vkDestroyPipeline(dev_, p, nullptr), --g_live;  // o passe mudou
        pipes_.clear();
        // 4 fragmentos por pixel na resolucao do jogo; com a escala, 2 (o quadro cresce sozinho se faltar)
        return fragments(std::max(fragCapacity_, w * h * (scale_ == 1.f ? 4u : 2u)));
    }

    void freeTarget() {
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
            const VkBuffer old = tris_.buf;
            if (!ensureHost(tris_, tbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return fail("dados dos triangulos");
            if (tris_.buf != old) writeFrameSets();
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
        for (const Upload& u : uploads_) {
            Tex* t = u.tex == dummyId_ ? &dummy_ : &texs_[u.tex];
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
        VkBufferImageCopy rc{};
        rc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, rc.imageExtent = {w_, h_, 1};
        f_.vkCmdCopyImageToBuffer(cmd_, color_.img, VK_IMAGE_LAYOUT_GENERAL, readback_.buf, 1, &rc);
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
        f_.vkResetFences(dev_, 1, &fence_);
        // diagnostico: BS_VK_ATRASO_MS=<ms> espera antes de cada submissao (a GPU ociosa entre quadros, como no modo
        // comparar, em que o software ocupa a CPU)
        static const int idleMs = [] {
            const char* v = std::getenv("BS_VK_ATRASO_MS");
            return v ? std::atoi(v) : 0;
        }();
        if (idleMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(idleMs));
        const auto t0 = std::chrono::steady_clock::now();
        if (VkResult r = f_.vkQueueSubmit(queue_, 1, &si, fence_); r != VK_SUCCESS) return fail("vkQueueSubmit", r);
        if (VkResult r = f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX); r != VK_SUCCESS)
            return fail("vkWaitForFences", r);
        // a perda do dispositivo pode sinalizar a cerca sem o trabalho feito (AMD integrada, 2026-10-22: o quadro saia
        // com a imagem anterior e so o envio seguinte acusava): a fila ociosa confirma que o quadro terminou de fato
        if (VkResult r = f_.vkQueueWaitIdle(queue_); r != VK_SUCCESS) return fail("vkQueueWaitIdle", r);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        st_.gpu_ms_total += ms, st_.gpu_ms_max = std::max(st_.gpu_ms_max, ms);
        uploads_.clear();  // ja copiados (uma nova tentativa do quadro nao os repete)
        return {};
    }

    void destroy() {
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

std::unique_ptr<VulkanDevice> VulkanDevice::create(std::string* error) {
    auto d = std::make_unique<VkDev>();
    if (!d->init(error)) return nullptr;
    return d;
}

int vulkan_live_objects() { return g_live.load(); }

} // namespace bs::native::render
