#include "game_host.h"

#include "boot/native_boot.h"
#include "disc/disc_image.h"
#include "cpu/sh4_runtime.h"
#include "cpu/sh4_threads.h"
#include "gdrom/gdrom.h"
#include "gdrom/data_source.h"
#include "hle/flashrom.h"
#include "debug/call_trace.h"
#include "debug/cri_diag.h"
#include "bu_adapter.h"
#include "gdfs_adapter.h"
#include "pad_adapter.h"
#include "pad_contract.h"
#include "pad_probe.h"
#include "input/input.h"
#include "time_adapter.h"
#include "cri_idle.h"
#include "mods/mod_api.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_video.h"
#include "maple/maple.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// Hora do sistema no boot (RTC, segundos desde 1950). Jogo normal: hora local do host. Testes
// e roteiros (D5): BS_RELOGIO_FIXO=<segundos Unix, hora local> fixa o relogio.
uint32_t rtcAtBoot() {
    if (const char* v = std::getenv("BS_RELOGIO_FIXO")) {
        char*         end = nullptr;
        const int64_t t   = std::strtoll(v, &end, 10);
        if (end == v || *end) throw std::runtime_error(std::string("BS_RELOGIO_FIXO invalido: ") + v);
        return uint32_t(t + 7305ll * 86400);  // 1950 -> 1970
    }
    return boot_rtc_now();
}

bool traceOn() {
    static const bool on = std::getenv("BS_APP_TRACE") != nullptr;
    return on;
}

// Diagnostico BS_STATE_PASSO (ver threadMain): estado logico no inicio de cada passo de jogo.
struct StepLog {
    FILE*           f     = nullptr;
    uint64_t        every = 10;
    const uint8_t*  ram   = nullptr;
    const GameHost* host  = nullptr;
} g_stepLog;

// Diagnostico BS_PAD_TRACE (N3-0): a cada passo de jogo, no inicio do passo (antes do servidor
// pd do passo), o PDS_PERIPHERAL da porta A0 como o jogo o deixou no passo anterior (depois do
// servidor e do pos-processamento 8C064878), o ponteiro que o jogo usa, a info apontada e a
// palavra de acoes do jogo (8C24C5D0..E0, 8C03AC2E) e o pedido de soft reset (bit 2 de 8C24F878).
// Comparavel entre execucoes passo a passo (tools/dev/compara_pad.py): e a referencia do N3-2.
FILE*          g_padTrace = nullptr;
const uint8_t* g_padRam   = nullptr;

// Leituras do controle da porta A: no legado, os GET_CONDITION respondidos pelo Maple; no
// --controles nativo (N3-2), as leituras do modelo do pd nativo (mesmo indice).
uint64_t controllerReads() { return transitional::pad::installed() ? transitional::pad::reads() : maple_cond_seq(0); }

void padTraceWrite(uint64_t step) {
    namespace pad = transitional::pad;
    const uint8_t* ram = g_padRam;
    auto u32 = [&](uint32_t a) {
        uint32_t v;
        std::memcpy(&v, ram + (a & 0xFFFFFF), 4);
        return v;
    };
    const pad::Pds p     = pad::pds_load(ram + (pad::kTable & 0xFFFFFF));
    const uint32_t ptr   = u32(pad::kGamePdsPtr);
    const bool     infoOk = (p.info >> 24) == 0x8C;
    std::string    name;
    if ((p.name >> 24) == 0x8C)
        for (uint32_t i = 0; i < 32; ++i) {
            const char ch = char(ram[(p.name + i) & 0xFFFFFF]);
            if (ch < 0x20 || ch > 0x7E) break;
            name += ch;
        }
    std::fprintf(g_padTrace,
                 "%llu\t%llu\t%llu\t%08X\t%08X\t%08X\t%08X\t%08X\t%08X\t%08X\t%u\t%u\t%d\t%d\t%d\t%d\t%08X\t%08X\t%s\t%08X\t%08X\t"
                 "%08X\t%08X\t%08X\t%u\n",
                 static_cast<unsigned long long>(step), static_cast<unsigned long long>(g_stepLog.host->vblanks()),
                 static_cast<unsigned long long>(controllerReads()), ptr, p.id, p.support, p.on, p.off, p.press, p.release,
                 p.r, p.l, p.x1, p.y1, p.x2, p.y2, p.old, infoOk ? u32(p.info) : 0u, name.empty() ? "-" : name.c_str(),
                 u32(0x8C24C5D0), u32(0x8C24C5D4), u32(0x8C24C5D8), u32(0x8C24C5DC), u32(0x8C24C5E0),
                 (u32(0x8C24F878) >> 2) & 1u);
    if (step % 250 == 0) std::fflush(g_padTrace);
}

void stepLogWrite(uint64_t step, void*) {
    if (g_padTrace) padTraceWrite(step);
    if (!g_stepLog.f || step % g_stepLog.every) return;
    const uint8_t* ram = g_stepLog.ram;
    uint64_t       h   = 0xCBF29CE484222325ull;
    for (uint32_t a = 0x240000; a < 0x260000; ++a) h = (h ^ ram[a]) * 0x100000001B3ull;
    int16_t life;
    float   p[3];
    std::memcpy(&life, ram + 0x24FBCC, 2);
    std::memcpy(p, ram + 0x24D668, 12);
    std::fprintf(g_stepLog.f, "%llu\t%llu\t%016llX\t%d\t%.3f\t%.3f\t%.3f\n", static_cast<unsigned long long>(step),
                 static_cast<unsigned long long>(g_stepLog.host->vblanks()), static_cast<unsigned long long>(h), life, p[0],
                 p[1], p[2]);
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
    maple_vblank_in();  // N3-0: indice "vblank" dos roteiros e negativo do ponto de amostragem
    // Diagnostico: BS_RAM_DUMP=<vblank>[,<vblank>...]:<arquivo> grava os 16 MB da RAM do guest
    // nesses VBlanks (com mais de um, em <arquivo>_<vblank>)
    static const std::pair<std::vector<uint64_t>, std::string> kRamDump = [] {
        std::pair<std::vector<uint64_t>, std::string> d;
        const char* v = std::getenv("BS_RAM_DUMP");
        if (!v || !std::strchr(v, ':')) return d;
        for (const char* p = v; p < std::strchr(v, ':');) {
            char* end = nullptr;
            d.first.push_back(std::strtoull(p, &end, 10));
            p = *end == ',' ? end + 1 : end;
            if (end == p && *end != ',') break;
        }
        d.second = std::strchr(v, ':') + 1;
        return d;
    }();
    if (ram_)
        for (uint64_t at : kRamDump.first)
            if (n == at) {
                const std::string path = kRamDump.first.size() > 1 ? kRamDump.second + "_" + std::to_string(n) : kRamDump.second;
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    std::fwrite(ram_, 1, 16u << 20, f);
                    std::fclose(f);
                }
            }
    // Diagnostico (N2b-0, criterio B6): BS_STATE_LOG=<arquivo> grava a cada 250 VBlanks o hash da
    // regiao de variaveis do jogo (0x8C240000-0x8C25FFFF: estado logico provisorio, ate os campos
    // terem nome). BS_RAM_MUTACAO=<vblank>:<endereco> troca um byte nesse VBlank (negativo).
    static FILE* stateLog = [] {
        const char* v = std::getenv("BS_STATE_LOG");
        FILE*       f = v && *v ? std::fopen(v, "w") : nullptr;
        if (f) std::fprintf(f, "vblank\testado\n");
        return f;
    }();
    static const std::pair<uint64_t, uint32_t> kRamMut = [] {
        const char* v = std::getenv("BS_RAM_MUTACAO");
        if (!v || !std::strchr(v, ':')) return std::pair<uint64_t, uint32_t>{0, 0};
        return std::pair<uint64_t, uint32_t>{std::strtoull(v, nullptr, 10),
                                             uint32_t(std::strtoul(std::strchr(v, ':') + 1, nullptr, 16))};
    }();
    if (kRamMut.first && n == kRamMut.first && ram_) ram_[kRamMut.second & 0xFFFFFF] ^= 0x01;
    if (stateLog && ram_ && n % 250 == 0) {
        uint64_t h = 0xCBF29CE484222325ull;
        for (uint32_t a = 0x240000; a < 0x260000; ++a) h = (h ^ ram_[a]) * 0x100000001B3ull;
        std::fprintf(stateLog, "%llu\t%016llX\n", static_cast<unsigned long long>(n), static_cast<unsigned long long>(h));
        std::fflush(stateLog);
    }
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
    // Diagnostico (N2b-2): BS_FRAME_LOG=<arquivo> grava o hash do quadro exibido a cada VBlank
    // (compara a sequencia de quadros entre modos de tempo com linhas do tempo deslocadas)
    static FILE* frameLog = [] {
        const char* v = std::getenv("BS_FRAME_LOG");
        FILE*       f = v && *v ? std::fopen(v, "w") : nullptr;
        // entrada: leituras do controle (indice do roteiro); passo: voltas do laco principal do jogo
        if (f) std::fprintf(f, "vblank\tquadro\tentrada\tpasso\n");
        return f;
    }();
    if (frameLog) {
        uint64_t h = 0xCBF29CE484222325ull ^ (uint64_t(scratch_.width) << 32 | scratch_.height);
        for (uint32_t px : scratch_.argb) h = (h ^ px) * 0x100000001B3ull;
        std::fprintf(frameLog, "%llu\t%016llX\t%llu\t%llu\n", static_cast<unsigned long long>(n),
                     static_cast<unsigned long long>(h), static_cast<unsigned long long>(controllerReads()),
                     static_cast<unsigned long long>(transitional::time::game_steps()));
        if (n % 250 == 0) std::fflush(frameLog);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(frame_, scratch_);
        ++frameSeq_;
    }
    if (opt_.vblankHook &&
        !opt_.vblankHook({n, transitional::time::game_steps(), controllerReads(), frame_, ram_}))
        stop_.store(true);
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
    // N2b-2: no tempo experimental a RAM tem rastreamento de escrita (ociosidade da thread 3); no
    // padrao, a alocacao de sempre
    std::vector<uint8_t> ram;
    uint8_t*             watched = nullptr;
    std::string          err;
    try {
        if (opt_.time == TimeProviderKind::Experimental) watched = transitional::cri::alloc_watched_ram(16u << 20);
        else ram.resize(16u << 20);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = e.what();
        running_.store(false);
        return;
    }
    Sh4Context c{};
    c.ram = watched ? watched : ram.data();
    ram_  = c.ram;  // so diagnostico (BS_RAM_DUMP)
    try {
        if (opt_.registerFunctions) opt_.registerFunctions();
        bs::mods::reset();     // sessao nova: nada herdado de uma anterior
        bs::mods::init_all();  // mods compilados no executavel (projeto editavel; BS_MOD)
        bs::call_trace_install();  // diagnostico BS_CALL_TRACE (desligado por padrao)
        bs::cri_diag_install();    // diagnostico BS_CRI_DIAG (N2b-0; desligado por padrao)
        transitional::pad::probe_install();  // teste de contrato BS_PAD_CONTRATO (N3-0; desligado por padrao)
        transitional::pad::closure_install();  // prova de fechamento BS_PAD_FECHAMENTO (N3-2; desligado por padrao)
        // N3-2: controles pelo provedor nativo do SDK pd; o Maple fica sem controle (a camada per
        // continua, para a VMU ate o N4). A mutacao "maple" (negativo) deixa o controle no Maple.
        if (opt_.controls == ControlsProvider::Native) {
            if (!opt_.input) throw std::runtime_error("--controles nativo sem fonte de entrada (InputSystem)");
            if (std::getenv("BS_PAD_CONTRATO")) throw std::runtime_error("BS_PAD_CONTRATO confere o servidor legado: use --controles legado");
            const char*    mv  = std::getenv("BS_PAD_NATIVO_MUTACAO");
            const uint32_t mut = transitional::pad::parse_mutations(mv ? mv : "");
            maple_set_controller_present((mut & transitional::pad::kNatMaple) != 0);
            // coleta onde o legado entrega o fim do DMA: logo depois do servidor com tempo por ciclos
            // (nativo/legado), no disparo seguinte com o tempo experimental (pad_adapter.cpp, collect)
            transitional::pad::install(transitional::boundary_registry(), *opt_.input, mut,
                                       opt_.time != TimeProviderKind::Experimental);
        }
        // N4: saves pelo provedor nativo (API bu sobre arquivos; sem VMU nem Maple)
        if (opt_.saves == SavesProvider::Native) {
            if (opt_.savesRoot.empty()) throw std::runtime_error("--saves nativo sem pasta de saves");
            native::saves::FileStoreOptions so;
            so.log = [](const std::string& m) { std::fprintf(stderr, "[saves] %s\n", m.c_str()); };
            saves_ = native::saves::make_file_store(opt_.savesRoot, so);
            std::fprintf(stderr, "[saves] nativo: %s\n", disc::pathText(opt_.savesRoot).c_str());
            transitional::bu::install(transitional::boundary_registry(), *saves_);
            transitional::bu::h1_install();
        }
        // N1: arquivos pelo provedor nativo (API gdFs sobre a pasta de recursos)
        if (!opt_.data.empty() && opt_.files == FilesProvider::Native) {
            files_ = std::make_unique<native::files::DataFileSystem>(opt_.data / L"disc", opt_.modAssets);
            for (const auto& m : files_->mods())
                std::fprintf(stderr, "[mods] recurso substituido: %s (%llu -> %llu bytes)\n", m.logical.c_str(),
                             static_cast<unsigned long long>(m.originalSize), static_cast<unsigned long long>(m.modSize));
            transitional::gdfs::check_sizes(*files_);
            transitional::gdfs::install(transitional::boundary_registry(), *files_);
        }
        // N2a: tempo e eventos pelo provedor nativo (antes do boot: o reset do hardware o inicia)
        if (opt_.time == TimeProviderKind::Native || opt_.time == TimeProviderKind::Experimental) {
            transitional::time::Options to;
            to.experimental = opt_.time == TimeProviderKind::Experimental;
            to.ram          = watched;
            if (const char* v = std::getenv("BS_TIME_TRACE")) to.trace = v;
            if (const char* v = std::getenv("BS_TEMPO_MUTACAO")) to.mutation = v;
            if (const char* v = std::getenv("BS_PROGRESS_TRACE")) to.progress_trace = v;
            if (const char* v = std::getenv("BS_PROGRESSO_LIMITE")) to.stall_units = std::strtoull(v, nullptr, 10);
            else to.stall_units = transitional::time::kDefaultStallUnits;
            transitional::time::install(to);
            input_set_step_counter([] { return transitional::time::game_steps(); });  // roteiros por passo
            // Diagnostico (gameplay assistido): BS_STATE_PASSO=<arquivo> grava a cada BS_STATE_PASSO_CADA
            // passos de jogo (padrao 10), no inicio do passo, o hash da regiao de variaveis do jogo
            // (0x8C240000-0x8C25FFFF), a vida do Eliot (int16 em 0x8C24FBCC) e a posicao dele (floats em
            // 0x8C24D668). Comparavel entre modos de tempo passo a passo (tools/dev/compara_modos.py).
            if (const char* v = std::getenv("BS_STATE_PASSO"); v && *v) {
                g_stepLog.f = std::fopen(v, "w");
                if (!g_stepLog.f) throw std::runtime_error(std::string("BS_STATE_PASSO: nao abre ") + v);
                g_stepLog.every = 10;
                if (const char* k = std::getenv("BS_STATE_PASSO_CADA"))
                    g_stepLog.every = std::max<uint64_t>(1, std::strtoull(k, nullptr, 10));
                std::fprintf(g_stepLog.f, "passo\tvblank\testado\tvida\tx\ty\tz\n");
            }
            if (const char* v = std::getenv("BS_PAD_TRACE"); v && *v) {
                g_padTrace = std::fopen(v, "w");
                if (!g_padTrace) throw std::runtime_error(std::string("BS_PAD_TRACE: nao abre ") + v);
                std::fprintf(g_padTrace, "passo\tvblank\tleituras\tptr\tid\tsupport\ton\toff\tpress\trelease\tr\tl\tx1\ty1\t"
                                         "x2\ty2\told\tinfo\tnome\tacao0\tacao1\tacao2\tacao3\tacao4\treset\n");
                g_padRam = c.ram;
            }
            g_stepLog.ram  = c.ram;
            g_stepLog.host = this;
            if (g_stepLog.f || g_padTrace) transitional::time::set_step_observer(&stepLogWrite, nullptr);
        }
        sh4_set_fatal_handler(onFatal);
        if (opt_.prepare) opt_.prepare(c);
        else boot_prepare(c, boot_read_data(opt_.data), rtcAtBoot());
        // GD-ROM HLE (legado): com o provedor nativo, so inicializacao e estado do drive; os
        // mods valem pelo provedor nativo (de qualquer tamanho), nao pelos setores
        if (!opt_.data.empty())
            gdrom_set_disc(open_runtime_data(opt_.data, files_ ? std::filesystem::path() : opt_.modAssets));
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
    std::fputs(cri_diag_summary().c_str(), stderr);
    transitional::pad::probe_finish();
    transitional::pad::closure_finish(transitional::pad::installed());
    if (transitional::pad::installed()) {
        std::fputs(transitional::pad::summary().c_str(), stderr);
        transitional::pad::uninstall(transitional::boundary_registry());
    }
    maple_set_controller_present(true);
    {
        const MapleCounters m = maple_counters();
        std::fprintf(stderr, "[maple] DMAs %llu; porta A: DEVICE_INFO %llu, GET_CONDITION %llu; respostas do controle %llu, "
                             "leituras da fonte %llu\n",
                     static_cast<unsigned long long>(m.dmas), static_cast<unsigned long long>(m.devInfo[0]),
                     static_cast<unsigned long long>(m.getCond[0]), static_cast<unsigned long long>(m.controllerResponses),
                     static_cast<unsigned long long>(m.sourceReads));
    }
    // N2b (criterio B4): VBlank-In que chegaram com o bit ainda aceso (um atendimento para dois)
    std::fprintf(stderr, "[vblank] fundidos %llu\n", static_cast<unsigned long long>(holly_vblank_fused()));
    if (transitional::time::installed()) {
        input_set_step_counter(nullptr);
        transitional::time::set_step_observer(nullptr, nullptr);
        if (g_stepLog.f) std::fclose(g_stepLog.f), g_stepLog.f = nullptr;
        if (g_padTrace) std::fclose(g_padTrace), g_padTrace = nullptr;
        transitional::time::uninstall();
        std::fputs(transitional::time::progress_summary().c_str(), stderr);
        const auto s = transitional::time::stats();
        std::fprintf(stderr, "[tempo] %s: VBlank-In %llu, VBlank-Out %llu, render %llu, Maple %llu, GD-ROM %llu; "
                             "agendados %llu, cancelados %llu, recuos do relogio %llu, VBlank-In colapsados %llu\n",
                     opt_.time == TimeProviderKind::Experimental ? "experimental" : "nativo",
                     static_cast<unsigned long long>(s.delivered[0]), static_cast<unsigned long long>(s.delivered[1]),
                     static_cast<unsigned long long>(s.delivered[2]), static_cast<unsigned long long>(s.delivered[3]),
                     static_cast<unsigned long long>(s.delivered[4]), static_cast<unsigned long long>(s.posted),
                     static_cast<unsigned long long>(s.cancelled), static_cast<unsigned long long>(s.clockRegressions),
                     static_cast<unsigned long long>(s.vblankCollapsed));
    }
    if (transitional::bu::installed()) {
        std::fputs(transitional::bu::summary().c_str(), stderr);
        transitional::bu::uninstall(transitional::boundary_registry());
    }
    saves_.reset();
    if (files_) {
        const auto s = transitional::gdfs::stats();
        std::fprintf(stderr, "[arquivos] nativo: %llu aberturas, %llu leituras, %llu bytes, %llu nao encontrados\n",
                     static_cast<unsigned long long>(s.opens), static_cast<unsigned long long>(s.reads),
                     static_cast<unsigned long long>(s.bytes), static_cast<unsigned long long>(s.notFound));
        transitional::gdfs::uninstall(transitional::boundary_registry());
        files_.reset();
    }
    ram_ = nullptr;
    transitional::cri::free_watched_ram(watched);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = err;
    }
    running_.store(false);
}

} // namespace bs
