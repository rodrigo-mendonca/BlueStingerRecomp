// Provedor de tempo legado (--tempo legado): o calculo preguicoso por ciclos que os modulos
// faziam ate o N1, reunido aqui sem mudanca de comportamento. E a referencia e o rollback do
// provedor nativo (transitional/time_adapter); docs/N2-PLANO.md, N2a.
#include "cpu/time_hooks.h"

#include "pvr/pvr_regs.h"

namespace bs {
namespace {

class LegacyTime final : public TimeProvider {
public:
    const char* name() const override { return "legado"; }

    void reset(uint64_t now) override {
        last_      = now;
        renderDue_ = 0;
        mapleDue_  = 0;
        discDue_   = 0;
        discOn_    = false;
    }
    uint64_t clock(uint64_t cycles) override { return cycles; }

    uint32_t holly_due(uint64_t now) override {
        uint32_t due = 0;
        if (pvr_spg_crossed(last_, now, pvr_vblank_in_line())) due |= kDueVblankIn;
        if (pvr_spg_crossed(last_, now, pvr_vblank_out_line())) due |= kDueVblankOut;
        if (renderDue_ && now >= renderDue_) {
            due |= kDueRender;
            renderDue_ = 0;
        }
        if (mapleDue_ && now >= mapleDue_) {
            due |= kDueMaple;
            mapleDue_ = 0;
        }
        last_ = now;
        return due;
    }
    void spg_changed() override {}  // os cruzamentos sao calculados na hora, com a configuracao atual
    void render_scheduled(uint64_t due) override { renderDue_ = due; }
    void maple_scheduled(uint64_t due) override { mapleDue_ = due; }
    void maple_cancel() override { mapleDue_ = 0; }

    void disc_scheduled(uint64_t due) override {
        discDue_ = due;
        discOn_  = true;
    }
    bool disc_due(uint64_t now) override {
        if (!discOn_ || now < discDue_) return false;
        discOn_ = false;
        return true;
    }
    void disc_cancel() override { discOn_ = false; }

    uint32_t rtc_seconds(uint64_t now) override {
        return rtcBase_ + uint32_t((now >= rtcRef_ ? now - rtcRef_ : 0) / kCompatTicksPerSecond);
    }
    void rtc_set(uint32_t seconds, uint64_t now) override {
        rtcBase_ = seconds;
        rtcRef_  = now;
    }

private:
    uint64_t last_ = 0, renderDue_ = 0, mapleDue_ = 0, discDue_ = 0;
    bool     discOn_  = false;
    uint32_t rtcBase_ = 0;
    uint64_t rtcRef_  = 0;
};

LegacyTime    g_legacy;
TimeProvider* g_provider = &g_legacy;

} // namespace

TimeProvider& time_provider() { return *g_provider; }
void          time_set_provider(TimeProvider* p) { g_provider = p ? p : &g_legacy; }
TimeProvider& time_legacy_provider() { return g_legacy; }

} // namespace bs
