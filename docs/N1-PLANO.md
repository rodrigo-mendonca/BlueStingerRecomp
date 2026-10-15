# N1: sistema de arquivos nativo (plano técnico e critérios)

Data: 2026-10-15. Autorização do mantenedor: N1 dentro deste escopo, sem N2 e sem commits
adicionais. Base: N0 (`docs/N0-RELATORIO.md`, commit `e267149`).

## 1. Situação atual (legado)

```
jogo / CRI (Sofdec, ADX) / Ninja
   -> gdFs (Shinobi 1.05, recompilado)
      -> "media handler" (tabela em 0x8C15C060)
         -> stubs de syscall do BIOS (vetor 0xBC)
            -> GD-ROM HLE (runtime/gdrom: REQ_CMD / GET_CMD_STAT / EXEC_SERVER)
               -> RuntimeDisc::readSector (LBA -> arquivo de data\disc + disc_meta)
```

O jogo pede **arquivos por nome**. Abaixo do gdFs, tudo é Dreamcast:
- registro de diretório ISO 9660;
- FAD/LBA, setores de 2 048 bytes;
- comandos do drive;
- setores de sistema (`disc_meta`).

## 2. Identificação por contrato (evidências)

Método:
- disassembly do módulo gdFs (`0x8C15A5E8`-`0x8C15C300`, banner `gdFs Ver 1.05`);
- grafo de chamadas do N0;
- trace de argumentos e retornos com `BS_CALL_TRACE`: build BS_MODS, roteiro NEW GAME,
  6 000 VBlanks, 68 aberturas.

Estrutura GDFS deduzida:

| Deslocamento | Conteúdo |
|---|---|
| +8 | FAD |
| +12 | tamanho em bytes |
| +16 | tamanho em setores |
| +20 | posição em setores |
| +24 | bytes transferidos |
| +36/+40 | callback de fim de leitura (função, objeto) |
| +44/+48 | callback 2 |
| +52/+56 | callback de erro |
| +76 (w) | aberto |
| +78 (w) | modo de transferência |
| +80 (w) | estado |
| +82 (w) | erro |

| Endereço | Contrato | Nome (SDK Katana) | Observado |
|---|---|---|---|
| `8C15BF60` | `(nome, dirrec)` → GDFS ou 0 | gdFsOpen | 68× (jogo, CRI, Ninja) |
| `8C15B626` | `(nome)` → 0 | gdFsChangeDir | 1× (`\BSDATA`) |
| `8C15B644` | `(gdfs)` | gdFsClose | 67× |
| `8C15B680` | `(gdfs, &bytes)` → 1/0 | gdFsGetFileSize | 50× |
| `8C15B69A` | `(gdfs, &setores)` → 1/0 | gdFsGetFileSctSize | 18× |
| `8C15B6CE` | `(gdfs, setor, origem 0/1/2)` → 0/−10/−16/−17 | gdFsSeek | 5× |
| `8C15B71C` | `(gdfs)` → posição | gdFsTell | não exercitada |
| `8C15B732` | `(gdfs, &fim)` → 0/−10 | (fim de arquivo) | não exercitada |
| `8C15B75A` | `(gdfs, modo)` → 0/−10 | gdFsSetTransMode | não exercitada |
| `8C15B774` | `(gdfs, nsct, buf)` → 0/erro (síncrona) | gdFsRead | 64× |
| `8C15B8D4` | `(gdfs, nsct, buf)` → nsct/0/−10/−14 (assíncrona, buf alinhado a 32) | gdFsReqRd32 | 43× (streaming CRI) |
| `8C15BBF8` | `(gdfs)` → estado (1 concluído, 2-4 ocupado, 5-6 erro) / −10 | gdFsGetStat | 92 329× |
| `8C15BC10` | `(gdfs)` → bytes transferidos | gdFsGetTransStat | 43× |
| `8C15BC26` | `(gdfs)` → 0/−10/−13/−21 | gdFsStop | interna (Close) |
| `8C15BD40` | `(gdfs)` → erro | gdFsGetErr | não exercitada |
| `8C15BD58`, `8C15BDA4`, `8C15BDDE` | `(gdfs, função, objeto)` | registro de callbacks | sem chamada externa / 1 estática |

Quatro dos 21 endereços **não têm código gerado**: a análise nunca alcançou essas funções,
então nada no executável as chama. São `8C15B6B4` (GetFileFad), `8C15B75A`
(SetTransMode), `8C15BD58` e `8C15BDA4` (callbacks). O adaptador as ignora; os 17 restantes
passam ao provedor nativo.

Ficam **legadas** no N1 (não recebem GDFS; documentadas como caminhos restantes):
- inicialização (`8C15A8D0`, `8C15C10C`, `8C15BE60`);
- servidor por quadro (`8C15A644`);
- estado do drive (`8C15C016` jogo ×16, `8C15BFD2` CRI ×2 M);
- callback de erro global (`8C15BFE8`);
- funções de diretório por "dirrec".

## 3. Arquitetura do N1

```
jogo / CRI / Ninja --(slot)--> adaptador gdFs (transitional/gdfs_adapter)
                                   |  GuestCall/GuestMemory, handles de 32 bits
                                   v
                     native::files::IFileSystem
                                   |
                     DataFileSystem (native/platform): arquivos de data\disc + mods\assets
```

**Interface nativa:** `native::files::IFileSystem`, já criada no N0, com nomes lógicos e
bytes.

**Implementação nativa** (`DataFileSystem`):
- usa só `std::filesystem` e arquivos do Windows: a árvore `data\disc\` indexada por caminho
  lógico, sem diferenciar maiúsculas;
- sem LBA, FAD, setor, ISO 9660 ou `disc.manifest`, e sem caminho absoluto embutido (raiz e
  pasta de mods vêm do executável);
- mods de recursos (`mods\assets\<caminho>`) são verificados:
  - precisam substituir um arquivo existente;
  - precisam ser arquivo regular e legível;
  - erro com caminho e motivo.

**Adaptador** (`transitional/gdfs_adapter`): implementa os contratos da seção 2 sobre o
`IFileSystem`.
- O GDFS do guest vira um handle de 32 bits fora da RAM (`0x7Exxxxxx`). Um acesso direto do
  guest a esse "ponteiro" cai no caminho lento e aparece no log/sonda.
- Os setores de 2 048 bytes são unidade da **API** gdFs, não do disco: viram deslocamentos
  em bytes no arquivo.
- O último setor é completado com zeros.
- Leituras assíncronas seguem o contrato do binário: `ReqRd32` publica "ocupado" com 0 bytes, e a
  transferência acontece na próxima execução do servidor gdFs (`8C15AE28`, com gancho no slot).
  Revisão do Codex de 2026-10-15.

**Seleção de provedor:**
- `BoundaryRegistry`, com slots **sempre ativos** nas entradas do gdFs (gerador:
  `[native] slots` no config), em todos os builds;
- `--arquivos nativo|legado` (padrão **nativo** depois dos portões; `legado` fica para
  referência e rollback);
- um GDFS legado (ponteiro na RAM) que chegue ao adaptador é repassado à função original.

**Guardas:** um handle nativo que chegue a uma função gdFs legada que recebe GDFS (internas,
como `8C15B548`) dá erro fatal com mensagem clara, nunca leitura silenciosa.

## 4. Critérios de validação (portões)

| # | Critério | Como |
|---|---|---|
| P1 | Contrato de recursos igual ao legado | Log normalizado de leituras de dados de arquivo, por blocos de 2 048 bytes recortados no fim do arquivo: `(arquivo, deslocamento, SHA-256)`. Legado registrado no GD-ROM HLE (LBA → arquivo); nativo no adaptador. Mesma sequência por arquivo e mesmo conjunto total, no roteiro NEW GAME (8 000 VBlanks). |
| P2 | Caminho migrado sem GD-ROM | No modo nativo, **0** setores de dados de arquivo lidos pelo GD-ROM HLE. Os restantes (inicialização: TOC, descritores, diretórios) são contados e listados. |
| P3 | Comportamento do jogo | Quadro final depois de N VBlanks, legado × nativo, mais `test_gameplay`. Se o tempo das leituras mudar (as nativas terminam antes), a diferença tem de ser explicada e o roteiro completado com o mesmo resultado. |
| P4 | Interface independente | `n0_native_limpo` cobre a implementação; teste de unidade do `DataFileSystem` sem nada do runtime. |
| P5 | Mods | Substituição aplicada e registrada; mod para arquivo inexistente ou ilegível dá erro claro; `test_projeto` (mod de arquivo). |
| P6 | Distribuição | `test_builder`, `test_preview`, `test_projeto`, `test_gameplay` passam; o Builder gera o executável com o provedor nativo por padrão. |
| P7 | Fechamento | `bs-hwscan`: as entradas gdFs da fronteira passam a `provedor = "nativo"`; a catraca de `fronteira.provedor.legado` cai; `legado.subsistema.arquivos` é medido. |
| P8 | Rollback | `--arquivos legado` reproduz o comportamento atual (quadro idêntico ao build do N0). |

## 5. Etapas

1. Gerador: slots sempre ativos por lista no config (comportamento idêntico; diferencial
   de quadro).
2. `DataFileSystem` + testes de unidade.
3. Adaptador gdFs + guardas + registro + opção de provedor + log de leituras nativo.
4. Log de leituras legado (GD-ROM HLE) + contador de setores restantes + comparação.
5. Portões P1-P8; suítes.
6. Revisão do Codex; `STATUS.md` e `docs/N1-RELATORIO.md`.

## 6. Fora do N1 (caminhos restantes, documentados)

Continuam legados no N1:
- boot (IP.BIN e executável por `RuntimeDisc`, parte do BIOS HLE: N8);
- inicialização do gdFs e estado do drive (GD-ROM HLE);
- `disc_meta`;
- o próprio GD-ROM HLE.

Eles só saem quando não houver consumidor. Isso **não** conta como eliminação do hardware
original.
