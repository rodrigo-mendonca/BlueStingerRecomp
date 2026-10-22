# third_party/

Cada dependência entra via CMake FetchContent ou submódulo **quando o marco que
a usa chegar**. Toda dependência precisa ter licença compatível com a do
projeto (ver LEGAL.md).

| Dependência | Licença | Uso | Marco |
|---|---|---|---|
| libchdr | BSD-3 | leitura de CHD | M1 |
| SHA-256 / CRC32 | implementação própria | hashes do extrator | M1 |
| toml++ | MIT | config por jogo | M3 |
| SDL3 | zlib | janela, input, áudio | M4 |
| Vulkan-Headers 1.3.282 | Apache-2.0 | backend Vulkan; só `vulkan.h`, `vulkan_core.h`, `vk_platform.h` e `vk_video/` (`third_party/vulkan`, a mesma versão que o SDL3 3.2.30 traz); o carregador é aberto em tempo de execução, sem volk nem VMA | N7b |
| xxHash | BSD-2 | cache de texturas/pipelines | M5 |
| pl_mpeg | MIT | conversão dos filmes no Builder (`third_party/pl_mpeg`) | N5 |
| dr_flac | domínio público ou MIT-0 | FLAC de mods de áudio (`third_party/dr_libs`) | N6 |
| libogg + libvorbis | BSD-3 (Xiph.Org) | OGG/Vorbis de mods de áudio, só a decodificação (`third_party/xiph`) | N6 |
