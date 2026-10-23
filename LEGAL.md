# Aspectos legais

## O que este projeto é

Um conjunto de ferramentas (extrator, recompilador e runtime) que transforma uma
cópia **fornecida pelo próprio usuário** do disco de Blue Stinger em um
executável nativo. Blue Stinger é © Climax Graphics / Sega / Activision. Este
projeto não tem afiliação com nenhum detentor de direitos.

## O que nunca entra no repositório

- Imagens de disco (CDI, GDI, CHD, ISO, BIN/CUE).
- BIOS ou flash do Dreamcast. Também não são necessárias, porque o boot e as
  syscalls são HLE.
- Arquivos extraídos do disco (`game-data/`): executável, modelos, texturas,
  áudio, vídeo.
- Código gerado a partir do executável do jogo (`generated/`, `analysis/`).
- Saves, traces e capturas que contenham dados do jogo.
- Transcrições e traduções das falas e dos filmes (pacotes de legendas): são texto derivado do jogo e não há
  autorização para publicá-las. As ferramentas que as geram são do projeto; cada usuário gera os pacotes a partir da
  própria cópia (`mods-padrao/README.md`, `docs/LEGENDAS-PENDENCIAS.md`).

O `.gitignore` bloqueia esses caminhos e extensões. `config/*.toml` contém só
metadados (ID de produto, hashes, tamanhos e endereços) e nenhum byte do jogo.

## Licença do projeto

**MIT** (ver [LICENSE](LICENSE)). Ela cobre só o código deste repositório e
não concede nenhum direito sobre o Blue Stinger ou sobre dados derivados dele.

## Uso de projetos de referência

| Projeto | Licença | Como é usado aqui |
|---|---|---|
| Flycast | GPL-2.0 | Leitura como referência de comportamento e **oráculo externo** em testes diferenciais (processo separado). **Nenhum código copiado ou linkado.** |
| N64ModernRuntime | GPL-3.0 | Só ideias de arquitetura. Nenhum código. |
| N64Recomp | MIT | Ideias de arquitetura. Se algum trecho for adaptado, manter o aviso de copyright MIT. |
| XenonRecomp / XenosRecomp | MIT | Idem. |
| Initial D AS3 SH-4 Recomp | sem licença visível no README (todos os direitos reservados até prova em contrário) | Só a estrutura do repositório e as lições publicadas. Nenhum código. |
| KallistiOS | KOS License (BSD-like) | Documentação de hardware. |
| gditools3 / loaders Ghidra | não confirmada | Documentação de formato. Nenhum código até confirmar a licença. |

Regra: qualquer código de terceiros só entra via `third_party/`, com a licença
registrada em `third_party/README.md`.
