#pragma once
// Execucao do jogo recompilado numa thread propria, sem janela: boot nativo, ritmo do
// Dreamcast (VBlank de 59,94 Hz), publicacao do quadro exibido a cada VBlank e
// encerramento limpo. O front-end (SDL3, app/main_sdl.cpp) so apresenta quadros e entrega
// eventos; a frequencia de apresentacao dele e independente da do jogo.
#include "cpu/sh4_context.h"
#include "platform/data_files.h"
#include "platform/saves.h"

#include <atomic>
#include <condition_variable>
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
    uint64_t              vblank = 0;  // o VBlank em que o quadro foi publicado
    // N7c: legenda das falas ativa neste VBlank (SubtitleService do N6; vazia = nenhuma ou legendas desligadas).
    // Fica fora da imagem: quem apresenta a desenha por cima (app/subtitle_overlay.h).
    std::wstring          subtitle;
    // N7c: resolucao interna. Quando o quadro exibido e um render do Vulkan na escala, o slot da imagem maior dele na GPU
    // (hiresSeq != 0; a apresentacao direta a desenha sem passar pela CPU). `argb` continua a da VRAM, que as capturas,
    // o hash dos quadros e a apresentacao SDL usam. `hires` (CPU) so nos quadros capturados (--capture).
    uint64_t                                     hiresSeq = 0;
    uint32_t                                     hiresWidth = 0, hiresHeight = 0;
    uint32_t                                     hiresY0 = 0, hiresRows = 0;  // as linhas que correspondem a `argb`
    std::shared_ptr<const std::vector<uint32_t>> hires;
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

namespace native::input {
class InputSystem;
}

// Provedor dos controles (N3, docs/N3-PLANO.md): nativo (padrao desde o fim do N3) = o SDK pd e
// alimentado direto (transitional/pad_adapter), sem controle no Maple; legado (fallback temporario)
// = o Maple responde como controle e le o InputSystem pela ponte.
enum class ControlsProvider { Legacy, Native };

// Provedor dos saves (N4, docs/N4-PREPARACAO.md): nativo (padrao desde o fim do N4) = a API bu
// atendida por transitional/bu_adapter sobre arquivos em `savesRoot` (um envelope por slot);
// legado (fallback temporario) = a biblioteca bu original, sem VMU no Maple ("VMU not found").
enum class SavesProvider { Legacy, Native };

// Provedor dos filmes (N5, docs/N5-PLANO.md): nativo (padrao desde o fim do N5) = as entradas MW da
// funcao de video do jogo atendidas por transitional/video_adapter (MP4 do Builder em data\video,
// Media Foundation e XAudio2; mods em mods\video); legado (fallback temporario) = Sofdec original.
enum class VideoProvider { Legacy, Native };

// Provedor dos efeitos sonoros (N6 fase D, docs/N6-PLANO.md secao 14): legado (padrao ate a prova de equivalencia)
// = o SDK de som original na AICA emulada; nativo = as 5 entradas do SDK atendidas por transitional/sound_adapter
// (BS_SND.MLT do disco do usuario, mixer nativo). Musica e falas continuam no legado nesta fase.
enum class AudioEffectsProvider { Legacy, Native };
// Provedor das falas (N6 fase E, docs/N6-PLANO.md secao 16): legado (padrao ate a prova) = o MW WAV da CRI original;
// nativo = transitional/sound_adapter (WAV do disco do usuario; dublagem de mods em modsudio\<idioma>oz).
enum class AudioVoicesProvider { Legacy, Native };
// Provedor da musica (N6 fase F, docs/N6-PLANO.md secao 18): legado (padrao ate a prova) = o SDK de som e o driver
// Manatee originais na AICA emulada; nativo = o sequenciador e o sintetizador Manatee em C++ (native/audio/manatee_*).
enum class AudioMusicProvider { Legacy, Native };
// Renderizador (N7b, docs/N7-PLANO.md secao 14): software (padrao ate a aprovacao do Vulkan) = o rasterizador de
// referencia; nativo = a IR do TA desenhada no Vulkan (transitional/render_capture + native/render/vulkan), com
// recuo automatico para o software se nao houver Vulkan.
enum class RenderProvider { Software, Native };

struct GameOptions {
    // dados do jogo preparados pelo Builder (pasta data/ da instalacao); o jogo nao le o CDI
    std::filesystem::path data;
    std::filesystem::path flash;  // flash de 128 KB opcional (sem: apagada, 0xFF)
    std::filesystem::path modAssets;  // mods de arquivos (mods\assets), opcional
    FilesProvider         files = FilesProvider::Native;
    TimeProviderKind      time  = TimeProviderKind::Native;
    ControlsProvider      controls = ControlsProvider::Native;
    SavesProvider         saves = SavesProvider::Native;
    std::filesystem::path savesRoot;  // saves nativos: <raiz>\card0\BLUESTIN.00n.bsv
    VideoProvider         video = VideoProvider::Native;
    std::filesystem::path modVideo;  // mods de filmes e legendas (mods\video), opcional
    std::string           subtitles;  // idioma das legendas dos filmes ("" = sem)
    AudioEffectsProvider  audioEffects = AudioEffectsProvider::Legacy;
    AudioVoicesProvider   audioVoices  = AudioVoicesProvider::Legacy;
    AudioMusicProvider    audioMusic   = AudioMusicProvider::Legacy;
    RenderProvider        render       = RenderProvider::Software;
    std::filesystem::path modAudio;  // mods de audio (modsudio): dublagens por idioma, opcional
    std::string           dubbing;   // idioma da dublagem ("" = vozes originais)
    // Amostras do jogador 1 no modo nativo dos controles (o chamador mantem vivo ate o join).
    native::input::InputSystem* input = nullptr;
    // Registra a tabela do codigo recompilado (bsg::register_functions no executavel).
    std::function<void()> registerFunctions;
    // Prepara RAM e registradores. Padrao: boot nativo a partir de `data`. Testes trocam.
    std::function<void(Sh4Context&)> prepare;
    bool     realtime    = true;  // segura o jogo em 59,94 VBlanks por segundo
    uint64_t vblankLimit = 0;     // > 0: para sozinho depois desses VBlanks (headless)
    // Desenvolvimento (gameplay assistido, app/assist.h): chamado na thread do jogo a cada VBlank,
    // depois do quadro exibido. Pode bloquear (pausa entre VBlanks, sem mudar o jogo); devolve
    // false para encerrar. `ram`: os 16 MB da RAM do guest, so leitura.
    struct VblankView {
        uint64_t         vblank, step, reads;  // VBlank, passo de jogo, leituras do controle (porta 0)
        const GameFrame& frame;
        const uint8_t*   ram;
    };
    std::function<bool(const VblankView&)> vblankHook;
    // Capturas exatas (headless --dump-every N --capture): chamado na thread do jogo com o quadro publicado nos VBlanks
    // multiplos de captureEvery (o mesmo VBlank em execucoes diferentes da mesma rota, o que a comparacao precisa).
    uint64_t                                captureEvery = 0;
    std::function<void(const GameFrame&)>   capture;
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

    // N7c (menu interno; chamadas da thread da janela). A pausa segura o jogo ENTRE VBlanks, depois do quadro
    // publicado (a imagem fica, o audio nativo para junto, nada do jogo muda); os pedidos valem no proximo VBlank, na
    // thread do jogo (o mixer, a captura do render e as legendas so sao tocados la).
    void           setPaused(bool on);
    bool           paused() const { return paused_.load(); }
    void           setVolumes(float music, float voices, float effects);  // 0..1, com o volume geral (audio nativo)
    void           setSubtitleLanguage(const std::string& language);      // falas e filmes; "" = desligadas
    void           setRenderer(RenderProvider r);
    void           setRenderScale(float scale);  // N7c: resolucao interna do Vulkan (1 = fiel; altura interna / 480)
    RenderProvider renderer() const { return RenderProvider(renderer_.load()); }

private:
    static void onVblank(void* self);
    void        vblank();
    void        threadMain();
    void        applyRequests();  // na thread do jogo, no inicio do VBlank

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
    std::unique_ptr<native::saves::ISaveStore>     saves_;  // provedor nativo de saves (N4)
    GameFrame                                      movie_;  // ultimo quadro do filme nativo (N5)
    uint8_t*                                       ram_ = nullptr;  // RAM do guest (diagnostico)
    // N6 fase E: legendas das falas (mods), sem desenho (o apresentador e do N7): o servico decide o texto ativo
    struct Dialogue;
    struct DialogueDelete {
        void operator()(Dialogue* d) const;  // definido no .cpp (tipo completo so la)
    };
    std::unique_ptr<Dialogue, DialogueDelete>      dialogue_;
    void                                           dialogueVblank(uint64_t n);
    // N7c: pausa e pedidos do menu
    std::atomic<bool>                              paused_{false};
    std::mutex                                     pauseMutex_;
    std::condition_variable                        pauseCv_;
    std::mutex                                     reqMutex_;
    bool                                           volumesPending_ = false, langPending_ = false;
    float                                          volumes_[3]     = {1, 1, 1};
    std::string                                    lang_;
    std::atomic<int>                               rendererPending_{-1}, renderer_{0};
    bool                                           voicesNative_ = false;  // o servico de legendas das falas existe
    std::atomic<bool>                              requestsPending_{false};  // acorda a pausa para aplicar pedidos
    std::atomic<float>                             scalePending_{0.f};         // 0 = nenhum pedido
    void                                           notifyRequest();
};

} // namespace bs
