# legacy/

A simulação do Dreamcast (CPU SH-4, memória, PowerVR2/TA/VRAM, AICA, Maple, GD-ROM, BIOS
HLE, periféricos do SH-4) fica no alvo `bs_legacy`. É a base de validação de cada etapa:
o provedor nativo precisa reproduzir o comportamento do legado.

**N0:** os arquivos continuam em `runtime/`, e o alvo só dá nome ao conjunto
(`docs/PLANO-PORT-NATIVO.md`, decisão 5). Cada subsistema sai daqui quando migrar, e N8
remove o legado do produto.

O que já fica fora do produto:
- a leitura direta do CDI (`bs_runtime_cdi`: `gdrom/image_source.cpp`,
  `boot/disc_boot.cpp`), ligada só pelo Builder, pelo `bs-diff` e pelos testes com a
  imagem real;
- a garantia, conferida no mapa do linker pelo teste `n0_produto_sem_cdi`.

Instrumentação do N0 (desligada por padrão; não muda o comportamento):

| Variável | Grava |
|---|---|
| `BS_HW_PROBE=<tsv>` | sonda de hardware (`runtime/debug/hw_probe`): acessos fora da RAM, store queues e desvios indiretos, por função do guest |
| `BS_EVENT_TRACE=<tsv>` | trace de ordem (`runtime/debug/event_trace`): VBlank, interrupções, continuações, threads e longjmp |
