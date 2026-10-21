# libogg e libvorbis (terceiros, BSD-3, Xiph.Org)

Decodificador OGG/Vorbis do áudio de mods (dublagens, músicas remasterizadas), escolhido pelo mantenedor em
2026-10-20 no lugar do `stb_vorbis` (CVE-2023-45675…45682 sem correção publicada). O jogo o usa só para arquivos de
`mods\audio`; os WAV e o MLT originais do disco não passam por ele. Só a **decodificação** está aqui (sem o
codificador `vorbisenc` e sem os livros/modos dele).

| Biblioteca | Versão | Tarball oficial | SHA-256 do tarball (conferido com `SHA256SUMS` da Xiph) |
|---|---|---|---|
| libogg | 1.3.6 (2025-06-16) | https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.gz | `83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638` |
| libvorbis | 1.3.7 (2020-07-04, a última versão publicada) | https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.gz | `0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab` |

- **Arquivos:** `libogg/include/ogg/{ogg.h,os_types.h}`, `libogg/src/{bitwise.c,framing.c,crctable.h}`,
  `libvorbis/include/vorbis/{codec.h,vorbisfile.h}`, `libvorbis/lib/` (as fontes do alvo `vorbis` do CMake oficial, o
  `vorbisfile.c` e os cabeçalhos internos). Copiados sem alterações, exceto:
- **`libogg/include/ogg/config_types.h`**: escrito aqui no lugar do `config_types.h.in` que o configure/CMake do libogg
  preenche (tipos de `<stdint.h>`; Windows x64 e Linux x86_64).
- **Licenças:** `libogg/COPYING` e `libvorbis/COPYING` (BSD de 3 cláusulas): o aviso de copyright vai junto da
  distribuição (`make_builder.ps1` copia para `licencas\`).
- **Segurança:** os CVE conhecidos do libvorbis são de versões antigas ou do codificador (a 1.3.7 corrige
  CVE-2017-14160, CVE-2018-10392 e CVE-2018-10393); a 1.3.6 do libogg melhora o tratamento de falha de alocação. A
  entrada de mods é não confiável: `native/audio/sound_vorbis.cpp` lê só da memória, confere canais, taxa, duração e
  o orçamento de PCM antes de decodificar (os mesmos limites do FLAC) e os testes passam arquivos corrompidos.
- Compilado pelo `native/CMakeLists.txt` (alvo `bs_xiph_vorbis`, C, avisos de terceiros desligados), no kit do
  Builder e no projeto exportado (`kKitEntries`): compila sem internet.
