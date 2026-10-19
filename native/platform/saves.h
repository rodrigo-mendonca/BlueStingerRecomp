#pragma once
// Saves (N4, docs/N4-PREPARACAO.md). Substitui a VMU e a biblioteca de backup: cada save e um
// arquivo logico (p.ex. "BLUESTIN.000") cujo conteudo e a imagem VMS que o jogo monta, guardada
// inteira e inalterada num envelope nativo versionado com checksum. Sincrono e sem nada do
// Dreamcast: o protocolo assincrono do jogo (ocupado, conclusao, callbacks) fica no adaptador
// transitorio (transitional/bu_adapter), que chama o store no instante da conclusao.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::saves {

constexpr size_t   kBlock          = 512;   // bloco de um cartao (o jogo conta o tamanho nessa unidade)
constexpr uint32_t kCardBlocks     = 200;   // blocos de usuario de um cartao padrao
constexpr uint32_t kCardEntries    = 200;   // arquivos por cartao (um por bloco, no maximo)
constexpr uint16_t kEnvelopeVersion = 1;

enum class SaveError : uint8_t {
    None,
    NoCard,         // unidade sem cartao
    Unformatted,    // cartao presente sem sistema de arquivos
    NotFound,       // save inexistente
    ReadOnly,       // a pasta do cartao nao aceita gravacao
    NoSpace,        // blocos livres insuficientes
    NoEntries,      // sem entrada livre no diretorio
    BadName,        // nome fora do formato de arquivo do cartao
    BadImage,       // imagem vazia, fora do multiplo de 512 ou grande demais
    Corrupt,        // envelope invalido (magic, tamanho, checksum, truncado)
    FutureVersion,  // envelope de versao mais nova que esta
    Io,             // falha do sistema de arquivos
};
const char* save_error_name(SaveError e);

struct CardState {
    bool     present = false, formatted = false, read_only = false;
    uint32_t total_blocks = 0, free_blocks = 0, total_entries = 0, free_entries = 0;
};

struct SaveEntry {
    std::string name;
    uint32_t    blocks        = 0;
    int64_t     modified_unix = 0;
};

class ISaveStore {
public:
    virtual ~ISaveStore() = default;
    // `unit` e o cartao logico (0 = primeiro).
    virtual SaveError card(int unit, CardState& out) = 0;
    virtual SaveError list(int unit, std::vector<SaveEntry>& out) = 0;
    // A imagem inteira (multiplo de 512), exatamente como foi gravada.
    virtual SaveError load(int unit, std::string_view name, std::vector<std::byte>& image) = 0;
    // So devolve None com o arquivo duravel no disco (gravado, conferido e trocado); qualquer falha
    // deixa o save anterior intacto.
    virtual SaveError save_atomic(int unit, std::string_view name, std::span<const std::byte> image) = 0;
    virtual SaveError remove(int unit, std::string_view name) = 0;
};

// ---- formato (funcoes puras, testadas sem disco) ----

// CRC do cabecalho VMS (rotina 8C08CDE2 do jogo): polinomio 0x1021, inicio 0, MSB primeiro.
uint16_t vms_crc16(std::span<const std::byte> data);
// Confere uma imagem VMS de save: tamanho multiplo de 512, campos de icone/eyecatch/payload dentro
// da imagem, CRC (campo 0x46 zerado no calculo) e zeros depois dos dados. `why` explica a falha.
bool vms_check(std::span<const std::byte> image, std::string* why = nullptr);
// Nome de arquivo de cartao: 1 a 12 caracteres de A-Z, 0-9, '_' e '.', sem "..".
bool valid_save_name(std::string_view name);
// Envelope .bsv v1 (little-endian): "BSSV", versao u16, tamanho do cabecalho u16, tamanho da
// imagem u32, CRC-32 da imagem u32, data i64, 8 reservados, imagem.
std::vector<std::byte> envelope_encode(std::span<const std::byte> image, int64_t modified_unix);
SaveError              envelope_decode(std::span<const std::byte> file, std::vector<std::byte>& image,
                                       int64_t* modified_unix = nullptr);
uint32_t               crc32(std::span<const std::byte> data);

// ---- armazenamento em disco ----

// Pontos de falha injetaveis (so testes): a escrita atomica para no ponto pedido.
enum class FaultPoint : uint8_t { CreateTemp, Write, Flush, Reread, Swap };

struct FileStoreOptions {
    int                                     units = 1;  // cartoes presentes: 0..units-1
    std::function<int64_t()>                now;        // data gravada (padrao: relogio do sistema)
    std::function<void(const std::string&)> log;        // recuperacoes e descartes
    std::function<bool(FaultPoint)>         fault;      // true = falhar neste ponto (testes)
};

// <raiz>\card<n>\<NOME>.bsv, com .bak (versao anterior) e .tmp (escrita em andamento) ao lado.
// Na abertura de cada cartao: .tmp e descartado (o jogo nunca viu aquele save concluido); destino
// invalido com .bak valido volta pelo .bak (registrado no log); os dois invalidos viram .corrompido
// (nunca apagados) e o slot fica vazio.
std::unique_ptr<ISaveStore> make_file_store(std::filesystem::path root, FileStoreOptions opt = {});

// VMS puro (o arquivo de dados como um cartao o guarda): exportar e importar sem cartao emulado.
SaveError export_vms(ISaveStore& s, int unit, std::string_view name, const std::filesystem::path& file);
SaveError import_vms(ISaveStore& s, int unit, std::string_view name, const std::filesystem::path& file);

} // namespace bs::native::saves
