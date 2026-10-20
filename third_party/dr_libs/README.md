# dr_flac (terceiros, domínio público ou MIT-0)

Decodificador FLAC das dublagens e do áudio de mods (N6 fase E, `docs/N6-PLANO.md` seção 16). O jogo o usa só para
arquivos de `mods\audio`; os WAV originais do disco não passam por ele.

- Origem: https://github.com/mackron/dr_libs (`dr_flac.h`, cabeçalho único)
- Commit fixo: `dfe8377631000664666519fdb83da193fd8037f4` (2026-08-31, "dr_flac: Fix a seeking error"); versão no
  cabeçalho `v0.13.4 - TBD`, posterior à v0.13.3 (2026-01-17) com as verificações de limites e de estouro de 2026-04
  (metadados, imagem, realocação) e a validação na inicialização.
- `dr_flac.h` SHA-256: `111144e778f55738db6851cb226015c419e00d04b916a09506d4856d9cff945c`
- Licença: escolha entre domínio público e MIT-0 (alternativa 2 do fim do cabeçalho, copiada em `LICENSE`).
  Sem alterações locais.
- Entrada não confiável (arquivos de mods): só leitura da memória (`drflac_open_memory`), tamanho máximo
  limitado antes de decodificar (`native/audio/sound_file.cpp`), sem callbacks de metadados.
