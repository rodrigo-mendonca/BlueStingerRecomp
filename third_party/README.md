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
| Vulkan-Headers / volk / VMA | Apache-2.0 / MIT / MIT | renderer | M4 |
| xxHash | BSD-2 | cache de texturas/pipelines | M5 |
