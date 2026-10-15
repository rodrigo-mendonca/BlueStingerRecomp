# native/

Sistemas definitivos do port nativo (o que vai no `BlueStinger.exe` ao fim da migração;
`docs/PLANO-PORT-NATIVO.md`, seção 2). C++20 puro: **nada** do Dreamcast aqui. Não há
`Sh4Context`, endereços do guest, registradores ou layout de hardware. O teste
`n0_native_limpo` confere isso e `n0_native_api` compila os cabeçalhos só com `native/` no
include path.

No N0 só existem as **interfaces**. Os provedores entram etapa por etapa:

| Interface | Arquivo | Substitui | Etapa |
|---|---|---|---|
| `files::IFileSystem` | `platform/files.h` | GD-ROM (setores, FAD, syscalls do BIOS) | N1 |
| `time::IClock`, `time::IEventQueue` | `platform/time.h` | ciclos, TMU, RTC, VBlank, INTC | N2a/N2b |
| `input::IInput` | `platform/input.h` | Maple, serialização do pad | N3 |
| `saves::ISaveStore` | `platform/saves.h` | VMU, biblioteca de backup, flash | N4 |
| `video::IVideoPlayer` | `video/video.h` | Sofdec (SFD) sobre PowerVR/AICA | N5 |
| `audio::IAudioEngine`, `audio::IAudioOutput` | `audio/audio.h` | AICA, ARM7, `MANATEE.DRV` | N6 |
| `render::IRenderDevice` + IR (`CommandList`) | `render/render.h` | TA, store queues, VRAM, PowerVR2 | N7a/N7b |

Decisões do mantenedor (2026-10-15): renderizador em Direct3D 11 atrás de
`IRenderDevice`; saída de áudio XAudio2 atrás de `IAudioOutput`, com a lógica do Manatee e
os bancos reconstruídos nativamente; vídeos convertidos pelo Builder e tocados com Media
Foundation atrás de `IVideoPlayer`, desde que formato e fidelidade sejam validados (senão,
apresentar alternativas).

`core/` traz `Result<T>`/`Error` e `Handle<Tag>`, o handle de 32 bits tipado. Os valores
vêm de `transitional/handle_table.h` enquanto o guest guarda handles.

`sdk/` e `game/` (APIs do SDK em forma tipada e sistemas do jogo elevados) começam quando
houver identificação por contrato (N1 em diante). Não há nomes de API do SDK sem evidência.
