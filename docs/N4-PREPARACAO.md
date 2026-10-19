# N4 (saves nativos): preparação

Data: 2026-10-18. **Só planejamento**, adiantado a pedido do mantenedor. O N4 não foi autorizado e
nada foi implementado. Fonte: inventário somente leitura do Codex sobre o binário
(`reviews/codex-20261018-220833.md`, privado). As identificações abaixo são hipóteses de trabalho até
a investigação do N4 conferir cada uma no disassembly. Itens sem evidência no binário estão marcados
"não verificado".

## Situação atual

O runtime não tem VMU: o jogo mostra "VMU not found", o LOAD GAME sem VMU funciona e não há como
salvar. O N4 entrega uma capacidade nova (salvar e carregar) por um provedor nativo. Não há caminho
legado funcionando que precise ser substituído.

## Fronteira proposta: API pública `bu`, não as rotinas internas

As funções `8C09945A`, `8C0994B4` e `8C094FF0` (citadas no `docs/N3-PLANO.md`) são predicados
internos da `bu` que consultam a camada `per`. Usá-las manteria o gerenciador, a FAT e o estado da VMU
originais, o que equivale a emular a VMU. A fronteira proposta são os pontos públicos:

| Endereço | Papel provável | Observado |
|---|---|---|
| `8C08BDEC` | `buInit(capacidade, máscara, work, callback)` | chamado por `8C01C41E` (capacidade 0x7A, máscara 3) |
| `8C08C074` / `8C08C07A` | registro dos callbacks de conclusão / progresso | `8C01C2D2` e `8C01C364` |
| `8C08CA34` | montagem da unidade (`buMountDisk`?) | nome não verificado |
| `8C08C1FC` | informação do disco | |
| `8C08C636` | existência de arquivo | `8C01C8CE` varre `BLUESTIN.000`–`.005` |
| `8C08C8D4` | `buLoadFileEx(drive, nome, dst, bloco_inicial, nblocos)` | string de erro no binário |
| `8C08C818` | gravação (`drive, nome, src, blocos`, pilha: tempo, flag) | ordem dos argumentos de pilha não verificada |
| `8C08C696` | estado da operação (−1 ocupada, 0 concluída) | polling com timeout de 0x1C2 iterações |
| `8C08C112` | último erro | |
| `8C08BEBC` | encerramento (`buExit`?) | não verificado |
| `8C08CC58`, `8C08CA5C` | tamanho e montagem da imagem VMS | rotinas puras: podem ficar no recompilado como oráculo |
| `8C15E5BC`, `8C15E6D8`, `8C15E73C` | `pdLcd` (tela da VMU, 192 bytes) | responder "LCD não suportado", desacoplado do save |

## Contrato que o `ISaveStore` atual não cobre

1. **Protocolo assíncrono:** aceito → ocupado → concluído → último erro, com callback de conclusão
   uma única vez, progresso determinístico e segunda operação recusada enquanto há uma ocupada.
2. **Imagem VMS:** cabeçalho de 128 bytes (descrição curta "BlueStinger", longa "BLUESTINGER SAVE
   DATA", identificador de 16 bytes), um ícone de 512 bytes, payload de 1 200 bytes (`8C24F93C`),
   CRC de 16 bits (0x1021). São 1 840 bytes, arredondados para 4 blocos (2 048). O LOAD lê a partir do
   bloco 1 para `8C24F8BC`, deixando o payload em +0x80, então a geometria VMS faz parte do contrato.
   Proposta: guardar os 2 048 bytes VMS canônicos dentro de um envelope nativo versionado (a versão
   pertence ao envelope, não ao VMS).
3. **Capacidade e erros:** disco ausente (`0xFF01`), não formatado (`0xFF03`), não encontrado
   (`0xFF05`), não abre (`0xFF07`), blocos livres e entradas livres. Gravação atômica: um erro no meio
   não destrói o save anterior.
4. Delete e format pelo jogo: não verificados.

## Ordem de implementação proposta (quando autorizado)

1. Investigação curta: conferir no disassembly as assinaturas da tabela e os callbacks.
2. Contrato: `ISaveStore` estendido (envelope versionado, estado, erros) e unitários de serialização
   e CRC, contra as rotinas puras como oráculo.
3. Adaptador `transitional/bu_adapter` com slots na API pública e o protocolo assíncrono; `pdLcd`
   respondendo "não suportado".
4. Testes rápidos:
   - imagem de 1 200 → 2 048 bytes;
   - offsets e CRC;
   - leitura a partir do bloco 1;
   - recusa de imagem truncada ou de versão futura;
   - disco cheio, somente leitura e arquivo ausente;
   - segunda operação durante "ocupado";
   - callback chamado uma vez só;
   - gravação interrompida preserva o save anterior.
5. Gameplay assistido, em poucas rotas curtas:
   - salvar `BLUESTIN.000`, reiniciar o processo e carregar;
   - comparar sala, inventário e estado;
   - sobrescrever um save;
   - disco cheio, com a mensagem correta.
6. Revisão do Codex e regressão completa no encerramento do N4.
