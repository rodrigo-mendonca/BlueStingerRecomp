# Mods padrão (legendas) — conteúdo só local

O Builder (Recompiler, Developer e `bs-builder`) instala em toda instalação os pacotes que estiverem aqui
(`mods-padrao/<id>/mod.toml`): a pasta vai para `<instalação>\mods\<id>` e os `.srt` de `video\` do pacote vão para
`<instalação>\mods\video`.

**O conteúdo destas pastas não é versionado nem publicado** (`.gitignore`): as legendas são a transcrição e a tradução
das falas do jogo comercial, geradas na máquina de quem tem o disco. Só este README está no repositório.

Como gerar localmente, a partir dos dados do seu disco (`tools/dev/legendas`, ver `docs/LEGENDAS-EN-PLANO.md`):

1. `catalogo.py` e `transcrever.py` (faster-whisper local, no venv do ASR): as transcrições em
   `%LOCALAPPDATA%\BlueStingerRecomp\legendas-en`.
2. `reconferir.py`: retranscreve as falas duvidosas com os nomes do jogo (a escolha vai para `revisoes.en-US.tsv`).
3. `gerar_mod.py --versao <v>`: o pacote `legendas-en` (glossário, revisões, alucinações excluídas).
4. `gerar_traducao.py --versao <v>`: o pacote `legendas-pt-br` a partir de `traducao.pt-BR.tsv`.
5. Copiar `%LOCALAPPDATA%\BlueStingerRecomp\legendas-en\mod\legendas-en` e `...\mod\legendas-pt-br` para esta pasta.

`revisoes.en-US.tsv` e `traducao.pt-BR.tsv` (em `tools/dev/legendas`) também ficam só locais, com cópias datadas em
`%LOCALAPPDATA%\BlueStingerRecomp\backups`.

Para a distribuição local de testes, `tools/dist/make_builder.ps1 -LegendasLocais` copia estes pacotes para o kit (com
aviso de uso local no `VERSAO.txt`); sem a opção, o Builder sai sem legendas. `tools/dist/make_preview.ps1` usa o
repositório como kit e leva os pacotes daqui.
