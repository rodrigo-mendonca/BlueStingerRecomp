#pragma once
// Protocolo assincrono da biblioteca de backup "bu" do Katana sobre um ISaveStore sincrono (N4,
// docs/N4-PREPARACAO.md, "Oraculo SAVE/LOAD no Flycast: resultados"). Sem Sh4Context nem RAM do
// guest: o adaptador (bu_adapter) le os argumentos, chama esta maquina e entrega os eventos que ela
// devolve aos callbacks do jogo. Testada sozinha com um store em memoria.
//
// O que o oraculo fixou (jogo original no Flycast com VMU, 2026-10-19):
//   - cada operacao termina com progresso(drive, op, k, total) para k = 1..n e depois
//     conclusao(drive, op, resultado); a conclusao vem ANTES de status = 0;
//   - insercao do cartao (op 0x12, resultado = blocos de trabalho 15) depois do init, sem pedido do
//     jogo; montagem (op 1, total 0x11, k ate 0x10); SAVE (op 0x0C, total 10 para 4 blocos, com
//     consultas "ocupada" antes); LOAD (op 0x0E, total = blocos lidos);
//   - status: -1 com operacao pendente na unidade, senao 0; exists: 0 ou FF05.
// O tempo e abstraido: cada operacao conclui num numero fixo de passadas do servidor bu (tick),
// o mesmo nos dois modos de tempo, nunca pelo relogio de parede.
#include "platform/saves.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bs::transitional::bu {

constexpr int      kDrives = 8;  // unidades da bu (a mascara do init habilita as que o jogo usa)
constexpr uint32_t kOpMount = 1, kOpRemove = 2, kOpSave = 0x0C, kOpLoad = 0x0E, kOpInsert = 0x12;
constexpr uint32_t kWorkBlocks = 15;  // resultado da insercao: o jogo aloca 15 * 512 + 32 de trabalho
// Codigos de resultado (o jogo os compara sign-extended: mov.w)
constexpr int32_t kOk = 0, kBusy = -1, kNoCard = int32_t(0xFFFFFF01), kUnformatted = int32_t(0xFFFFFF03),
                  kNotFound = int32_t(0xFFFFFF05), kCannotOpen = int32_t(0xFFFFFF07);
constexpr size_t  kDiskInfoSize = 56;  // escritos por disk_info (bloco do jogo +20 .. +75)

int32_t result_code(native::saves::SaveError e);  // erro do store -> codigo da bu

struct Event {
    enum class Kind : uint8_t { Progress, Complete } kind;
    int      drive;
    uint32_t op;
    uint32_t a = 0, b = 0;      // progresso: k, total; conclusao: a = resultado
    std::vector<std::byte> data;  // conclusao de LOAD com sucesso: bytes para o destino
};

struct Ticks {  // passadas do servidor ate concluir (>= 1)
    int insert = 1, mount = 2, save = 4, load = 1;
};

class Machine {
public:
    explicit Machine(native::saves::ISaveStore& store, Ticks t = {}) : store_(store), ticks_(t) {}

    // init(mascara): as unidades com cartao recebem a insercao na proxima passada.
    void    init(uint32_t mask);
    int32_t mount(int drive);
    int32_t disk_info(int drive, std::span<std::byte, kDiskInfoSize> out);
    int32_t exists(int drive, const std::string& name);
    int32_t save(int drive, const std::string& name, std::vector<std::byte> image);
    int32_t load(int drive, const std::string& name, uint32_t startBlock, uint32_t nblocks);
    int32_t status(int drive) const;
    int32_t last_error(int drive) const;
    int32_t exit();  // -1 com alguma unidade ocupada (como 8C08BEBC)
    // Uma passada do servidor: eventos a entregar, em ordem. Com `hold`, a unidade que concluiu continua
    // ocupada (status -1) ate release(drive): o adaptador entrega os progressos assim e libera logo
    // antes do callback de conclusao, como o oraculo (dentro dele o jogo ja consulta disk_info).
    std::vector<Event> tick(bool hold = false);
    void               release(int drive);

    bool busy(int drive) const { return valid(drive) && u_[drive].op != 0; }

private:
    struct Unit {
        bool                   enabled = false, mounted = false;
        uint32_t               op = 0;  // operacao pendente (0 = livre)
        bool                   releasing = false;  // concluida, aguardando release()
        int                    left = 0;
        std::string            name;
        std::vector<std::byte> image;
        uint32_t               start = 0, nblocks = 0;
        int32_t                lastError = 0;
    };
    static bool valid(int d) { return d >= 0 && d < kDrives; }
    bool        present(int drive) { return card_code(drive) == kOk; }
    int32_t     card_code(int drive);  // kOk, kNoCard ou kUnformatted
    int32_t     begin(int drive, uint32_t op, int ticks);
    void        finish(int drive, Unit& u, std::vector<Event>& out, bool hold);

    native::saves::ISaveStore& store_;
    Ticks                      ticks_;
    std::array<Unit, kDrives>  u_{};
};

} // namespace bs::transitional::bu
