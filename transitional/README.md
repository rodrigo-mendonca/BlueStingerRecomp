# transitional/

Interoperabilidade da fase híbrida (`docs/PLANO-PORT-NATIVO.md`, seção 2). O código daqui
**some** quando os chamadores forem elevados (N10). Nada daqui entra na API nativa.

| Arquivo | Papel |
|---|---|
| `handle_table.h` | Handles de 32 bits com geração (12 bits de geração, 20 de índice; 0 inválido). Objetos nativos ficam no host e o guest só vê o handle. Liberar invalida o handle antigo, e `clear()` encerra a sessão. |
| `guest_abi.h` | Camada 2, o adaptador `Sh4Context` ↔ `native/`. `GuestCall` lê os argumentos (r4-r7, depois a pilha; floats em fr4-fr11) e escreve o retorno (r0/fr0). `GuestMemory` só acessa a RAM principal e recusa hardware e P4. `GuestStruct` lê campos com layout declarado (nome, versão, tamanho). |
| `boundary_registry.h` | Camada 3: o provedor (legado/nativo) de cada função da fronteira declarada (`config/fronteiras_us.toml`). A troca usa o mecanismo dos mods. Com slots (BS_MODS) vale para chamadas diretas e indiretas. Sem slots vale só para as indiretas, e `select()` devolve `IndirectOnly`. |

Regras:
- ponteiros do host nunca vão para a RAM guest;
- quem cria um handle o libera, e o fim de sessão limpa tudo;
- o marshaling é explícito e versionado.

**N0:** nenhum subsistema tem provedor nativo, então tudo fica no legado. Testes em
`tests/unit/test_transitional.cpp`.
