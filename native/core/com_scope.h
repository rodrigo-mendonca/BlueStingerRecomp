#pragma once
// COM por thread, balanceado (revisao Codex 2026-10-19): cada objeto que usa COM (Media Foundation,
// XAudio2, DirectWrite/WIC) guarda um destes, criado na thread em que ele vive. CoInitializeEx devolve
// S_OK ou S_FALSE (ja iniciado na thread) e, nos dois casos, pede o seu CoUninitialize; outro modo de
// concorrencia ja escolhido (RPC_E_CHANGED_MODE) nao conta e nao e desfeito. Os objetos de video e
// audio sao usados na thread em que foram criados (a do jogo).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>

namespace bs::native {

class ComScope {
public:
    ComScope() : ok_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope() {
        if (ok_) CoUninitialize();
    }
    ComScope(const ComScope&)            = delete;
    ComScope& operator=(const ComScope&) = delete;
    bool initialized() const { return ok_; }

private:
    bool ok_;
};

} // namespace bs::native
