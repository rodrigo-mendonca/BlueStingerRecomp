#pragma once
// Execucao do jogo recompilado numa thread propria, sem janela: boot nativo, ritmo do
// Dreamcast (VBlank de 59,94 Hz), publicacao do quadro exibido a cada VBlank e
// encerramento limpo. O front-end (SDL3, app/main_sdl.cpp) so apresenta quadros e entrega
// eventos; a frequencia de apresentacao dele e independente da do jogo.
#include "cpu/sh4_context.h"
#include "platform/data_files.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bs {

struct GameFrame {
    std::vector<uint32_t> argb;  // ARGB8888
    uint32_t              width = 0, height = 0;
};

// Desempenho medido na thread do jogo. "Trabalho" = tempo de host gasto emulando um campo
// (entre dois VBlanks, sem contar a espera do ritmo); orcamento de um campo: 16,683 ms.
struct GameStats {
    uint64_t vblanks = 0;
    double   workAvgMs = 0, workMaxMs = 0;  // media e maximo desde o inicio
    uint64_t overBudget = 0;                // campos cujo trabalho passou do orcamento
    double   fieldsPerSecond = 0;           // VBlanks por segundo de relogio (ultimo segundo)
};

// Provedor dos arquivos do jogo (N1, docs/N1-PLANO.md): nativo (padrao) = API gdFs sobre a
// pasta de recursos (data/disc) e mods/assets; legado = GD-ROM HLE por setores (referencia e
// rollback).
enum class FilesProvider { Native, Legacy };

// Provedor de tempo e eventos (N2a, docs/N2-PLANO.md): nativo (padrao) = relogio logico e fila
// de eventos de native/; legado = calculo preguicoso por ciclos (referencia e rollback);
// experimental (N2b-2, docs/N2b2-RELATORIO.md) = o tempo logico deixa de vir dos ciclos e so anda
// nos pontos de espera, ate o proximo evento. O padrao continua nativo.
enum class TimeProviderKind { Native, Legacy, Experimental };

struct GameOptions {
    // dados do jogo preparados pelo Builder (pasta data/ da instalacao); o jogo nao le o CDI
    std::filesystem::path data;
    std::filesystem::path flash;  // flash de 128 KB opcional (sem: apagada, 0xFF)
    std::filesystem::path modAssets;  // mods de arquivos (mods\assets), opcional
    FilesProvider         files = FilesProvider::Native;
    TimeProviderKind      time  = TimeProviderKind::Native;
    // Registra a tabela do codigo recompilado (bsg::register_functions no executavel).
    std::function<void()> registerFunctions;
    // Prepara RAM e registradores. Padrao: boot nativo a partir de `data`. Testes trocam.
    std::function<void(Sh4Context&)> prepare;
    bool     realtime    = true;  // segura o jogo em 59,94 VBlanks por segundo
    uint64_t vblankLimit = 0;     // > 0: para sozinho depois desses VBlanks (headless)
};

class GameHost {
public:
    GameHost() = default;
    ~GameHost();
    GameHost(const GameHost&)            = delete;
    GameHost& operator=(const GameHost&) = delete;

    // Comeca a thread do jogo. Um GameHost por processo (o runtime tem estado global).
    void start(GameOptions opt);
    void requestStop();  // o jogo para no proximo VBlank
    void join();         // espera a thread (que libera fibras e recursos dela)

    bool        running() const { return running_.load(); }
    uint64_t    vblanks() const { return vblanks_.load(); }
    std::string error() const;  // vazio se terminou por pedido ou limite

    // Copia o quadro mais recente se for novo (`seq` = ultimo numero visto, atualizado).
    bool latestFrame(GameFrame& out, uint64_t& seq);
    GameStats stats() const;

private:
    static void onVblank(void* self);
    void        vblank();
    void        threadMain();

    GameOptions           opt_;
    std::thread           thread_;
    std::atomic<bool>     running_{false}, stop_{false};
    std::atomic<uint64_t> vblanks_{0};
    mutable std::mutex    mutex_;
    GameFrame             frame_;
    uint64_t              frameSeq_ = 0;
    std::string           error_;
    GameFrame             scratch_;
    std::chrono::steady_clock::time_point epoch_;
    uint64_t                              epochVblank_ = 0;
    std::chrono::steady_clock::time_point workStart_, rateStart_;
    uint64_t                              rateVblank_ = 0;
    GameStats                             stats_;  // protegido por mutex_
    double                                workSumMs_ = 0;
    std::unique_ptr<native::files::DataFileSystem> files_;  // provedor nativo de arquivos (N1)
    uint8_t*                                       ram_ = nullptr;  // RAM do guest (diagnostico)
};

} // namespace bs
