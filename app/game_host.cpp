#include "game_host.h"

#include "boot/native_boot.h"
#include "disc/disc_image.h"
#include "cpu/sh4_runtime.h"
#include "cpu/sh4_threads.h"
#include "gdrom/gdrom.h"
#include "gdrom/data_source.h"
#include "hle/flashrom.h"
#include "mods/mod_api.h"
#include "pvr/pvr_video.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace bs {
namespace {

// Parada pedida (janela fechada, limite de VBlanks): nao e erro.
struct StopRequest {};

struct FatalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void onFatal(Sh4Context*, const char* what, uint32_t pc, uint32_t extra) {
    char b[256];
    std::snprintf(b, sizeof(b), "%s em PC=%08X (valor %08X)", what, pc, extra);
    throw FatalError(b);
}

// Diagnostico: BS_APP_TRACE=1 registra as etapas do encerramento.
bool traceOn() {
    static const bool on = std::getenv("BS_APP_TRACE") != nullptr;
    return on;
}

// Periodo de um campo NTSC: 1001 / 60000 s (59,94 Hz).
constexpr auto kField = std::chrono::nanoseconds(16683333);

} // namespace

GameHost::~GameHost() {
    requestStop();
    join();
}

void GameHost::start(GameOptions opt) {
    if (running_.load() || thread_.joinable()) throw std::logic_error("GameHost ja iniciado");
    opt_ = std::move(opt);
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread([this] { threadMain(); });
}

void GameHost::requestStop() { stop_.store(true); }

void GameHost::join() {
    if (thread_.joinable()) thread_.join();
}

std::string GameHost::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

bool GameHost::latestFrame(GameFrame& out, uint64_t& seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frameSeq_ == seq) return false;
    out = frame_;
    seq = frameSeq_;
    return true;
}

void GameHost::onVblank(void* self) { static_cast<GameHost*>(self)->vblank(); }

// Na thread do jogo, no inicio de cada VBlank: publica o quadro exibido, segura o ritmo e
// atende pedidos de parada.
void GameHost::vblank() {
    const uint64_t n   = vblanks_.fetch_add(1) + 1;
    const auto     now = std::chrono::steady_clock::now();
    {
        const double work = std::chrono::duration<double, std::milli>(now - workStart_).count();
        std::lock_guard<std::mutex> lock(mutex_);
        workSumMs_ += work;
        stats_.vblanks   = n;
        stats_.workAvgMs = workSumMs_ / double(n);
        stats_.workMaxMs = std::max(stats_.workMaxMs, work);
        if (work > 16.683) ++stats_.overBudget;
        // Diagnostico: BS_FIELD_LOG=1 lista os campos acima do orcamento
        static const bool fieldLog = std::getenv("BS_FIELD_LOG") != nullptr;
        if (fieldLog && work > 16.683) std::fprintf(stderr, "[campo] %llu: %.2f ms\n", (unsigned long long)n, work);
        const double since = std::chrono::duration<double>(now - rateStart_).count();
        if (since >= 1.0) {
            stats_.fieldsPerSecond = double(n - rateVblank_) / since;
            rateStart_             = now;
            rateVblank_            = n;
        }
    }
    pvr_video_frame(scratch_.argb, scratch_.width, scratch_.height);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(frame_, scratch_);
        ++frameSeq_;
    }
    if (stop_.load() || (opt_.vblankLimit && n >= opt_.vblankLimit)) {
        if (traceOn()) std::fprintf(stderr, "[host] parada vista no VBlank %llu\n", (unsigned long long)n);
        throw StopRequest{};
    }
    if (opt_.realtime) {
        const auto t      = std::chrono::steady_clock::now();
        const auto target = epoch_ + kField * (n - epochVblank_);
        if (target > t) std::this_thread::sleep_until(target);
        else if (t - target > std::chrono::milliseconds(250)) {
            epoch_       = t;  // atrasado demais (carga, depurador): recomeca a contagem
            epochVblank_ = n;
        }
    }
    workStart_ = std::chrono::steady_clock::now();  // o proximo campo comeca aqui
}

GameStats GameHost::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void GameHost::threadMain() {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    std::string err;
    try {
        if (opt_.registerFunctions) opt_.registerFunctions();
        bs::mods::reset();     // sessao nova: nada herdado de uma anterior
        bs::mods::init_all();  // mods compilados no executavel (projeto editavel; BS_MOD)
        sh4_set_fatal_handler(onFatal);
        if (opt_.prepare) opt_.prepare(c);
        else boot_prepare(c, boot_read_data(opt_.data), boot_rtc_now());
        // os setores vem dos dados preparados pelo Builder: o jogo nao le o CDI
        if (!opt_.data.empty()) gdrom_set_disc(open_runtime_data(opt_.data, opt_.modAssets));
        if (!opt_.flash.empty() && !flashrom_load(opt_.flash))
            throw std::runtime_error("flash invalida (esperado 128 KB): " + disc::pathText(opt_.flash));
        epoch_       = std::chrono::steady_clock::now();
        epochVblank_ = 0;
        workStart_ = rateStart_ = epoch_;
        pvr_set_vblank_hook(&GameHost::onVblank, this);
        const Sh4Func entry = sh4_lookup(c.pc);
        if (!entry) throw std::runtime_error("entrada do jogo sem codigo recompilado");
        entry(&c);
        err = "a funcao de entrada do jogo retornou";
    } catch (const StopRequest&) {
    } catch (const std::exception& e) {
        err = e.what();
    }
    pvr_set_vblank_hook(nullptr, nullptr);
    if (traceOn()) std::fprintf(stderr, "[host] execucao encerrada (%s); liberando fibras\n", err.empty() ? "ok" : err.c_str());
    if (!sh4_threads_shutdown() && err.empty()) err = "falha ao liberar as fibras das threads do guest";
    if (traceOn()) std::fprintf(stderr, "[host] fibras liberadas\n");
    gdrom_set_disc(nullptr);  // fecha a imagem de disco
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = err;
    }
    running_.store(false);
}

} // namespace bs
