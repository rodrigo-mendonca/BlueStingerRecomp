#pragma once
// Tipos basicos das interfaces nativas (N0). Nada aqui conhece o Dreamcast: sem enderecos
// do guest, registradores, contexto da CPU emulada ou layout de hardware.
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace bs::native {

enum class Error : uint8_t {
    None,
    NotFound,       // arquivo, banco, filme ou save inexistente
    InvalidHandle,  // handle de outra sessao, liberado ou de outro tipo
    OutOfRange,     // deslocamento/indice fora do recurso
    Io,             // falha do sistema operacional
    Busy,           // operacao assincrona ainda em andamento
    Unsupported,    // formato ou operacao nao suportada pelo backend
    Corrupt,        // dados presentes mas invalidos (hash, cabecalho)
};

const char* error_name(Error e);

// Resultado de uma operacao: valor ou erro (std::expected so chega no C++23).
template <class T>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}
    Result(Error e) : v_(e) {}
    bool     ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }
    const T& value() const { return std::get<0>(v_); }
    T&       value() { return std::get<0>(v_); }
    Error    error() const { return ok() ? Error::None : std::get<1>(v_); }

private:
    std::variant<T, Error> v_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error e) : e_(e) {}
    bool     ok() const { return e_ == Error::None; }
    explicit operator bool() const { return ok(); }
    Error    error() const { return e_; }

private:
    Error e_ = Error::None;
};

inline const char* error_name(Error e) {
    switch (e) {
    case Error::None: return "ok";
    case Error::NotFound: return "nao encontrado";
    case Error::InvalidHandle: return "handle invalido";
    case Error::OutOfRange: return "fora da faixa";
    case Error::Io: return "erro de E/S";
    case Error::Busy: return "ocupado";
    case Error::Unsupported: return "nao suportado";
    case Error::Corrupt: return "dados corrompidos";
    }
    return "?";
}

} // namespace bs::native
