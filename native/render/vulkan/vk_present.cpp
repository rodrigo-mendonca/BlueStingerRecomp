// N7c: apresentacao direta pelo Vulkan (caminho B, docs/N7-PLANO.md secao 20). O mesmo dispositivo do renderizador
// desenha na janela, numa swapchain propria:
//   1. fundo preto (faixas fora da area 4:3);
//   2. a imagem do jogo na area: um slot (a imagem na resolucao interna, que nunca sai da GPU) ou a imagem da CPU
//      (renderizador em software, filmes, telas escritas pela CPU), enviada so quando muda;
//   3. as camadas nativas (menu, legendas) na resolucao da janela, pre-multiplicadas, enviadas so quando mudam.
// Um quadro em voo: o seguinte espera a cerca do anterior (e so entao solta o slot que ele fixou). A fila e a do
// renderizador, sob a trava de VkBase.
#include "render/vulkan/vk_internal.h"
#include "render/vulkan/spirv/present_frag.h"
#include "render/vulkan/spirv/present_vert.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace bs::native::render {
namespace {

struct QuadPush {  // o layout de `Quad` em present.vert/present.frag
    float    dst[4];
    float    src[4];
    uint32_t flags;
};
static_assert(sizeof(QuadPush) == 36);

constexpr uint32_t kOpaque  = 1;  // imagem do jogo: o alfa nao conta
constexpr float    kFull[4] = {0, 0, 1, 1};

class Presenter final : public PresenterImpl {
public:
    Presenter(VkBase& b, SlotSource& s) : b_(b), slots_(s), f_(b.f_) {}
    ~Presenter() override {
        detach();
        destroyFixed();
    }

    bool attach(VkSurfaceKHR surface, std::string* error) override {
        detach();
        auto fail = [&](const std::string& m) {
            if (error) *error = m;
            return false;
        };
        if (!f_.vkGetPhysicalDeviceSurfaceSupportKHR || !f_.vkCreateSwapchainKHR) return fail("funcoes da apresentacao ausentes");
        VkBool32 sup = VK_FALSE;
        if (f_.vkGetPhysicalDeviceSurfaceSupportKHR(b_.phys_, b_.qfam_, surface, &sup) != VK_SUCCESS || !sup)
            return fail("a fila do dispositivo nao apresenta nesta janela");
        // formato sem sRGB: os valores do jogo e das camadas vao para a tela como estao (a mesma saida do SDL)
        uint32_t n = 0;
        f_.vkGetPhysicalDeviceSurfaceFormatsKHR(b_.phys_, surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fm(n);
        f_.vkGetPhysicalDeviceSurfaceFormatsKHR(b_.phys_, surface, &n, fm.data());
        VkFormat want = VK_FORMAT_UNDEFINED;
        for (VkFormat c : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
            for (const auto& x : fm)
                if (want == VK_FORMAT_UNDEFINED && x.format == c && x.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) want = c;
        if (want == VK_FORMAT_UNDEFINED) return fail("a janela nao oferece B8G8R8A8/R8G8B8A8 UNORM");
        if (want != format_) destroyFixed();
        format_  = want;
        surface_ = surface;
        if (!fixed(error)) {
            surface_ = VK_NULL_HANDLE;
            return false;
        }
        return true;  // a swapchain nasce no primeiro present (o tamanho da janela vem nele)
    }

    void detach() override {
        if (!b_.dev_) return;
        b_.waitIdle();
        releasePinned();
        destroySwapchain();
        surface_ = VK_NULL_HANDLE;
    }

    Result<void> present(const PresentRequest& r) override {
        const auto t0 = std::chrono::steady_clock::now();
        if (!surface_) return Error::NotFound;
        // o quadro anterior terminou: o slot que ele usava volta para o renderizador
        double waited = 0;
        {
            const auto tw = std::chrono::steady_clock::now();
            if (VkResult w = f_.vkWaitForFences(b_.dev_, 1, &fence_, VK_TRUE, UINT64_MAX); w != VK_SUCCESS) return fail("cerca", w);
            waited += ms(tw);
        }
        releasePinned();
        if (!r.out_w || !r.out_h) return {};  // janela minimizada
        if (!swap_ || r.out_w != sw_ || r.out_h != sh_ || r.vsync != vsync_ || recreate_)
            if (!createSwapchain(r.out_w, r.out_h, r.vsync)) return fail("swapchain", VK_SUCCESS);
        uint32_t   idx = 0;
        const auto ta  = std::chrono::steady_clock::now();
        VkResult   ar  = f_.vkAcquireNextImageKHR(b_.dev_, swap_, UINT64_MAX, imgAvail_, VK_NULL_HANDLE, &idx);
        waited += ms(ta);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR) {  // a janela mudou: refaz no proximo quadro
            recreate_ = true;
            return {};
        }
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) return fail("vkAcquireNextImageKHR", ar);
        if (ar == VK_SUBOPTIMAL_KHR) recreate_ = true;

        uploads_.clear(), staged_ = 0;
        if (sets_.size() >= 24) forgetSets();  // no inicio do quadro (o anterior terminou): ate 3 conjuntos novos por quadro
        // a imagem do jogo: o slot (GPU) ou a da CPU
        VkImageView gameView = VK_NULL_HANDLE;
        float       uv[4]    = {0, 0, 1, 1};
        bool        linear   = r.linear;
        VkImage       img      = VK_NULL_HANDLE;
        VkImageLayout gameLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkSemaphore   slotWait = VK_NULL_HANDLE;  // a escrita do slot na fila do renderizador (primeira apresentacao dele)
        uint32_t      w = 0, h = 0;
        if (r.slot_seq && slots_.pin(r.slot_seq, img, gameView, w, h, pinned_, slotWait)) {
            gameLayout = VK_IMAGE_LAYOUT_GENERAL;  // os slots ficam em GENERAL (vk_internal.h)
            const uint32_t rows = r.slot_rows ? r.slot_rows : h;
            uv[1] = float(r.slot_y0) / float(h), uv[3] = float(r.slot_y0 + rows) / float(h);
            linear = true, ++st_.slot_frames;
        } else if (r.argb && r.width && r.height) {
            if (!stage(game_, r.argb, r.width, r.height, r.argb_version)) return resync("imagem do jogo");
            gameView = game_.image.view, ++st_.cpu_frames;
        }
        bool layerOn[2] = {};
        for (uint32_t i = 0; i < r.layer_count && i < 2; ++i) {
            const PresentLayer& l = r.layers[i];
            if (!l.argb || !l.width || !l.height) continue;
            if (!stage(layer_[i], l.argb, l.width, l.height, l.version)) return resync("camada");
            layerOn[i] = true;
        }
        const bool capture = r.capture && canCapture_;
        const uint32_t srcW = pinned_ >= 0 ? w : game_.w, srcH = pinned_ >= 0 ? h : game_.h;  // a imagem do jogo  // sem TRANSFER_SRC na swapchain: captura indisponivel
        if (r.capture) r.capture->clear();
        if (capture && !ensureHost(capture_, VkDeviceSize(sw_) * sh_ * 4)) return resync("captura");

        f_.vkResetCommandBuffer(cmd_, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        f_.vkBeginCommandBuffer(cmd_, &bi);
        for (const auto& [t, offset] : uploads_) {
            VkBase::barrier(f_, cmd_, t->image.img, t->ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy c{};
            c.bufferOffset = offset, c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, c.imageExtent = {t->w, t->h, 1};
            f_.vkCmdCopyBufferToImage(cmd_, staging_.buf, t->image.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            VkBase::barrier(f_, cmd_, t->image.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
            t->ready = true;
        }
        // pos-processamento (secao 21): so a imagem do jogo, antes do passe da swapchain (as camadas vem depois, nitidas)
        if (gameView && r.post && r.post_count)
            if (PostChain* pc = slots_.post_chain()) {
                // o CRT desenha as 480 linhas do jogo: so com uma fonte de verdade alta (o slot na resolucao interna); a
                // imagem da CPU (filmes, telas escritas pela CPU) chega em 480 e fica sem ele (revisao Codex 2026-10-22)
                post_.assign(r.post, r.post + r.post_count);
                if (srcH < kCrtMinSourceHeight)
                    std::erase_if(post_, [&](const PostPass& p) { return p.shader == pc->builtin(PostBuiltin::Crt); });
                if (VkImageView v = pc->record(cmd_, gameView, gameLayout, srcW, srcH, post_))
                    gameView = v, gameLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ++st_.post_frames;
            }
        VkClearValue          clear{};
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = pass_, rb.framebuffer = fbs_[idx], rb.renderArea = {{0, 0}, {sw_, sh_}};
        rb.clearValueCount = 1, rb.pClearValues = &clear;
        f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport vp{0, 0, float(sw_), float(sh_), 0, 1};
        const VkRect2D   sc{{0, 0}, {sw_, sh_}};
        f_.vkCmdSetViewport(cmd_, 0, 1, &vp);
        f_.vkCmdSetScissor(cmd_, 0, 1, &sc);
        if (gameView) {
            const float x0 = r.area[0] / float(sw_) * 2 - 1, y0 = r.area[1] / float(sh_) * 2 - 1;
            const float x1 = (r.area[0] + r.area[2]) / float(sw_) * 2 - 1, y1 = (r.area[1] + r.area[3]) / float(sh_) * 2 - 1;
            quad(opaque_, gameView, gameLayout, linear ? linear_ : nearest_, {x0, y0, x1, y1}, uv, kOpaque);
        }
        for (int i = 0; i < 2; ++i)
            if (layerOn[i]) quad(blend_, layer_[i].image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, nearest_, {-1, -1, 1, 1}, kFull, 0);
        f_.vkCmdEndRenderPass(cmd_);
        if (capture) {
            VkBase::barrier(f_, cmd_, images_[idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy c{};
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, c.imageExtent = {sw_, sh_, 1};
            f_.vkCmdCopyImageToBuffer(cmd_, images_[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture_.buf, 1, &c);
            VkBase::barrier(f_, cmd_, images_[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
            VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
        }
        f_.vkEndCommandBuffer(cmd_);

        const VkPipelineStageFlags waitStages[2] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
        const VkSemaphore          waits[2]      = {imgAvail_, slotWait};
        VkSubmitInfo               si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = slotWait ? 2 : 1, si.pWaitSemaphores = waits, si.pWaitDstStageMask = waitStages;
        si.commandBufferCount = 1, si.pCommandBuffers = &cmd_;
        si.signalSemaphoreCount = 1, si.pSignalSemaphores = &done_[idx];
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1, pi.pWaitSemaphores = &done_[idx], pi.swapchainCount = 1, pi.pSwapchains = &swap_;
        pi.pImageIndices = &idx;
        VkResult pr = VK_SUCCESS;
        {
            std::lock_guard<std::mutex> lock(b_.presentLock());
            f_.vkResetFences(b_.dev_, 1, &fence_);  // logo antes do envio (uma falha dele refaz a cerca: resync)
            pr = f_.vkQueueSubmit(b_.presentQueue_, 1, &si, fence_);
        }
        if (pr != VK_SUCCESS) return pr == VK_ERROR_DEVICE_LOST ? fail("vkQueueSubmit", pr) : resync("vkQueueSubmit");
        {
            std::lock_guard<std::mutex> lock(b_.presentLock());
            const auto tp = std::chrono::steady_clock::now();
            pr = f_.vkQueuePresentKHR(b_.presentQueue_, &pi);  // com V-Sync pode esperar o monitor
            waited += ms(tp);
        }
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) recreate_ = true;
        else if (pr != VK_SUCCESS) return fail("vkQueuePresentKHR", pr);
        if (capture) {
            if (VkResult w = f_.vkWaitForFences(b_.dev_, 1, &fence_, VK_TRUE, UINT64_MAX); w != VK_SUCCESS) return fail("cerca", w);
            r.capture->resize(size_t(sw_) * sh_);
            const uint8_t* p = static_cast<const uint8_t*>(capture_.map);
            if (format_ == VK_FORMAT_B8G8R8A8_UNORM) std::memcpy(r.capture->data(), p, r.capture->size() * 4);  // B,G,R,A = ARGB
            else
                for (size_t i = 0; i < r.capture->size(); ++i, p += 4)
                    (*r.capture)[i] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        }
        const double c = std::max(0.0, ms(t0) - waited);
        ++st_.frames, st_.cpu_ms_total += c, st_.cpu_ms_max = std::max(st_.cpu_ms_max, c), st_.wait_ms_total += waited;
        return {};
    }

    const PresentStats& stats() const override { return st_; }

private:
    struct Tex {
        Image           image;
        uint32_t        w = 0, h = 0;
        uint64_t        version = ~0ull;
        bool            ready = false;
    };

    VkBase&      b_;
    SlotSource&  slots_;
    Fn&          f_;
    PresentStats st_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkFormat     format_ = VK_FORMAT_UNDEFINED;
    // fixos (por formato)
    VkRenderPass          pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      layout_ = VK_NULL_HANDLE;
    VkShaderModule        vert_ = VK_NULL_HANDLE, frag_ = VK_NULL_HANDLE;
    VkPipeline            opaque_ = VK_NULL_HANDLE, blend_ = VK_NULL_HANDLE;
    VkSampler             linear_ = VK_NULL_HANDLE, nearest_ = VK_NULL_HANDLE;
    VkDescriptorPool      pool_ = VK_NULL_HANDLE;
    VkCommandPool         cmdPool_ = VK_NULL_HANDLE;
    VkCommandBuffer       cmd_ = VK_NULL_HANDLE;
    VkFence               fence_ = VK_NULL_HANDLE;
    VkSemaphore           imgAvail_ = VK_NULL_HANDLE;
    std::vector<std::pair<std::pair<VkImageView, VkSampler>, VkDescriptorSet>> sets_;
    Tex                   game_, layer_[2];
    Buffer                staging_, capture_;
    std::vector<std::pair<Tex*, VkDeviceSize>> uploads_;
    VkDeviceSize          staged_ = 0;
    std::vector<PostPass> post_;  // os passes deste quadro (sem os que a fonte nao comporta)
    int                   pinned_ = -1;
    // swapchain
    VkSwapchainKHR             swap_ = VK_NULL_HANDLE;
    uint32_t                   sw_ = 0, sh_ = 0;
    bool                       vsync_ = true, recreate_ = false, canCapture_ = false;
    std::vector<VkImage>       images_;
    std::vector<VkImageView>   views_;
    std::vector<VkFramebuffer> fbs_;
    std::vector<VkSemaphore>   done_;

    static double ms(std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    }
    Error fail(const char* what, VkResult r) {
        std::fprintf(stderr, "[apresentacao] %s falhou (VkResult %d)\n", what, int(r));
        return r == VK_ERROR_DEVICE_LOST ? Error::Io : Error::Unsupported;
    }

    // depois de uma aquisicao, toda falha antes do envio deixa o semaforo da aquisicao sinalizado e a imagem presa: a
    // swapchain e as primitivas sao refeitas (revisao Codex 2026-10-22)
    Error resync(const char* what) {
        std::fprintf(stderr, "[apresentacao] %s falhou: swapchain e sincronizacao refeitas\n", what);
        b_.waitIdle();
        releasePinned();
        destroySwapchain();
        if (imgAvail_) f_.vkDestroySemaphore(b_.dev_, imgAvail_, nullptr), --g_live, imgAvail_ = VK_NULL_HANDLE;
        if (fence_) f_.vkDestroyFence(b_.dev_, fence_, nullptr), --g_live, fence_ = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo     fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (f_.vkCreateSemaphore(b_.dev_, &sc, nullptr, &imgAvail_) == VK_SUCCESS) ++g_live;
        else imgAvail_ = VK_NULL_HANDLE;
        if (f_.vkCreateFence(b_.dev_, &fi, nullptr, &fence_) == VK_SUCCESS) ++g_live;
        else fence_ = VK_NULL_HANDLE;
        if (!imgAvail_ || !fence_) surface_ = VK_NULL_HANDLE;  // sem primitivas: a apresentacao para (o app recua)
        recreate_ = true;
        return Error::Unsupported;
    }

    void releasePinned() {
        if (pinned_ >= 0) slots_.unpin(pinned_), pinned_ = -1;
    }

    bool ensureHost(Buffer& buf, VkDeviceSize size) {
        if (buf.size >= size) return true;
        b_.freeBuffer(buf);
        buf = b_.makeBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        return buf.map != nullptr;
    }

    // envio de uma imagem da CPU (ARGB = B8G8R8A8 na memoria) so quando a versao ou o tamanho mudam
    bool stage(Tex& t, const uint32_t* argb, uint32_t w, uint32_t h, uint64_t version) {
        if (t.image.img && t.w == w && t.h == h && t.version == version) return true;
        if (!t.image.img || t.w != w || t.h != h) {
            b_.freeImage(t.image);
            t.image = b_.makeImage(w, h, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT);
            if (!t.image.img) return false;
            t.w = w, t.h = h, t.ready = false;
            forgetSets();  // a vista antiga pode ter um conjunto em cache
        }
        const VkDeviceSize bytes = VkDeviceSize(w) * h * 4;
        // o staging cresce antes dos envios do quadro serem gravados (todos usam o mesmo buffer)
        if (staging_.size < staged_ + bytes) {
            Buffer bigger = b_.makeBuffer(std::max<VkDeviceSize>((staged_ + bytes) * 3 / 2, 4u << 20), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
            if (!bigger.map) return false;
            if (staged_) std::memcpy(bigger.map, staging_.map, size_t(staged_));
            b_.freeBuffer(staging_);
            staging_ = bigger;
        }
        std::memcpy(static_cast<uint8_t*>(staging_.map) + staged_, argb, size_t(bytes));
        uploads_.push_back({&t, staged_});
        staged_ += bytes, st_.upload_bytes += bytes;
        t.version = version;
        return true;
    }

    VkDescriptorSet set(VkImageView view, VkImageLayout layout, VkSampler sampler) {
        for (const auto& [k, s] : sets_)
            if (k.first == view && k.second == sampler) return s;  // a mesma vista tem sempre o mesmo layout
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &setLayout_;
        VkDescriptorSet s = VK_NULL_HANDLE;
        if (f_.vkAllocateDescriptorSets(b_.dev_, &ai, &s) != VK_SUCCESS) return VK_NULL_HANDLE;
        VkDescriptorImageInfo ii{sampler, view, layout};
        VkWriteDescriptorSet  w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = s, w.descriptorCount = 1, w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w.pImageInfo = &ii;
        f_.vkUpdateDescriptorSets(b_.dev_, 1, &w, 0, nullptr);
        sets_.push_back({{view, sampler}, s});
        return s;
    }
    void forgetSets() {
        if (pool_ && !sets_.empty()) f_.vkResetDescriptorPool(b_.dev_, pool_, 0);
        sets_.clear();
    }

    void quad(VkPipeline p, VkImageView view, VkImageLayout layout, VkSampler sampler, std::initializer_list<float> dst,
              const float* src, uint32_t flags) {
        const VkDescriptorSet s = set(view, layout, sampler);
        if (!s) return;
        QuadPush q{};
        std::copy(dst.begin(), dst.end(), q.dst);
        std::copy(src, src + 4, q.src);
        q.flags = flags;
        f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
        f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &s, 0, nullptr);
        f_.vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof q, &q);
        f_.vkCmdDraw(cmd_, 6, 1, 0, 0);
    }

    // passe, pipelines, amostradores, conjuntos e sincronizacao (uma vez por formato da janela)
    bool fixed(std::string* error) {
        if (pass_) return true;
        auto bad = [&](const char* m) {
            if (error) *error = m;
            destroyFixed();
            return false;
        };
        VkAttachmentDescription at{};
        at.format = format_, at.samples = VK_SAMPLE_COUNT_1_BIT, at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at.storeOp = VK_ATTACHMENT_STORE_OP_STORE, at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription  sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sp.colorAttachmentCount = 1, sp.pColorAttachments = &cr;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL, dep.dstSubpass = 0;
        dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1, rp.pAttachments = &at, rp.subpassCount = 1, rp.pSubpasses = &sp;
        rp.dependencyCount = 1, rp.pDependencies = &dep;
        if (f_.vkCreateRenderPass(b_.dev_, &rp, nullptr, &pass_) != VK_SUCCESS) return bad("passe da apresentacao");
        ++g_live;
        VkDescriptorSetLayoutBinding    tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 1, li.pBindings = &tb;
        if (f_.vkCreateDescriptorSetLayout(b_.dev_, &li, nullptr, &setLayout_) != VK_SUCCESS) return bad("layout");
        ++g_live;
        VkPushConstantRange        pr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(QuadPush)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1, pl.pSetLayouts = &setLayout_, pl.pushConstantRangeCount = 1, pl.pPushConstantRanges = &pr;
        if (f_.vkCreatePipelineLayout(b_.dev_, &pl, nullptr, &layout_) != VK_SUCCESS) return bad("layout do pipeline");
        ++g_live;
        vert_ = b_.module(kPresentVert, sizeof kPresentVert), frag_ = b_.module(kPresentFrag, sizeof kPresentFrag);
        if (!vert_ || !frag_) return bad("shaders da apresentacao");
        opaque_ = pipeline(false), blend_ = pipeline(true);
        if (!opaque_ || !blend_) return bad("pipelines da apresentacao");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        if (f_.vkCreateSampler(b_.dev_, &si, nullptr, &linear_) != VK_SUCCESS) return bad("amostrador");
        ++g_live;
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        if (f_.vkCreateSampler(b_.dev_, &si, nullptr, &nearest_) != VK_SUCCESS) return bad("amostrador");
        ++g_live;
        VkDescriptorPoolSize       ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = 32, dp.poolSizeCount = 1, dp.pPoolSizes = &ps;
        if (f_.vkCreateDescriptorPool(b_.dev_, &dp, nullptr, &pool_) != VK_SUCCESS) return bad("conjuntos");
        ++g_live;
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, cp.queueFamilyIndex = b_.qfam_;
        if (f_.vkCreateCommandPool(b_.dev_, &cp, nullptr, &cmdPool_) != VK_SUCCESS) return bad("comandos");
        ++g_live;
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = cmdPool_, ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, ca.commandBufferCount = 1;
        if (f_.vkAllocateCommandBuffers(b_.dev_, &ca, &cmd_) != VK_SUCCESS) return bad("comandos");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // o primeiro present nao espera
        if (f_.vkCreateFence(b_.dev_, &fi, nullptr, &fence_) != VK_SUCCESS) return bad("cerca");
        ++g_live;
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (f_.vkCreateSemaphore(b_.dev_, &sc, nullptr, &imgAvail_) != VK_SUCCESS) return bad("semaforo");
        ++g_live;
        return true;
    }

    VkPipeline pipeline(bool blend) {
        VkPipelineShaderStageCreateInfo st[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag_, "main", nullptr}};
        VkPipelineVertexInputStateCreateInfo   vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1, vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL, rs.cullMode = VK_CULL_MODE_NONE, rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = 0xF;
        if (blend) {  // pre-multiplicado: dst = src + dst * (1 - alfa)
            cb.blendEnable         = VK_TRUE;
            cb.srcColorBlendFactor = cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cb.dstColorBlendFactor = cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cb.colorBlendOp = cb.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cbs.attachmentCount = 1, cbs.pAttachments = &cb;
        VkDynamicState                   dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2, dy.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2, gp.pStages = st, gp.pVertexInputState = &vi, gp.pInputAssemblyState = &ia, gp.pViewportState = &vp;
        gp.pRasterizationState = &rs, gp.pMultisampleState = &ms, gp.pColorBlendState = &cbs, gp.pDynamicState = &dy;
        gp.layout = layout_, gp.renderPass = pass_;
        VkPipeline p = VK_NULL_HANDLE;
        if (f_.vkCreateGraphicsPipelines(b_.dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
        ++g_live;
        return p;
    }

    bool createSwapchain(uint32_t w, uint32_t h, bool vsync) {
        b_.waitIdle();
        VkSurfaceCapabilitiesKHR caps{};
        if (f_.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(b_.phys_, surface_, &caps) != VK_SUCCESS) return false;
        VkExtent2D ext{w, h};
        if (caps.currentExtent.width != 0xFFFFFFFFu) ext = caps.currentExtent;
        ext.width  = std::clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        if (!ext.width || !ext.height) return false;
        uint32_t n = 0;
        f_.vkGetPhysicalDeviceSurfacePresentModesKHR(b_.phys_, surface_, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        f_.vkGetPhysicalDeviceSurfacePresentModesKHR(b_.phys_, surface_, &n, modes.data());
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // V-Sync (sempre disponivel)
        if (!vsync)
            for (VkPresentModeKHR m : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
                if (mode == VK_PRESENT_MODE_FIFO_KHR && std::find(modes.begin(), modes.end(), m) != modes.end()) mode = m;
        uint32_t count = std::max(caps.minImageCount, 2u) + 1;
        if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface_, ci.minImageCount = count, ci.imageFormat = format_;
        ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, ci.imageExtent = ext, ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                : VkCompositeAlphaFlagBitsKHR(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
        ci.presentMode = mode, ci.clipped = VK_TRUE, ci.oldSwapchain = swap_;
        VkSwapchainKHR next = VK_NULL_HANDLE;
        const VkResult r    = f_.vkCreateSwapchainKHR(b_.dev_, &ci, nullptr, &next);
        destroySwapchain();  // a antiga (aposentada pela nova) e os recursos dela
        if (r != VK_SUCCESS) return false;
        ++g_live;
        // tudo em temporarios: so uma swapchain completa vira a atual (uma falha no meio nao deixa vetores incompletos)
        std::vector<VkImage>       images;
        std::vector<VkImageView>   views;
        std::vector<VkFramebuffer> fbs;
        std::vector<VkSemaphore>   done;
        auto drop = [&] {
            for (VkFramebuffer fb : fbs) f_.vkDestroyFramebuffer(b_.dev_, fb, nullptr), --g_live;
            for (VkImageView v : views) f_.vkDestroyImageView(b_.dev_, v, nullptr), --g_live;
            for (VkSemaphore x : done) f_.vkDestroySemaphore(b_.dev_, x, nullptr), --g_live;
            f_.vkDestroySwapchainKHR(b_.dev_, next, nullptr), --g_live;
            recreate_ = true;
            return false;
        };
        n = 0;
        f_.vkGetSwapchainImagesKHR(b_.dev_, next, &n, nullptr);
        images.resize(n);
        f_.vkGetSwapchainImagesKHR(b_.dev_, next, &n, images.data());
        for (VkImage im : images) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = im, vi.viewType = VK_IMAGE_VIEW_TYPE_2D, vi.format = format_;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView v = VK_NULL_HANDLE;
            if (f_.vkCreateImageView(b_.dev_, &vi, nullptr, &v) != VK_SUCCESS) return drop();
            views.push_back(v), ++g_live;
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = pass_, fi.attachmentCount = 1, fi.pAttachments = &v, fi.width = ext.width, fi.height = ext.height, fi.layers = 1;
            VkFramebuffer fb = VK_NULL_HANDLE;
            if (f_.vkCreateFramebuffer(b_.dev_, &fi, nullptr, &fb) != VK_SUCCESS) return drop();
            fbs.push_back(fb), ++g_live;
            VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore           x = VK_NULL_HANDLE;
            if (f_.vkCreateSemaphore(b_.dev_, &sc, nullptr, &x) != VK_SUCCESS) return drop();
            done.push_back(x), ++g_live;
        }
        swap_ = next, images_ = std::move(images), views_ = std::move(views), fbs_ = std::move(fbs), done_ = std::move(done);
        sw_ = ext.width, sh_ = ext.height, vsync_ = vsync, recreate_ = false, ++st_.recreated;
        canCapture_ = (ci.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (!canCapture_) std::fprintf(stderr, "[apresentacao] sem captura (a janela nao permite)\n");
        return true;
    }

    void destroySwapchain() {
        for (VkFramebuffer fb : fbs_) f_.vkDestroyFramebuffer(b_.dev_, fb, nullptr), --g_live;
        for (VkImageView v : views_) f_.vkDestroyImageView(b_.dev_, v, nullptr), --g_live;
        for (VkSemaphore s : done_) f_.vkDestroySemaphore(b_.dev_, s, nullptr), --g_live;
        fbs_.clear(), views_.clear(), done_.clear(), images_.clear();
        if (swap_) f_.vkDestroySwapchainKHR(b_.dev_, swap_, nullptr), --g_live, swap_ = VK_NULL_HANDLE;
        sw_ = sh_ = 0;
    }

    void destroyFixed() {
        if (!b_.dev_) return;
        b_.waitIdle();
        releasePinned();
        destroySwapchain();
        forgetSets();
        b_.freeImage(game_.image), b_.freeImage(layer_[0].image), b_.freeImage(layer_[1].image);
        game_ = {}, layer_[0] = {}, layer_[1] = {};
        b_.freeBuffer(staging_), b_.freeBuffer(capture_);
        if (imgAvail_) f_.vkDestroySemaphore(b_.dev_, imgAvail_, nullptr), --g_live, imgAvail_ = VK_NULL_HANDLE;
        if (fence_) f_.vkDestroyFence(b_.dev_, fence_, nullptr), --g_live, fence_ = VK_NULL_HANDLE;
        if (cmdPool_) f_.vkDestroyCommandPool(b_.dev_, cmdPool_, nullptr), --g_live, cmdPool_ = VK_NULL_HANDLE;
        cmd_ = VK_NULL_HANDLE;
        if (pool_) f_.vkDestroyDescriptorPool(b_.dev_, pool_, nullptr), --g_live, pool_ = VK_NULL_HANDLE;
        for (VkPipeline* p : {&opaque_, &blend_})
            if (*p) f_.vkDestroyPipeline(b_.dev_, *p, nullptr), --g_live, *p = VK_NULL_HANDLE;
        for (VkSampler* s : {&linear_, &nearest_})
            if (*s) f_.vkDestroySampler(b_.dev_, *s, nullptr), --g_live, *s = VK_NULL_HANDLE;
        for (VkShaderModule* m : {&vert_, &frag_})
            if (*m) f_.vkDestroyShaderModule(b_.dev_, *m, nullptr), --g_live, *m = VK_NULL_HANDLE;
        if (layout_) f_.vkDestroyPipelineLayout(b_.dev_, layout_, nullptr), --g_live, layout_ = VK_NULL_HANDLE;
        if (setLayout_) f_.vkDestroyDescriptorSetLayout(b_.dev_, setLayout_, nullptr), --g_live, setLayout_ = VK_NULL_HANDLE;
        if (pass_) f_.vkDestroyRenderPass(b_.dev_, pass_, nullptr), --g_live, pass_ = VK_NULL_HANDLE;
    }
};

} // namespace

std::unique_ptr<PresenterImpl> make_presenter(VkBase& base, SlotSource& slots) {
    return std::make_unique<Presenter>(base, slots);
}

} // namespace bs::native::render
