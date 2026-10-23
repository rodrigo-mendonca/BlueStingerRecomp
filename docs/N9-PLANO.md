# N9 — Blue Stinger Remastered: planejamento

**Situação: planejamento aprovado pelo mantenedor (2026-10-23). N9 NÃO INICIADO.** Este documento só registra o
escopo, a arquitetura e os critérios. Nada do N9 é implementado antes do encerramento do N8 (executável sem legado) e
da autorização do mantenedor para começar. A ordem N0–N10 não muda (`docs/PLANO-PORT-NATIVO.md`, seções 4 e 4.2).

Fontes: o pedido do mantenedor de 2026-10-23 ("Planejamento oficial do N9: Remasterização"), a definição do N9 de
2026-10-22 (`docs/PLANO-PORT-NATIVO.md` seção 4) e os itens transferidos do N7 (`docs/N7-PLANO.md` seção 25.2: SSAO,
widescreen real, MSAA, filtragem, iluminação e sombras aprimoradas, presets).

---

## 1. Visão

O N9 transforma o Blue Stinger num **jogo moderno de PC**, e não só no original executado em 4K. O projeto continua
entregando, do mesmo código, uma **edição Original** fiel à experiência do Dreamcast.

| A Remastered oferece | Base que já existe (N0–N8) |
|---|---|
| Instalação moderna (instalador gráfico) | Builder/Recompiler (`builder/`, `docs/RECOMPILER-UI.md`), `instalacao.toml`, modo portátil |
| Interface integrada ao jogo (menu inicial, pausa, configurações) | menu interno do N7c (`app/settings_menu.*`, `app/menu_overlay.*`), tradução pela língua das legendas |
| Câmera livre obrigatória | câmera livre do N7c (`transitional/free_camera.*`, vista em `0x8C24D680`), movimento relativo à câmera |
| Controles modernos | N3: SDL3 Gamepad, XInput, `controles.toml`, perfis por GUID |
| Saves sem a interface de VMU | N4: saves nativos (`--saves nativo`, Documentos\DC-Recomp\Blue Stinger\saves, importar/exportar `.vms`) |
| Legendas ativadas por padrão | legendas sincronizadas pela voz audível (N7, pacotes locais en-US 0.3.0 e pt-BR 0.2.0) |
| Gráficos e iluminação modernizados; widescreen; texturas HD; água | N7: Vulkan, resolução interna, cadeia de pós-processamento, contrato de texturas HD por conteúdo, shaders |
| Cinemáticas e áudio remasterizáveis | N5 (MP4 do Builder, `mods\video`) e N6 (FLAC/OGG, dublagens, músicas por ID) |
| Acessibilidade e configurações completas | opções de legendas (tamanho, contorno, fundo), volumes por barramento, sensibilidade da câmera |
| Otimização e estabilidade | N2 (tempo lógico), N7 (diagnósticos `BS_VK_LENTOS`, recuperação do dispositivo), N8 (sem legado) |

### 1.1 Objetivos

1. Duas edições instaláveis do **mesmo núcleo C++**: **Blue Stinger Original** e **Blue Stinger Remastered**.
2. Uma experiência de PC completa na Remastered: instalador, menus, saves, controles, câmera, gráficos, áudio e
   acessibilidade.
3. A Original preservada e validada contra as referências que já existem (estado por passo, quadros do modo fiel,
   oráculo do Flycast).
4. Recursos audiovisuais substituíveis sem recompilar o executável, sempre que possível.

### 1.2 Fora de escopo do N9

- **Ferramentas de modificação e APIs de extensão** (editores, carregador geral de pacotes, SDK de mods, elevação dos
  sistemas do jogo para C++ editável): ficam no **N10**.
- Mudar o conteúdo do jogo: história, falas, regras de combate, física, IA, dificuldade e quebra-cabeças.
- Reintroduzir a camada legada removida no N8 (CPU SH-4 interpretada, periféricos simulados, BIOS HLE).
- Distribuir recursos comerciais (CDI, BIOS, arte, áudio, vídeo, texto do jogo) no repositório público ou num pacote
  público sem autorização.
- Multijogador, conteúdo novo de história e serviços online.

---

## 2. Arquitetura: um projeto, duas edições

### 2.1 Princípios (critérios formais)

1. **Original e Remastered compartilham o mesmo núcleo C++**: gameplay, temporização, áudio, saves, controles e
   Vulkan. Nada de projeto separado, recompilação do zero, fork permanente ou branch por edição.
2. **O Recompiler escolhe o perfil de instalação** (a edição) e o grava na instalação.
3. **O perfil Remastered impõe a câmera livre**; as outras preferências continuam configuráveis.
4. **O núcleo de gameplay não conhece detalhes de apresentação** que podem viver em módulos separados (menus,
   HUD moderno, efeitos, gerenciador de saves). Ele expõe eventos e estados; a apresentação os consome.
5. **Recursos audiovisuais são substituíveis sem recompilar** o executável sempre que possível (filmes, músicas,
   dublagens, texturas, shaders, legendas).
6. As ferramentas avançadas de modificação e as APIs de extensão ficam no **N10**.
7. **Nenhuma camada legada removida no N8 volta.**
8. **Saves e configurações sobrevivem às atualizações** (as do jogador nunca são sobrescritas).
9. **Validação separada** para Original e Remastered.
10. **Nenhum arquivo comercial protegido no repositório público** sem autorização.

### 2.2 Camadas

```
            +---------------------------------------------------------------+
 Instalador | N9.1: telas, idioma, edição, CDI, componentes, pasta, progresso |
 (Builder)  | (mecanismo: bs-builder / builder_core, sem mudar o contrato)   |
            +-------------------------------+-------------------------------+
                                            | instalacao.toml [edicao]
            +-------------------------------v-------------------------------+
 Perfil de  | EditionProfile (imutável na execução): original | remastered    |
 edição     | políticas: câmera, legendas padrão, menus, saves, gráficos padrão|
            +-------------------------------+-------------------------------+
                                            | políticas, padrões
            +-------------------------------v-------------------------------+
 Preferên-  | Preferences (graficos/audio/legendas/controles.toml), por jogador |
 cias       | clamp_to(perfil): o perfil limita, as preferências escolhem      |
            +-------------------------------+-------------------------------+
                                            |
 Apresenta- | UI nativa (menu inicial, pausa, configurações, saves, HUD),     |
 ção        | pós-processamento, iluminação, água, texturas HD, widescreen    |
            +-------------------------------+-------------------------------+
                                            | eventos e estados (somente leitura)
 Núcleo     | código gerado + native/ (arquivos, tempo, controles, saves,     |
            | áudio, vídeo, Vulkan) — o MESMO nas duas edições                |
            +---------------------------------------------------------------+
```

- **`EditionProfile`** (planejado; nome provisório, em `native/platform`): carregado uma vez, de
  `instalacao.toml` (`[edicao] perfil = "original" | "remastered"`). Não é uma preferência: o jogador não o troca pelo
  menu, os saves não o carregam e uma atualização não o muda sem o instalador.
- **Preferências** continuam por jogador (os arquivos de hoje). O perfil define os **padrões** da primeira execução e
  as **restrições** (`clamp_to` com o perfil, como hoje com as capacidades do renderizador). Exemplo: na Remastered,
  `camera.mode = Original` num `controles.toml` antigo é ignorado (e registrado no log), sem apagar o arquivo.
- **Perfil ≠ preset gráfico.** O preset gráfico (Original, Remastered padrão, qualidade) é uma preferência dentro do
  perfil; trocar o preset não muda a edição. A apresentação visual Original pode ser escolhida na Remastered quando for
  compatível, sem desligar os recursos obrigatórios da edição (câmera livre, menus e saves modernos).
- **Detalhes de edição no núcleo**: só onde o comportamento do jogo depende da política (por exemplo, a instalação dos
  slots da câmera livre). Cada ponto desses é registrado na documentação e tem teste nas duas edições.

### 2.3 Instalação e dados

- O Builder já gera o executável a partir do CDI do usuário. Com o N9, ele recebe a edição escolhida e a grava em
  `instalacao.toml`. A mesma pasta não tem as duas edições ao mesmo tempo; duas instalações lado a lado são permitidas.
- **Arquivos do jogo** (pasta de instalação: executável, `data\`, `mods\`) ficam separados dos **dados pessoais**
  (saves, preferências, capturas, log), que vão para a pasta do usuário (hoje Documentos\DC-Recomp\Blue Stinger e a
  pasta das preferências; o modo portátil continua existindo).
- Atualização: o instalador detecta a instalação, preserva os dados pessoais, troca só os arquivos do jogo e confere os
  hashes.

---

## 3. Regras obrigatórias da edição Remastered

### 3.1 Câmera

- Câmera livre **ligada permanentemente**; nenhuma opção para desligá-la nem para escolher a Original.
- Analógico direito controla a câmera; o esquerdo acompanha a orientação horizontal da vista (movimento relativo à
  câmera, como no N7c).
- A direção é preservada nas transições entre cenários, no combate, no LOAD e nos cortes automáticos (o defeito de
  transição corrigido no N7 é pré-requisito, com teste de regressão).
- Configuráveis: sensibilidade, inversão de eixos, zona morta, suavização e recentralização.
- Saves antigos não alteram a política da câmera.
- As rotinas originais da câmera continuam no núcleo para os eventos roteirizados e para a edição Original.

### 3.2 Legendas e idioma

- Ativadas por padrão; idioma inicial **Português (Brasil)**; English e Desativadas disponíveis.
- A interface acompanha o idioma das legendas; desativadas mantêm o último idioma (regra já implementada no menu do
  N7c em 2026-10-23).
- Sincronização com o áudio real e separação das falas preservadas; valem também nos filmes.
- Identificação de personagem só com metadados confiáveis (nenhum pacote os tem hoje).
- Atualizações não sobrescrevem as preferências do jogador.

### 3.3 Gráficos

- O preset gráfico Remastered é o padrão da edição; cada efeito e cada nível de qualidade continuam ajustáveis.
- O perfil de edição não se confunde com um preset gráfico (2.2).
- A apresentação Original continua disponível quando for tecnicamente compatível.

---

## 4. Matriz Original × Remastered

| Aspecto | Original | Remastered |
|---|---|---|
| Núcleo (gameplay, tempo, áudio, saves, controles, Vulkan) | o mesmo | o mesmo |
| Câmera | Original ou Livre (escolha do jogador) | Livre, obrigatória |
| Movimento | do console ou relativo à câmera (na Livre) | relativo à câmera |
| Legendas | opcionais (padrão: idioma automático dos pacotes) | ligadas, pt-BR inicial |
| Interface | menu do jogo original + menu interno de configurações | menu inicial, pausa e configurações nativos |
| Saves | tela original (slots/VMU), cartão nativo do N4 | gerenciador de saves de PC; VMU invisível |
| Gráficos padrão | fiel (modo Original Dreamcast) | preset Remastered (ajustável) |
| Proporção | 4:3 | 16:9 verdadeiro (21:9 avaliado); 4:3 opcional |
| Texturas | originais (HD opcional se instalado) | HD quando o pacote existe |
| Iluminação, água, reflexos | originais | modernizados (ajustáveis) |
| Filmes e áudio | originais (mods opcionais) | remasterizados quando existirem; originais como recuo |
| Acessibilidade | opções do menu interno | conjunto completo (N9.9) |
| Validação | referências do port (estado por passo, quadros fiéis, Flycast) | campanha completa + matriz própria (seção 9) |

---

## 5. Subetapas

Cada subetapa tem requisitos, dependências, critérios de aceitação e prioridade (**E** = essencial, **R** =
recomendada, **O** = opcional) com complexidade (**P**, **M**, **G**, **GG**).

### N9.0 — Definição arquitetural e perfis de edição (E, M)

- `EditionProfile` em `instalacao.toml`, lido uma vez; padrões e restrições aplicados às preferências.
- Pontos do núcleo que dependem da edição listados e testados.
- Os testes de hoje rodam na Original sem diferença; a Remastered tem a sua bateria.
- **Aceitação:** as duas edições do mesmo executável; Original idêntica ao executável anterior no estado por passo
  das sessões gravadas e nos quadros do modo fiel; preferência que viola o perfil é ignorada e registrada; o perfil não
  muda pelo menu, por save ou por atualização sem o instalador.

### N9.1 — Instalador moderno e identidade visual (E, GG)

- Interface nova sobre o Builder atual (o mecanismo não muda): identidade visual própria, logotipo e ícones em várias
  resoluções, imagens de apresentação, tipografia e animações discretas, pt-BR e en-US, mouse e teclado, DPI alto.
- Telas: (1) boas-vindas; (2) escolha da edição, com imagem e descrição; (3) seleção e validação do CDI do usuário;
  (4) componentes opcionais e idiomas (legendas, dublagens, pacotes HD locais); (5) pasta; (6) progresso real de
  extração, preparação e instalação; (7) conclusão, atalhos e "Jogar agora".
- Também: detecção de instalações, atualização sem perder dados pessoais, verificação de arquivos, desinstalador,
  erros compreensíveis, log técnico opcional, cancelamento seguro e recuperação de falha (a troca segura de pastas que o
  Builder já faz), separação entre arquivos do jogo e dados pessoais.
- Arte: origem e direitos documentados para cada imagem; nada do jogo no instalador público (arte do jogo só a partir
  do disco ou dos arquivos do próprio usuário, como o ícone local de 2026-10-23).
- **Aceitação:** instalação limpa, atualização e desinstalação nas duas edições, com os dados pessoais intactos (hash
  dos saves e das preferências antes e depois); cancelamento em cada fase deixa a instalação anterior utilizável;
  progresso monotônico com as fases reais; 100 %/125 %/150 %/200 % de escala sem cortes; navegação completa só pelo
  teclado; textos em pt-BR e en-US sem chave faltando (o teste de catálogo que o Recompiler já tem).

### N9.2 — Interface moderna do jogo (E, G)

- Uma interface nativa compartilhada: menu inicial (Novo jogo, Continuar, Carregar, Configurações, Extras, Sair),
  pausa (Continuar, Salvar quando permitido, Carregar, Configurações, Menu principal, Sair) e configurações (vídeo e
  gráficos, áudio, controles, câmera conforme a edição, legendas e idioma, acessibilidade).
- As configurações são a mesma implementação e a mesma persistência no menu inicial e na pausa (evolução do
  `SettingsMenu`).
- Mouse, teclado e gamepad; foco visível; indicador de submenu; tradução completa; confirmação de resolução; retomada
  correta depois de qualquer mudança (regressão `tools/dev/n7_menu_pausa.py`).
- Ações com perda de progresso pedem confirmação.
- "Continuar" carrega o save válido mais recente, com tratamento de erro.
- A interface original do Dreamcast é substituída na Remastered só onde há equivalente nativo, sem quebrar a lógica do
  jogo (as telas originais continuam no núcleo e na Original).
- **Aceitação:** cada item dos três menus nas duas línguas, por mouse, teclado e gamepad; nenhuma mudança deixa o jogo
  pausado; a interface original não aparece na Remastered nos caminhos substituídos (roteiros de gameplay); a Original
  sem mudança.

### N9.3 — Saves nativos de PC (E, G)

- Na Remastered não aparecem os slots 1 e 2 nem os cartões VMU: o jogador escolhe o save numa lista (arquivo, data e
  hora, tempo de jogo, local quando disponível, prévia quando viável, nome).
- Criar, carregar, excluir e organizar; "Continuar" pelo mais recente válido; autosave opcional só em pontos
  comprovadamente seguros; backups e recuperação de corrupção.
- Dados na pasta do usuário, separados do executável.
- Saves antigos lidos e migrados com backup, sem modificar os originais automaticamente.
- A lógica de VMU do jogo continua no núcleo por um adaptador interno até existir uma camada nativa comprovadamente
  compatível (o cartão nativo do N4 já é esse adaptador). A interface de VMU some só para o jogador da Remastered.
- Quantidade de saves: só depois de medir os limites reais da lógica de salvamento do jogo (o que o jogo grava, o
  tamanho, os blocos, o que ele assume de um slot).
- **Aceitação:** save e LOAD de gameplay real (inclusive num save point) nas duas edições; saves da Original carregam
  na Remastered e vice-versa onde o formato permitir; corrupção simulada recuperada do backup; nenhum save original
  alterado pela migração (hash); o oráculo do Flycast continua aceitando os `.vms` exportados.

### N9.4 — Controles e experiência de PC (E, M)

- Remapeamento por ação no menu; teclado, mouse, Xbox e PlayStation, com ícones pelo dispositivo; sensibilidade e
  zonas mortas; inversão; vibração configurável; vários monitores; janela, tela cheia e sem bordas; cursor e foco
  corretos (regra de 2026-10-23, `app/cursor_policy.h`); escala da interface em 1080p, 1440p, 4K e ultrawide.
- Conforto não muda velocidade, física nem combate.
- **Aceitação:** cada ação remapeável e persistida; troca de dispositivo troca os ícones na hora; estado por passo
  igual ao do mapa padrão com o mesmo roteiro lógico; testes em 1 e 2 monitores.

### N9.5 — Widescreen verdadeiro e HUD (E, G)

- 16:9 verdadeiro (21:9 avaliado), sem esticar o 4:3: projeção do SDK (`njSetScreen` ou equivalente) e campo de visão,
  enquadramento da câmera, HUD independente da resolução, legendas e menus em áreas seguras.
- Geometria fora do enquadramento original verificada (paredes faltando, objetos que somem, culling); cenas
  roteirizadas e filmes com tratamento próprio (pilares ou recorte); alternância Original/widescreen.
- Pré-requisitos do N7 (seção 25.2): marca cena/HUD por desenho na IR.
- **Aceitação:** rotas de gameplay e cenas sem buracos visíveis nas bordas (capturas comparadas por área nova), HUD na
  posição certa em 16:9 e 21:9, filmes na proporção original.

### N9.6 — Iluminação, sombras e materiais (R, GG)

- Um pipeline integrado à geometria 3D, não só filtros de imagem: investigar profundidade e normais disponíveis,
  transformações e câmera, materiais e luzes originais; luzes dinâmicas, sombras em tempo real, emissivos, metal,
  vidro, plástico e superfícies molhadas; SSAO só com dados confiáveis (profundidade guardada depois do render pass,
  N7 25.2); compatível com os efeitos do N7.
- Provas técnicas antes de escolher os algoritmos.
- **Aceitação:** prova de cada técnica numa sala de referência com custo medido; nenhuma mudança no estado por passo;
  o preset Original continua idêntico ao modo fiel.

### N9.7 — Água e reflexos (R, G)

- Ondulação, reflexos em superfícies molhadas, SSR quando viável, alternativa para o que está fora da tela,
  transparência e refração quando apropriadas, qualidade ajustável.
- Só nas superfícies identificadas (material, textura ou malha), nunca reflexo genérico na cena inteira.
- **Aceitação:** lista das superfícies de água e molhadas por área, com a identificação usada; nenhuma superfície
  errada nas rotas de referência; custo dentro do orçamento de quadro do perfil.

### N9.8 — Texturas HD, antialiasing e pós-processamento (E para texturas e AA; R para o resto; G)

- Substituição de texturas sem alterar os dados originais: identificação estável por conteúdo (contrato do N7),
  manifestos, pacotes HD opcionais, cache, mipmaps, anisotrópica, UVs e transparência preservados.
- Antialiasing: avaliar MSAA (alvo multiamostrado e resolve, N7 25.2), SMAA, FXAA e TAA pela viabilidade.
- Refinar bloom, tonemapping e nitidez existentes, sem prejudicar a legibilidade do HUD, dos menus e das legendas.
- **Aceitação:** pacote HD de teste carregado, com recuo ao original por textura ausente; uso de VRAM medido por área;
  AA escolhido com comparação de custo e qualidade documentada; HUD e legendas fora dos filtros.

### N9.9 — Cinemáticas, áudio e acessibilidade (E para acessibilidade; R para os recursos; M)

- Filmes substituíveis (upscale) com a mesma duração, proporção, eventos e sincronia de áudio e legendas (contrato do
  N5).
- Músicas e dublagens substituíveis com eventos e sincronia preservados (contrato do N6); volumes separados.
- Acessibilidade: tamanho, contorno, fundo e contraste das legendas; escala do HUD; redução de flashes e efeitos
  intensos; controles e sensibilidade; identificação de falantes só quando comprovada; legibilidade e navegação.
- Tudo respeita o idioma e a edição instalados.
- **Aceitação:** os 17 filmes com a mesma duração e eventos com pacote de teste; música e fala substituídas sem mudar o
  estado por passo; cada opção de acessibilidade persistida e aplicada nos menus, no HUD e nas legendas.

### N9.10 — Refinamentos e recursos de PC (O/R, ver tabela 8)

Inventário moderno, mapa das áreas visitadas, diário opcional de objetivos, tempo de jogo, estatísticas, capturas,
modo foto, conquistas locais, perfis de jogador, detecção de atualização e erros melhores. Inventário, mapa e
objetivos preservam a descoberta e as regras do jogo, sem revelar conteúdo antes da hora.

### N9.11 — Desempenho, compatibilidade e qualidade (E, G)

- Perfis de qualidade; VRAM e recursos Vulkan otimizados; menos esperas entre CPU e GPU; frame time e memória
  monitorados; NVIDIA, AMD e Intel; recuperação segura de falha do dispositivo; 1080p, 1440p e 4K; saves antigos;
  campanha completa, câmera livre, combate, transições, menus, filmes e áudio; determinismo onde exigido; Original
  comparada com as referências validadas.
- Nunca sacrificar a correção do gameplay por ganho gráfico.
- **Aceitação:** matriz de GPUs com os três fabricantes, em três resoluções, sem falha nem perda do dispositivo nas
  rotas; percentil 99 do tempo de quadro dentro do orçamento do perfil; 0 faltas de áudio no H9 em cada perfil; campanha
  completa nas duas edições.

---

## 6. Requisitos não funcionais

- **Fidelidade da Original:** estado por passo e quadros fiéis iguais às referências do port.
- **Determinismo:** a Remastered não muda o estado lógico por causa de apresentação (mesmo roteiro, mesmo estado por
  passo, salvo o que a câmera livre muda por definição e o que estiver documentado).
- **Desempenho:** orçamento por perfil (por exemplo, 60 campos/s com folga no perfil padrão em GPU de entrada); nenhum
  recurso gráfico pode causar falta de áudio.
- **Robustez:** recurso ausente ou inválido volta ao original (e é registrado); falha do dispositivo recupera.
- **Persistência:** escrita atômica; atualização nunca sobrescreve dados pessoais.
- **Localização:** pt-BR e en-US completos em toda a interface (instalador, menus, mensagens).
- **Direitos:** cada arte, fonte e som do projeto com origem e licença documentadas.

## 7. Ordem de implementação e dependências

Pré-requisito: **N8 encerrado** (executável sem legado, com a regressão integral antes e depois dele).

| Marco | Subetapas | Depende de |
|---|---|---|
| 1 | N9.0 perfis de edição | N8 |
| 2 | N9.1 instalador e identidade visual | 1 |
| 3 | N9.2 interface do jogo | 1 |
| 4 | N9.3 saves modernos | 3 (lista e menus), N4 |
| 5 | N9.4 controles e PC | 3 |
| 6 | N9.5 widescreen e HUD | 1; marca cena/HUD na IR |
| 7 | N9.6 iluminação e materiais, N9.7 água e reflexos | 6 (projeção), profundidade guardada |
| 8 | N9.8 texturas HD e AA | 6 (para o AA com a projeção nova) |
| 9 | N9.9 filmes, áudio e acessibilidade | 3 |
| 10 | N9.10 refinamentos | 3, 4 |
| 11 | N9.11 otimização e validação final | todos |

Reorganização técnica sugerida: a marca cena/HUD na IR e a profundidade guardada (pré-requisitos do N7) entram no
começo do marco 6, porque servem ao 16:9, ao SSAO e à água. O instalador (marco 2) pode andar em paralelo com os
marcos 3–5, já que só depende do perfil.

## 8. Priorização

| Recurso | Prioridade | Complexidade |
|---|---|---|
| Perfil de edição | E | M |
| Instalador gráfico | E | GG |
| Menus inicial/pausa/configurações | E | G |
| Gerenciador de saves | E | G |
| Remapeamento por ação | E | M |
| Widescreen 16:9 e HUD | E | G |
| 21:9 | R | M |
| Texturas HD (carregador e pacotes) | E | G |
| Antialiasing | E | M |
| Iluminação e sombras | R | GG |
| Água e reflexos | R | G |
| SSAO | O | M |
| Filmes e áudio remasterizados (suporte) | R | M |
| Acessibilidade | E | M |
| Inventário moderno | R | G |
| Mapa de áreas visitadas | O | G |
| Diário de objetivos | O | G |
| Tempo de jogo e estatísticas | R | P |
| Capturas de tela | R | P |
| Modo foto | O | M |
| Conquistas locais | O | M |
| Perfis de jogador | O | M |
| Detecção de atualização | R | P |
| Perfis de qualidade e validação multi-GPU | E | G |

## 9. Estratégia de testes

- **Duas baterias**, Original e Remastered, com os mesmos roteiros por passo e as sessões gravadas
  (`tools/dev/gravacoes.py`, `tools/dev/n2_sessoes.py`).
- **Original:** igualdade com as referências do port (estado por passo, saves, quadros do modo fiel, oráculo do
  Flycast onde se aplica).
- **Remastered:** estado lógico igual à Original onde a apresentação não muda o jogo; diferenças da câmera livre
  listadas e medidas; capturas por área (widescreen, iluminação, água); campanha completa.
- **Interface:** testes de janela com o controle virtual (como `n7_menu_pausa.py` e `n7_cursor.py`) para cada menu,
  nas duas línguas e com mouse, teclado e gamepad.
- **Instalador:** instalação, atualização, cancelamento por fase e desinstalação em pastas isoladas, com os hashes dos
  dados pessoais.
- **Saves:** migração com backup, corrupção simulada, ida e volta entre edições, `.vms` no Flycast.
- **Desempenho:** tempo de quadro (p50/p99), VRAM e faltas de áudio por perfil e resolução; NVIDIA, AMD e Intel.
- **Revisão do Codex** em cada subetapa relevante, como nas etapas anteriores.

## 10. Riscos e limitações

| Risco | Mitigação |
|---|---|
| Geometria e culling do jogo pensados para 4:3 | verificação por área; recorte ou pilares nas cenas que não suportam |
| Pouca informação de material e luz nos modelos originais | provas técnicas antes de escolher; identificação por textura ou malha, com tabela por área |
| Saves com formato e limites fixos do jogo | medir os limites; adaptador interno; migração com backup |
| Câmera livre em eventos roteirizados | rotinas originais preservadas nos eventos; testes por cena |
| Arte do instalador e pacotes HD com direitos incertos | origem documentada; arte do jogo só do disco do usuário e só local |
| Custo gráfico causando faltas de áudio | orçamento por perfil; o H9 do N6 como critério |
| GPU AMD integrada (pendência do N7) | recuperação do dispositivo; a matriz do N9.11 inclui AMD |
| Escopo grande | prioridades E/R/O; marcos verificáveis; O só depois dos E |

## 11. Preparação para o N10

- Os contratos de substituição usados no N9 (texturas por conteúdo, filmes por ID, músicas e falas por ID, shaders,
  legendas) são os mesmos que o N10 vai expor aos mods, com manifestos versionados.
- A remasterização oficial fica separada dos mods do usuário: pacotes oficiais têm identificação própria e ordem de
  prioridade definida, e um mod pode substituí-los.
- Os módulos de apresentação do N9 (menus, HUD, saves) nascem com fronteiras claras, para a elevação dos sistemas do
  jogo no N10 não precisar desfazê-los.

## 12. Decisões técnicas em aberto

1. Formato e local do perfil de edição (`instalacao.toml` `[edicao]` é a proposta) e o comportamento de uma pasta
   com preferências de outra edição.
2. Tecnologia da interface do instalador (Win32 com Direct2D, como o menu do jogo; WinUI; ou um renderizador próprio
   compartilhado com a interface do jogo).
3. Se a interface do jogo (N9.2) usa o mesmo motor de desenho do instalador.
4. Formato dos saves de PC: o `.vms` do N4 com metadados ao lado, ou um contêiner novo com o `.vms` dentro; política de
   quantidade depois de medir os limites do jogo.
5. Pontos seguros de autosave: critério e prova.
6. Algoritmo de antialiasing (MSAA, SMAA, FXAA ou TAA) depois das provas.
7. Técnica de iluminação e sombras (forward com luzes por objeto, deferred parcial ou só pós-processamento com
   profundidade) depois das provas.
8. 21:9: suporte completo ou só em gameplay.
9. Origem das artes do instalador e da identidade visual (criação própria, encomenda, licença).
10. Pacotes HD e áudio remasterizado oficiais: quem produz, licença e como se distribuem sem conteúdo comercial.
11. Preset gráfico Remastered padrão por classe de GPU (detecção automática ou escolha no instalador).
12. Se a Original ganha o menu inicial nativo como opção, ou mantém só a interface original.
