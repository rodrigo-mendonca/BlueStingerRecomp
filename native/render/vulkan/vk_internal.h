#pragma once
// N7b/N7c: o que o renderizador (vk_device.cpp) e a apresentacao direta (vk_present.cpp) compartilham: a tabela de
// funcoes do Vulkan carregada em tempo de execucao (sem SDK nem biblioteca de importacao), os handles do dispositivo,
// a trava da fila (as duas threads, a do jogo e a da janela, enviam na mesma fila) e os auxiliares de memoria.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "render/vulkan/vk_device.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace bs::native::render {
namespace detail {
inline std::atomic<int> g_live{0};  // objetos Vulkan vivos (vulkan_live_objects)
}  // namespace detail
using detail::g_live;

inline constexpr uint32_t kNone = 0xFFFFFFFFu;

#define VK_GFUNCS(X) X(vkCreateInstance)
#define VK_IFUNCS(X)                                                                                           \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                        \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties)                          \
    X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) \
    X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceProperties2)
// apresentacao (VK_KHR_surface / VK_KHR_swapchain; nulas sem as extensoes)
#define VK_SFUNCS(X)                                                                                           \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                       \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkDestroySurfaceKHR)
#define VK_PFUNCS(X)                                                                                           \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR)        \
    X(vkQueuePresentKHR) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCmdCopyImage) X(vkResetDescriptorPool)    \
    X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults)
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
    VK_GFUNCS(DECL) VK_IFUNCS(DECL) VK_DFUNCS(DECL) VK_SFUNCS(DECL) VK_PFUNCS(DECL)
#undef DECL
};

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

// Dispositivo, fila e os auxiliares de memoria (o renderizador e a apresentacao usam os mesmos).
struct VkBase {
    Fn               f_;
    void*            lib_ = nullptr;
    VkInstance       inst_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_{};
    VkDevice         dev_ = VK_NULL_HANDLE;
    VkQueue          queue_ = VK_NULL_HANDLE;
    uint32_t         qfam_ = 0;
    std::mutex       queueMutex_;  // vkQueueSubmit/WaitIdle da fila do renderizador (sincronizacao externa)
    // N7c: a apresentacao usa a segunda fila da familia quando existe: vkQueuePresentKHR com V-Sync pode esperar o
    // monitor, e nessa fila isso nao segura o renderizador. Sem a segunda, a mesma fila e a mesma trava.
    VkQueue          presentQueue_ = VK_NULL_HANDLE;
    std::mutex       presentMutex_;
    uint32_t         queueCount_ = 1;  // filas da familia
    float            tsPeriod_ = 0;    // ns por tique de timestamp (0 = sem timestamps na fila)
    uint32_t         tsBits_ = 0;      // bits validos dos timestamps (36 a 64): a diferenca e modulo 2^bits
    std::mutex&      presentLock() { return presentQueue_ == queue_ ? queueMutex_ : presentMutex_; }
    void             waitIdle() {  // vkDeviceWaitIdle exige todas as filas sincronizadas
        std::scoped_lock lock(queueMutex_, presentMutex_);
        f_.vkDeviceWaitIdle(dev_);
    }

    static bool ok(VkResult r, const char* what, std::string* error) {
        if (r == VK_SUCCESS) return true;
        if (error) *error = std::string(what) + " falhou (VkResult " + std::to_string(int(r)) + ")";
        return false;
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

    static void barrier(const Fn& f, VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                        VkPipelineStageFlags ss, VkAccessFlags sa, VkPipelineStageFlags ds, VkAccessFlags da) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from, b.newLayout = to, b.srcAccessMask = sa, b.dstAccessMask = da;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img, b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        f.vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
};

// N7c: a apresentacao direta fixa o slot (imagem na resolucao interna) enquanto o desenha; o renderizador so
// reaproveita slots sem fixacao (vk_device.cpp). Os slots ficam sempre no layout GENERAL (amostrar e copiar sem
// transicoes, revisao Codex 2026-10-22). A escrita de um slot (fila do renderizador) sinaliza um semaforo; a primeira
// apresentacao que o fixa (outra fila) recebe esse semaforo em `wait` e o espera no estagio do fragmento (a dependencia
// entre as filas); as seguintes recebem VK_NULL_HANDLE. A dependencia inversa (a apresentacao terminou de ler antes de
// a escrita seguinte comecar) vem da cerca da apresentacao, esperada antes de soltar a fixacao.
struct SlotSource {
    virtual bool pin(uint64_t seq, VkImage& img, VkImageView& view, uint32_t& w, uint32_t& h, int& idx, VkSemaphore& wait) = 0;
    virtual void unpin(int idx) = 0;
    virtual class PostChain* post_chain() = 0;  // a cadeia de pos-processamento do dispositivo (nullptr = indisponivel)

protected:
    ~SlotSource() = default;
};

// N7c (secao 21): a cadeia de pos-processamento (vk_post.cpp). record grava os passes em `cmd` sobre a imagem `src`
// (w x h, no layout dado) e devolve a vista da ultima imagem (SHADER_READ_ONLY_OPTIMAL), ou VK_NULL_HANDLE se nenhum
// passe rodou. Quem grava garante que o quadro anterior terminou (as imagens e os conjuntos sao reaproveitados).
class PostChain {
public:
    virtual ~PostChain()                                                  = default;
    virtual int         load(std::span<const uint32_t> spirv, std::string* error) = 0;
    virtual int         builtin_color() const                                     = 0;
    virtual int         builtin(PostBuiltin which) const                          = 0;
    virtual std::vector<PostTiming> timings()                                     = 0;  // recolhe o ultimo quadro (ja terminado)
    virtual VkImageView record(VkCommandBuffer cmd, VkImageView src, VkImageLayout srcLayout, uint32_t w, uint32_t h,
                               std::span<const PostPass> passes)                  = 0;
    virtual VkImage     image_of(VkImageView view) const                          = 0;
};
std::unique_ptr<PostChain> make_post_chain(VkBase& base, std::string* error);

// A apresentacao direta (vk_present.cpp), no mesmo dispositivo do renderizador.
class PresenterImpl {
public:
    virtual ~PresenterImpl()                                          = default;
    virtual bool                attach(VkSurfaceKHR surface, std::string* error) = 0;
    virtual void                detach()                                         = 0;
    virtual Result<void>        present(const PresentRequest& r)                 = 0;
    virtual const PresentStats& stats() const                                    = 0;
};
std::unique_ptr<PresenterImpl> make_presenter(VkBase& base, SlotSource& slots);

}  // namespace bs::native::render
