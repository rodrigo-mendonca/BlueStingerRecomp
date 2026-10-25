#include "render/post_shader.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>

namespace bs::native::render {
namespace {

// opcodes e enums do SPIR-V usados aqui (especificacao SPIR-V 1.3)
enum : uint32_t {
    OpExtInstImport = 11, OpEntryPoint = 15, OpCapability = 17, OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21,
    OpTypeFloat = 22, OpTypeVector = 23, OpTypeMatrix = 24, OpTypeImage = 25, OpTypeSampler = 26, OpTypeSampledImage = 27,
    OpTypeArray = 28, OpTypeRuntimeArray = 29, OpTypeStruct = 30, OpTypePointer = 32, OpTypeFunction = 33, OpConstant = 43,
    OpVariable = 59, OpDecorate = 71, OpMemberDecorate = 72, OpImageWrite = 99, OpLoopMerge = 246,
};
enum : uint32_t { DecBuiltIn = 11, DecLocation = 30, DecBinding = 33, DecDescriptorSet = 34, DecOffset = 35, DecArrayStride = 6 };
enum : uint32_t { ScUniformConstant = 0, ScInput = 1, ScOutput = 3, ScPrivate = 6, ScFunction = 7, ScPushConstant = 9 };
constexpr uint32_t kFragment = 4, kFragCoord = 15, kDim2D = 1;

bool bad(std::string* e, const std::string& m) {
    if (e) *e = m;
    return false;
}

// operandos minimos (palavras depois da do opcode) de cada instrucao que o validador le
uint32_t min_operands(uint32_t op) {
    switch (op) {
    case OpCapability: return 1;
    case OpExtInstImport: return 2;   // id + string
    case OpEntryPoint: return 3;      // modelo, id, nome
    case OpDecorate: return 2;
    case OpMemberDecorate: return 3;
    case OpConstant: return 3;
    case OpVariable: return 3;
    case OpTypeVoid: case OpTypeBool: case OpTypeSampler: return 1;
    case OpTypeInt: return 3;
    case OpTypeFloat: return 2;
    case OpTypeVector: case OpTypeMatrix: return 3;
    case OpTypeImage: return 8;
    case OpTypeSampledImage: case OpTypeRuntimeArray: return 2;
    case OpTypeArray: return 3;
    case OpTypeStruct: return 1;
    case OpTypePointer: return 3;
    case OpTypeFunction: return 2;
    default: return 0;
    }
}

// a string literal comecando em `a` cabe nas `words` palavras da instrucao (terminada por zero)
bool string_in(const uint32_t* a, uint32_t words, std::string& out) {
    const char* s = reinterpret_cast<const char*>(a);
    const size_t max = size_t(words) * 4;
    const void*  z   = std::memchr(s, 0, max);
    if (!z) return false;
    out.assign(s, static_cast<const char*>(z));
    return true;
}

// `p` dentro de `root` (os dois ja canonicos), componente a componente
bool inside(const std::filesystem::path& p, const std::filesystem::path& root) {
    auto a = root.begin(), b = p.begin();
    for (; a != root.end(); ++a, ++b)
        if (b == p.end() || *a != *b) return false;
    return b != p.end();
}

} // namespace

bool validate_post_spirv(std::span<const uint32_t> w, std::string* error) {
    if (w.size() < 5 || w[0] != 0x07230203u) return bad(error, "nao e SPIR-V (numero magico)");
    if (w[1] > 0x00010300u) return bad(error, "versao do SPIR-V acima de 1.3 (Vulkan 1.1)");
    // capacidades permitidas: Matrix, Shader, ImageQuery, DerivativeControl
    const std::set<uint32_t> caps = {0, 1, 50, 51};
    std::map<uint32_t, std::vector<uint32_t>> types;  // id -> instrucao (opcode e operandos)
    std::map<uint32_t, uint32_t>              constants, varType, varClass;
    std::map<uint32_t, std::map<uint32_t, uint32_t>> dec;           // id -> decoracao -> valor
    std::map<uint32_t, std::map<uint32_t, uint32_t>> memberOffset;  // struct -> membro -> offset
    std::map<uint32_t, uint32_t>                     arrayStride;
    int                                              entries = 0;
    for (size_t i = 5; i < w.size();) {
        const uint32_t n = w[i] >> 16, op = w[i] & 0xFFFF;
        if (!n || i + n > w.size()) return bad(error, "instrucao truncada");
        if (n - 1 < min_operands(op)) return bad(error, "instrucao " + std::to_string(op) + " com operandos de menos");
        // ponteiro, nao w[i + 1]: na ultima instrucao sem operandos (OpFunctionEnd) i + 1 == size, e o operator[]
        // do span estoura no Debug; os operandos lidos ja foram limitados por min_operands acima
        const uint32_t* a = w.data() + i + 1;
        std::string     s;
        switch (op) {
        case OpCapability:
            if (!caps.count(a[0])) return bad(error, "capacidade nao permitida (" + std::to_string(a[0]) + ")");
            break;
        case OpExtInstImport:
            if (!string_in(a + 1, n - 2, s) || s != "GLSL.std.450") return bad(error, "conjunto de instrucoes estendido nao permitido");
            break;
        case OpEntryPoint:
            ++entries;
            if (a[0] != kFragment) return bad(error, "o ponto de entrada nao e um shader de fragmento");
            if (!string_in(a + 2, n - 3, s) || s != "main") return bad(error, "o ponto de entrada nao se chama main");
            break;
        // um passe de imagem nao repete: sem lacos, nenhum shader pode prender a GPU (lacos com orcamento: N9)
        case OpLoopMerge: return bad(error, "lacos nao sao permitidos (o passe tem de terminar)");
        case OpImageWrite: return bad(error, "escrita em imagem nao permitida");
        case OpDecorate:
            dec[a[0]][a[1]] = n >= 4 ? a[2] : 1;
            if (n >= 4 && a[1] == DecArrayStride) arrayStride[a[0]] = a[2];
            break;
        case OpMemberDecorate:
            if (n >= 5 && a[2] == DecOffset) memberOffset[a[0]][a[1]] = a[3];
            break;
        case OpConstant: constants[a[1]] = a[2]; break;
        case OpVariable: varType[a[1]] = a[0], varClass[a[1]] = a[2]; break;
        default:
            if (min_operands(op) && op >= OpTypeVoid && op <= OpTypeFunction)  // os tipos que o validador conhece
                types[a[0]] = std::vector<uint32_t>(w.begin() + i, w.begin() + i + n);
            else if (op >= OpTypeVoid && op <= 39)  // outros tipos (eventos, filas, pipes...): fora da interface
                return bad(error, "tipo nao permitido (" + std::to_string(op) + ")");
            break;
        }
        // atomicos e barreiras (opcodes 224..242) nao fazem sentido num passe de imagem
        if (op >= 224 && op <= 242) return bad(error, "instrucao atomica/barreira nao permitida");
        i += n;
    }
    if (entries != 1) return bad(error, "precisa de exatamente um ponto de entrada");
    auto ins = [&](uint32_t id) -> const std::vector<uint32_t>* {
        auto it = types.find(id);
        return it == types.end() ? nullptr : &it->second;
    };
    auto opOf = [&](uint32_t id) -> uint32_t {
        const auto* v = ins(id);
        return v ? ((*v)[0] & 0xFFFF) : 0;
    };
    auto pointee = [&](uint32_t ptr) -> uint32_t {
        const auto* v = ins(ptr);
        return v && ((*v)[0] & 0xFFFF) == OpTypePointer ? (*v)[3] : 0;
    };
    // tamanho exato de um tipo nas push constants: escalares, vetores, structs com Offset em todos os membros e arrays
    // com ArrayStride; matrizes e o resto (layout dependente de outras decoracoes) sao recusados. 0 = nao permitido.
    std::function<uint64_t(uint32_t, int)> size = [&](uint32_t t, int depth) -> uint64_t {
        const auto* v = ins(t);
        if (!v || depth > 8) return 0;
        switch ((*v)[0] & 0xFFFF) {
        case OpTypeInt:
        case OpTypeFloat: return (*v)[2] == 32 ? 4 : 0;
        case OpTypeVector: return (*v)[3] >= 2 && (*v)[3] <= 4 ? size((*v)[2], depth + 1) * (*v)[3] : 0;
        case OpTypeArray: {
            if (!arrayStride.count(t) || !constants.count((*v)[3])) return 0;
            const uint64_t n = constants[(*v)[3]], e = size((*v)[2], depth + 1);
            return n && e && n <= 64 ? arrayStride[t] * (n - 1) + e : 0;
        }
        case OpTypeStruct: {
            uint64_t end = 0;
            for (uint32_t m = 0; m + 2 < v->size(); ++m) {
                if (!memberOffset[t].count(m)) return 0;
                const uint64_t e = size((*v)[2 + m], depth + 1);
                if (!e) return 0;
                end = std::max(end, memberOffset[t][m] + e);
            }
            return end;
        }
        default: return 0;
        }
    };
    for (const auto& [var, cls] : varClass) {
        const uint32_t t = pointee(varType[var]);
        const auto&    d = dec[var];
        switch (cls) {
        case ScFunction:
        case ScPrivate: break;
        case ScInput:
            if (d.count(DecBuiltIn) ? d.at(DecBuiltIn) != kFragCoord : (!d.count(DecLocation) || d.at(DecLocation) != 0))
                return bad(error, "entrada fora da interface (so uv em location 0 e gl_FragCoord)");
            break;
        case ScOutput:
            if (!d.count(DecLocation) || d.at(DecLocation) != 0) return bad(error, "saida fora da interface (so location 0)");
            break;
        case ScUniformConstant: {
            const uint32_t set = d.count(DecDescriptorSet) ? d.at(DecDescriptorSet) : 99, bind = d.count(DecBinding) ? d.at(DecBinding) : 99;
            if (opOf(t) != OpTypeSampledImage) return bad(error, "so sampler2D e permitido (sem imagens de armazenamento)");
            // a imagem por tras: 2D, sem profundidade (shadow), sem array, sem multiamostra, amostrada
            const auto* im = ins((*ins(t))[2]);
            if (!im || ((*im)[0] & 0xFFFF) != OpTypeImage || (*im)[3] != kDim2D || (*im)[4] == 1 || (*im)[5] || (*im)[6] ||
                (*im)[7] != 1)
                return bad(error, "so sampler2D simples (sem 1D/3D/cubo, array, multiamostra ou shadow)");
            if (set != 0 || bind > 1) return bad(error, "sampler fora da interface (set 0, binding 0 ou 1)");
            break;
        }
        case ScPushConstant: {
            const uint64_t s = size(t, 0);
            if (!s) return bad(error, "bloco de push constants com layout nao suportado (so escalares/vetores de 32 bits, structs e arrays com offset/stride)");
            if (s > kPostPushBytes)
                return bad(error, "push constants com " + std::to_string(s) + " bytes (limite " + std::to_string(kPostPushBytes) + ")");
            break;
        }
        default: return bad(error, "classe de armazenamento nao permitida (" + std::to_string(cls) + "): sem buffers nem memoria compartilhada");
        }
    }
    return true;
}

PostShaderPackage load_post_package(const std::filesystem::path& dir) {
    PostShaderPackage p;
    p.dir  = dir;
    p.name = dir.filename().string();
    try {
        const toml::table m = toml::parse_file((dir / "manifesto.toml").string());
        if (m["formato"].value_or(0) != kPostFormat) {
            p.error = "formato do manifesto diferente de " + std::to_string(kPostFormat);
            return p;
        }
        p.name           = m["nome"].value_or(p.name);
        p.description    = m["descricao"].value_or(std::string());
        p.before_builtin = m["posicao"].value_or(std::string("depois")) == "antes";
        if (const auto* arr = m["parametros"].as_array())
            for (const auto& e : *arr) {
                const auto* t = e.as_table();
                if (!t) continue;
                PostParam q;
                q.name  = (*t)["nome"].value_or(std::string());
                q.label = (*t)["rotulo"].value_or(q.name);
                q.min = float((*t)["minimo"].value_or(0.0)), q.max = float((*t)["maximo"].value_or(1.0));
                q.def = float((*t)["padrao"].value_or(double(q.min))), q.step = float((*t)["passo"].value_or(0.05));
                if (q.name.empty() || !(q.min < q.max) || q.def < q.min || q.def > q.max || !(q.step > 0)) {
                    p.error = "parametro invalido: " + (q.name.empty() ? std::string("(sem nome)") : q.name);
                    return p;
                }
                p.params.push_back(q);
            }
        if (p.params.size() > kPostParams) {
            p.error = "mais de " + std::to_string(kPostParams) + " parametros";
            return p;
        }
        const std::string spv = m["shader"].value_or(std::string("shader.spv"));
        // o arquivo tem de ficar dentro do pacote DEPOIS de resolver links e juncoes (revisao Codex 2026-10-22)
        std::error_code ec;
        const auto      root = std::filesystem::weakly_canonical(dir, ec);
        const auto      file = std::filesystem::weakly_canonical(dir / spv, ec);
        if (ec || spv.find("..") != std::string::npos || std::filesystem::path(spv).is_absolute() || !inside(file, root)) {
            p.error = "caminho do shader fora do pacote";
            return p;
        }
        std::ifstream f(file, std::ios::binary);
        if (!f) {
            p.error = "sem o arquivo " + spv;
            return p;
        }
        std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.empty() || bytes.size() % 4 || bytes.size() > (1u << 20)) {
            p.error = "SPIR-V com tamanho invalido";
            return p;
        }
        p.spirv.resize(bytes.size() / 4);
        std::memcpy(p.spirv.data(), bytes.data(), bytes.size());
        std::string e;
        if (!validate_post_spirv(p.spirv, &e)) p.error = "shader invalido: " + e;
    } catch (const toml::parse_error& e) {
        p.error = std::string("manifesto invalido: ") + e.description().data();
    } catch (const std::exception& e) {
        p.error = e.what();
    }
    return p;
}

std::vector<PostShaderPackage> discover_post_shaders(const std::filesystem::path& root) {
    std::vector<PostShaderPackage> out;
    std::error_code                ec;
    if (!std::filesystem::is_directory(root, ec)) return out;
    const auto base = std::filesystem::weakly_canonical(root, ec);
    for (const auto& d : std::filesystem::directory_iterator(root, ec)) {
        if (!d.is_directory() || !std::filesystem::exists(d.path() / "manifesto.toml")) continue;
        std::error_code e2;
        if (!inside(std::filesystem::weakly_canonical(d.path(), e2), base) || e2) {  // pasta que aponta para fora (link)
            PostShaderPackage p;
            p.dir = d.path(), p.name = d.path().filename().string(), p.error = "pasta do pacote fora de mods\\shaders (link)";
            out.push_back(p);
            continue;
        }
        out.push_back(load_post_package(d.path()));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

} // namespace bs::native::render
