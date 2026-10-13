# runtime/

| Pasta | Conteúdo | Marco |
|---|---|---|
| core/ | laço principal, scheduler de eventos, log, config | M4 |
| cpu/ | contexto SH-4, helpers, interrupções | M3–M4 |
| memory/ | mapa de memória, RAM, espelhos, store queues, DMAC | M4 |
| hle/ | syscalls de BIOS, estado de boot | M4 |
| gdrom/ | GD-ROM sobre game-data/ | M4 |
| pvr/ | TA, PowerVR2, renderer Vulkan | M4–M5 |
| aica/ | som, ARM7, DSP | M6 |
| maple/ | controles, VMU | M7 |
| platform/ | SDL3, Vulkan, janela, caminhos | M4 |
| game/ | específico do Blue Stinger: overrides, hooks, melhorias | M3+ |
| app/ | `main()` do executável | M4 |

Veja ARCHITECTURE.md §6.
