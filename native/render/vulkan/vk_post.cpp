// N7c: cadeia de pos-processamento (docs/N7-PLANO.md secao 21). Cada passe e um shader de fragmento de tela cheia com a
// interface de render/post_shader.h: le a imagem anterior (binding 0) e a original (binding 1) e escreve numa das duas
// imagens intermediarias (RGBA8, o tamanho da fonte), alternadas. A apresentacao grava a cadeia antes do passe da
// swapchain e desenha a ultima imagem na area do jogo; as camadas nativas (menu, legendas) vem depois, sem efeito.
// Os shaders externos chegam ja validados (validate_post_spirv); aqui so o modulo e o pipeline, e uma falha deles
// tambem desativa o shader com o motivo.
#include "render/vulkan/vk_internal.h"
#include "render/post_shader.h"
#include "render/vulkan/spirv/post_color_frag.h"
#include "render/vulkan/spirv/post_crt_frag.h"
#include "render/vulkan/spirv/post_bloom_h_frag.h"
#include "render/vulkan/spirv/post_bloom_v_frag.h"
#include "render/vulkan/spirv/post_sharpen_frag.h"
#include "render/vulkan/spirv/post_tonemap_frag.h"
#include "render/vulkan/spirv/post_vert.h"

#include <cstring>
#include <map>

namespace bs::native::render {
namespace {

struct PostPush {  // o bloco `Post` dos shaders de passe
    float size[4];
    float p[kPostParams];
};
static_assert(sizeof(PostPush) == kPostPushBytes);

class Chain final : public PostChain {
public:
    explicit Chain(VkBase& b) : b_(b), f_(b.f_) {}
    ~Chain() override { destroy(); }

    bool init(std::string* error) {
        VkAttachmentDescription at{};
        at.format = VK_FORMAT_R8G8B8A8_UNORM, at.samples = VK_SAMPLE_COUNT_1_BIT, at.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at.storeOp = VK_ATTACHMENT_STORE_OP_STORE, at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription  sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sp.colorAttachmentCount = 1, sp.pColorAttachments = &cr;
        // antes: a imagem pode ter sido lida pelo passe anterior (escrita depois de leitura) ou escrita como anexo; depois:
        // o passe seguinte (ou a apresentacao) a amostra no fragmento
        VkSubpassDependency dep[2] = {};
        dep[0].srcSubpass = VK_SUBPASS_EXTERNAL, dep[0].dstSubpass = 0;
        dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[1].srcSubpass = 0, dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1, rp.pAttachments = &at, rp.subpassCount = 1, rp.pSubpasses = &sp;
        rp.dependencyCount = 2, rp.pDependencies = dep;
        if (f_.vkCreateRenderPass(b_.dev_, &rp, nullptr, &pass_) != VK_SUCCESS) return fail(error, "passe");
        ++g_live;
        VkDescriptorSetLayoutBinding tb[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 2, li.pBindings = tb;
        if (f_.vkCreateDescriptorSetLayout(b_.dev_, &li, nullptr, &setLayout_) != VK_SUCCESS) return fail(error, "layout");
        ++g_live;
        VkPushConstantRange        pr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, kPostPushBytes};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1, pl.pSetLayouts = &setLayout_, pl.pushConstantRangeCount = 1, pl.pPushConstantRanges = &pr;
        if (f_.vkCreatePipelineLayout(b_.dev_, &pl, nullptr, &layout_) != VK_SUCCESS) return fail(error, "layout do pipeline");
        ++g_live;
        vert_ = b_.module(kPostVert, sizeof kPostVert);
        if (!vert_) return fail(error, "shader de vertice");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        if (f_.vkCreateSampler(b_.dev_, &si, nullptr, &sampler_) != VK_SUCCESS) return fail(error, "amostrador");
        ++g_live;
        VkDescriptorPoolSize       ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * kMaxPasses};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = kMaxPasses, dp.poolSizeCount = 1, dp.pPoolSizes = &ps;
        if (f_.vkCreateDescriptorPool(b_.dev_, &dp, nullptr, &pool_) != VK_SUCCESS) return fail(error, "conjuntos");
        ++g_live;
        // os passes internos (interface igual a dos externos)
        auto builtin = [&](PostBuiltin b, const uint32_t* code, size_t bytes) {
            std::vector<uint32_t> w(code, code + bytes / 4);
            builtins_[size_t(b)] = load(w, error);
            return builtins_[size_t(b)] >= 0;
        };
        if (!builtin(PostBuiltin::Color, kPostColorFrag, sizeof kPostColorFrag)) return false;
        if (!builtin(PostBuiltin::Tonemap, kPostTonemapFrag, sizeof kPostTonemapFrag)) return false;
        if (!builtin(PostBuiltin::Sharpen, kPostSharpenFrag, sizeof kPostSharpenFrag)) return false;
        if (!builtin(PostBuiltin::BloomBlurH, kPostBloomHFrag, sizeof kPostBloomHFrag)) return false;
        if (!builtin(PostBuiltin::BloomBlurV, kPostBloomVFrag, sizeof kPostBloomVFrag)) return false;
        if (!builtin(PostBuiltin::Crt, kPostCrtFrag, sizeof kPostCrtFrag)) return false;
        builtinColor_ = builtins_[size_t(PostBuiltin::Color)];
        // tempo de GPU por passe: dois timestamps por passe
        if (b_.tsPeriod_ > 0) {
            VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP, qi.queryCount = 2 * kMaxPasses;
            if (f_.vkCreateQueryPool(b_.dev_, &qi, nullptr, &queries_) == VK_SUCCESS) ++g_live;
            else queries_ = VK_NULL_HANDLE;
        }
        return true;
    }

    int load(std::span<const uint32_t> spirv, std::string* error) override {
        std::lock_guard<std::mutex> lock(m_);
        VkShaderModuleCreateInfo    mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = spirv.size() * 4, mi.pCode = spirv.data();
        VkShaderModule mod = VK_NULL_HANDLE;
        if (f_.vkCreateShaderModule(b_.dev_, &mi, nullptr, &mod) != VK_SUCCESS) return fail(error, "modulo recusado pelo driver"), -1;
        ++g_live;
        VkPipeline p = pipeline(mod);
        if (!p) {
            f_.vkDestroyShaderModule(b_.dev_, mod, nullptr), --g_live;
            return fail(error, "pipeline recusado pelo driver"), -1;
        }
        shaders_.push_back({mod, p});
        return int(shaders_.size()) - 1;
    }
    int builtin_color() const override { return builtinColor_; }
    int builtin(PostBuiltin which) const override { return which < PostBuiltin::Count ? builtins_[size_t(which)] : -1; }

    std::vector<PostTiming> timings() override {
        std::lock_guard<std::mutex> lock(m_);
        collect();
        std::vector<PostTiming> out;
        for (const auto& [id, t] : timing_) out.push_back({id, t.first, t.second});
        return out;
    }

    VkImageView record(VkCommandBuffer cmd, VkImageView src, VkImageLayout srcLayout, uint32_t w, uint32_t h,
                       std::span<const PostPass> passes) override {
        std::lock_guard<std::mutex> lock(m_);
        if (passes.empty() || !w || !h) return VK_NULL_HANDLE;
        if (!targets(w, h)) return VK_NULL_HANDLE;
        collect();                                    // os tempos do quadro anterior (ja terminado)
        f_.vkResetDescriptorPool(b_.dev_, pool_, 0);  // o quadro anterior terminou (cerca de quem grava)
        if (queries_) f_.vkCmdResetQueryPool(cmd, queries_, 0, 2 * kMaxPasses);
        VkImageView   in = src;
        VkImageLayout inLayout = srcLayout;
        int           done = 0;
        // tres imagens em rodizio: a saida de um passe nunca e a entrada dele nem a imagem que o passe seguinte usa no
        // binding 1 (a entrada deste: bloom)
        std::vector<VkImageView>   inputs;
        std::vector<VkImageLayout> inputLayouts;
        for (size_t i = 0; i < passes.size(); ++i) {
            const PostPass& p = passes[i];
            if (done == int(kMaxPasses) || p.shader < 0 || p.shader >= int(shaders_.size()) || !shaders_[size_t(p.shader)].pipe) {
                inputs.push_back(in), inputLayouts.push_back(inLayout);
                continue;
            }
            inputs.push_back(in), inputLayouts.push_back(inLayout);
            // binding 1: a original, ou a entrada do passe imediatamente anterior
            VkImageView   orig = src;
            VkImageLayout origLayout = srcLayout;
            if (p.original >= 0 && size_t(p.original) + 1 == i) orig = inputs[i - 1], origLayout = inputLayouts[i - 1];
            const VkImageView keep = i + 1 < passes.size() && passes[i + 1].original == int(i) ? in : VK_NULL_HANDLE;
            int out = 0;
            while (out < 3 && (img_[out].view == in || img_[out].view == orig || img_[out].view == keep)) ++out;
            if (out == 3) break;  // nao acontece com as referencias permitidas
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = pool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &setLayout_;
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (f_.vkAllocateDescriptorSets(b_.dev_, &ai, &set) != VK_SUCCESS) break;
            VkDescriptorImageInfo ii[2] = {{sampler_, in, inLayout}, {sampler_, orig, origLayout}};
            VkWriteDescriptorSet  wr[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
            for (int k = 0; k < 2; ++k)
                wr[k].dstSet = set, wr[k].dstBinding = uint32_t(k), wr[k].descriptorCount = 1,
                wr[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, wr[k].pImageInfo = &ii[k];
            f_.vkUpdateDescriptorSets(b_.dev_, 2, wr, 0, nullptr);
            VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rb.renderPass = pass_, rb.framebuffer = fb_[out], rb.renderArea = {{0, 0}, {w_, h_}};
            if (queries_) f_.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries_, uint32_t(2 * done));
            f_.vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
            const VkViewport vp{0, 0, float(w_), float(h_), 0, 1};
            const VkRect2D   sc{{0, 0}, {w_, h_}};
            f_.vkCmdSetViewport(cmd, 0, 1, &vp);
            f_.vkCmdSetScissor(cmd, 0, 1, &sc);
            f_.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shaders_[size_t(p.shader)].pipe);
            f_.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
            PostPush q{};
            q.size[0] = float(w_), q.size[1] = float(h_), q.size[2] = 1.f / float(w_), q.size[3] = 1.f / float(h_);
            std::memcpy(q.p, p.params, sizeof q.p);
            f_.vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof q, &q);
            f_.vkCmdDraw(cmd, 3, 1, 0, 0);
            f_.vkCmdEndRenderPass(cmd);
            if (queries_) f_.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_, uint32_t(2 * done + 1));
            recorded_.push_back(p.shader);
            in = img_[out].view, inLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ++done;
        }
        return done ? in : VK_NULL_HANDLE;
    }
    VkImage image_of(VkImageView v) const override {
        for (const Image& i : img_)
            if (i.view == v) return i.img;
        return VK_NULL_HANDLE;
    }

private:
    static constexpr uint32_t kMaxPasses = 8;
    struct Shader {
        VkShaderModule mod  = VK_NULL_HANDLE;
        VkPipeline     pipe = VK_NULL_HANDLE;
    };
    VkBase&               b_;
    Fn&                   f_;
    std::mutex            m_;
    VkRenderPass          pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      layout_ = VK_NULL_HANDLE;
    VkShaderModule        vert_ = VK_NULL_HANDLE;
    VkSampler             sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool      pool_ = VK_NULL_HANDLE;
    std::vector<Shader>   shaders_;
    int                   builtinColor_ = -1;
    int                   builtins_[size_t(PostBuiltin::Count)] = {-1, -1, -1, -1, -1, -1};
    Image                 img_[3];
    VkFramebuffer         fb_[3] = {};
    VkQueryPool           queries_ = VK_NULL_HANDLE;
    std::vector<int>      recorded_;  // os shaders do quadro gravado, na ordem dos timestamps
    std::map<int, std::pair<uint64_t, double>> timing_;  // shader -> (passes, ms)

    // os tempos do quadro gravado por ultimo (quem chama garante que ele terminou)
    void collect() {
        if (!queries_ || recorded_.empty()) return recorded_.clear();
        std::vector<uint64_t> ts(recorded_.size() * 2);
        if (f_.vkGetQueryPoolResults(b_.dev_, queries_, 0, uint32_t(ts.size()), ts.size() * 8, ts.data(), 8, VK_QUERY_RESULT_64_BIT) ==
            VK_SUCCESS)
            for (size_t i = 0; i < recorded_.size(); ++i) {
                auto& t = timing_[recorded_[i]];
                ++t.first, t.second += double(ts[2 * i + 1] - ts[2 * i]) * b_.tsPeriod_ / 1e6;
            }
        recorded_.clear();
    }
    uint32_t              w_ = 0, h_ = 0;

    static bool fail(std::string* e, const char* m) {
        if (e) *e = std::string("pos-processamento: ") + m;
        return false;
    }

    bool targets(uint32_t w, uint32_t h) {
        if (w == w_ && h == h_ && fb_[0]) return true;
        freeTargets();
        for (int i = 0; i < 3; ++i) {
            img_[i] = b_.makeImage(w, h, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT);
            if (!img_[i].img) return freeTargets(), false;
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = pass_, fi.attachmentCount = 1, fi.pAttachments = &img_[i].view, fi.width = w, fi.height = h, fi.layers = 1;
            if (f_.vkCreateFramebuffer(b_.dev_, &fi, nullptr, &fb_[i]) != VK_SUCCESS) return fb_[i] = VK_NULL_HANDLE, freeTargets(), false;
            ++g_live;
        }
        w_ = w, h_ = h;
        return true;
    }
    void freeTargets() {
        for (int i = 0; i < 3; ++i) {
            if (fb_[i]) f_.vkDestroyFramebuffer(b_.dev_, fb_[i], nullptr), --g_live, fb_[i] = VK_NULL_HANDLE;
            b_.freeImage(img_[i]);
        }
        w_ = h_ = 0;
    }

    VkPipeline pipeline(VkShaderModule frag) {
        VkPipelineShaderStageCreateInfo st[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr}};
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

    void destroy() {
        if (!b_.dev_) return;
        b_.waitIdle();
        freeTargets();
        for (Shader& s : shaders_) {
            if (s.pipe) f_.vkDestroyPipeline(b_.dev_, s.pipe, nullptr), --g_live;
            if (s.mod) f_.vkDestroyShaderModule(b_.dev_, s.mod, nullptr), --g_live;
        }
        shaders_.clear();
        if (pool_) f_.vkDestroyDescriptorPool(b_.dev_, pool_, nullptr), --g_live, pool_ = VK_NULL_HANDLE;
        if (queries_) f_.vkDestroyQueryPool(b_.dev_, queries_, nullptr), --g_live, queries_ = VK_NULL_HANDLE;
        if (sampler_) f_.vkDestroySampler(b_.dev_, sampler_, nullptr), --g_live, sampler_ = VK_NULL_HANDLE;
        if (vert_) f_.vkDestroyShaderModule(b_.dev_, vert_, nullptr), --g_live, vert_ = VK_NULL_HANDLE;
        if (layout_) f_.vkDestroyPipelineLayout(b_.dev_, layout_, nullptr), --g_live, layout_ = VK_NULL_HANDLE;
        if (setLayout_) f_.vkDestroyDescriptorSetLayout(b_.dev_, setLayout_, nullptr), --g_live, setLayout_ = VK_NULL_HANDLE;
        if (pass_) f_.vkDestroyRenderPass(b_.dev_, pass_, nullptr), --g_live, pass_ = VK_NULL_HANDLE;
    }
};

} // namespace

std::unique_ptr<PostChain> make_post_chain(VkBase& base, std::string* error) {
    auto c = std::make_unique<Chain>(base);
    if (!c->init(error)) return nullptr;
    return c;
}

} // namespace bs::native::render
