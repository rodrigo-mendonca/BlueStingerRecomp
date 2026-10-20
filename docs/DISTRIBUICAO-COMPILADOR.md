# Bloqueio da distribuição: o compilador na máquina do usuário (2026-10-19)

**Situação.** O `BlueStinger-Recompiler.exe` (e o Developer e a CLI) compila o C++ gerado com o
**Visual Studio Build Tools (MSVC)** instalado pelo usuário: `builder/toolchain.cpp` o localiza por
`vswhere`/`vcvars64.bat` e falha com `err.toolchain` se ele faltar.

Pelo critério do mantenedor, a distribuição **não está pronta** enquanto depender disso. A
compilação na máquina do usuário em si não muda (D7): o C++ gerado deriva da cópia do jogo dele e
não é distribuído pronto.

**Retrato do código** (para avaliar uma alternativa):
- O MSVC só aparece em blocos `if(MSVC)` do CMake (`/W4 /permissive- /utf-8`, `/bigobj` no
  gerado, `/sourceDependencies`), no teste de XMM em MASM (só nos testes) e no Builder.
- O `longjmp` do jogo é lógico (`c->redirect`), sem `setjmp`/SEH do host.
- O código gerado é C++20 sem intrínsecos do MSVC, mas ainda não foi compilado com Clang/GCC.
- As APIs do Windows usadas (fibras, Media Foundation, XAudio2, DirectWrite/Direct2D/WIC, XInput)
  existem nos cabeçalhos do mingw-w64.
- O Windows SDK e os cabeçalhos/STL do MSVC **não** podem ser redistribuídos.

## Opções

| | Opção | O que o usuário precisa | Custo | Observações |
|---|---|---|---|---|
| A | **Instalação guiada** do Build Tools pelo Recompiler (o bootstrapper da Microsoft com a carga C++, com consentimento) | Internet e ~3–6 GB, uma vez | pequeno: um botão e um passo de instalação | Não é autossuficiente offline; continua dependendo da Microsoft. Bom como paliativo imediato. |
| B | **Toolchain próprio redistribuível: `llvm-mingw`** (Clang + LLD + mingw-w64 + libc++; licenças Apache-2.0 com exceção LLVM, ZPL, domínio público) junto do Recompiler | nada (offline) | médio: build com Clang/MinGW (CMake sem `if(MSVC)` obrigatório, `-MD` no lugar do `/sourceDependencies`, sem MASM no produto), SDL3 e toml++ com MinGW, validar o C++ gerado (981 mil linhas) e o desempenho; ~150–250 MB na distribuição | **Autossuficiente.** Prepara o Linux (Clang/GCC, D13). Exige rodar a mesma bateria de fidelidade (quadros e estado por passo iguais aos do MSVC). |
| C | `clang-cl` + `lld-link` redistribuídos com o SDK e o CRT do MSVC | — | — | **Inviável:** o SDK e o CRT/STL do MSVC não podem ser redistribuídos. |

**Recomendação:**
1. **A** já, como paliativo: o usuário clica, aceita e o Recompiler segue.
2. **B** como a solução definitiva e menor autossuficiente, num marco próprio antes de declarar a
   distribuição pronta. A prova: o mesmo C++ gerado compilado com Clang/MinGW dá os mesmos quadros
   e o mesmo estado por passo que o MSVC (`compara_modos.py`) nas rotas do `test_assistido`.

**Decisão do mantenedor pendente.** Nada foi implementado; a distribuição continua marcada como
**dependente do MSVC**.
