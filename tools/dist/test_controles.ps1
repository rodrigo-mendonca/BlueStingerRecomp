# N3-0: contrato, instrumentacao e testes de referencia dos controles (docs/N3-0-RELATORIO.md).
# Nada aqui muda o jogo por padrao: todos os casos usam variaveis de diagnostico.
#   K1  contrato x legado, matriz: a sonda BS_PAD_CONTRATO confere cada passada do servidor pd
#       (8C15C4E6) com transitional/pad_contract; os 256 valores de gatilho R, gatilho L, x1 e y1,
#       os 12 botoes, os 66 pares, gatilho com botao, bordas; 0 divergencias, 0 teclado. K1b: os 256
#       valores de x2 e y2 (support com os bits 20-21) e os quatro padroes de old x on
#   K2  64 combinacoes dos bits 16-21 do support (BS_MAPLE_FUNCAO): 0 divergencias em todas
#   K3  modo != 0 do servidor (BS_PAD_MODO=1): 0 divergencias
#   K4  negativos do contrato (C4): troca_ab, gatilho127, info_sem_conectado, old_nativo (este com
#       support incompativel, quando o jogo zera o on): divergencias > 0 em cada um
#   K5  determinismo: duas execucoes do release iguais, duas do build com mods iguais; e paridade
#       release x mods (traco de passo_limiares, com as colunas de tempo)
#   K6  limiares do jogo: zona morta estrita (-20 < v < 20), direcional so com |v| > 48, gatilho
#       digital com byte >= 128 (pad_traco.py limiares); contrato 0 divergencias
#   K7  conexao no gameplay: desconectar com A, reconectar neutro, reconectar com A (borda press)
#   K8  porta vazia desde o boot, conexao no passo 60, START no titulo
#   K9  soft reset A+B+X+Y+START: o jogo chama o menu do BIOS (sem HLE); o controle sem START nao
#   K10 negativos do traco (C4): A/B trocados no Maple, amostra adiantada um passo, amostra na entrega
#       do VBlank-In com uma transicao injetada depois dela e antes do disparo do DMA (indice
#       transicao, no primeiro acesso ao MDST): o compara acusa. Controle: a amostra no VBlank-In com
#       roteiro por passo da o mesmo traco (o ponto so importa se a entrada muda dentro do campo)
#   K11 campos que o jogo nao le (r, l, x2, y2, old, name, extend) com lixo depois de cada passada:
#       quadros, traco e estado do jogo a cada passo (BS_STATE_PASSO) iguais nas 4 rotas do gameplay
#       assistido; contrato 0 divergencias nelas. Vale para essas rotas, nao para o jogo inteiro
#   K12 roteiros por leitura convertidos para "indice passo": traco e quadros iguais aos originais
#   K13 padrao inalterado: sem variaveis, quadros e leituras iguais ao executavel de referencia
#       (-ExeRef, antes do N3-0; obrigatorio) em passo_cgate, passo_heliporto e newgame_area1 (indice
#       por leitura); o traco ligado nao muda os quadros. O lado novo com --video legado e --audio legado (a
#       referencia e anterior ao N5/N6); a referencia sem --video (2026-10-25)
#   Comparacoes de igualdade (K5, K10 controle, K11, K12, K13) so com as duas execucoes completas (Completa: rc,
#   VBlanks, vivacidade, todo log com dados, quadros/traco/estado no ultimo indice do roteiro): dois tracos truncados
#   ou vazios iguais reprovam
#   powershell -File tools/dist/test_controles.ps1 [-Exe <release>] [-ExeMods <release com mods>]
#       [-ExeRef <exe anterior>] [-Data <data>] [-Paralelo 8] [-Casos K1,K2,...] [-RotasK13 r1,r2]
# video legado: estabelecida com o Sofdec original; o filme nativo (N5) dura o tempo real do filme e
# desloca os artefatos por VBlank depois de cada filme (a logica por passo e a mesma).
param(
    # N8.4: --controles legado e --saves legado (modelo do Maple) so existem no build de validacao; o codigo nativo nele e o do produto
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-validacao\app\bluestinger.exe"),
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-mods\app\bluestinger.exe"),
    [string]$ExeRef = (Join-Path $PSScriptRoot "..\..\analysis\rollback-antes-n3\exe\bluestinger-release.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$Paralelo = 8,
    [string[]]$Casos = @("K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8", "K9", "K10", "K11", "K12", "K13"),
    [string[]]$RotasK13 = @("passo_cgate", "passo_heliporto", "newgame_area1"),  # roteiros de tools/dist/roteiros
    [string]$Saida = (Join-Path $env:TEMP "bs-controles-teste")
)
. (Join-Path $PSScriptRoot "valida_exe.ps1"); Assert-Validacao $Exe; Assert-Validacao $ExeMods  # N8.4: o exe precisa ser de validacao
$ErrorActionPreference = "Stop"
# com -File, "K1,K2" chega como um texto so
$Casos = @($Casos | ForEach-Object { $_ -split "," } | Where-Object { $_ }); $RotasK13 = @($RotasK13 | ForEach-Object { $_ -split "," } | Where-Object { $_ })
foreach ($e in @($Exe, $ExeMods)) { if (-not (Test-Path -LiteralPath $e)) { Write-Host "SKIP: executavel ausente ($e)"; exit 0 } }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { Write-Host "SKIP: python ausente"; exit 0 }
$Exe = (Resolve-Path $Exe).Path; $ExeMods = (Resolve-Path $ExeMods).Path; $Data = (Resolve-Path $Data).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$rot  = Join-Path $repo "tools\dist\roteiros"
$tool = Join-Path $repo "tools\dev\pad_traco.py"
$marker = Join-Path $Saida ".bs-controles-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
python (Join-Path $repo "tools\dev\pad_roteiros.py") $Saida | Out-Null
$want = @{}; foreach ($c in $Casos) { $want[$c] = $true }

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function VBlanksFor($file) {
    $last = 0
    foreach ($l in [IO.File]::ReadLines($file)) { $t = $l.Split("#")[0].Trim(); if ($t -match "^(\d+)\s") { $last = [int64]$Matches[1] } }
    return [int64]($last * 2 + 3000)
}

# --- execucoes (paralelas, no maximo $Paralelo) ---
$jobs = [System.Collections.ArrayList]::new()
# K1-K12 leem o contrato do Maple (BS_PAD_CONTRATO) e trocam a funcao do controle: so existem no
# provedor legado, que deixou de ser o padrao no fim do N3. K13 roda sem a opcao (o padrao).
# Todos com --video legado (escopo historico com o Sofdec; revisao Codex 2026-10-20: K1-K11 caiam no padrao nativo).
# $video: "" = sem a opcao (o executavel de referencia do K13 e anterior ao N5 e nao a conhece; so tem o Sofdec, que e o
# legado). $audio: "" = o padrao (nativo); "legado" so onde o caso compara com um executavel anterior ao N6 (K13) ou
# com roteiros convertidos de execucoes com o audio legado (K12).
function Job($tag, $exe, $script, $n, $envs = @{}, $tempo = "nativo", [switch]$NoTrace, [switch]$Probe, $ctl = "legado", $saves = "", $video = "legado", $audio = "") {
    $p = Join-Path $Saida $tag
    $e = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = $script; BS_FRAME_LOG = "$p.fr.tsv"; BS_FILE_LOG = "$p.fl.tsv" }
    if (-not $NoTrace) { $e.BS_PAD_TRACE = "$p.trace.tsv" }
    if ($Probe) { $e.BS_PAD_CONTRATO = "$p.ct.tsv" }
    foreach ($k in $envs.Keys) { $e[$k] = $envs[$k] }
    [void]$script:jobs.Add(@{ Tag = $tag; Exe = $exe; N = $n; Env = $e; Tempo = $tempo; P = $p; Ctl = $ctl; Saves = $saves; Video = $video; Audio = $audio })
    $script:meta[$tag] = @{ Script = $script; N = $n; Env = $e }
}
$procs = @{}; $meta = @{}
function RunAll() {
    $running = @()
    foreach ($j in $script:jobs) {
        while (@($running | Where-Object { -not $_.HasExited }).Count -ge $Paralelo) { Start-Sleep -Milliseconds 500 }
        $cmd = ""
        foreach ($k in $j.Env.Keys) { $cmd += "set `"$k=$($j.Env[$k])`"&& " }
        $cmd += "`"$($j.Exe)`" --data `"$Data`" --tempo $($j.Tempo)$(if ($j.Ctl) { " --controles $($j.Ctl)" })$(if ($j.Saves) { " --saves $($j.Saves)" })$(if ($j.Video) { " --video $($j.Video)" })$(if ($j.Audio) { " --audio $($j.Audio)" }) --headless $($j.N) > `"$($j.P).out`" 2>&1"
        $proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c $cmd" -WindowStyle Hidden -PassThru
        $null = $proc.Handle
        $script:procs[$j.Tag] = $proc
        $running += $proc
    }
    $running | Wait-Process -Timeout 7200
    $script:jobs.Clear()
}
function Ct($tag) {
    $h = @{}; $f = Join-Path $Saida "$tag.ct.tsv"
    if (Test-Path $f) { foreach ($l in Get-Content $f) { $c = $l.Split("`t", 2); if ($c.Count -eq 2 -and -not $h.ContainsKey($c[0])) { $h[$c[0]] = $c[1] } } }
    return $h
}
function Out($tag) { $f = Join-Path $Saida "$tag.out"; if (Test-Path $f) { Get-Content $f -Raw } else { "" } }
function Py([string[]]$a) { $o = python $tool @a 2>&1 | Out-String; return @{ Code = $LASTEXITCODE; Out = $o.Trim() } }
# maior valor da coluna $col nas linhas de dados (as que comecam por digito; o cabecalho fica de fora); -1 = sem dados.
# $col -1: so a presenca de dados (0)
function MaxCol($f, [int]$col) {  # [int]: um -1 literal na chamada chega como texto "-1"
    if (-not (Test-Path -LiteralPath $f)) { return -1 }
    $max = -1
    foreach ($l in [IO.File]::ReadLines($f)) {
        if ($l.Length -eq 0 -or -not [char]::IsDigit($l[0])) { continue }
        if ($col -lt 0) { return 0 }
        $c = $l.Split("`t"); if ($c.Count -gt $col -and $c[$col] -match "^\d+$" -and [int64]$c[$col] -gt $max) { $max = [int64]$c[$col] }
    }
    return $max
}
function Same($a, $b) {
    $pa = Join-Path $Saida $a; $pb = Join-Path $Saida $b
    # ausente ou sem linhas de dados (vazio, so o cabecalho) nunca e igual
    if ((MaxCol $pa -1) -lt 0 -or (MaxCol $pb -1) -lt 0) { return $false }
    (Get-FileHash $pa).Hash -eq (Get-FileHash $pb).Hash
}
# Execucao completa (2026-10-25, N8.0): rc 0, o orcamento de VBlanks inteiro, sem falta de progresso/deadlock, todo
# log do job com dados, e quadros, traco e estado no ultimo indice do roteiro (passo de jogo, ou leitura do controle no
# roteiro por leitura). Uma comparacao de igualdade so vale com os dois lados completos: dois tracos truncados ou
# vazios iguais nao aprovam (revisao Codex 2026-10-25).
function Completa($tag) {
    $m = $script:meta[$tag]; $pr = $script:procs[$tag]
    if (-not $m -or -not $pr) { return "$tag sem execucao" }
    if ($pr.ExitCode -ne 0) { return "$tag rc=$($pr.ExitCode)" }
    $o = Out $tag
    if ($o -notmatch "VBlanks: $($m.N);") { return "$tag nao chegou a $($m.N) VBlanks" }
    # vivacidade: uma linha "erro:" (sh4_fatal; no experimental a guarda de falta de progresso e fatal e sai assim),
    # um deadlock contado ou a interrupcao nunca reconhecida. No --tempo nativo, "[progresso] falta de progresso" e
    # "faltas de progresso N" sao o relatorio do modo sombra (diagnostico, a execucao segue): durante o filme nativo ele
    # conta 1-2 janelas por ciclos (tambem no test_saves e no n5_e001 aprovados); no experimental o mesmo filme da 0
    $erros = @([regex]::Matches($o, "(?m)^erro:.*$") | ForEach-Object { $_.Value })
    if ($erros) { return "$tag $($erros[0].Trim())" }
    if ($o -match "deadlock [1-9]|interrupcao nunca reconhecida") { return "$tag guarda de vivacidade" }
    if (-not (Test-Path -LiteralPath $m.Script -PathType Leaf)) { return "$tag sem o roteiro $($m.Script)" }  # sem ele, nada a alcancar
    $cab = ""; $ult = 0
    foreach ($l in [IO.File]::ReadLines($m.Script)) {
        $t = $l.Split("#")[0].Trim(); if (-not $t) { continue }
        if ($t -match "^indice") { $cab = $t; continue }
        if ($t -match "^(\d+)\s") { $ult = [int64]$Matches[1] }
    }
    # todo log ligado no job tem linhas de dados; quadros, traco e estado chegam ao ultimo indice do roteiro (por passo:
    # quadros col. 3, traco e estado col. 0; por leitura: quadros e traco col. 2, o estado e por passo e so precisa de
    # dados). Indice por transicao injetada: sem coluna para alcancar, so os dados
    $passo = $cab -eq "indice passo"; $trans = $cab -match "transicao"
    foreach ($lg in @(@("BS_FRAME_LOG", $(if ($passo) { 3 } else { 2 }), "quadros"), @("BS_PAD_TRACE", $(if ($passo) { 0 } else { 2 }), "traco"),
                      @("BS_STATE_PASSO", $(if ($passo) { 0 } else { -1 }), "estado"), @("BS_FILE_LOG", -1, "leituras de arquivo"))) {
        $f = $m.Env[$lg[0]]; if (-not $f) { continue }
        $col = if ($trans) { -1 } else { $lg[1] }
        $max = MaxCol $f $col
        if ($max -lt 0) { return "$tag sem dados no log de $($lg[2])" }
        if ($col -ge 0 -and $max -lt $ult) { return "$tag parou em $max de $ult ($($lg[2]), $(if ($passo) { 'passos' } else { 'leituras' }))" }
    }
    return ""
}
function Completas([string[]]$tags) { @($tags | ForEach-Object { Completa $_ } | Where-Object { $_ }) }
$S = $Saida
$assistidos = @("passo_cgate", "passo_heliporto", "passo_combate", "passo_load_opcoes")

if ($want.K1) { Job "k1" $ExeMods "$S\pad_matriz.txt" 5200 -Probe; Job "k1b" $ExeMods "$S\pad_matriz2.txt" 1700 @{ BS_MAPLE_FUNCAO = "FE063F00" } -Probe }
if ($want.K2) { for ($x = 0; $x -lt 64; $x++) { Job ("k2_{0:D2}" -f $x) $ExeMods "$S\pad_suporte.txt" 700 @{ BS_MAPLE_FUNCAO = ("FE06{0:X2}00" -f $x) } -NoTrace -Probe } }
if ($want.K3) { Job "k3" $ExeMods "$S\pad_suporte.txt" 700 @{ BS_PAD_MODO = "1" } -NoTrace -Probe }
if ($want.K4) {
    foreach ($m in "troca_ab", "gatilho127", "info_sem_conectado") { Job "k4_$m" $ExeMods "$S\pad_suporte.txt" 700 @{ BS_PAD_CONTRATO_MUTACAO = $m } -NoTrace -Probe }
    Job "k4_old_nativo" $ExeMods "$S\pad_suporte.txt" 700 @{ BS_PAD_CONTRATO_MUTACAO = "old_nativo"; BS_MAPLE_FUNCAO = "FE060000" } -NoTrace -Probe
}
$nLim = VBlanksFor "$S\passo_limiares.txt"; $nCon = VBlanksFor "$S\passo_conexao.txt"
if ($want.K5 -or $want.K6 -or $want.K10) { Job "k6" $ExeMods "$S\passo_limiares.txt" $nLim -Probe }
if ($want.K5) { Job "k5" $Exe "$S\passo_limiares.txt" $nLim; Job "k5b" $Exe "$S\passo_limiares.txt" $nLim; Job "k6b" $ExeMods "$S\passo_limiares.txt" $nLim }
if ($want.K7 -or $want.K10) { Job "k7" $ExeMods "$S\passo_conexao.txt" $nCon -Probe }
if ($want.K8) { Job "k8" $ExeMods "$S\pad_conexao_boot.txt" 1500 -Probe }
if ($want.K9) { Job "k9" $ExeMods "$S\pad_softreset.txt" 1500; Job "k9c" $ExeMods "$S\pad_softreset_controle.txt" 600 }
if ($want.K10) {
    Job "k10_ab" $Exe "$S\passo_conexao.txt" $nCon @{ BS_PAD_MUTACAO = "troca_ab" }
    Job "k10_passo" $Exe "$S\passo_limiares.txt" $nLim @{ BS_PAD_MUTACAO = "passo_adiantado" }
    Job "k10_vb" $Exe "$S\pad_transicao.txt" 2500
    Job "k10_vbm" $Exe "$S\pad_transicao.txt" 2500 @{ BS_PAD_MUTACAO = "amostra_vblank_in" }
    Job "k10_vbpasso" $Exe "$S\passo_limiares.txt" $nLim @{ BS_PAD_MUTACAO = "amostra_vblank_in" }
}
if ($want.K11) {
    foreach ($r in $assistidos) {
        $n = VBlanksFor "$rot\$r.txt"
        Job "k11_$r" $ExeMods "$rot\$r.txt" $n @{ BS_STATE_PASSO = "$S\k11_$r.st.tsv"; BS_STATE_PASSO_CADA = "1" } -Probe
        Job "k11m_$r" $ExeMods "$rot\$r.txt" $n @{ BS_PAD_CONTRATO_MUTACAO = "campos_nao_usados"; BS_STATE_PASSO = "$S\k11m_$r.st.tsv"; BS_STATE_PASSO_CADA = "1" } -Probe
    }
}
if ($want.K12) {
    # saves legado: os roteiros por passo foram convertidos de execucoes sem cartao (o START do passo 95
    # fecha o "VMU not found"; com o cartao ele nao tem efeito e os quadros ficam iguais)
    # audio legado: os roteiros por passo foram convertidos de execucoes com o audio legado (padrao de entao); com o
    # nativo o mesmo START cai 2 VBlanks antes no passo 93 (A/B do N8.0, 2026-10-25, aprovado pelo mantenedor)
    Job "k12_o_area1" $Exe "$rot\newgame_area1.txt" 7300 -saves "legado" -audio "legado"; Job "k12_c_area1" $Exe "$rot\passo_newgame_area1.txt" 7300 -saves "legado" -audio "legado"
    Job "k12_o_expl" $Exe "$rot\newgame_exploracao.txt" 16000 -saves "legado" -audio "legado"; Job "k12_c_expl" $Exe "$rot\passo_newgame_exploracao.txt" 16000 -saves "legado" -audio "legado"
}
if ($want.K13) {
    if (-not (Test-Path -LiteralPath $ExeRef)) { Write-Host "ERRO: K13 exige o executavel de referencia (-ExeRef $ExeRef)"; exit 2 }
    $ExeRef = (Resolve-Path $ExeRef).Path
    foreach ($r in $RotasK13) {
        $n = if ($r -eq "newgame_area1") { 7300 } else { VBlanksFor "$rot\$r.txt" }
        # saves legado: a referencia e anterior ao N4 (sem cartao: "VMU not found"); o K13 confere os controles.
        # A referencia e anterior ao N5 e ao N6: sem --video (so tem o Sofdec) e com o audio legado de entao; o lado
        # novo fixa --video legado e --audio legado para comparar o mesmo cenario (prova de 2026-10-25: com o audio
        # legado, quadros e leituras identicos aos da referencia; com o nativo, diferem). Mesmo roteiro, --tempo e relogio.
        Job "k13_novo_$r" $Exe "$rot\$r.txt" $n -NoTrace -ctl "" -saves "legado" -audio "legado"
        Job "k13_ref_$r" $ExeRef "$rot\$r.txt" $n -NoTrace -ctl "" -video ""
    }
    $r0 = $RotasK13[0]  # traco ligado: na primeira rota
    Job "k13_traco" $Exe "$rot\$r0.txt" $(if ($r0 -eq "newgame_area1") { 7300 } else { VBlanksFor "$rot\$r0.txt" }) -ctl "" -saves "legado" -audio "legado"
}
RunAll

if ($want.K1) {
    $c = Ct "k1"
    Check "K1 contrato x legado (matriz)" ($c.divergencias -eq "0" -and [int]$c.chamadas -gt 2000 -and $c.teclado -eq "0" -and $c.sem_condicao -eq "0") "chamadas $($c.chamadas), divergencias $($c.divergencias)"
    Check "K1 cobertura: 256 valores de R, L, x1, y1" (($c.valores_gatilho_r, $c.valores_gatilho_l, $c.valores_x1, $c.valores_y1 | Where-Object { $_ -ne "256" }).Count -eq 0) "$($c.valores_gatilho_r)/$($c.valores_gatilho_l)/$($c.valores_x1)/$($c.valores_y1)"
    Check "K1 cobertura: 12 botoes, 66 pares, gatilho com botao, bordas" ($c.botoes -eq "12" -and $c.pares -eq "66" -and [int]$c.gatilho_com_botao -gt 0 -and [int]$c.press -gt 0 -and [int]$c.release -gt 0) "botoes $($c.botoes) ($($c.botoes_mascara)), pares $($c.pares), press $($c.press), release $($c.release)"
    $c = Ct "k1b"
    Check "K1b contrato x legado (x2, y2, old)" ($c.divergencias -eq "0" -and $c.valores_x2 -eq "256" -and $c.valores_y2 -eq "256") "divergencias $($c.divergencias), x2 $($c.valores_x2), y2 $($c.valores_y2)"
    Check "K1b cobertura dos padroes de old x on" ((($c.old_zero_on_nao, $c.old_igual_on, $c.old_diferente, $c.old_nao_on_zero) | Where-Object { [int]$_ -le 0 }).Count -eq 0) "zero/igual/diferente/on-zero: $($c.old_zero_on_nao)/$($c.old_igual_on)/$($c.old_diferente)/$($c.old_nao_on_zero)"
}
if ($want.K2) {
    $bad = 0; $sups = @{}
    for ($x = 0; $x -lt 64; $x++) {
        $c = Ct ("k2_{0:D2}" -f $x)
        if ($c.divergencias -ne "0" -or [int]$c.controle -lt 100) { $bad++ }
        foreach ($v in "$($c.support_valores)".Split(",")) { if ($v) { $sups[$v] = $true } }
    }
    $combos = @{}; foreach ($v in $sups.Keys) { $combos[(([Convert]::ToUInt32($v, 16)) -shr 16) -band 63] = $true }
    Check "K2 64 combinacoes de support: 0 divergencias" ($bad -eq 0) "execucoes com falha $bad"
    Check "K2 64 combinacoes vistas pelo SDK" ($combos.Count -eq 64) "$($combos.Count) combinacoes"
}
if ($want.K3) { $c = Ct "k3"; Check "K3 modo 1: 0 divergencias" ($c.divergencias -eq "0" -and $c.modos -eq "1" -and [int]$c.chamadas -gt 300) "chamadas $($c.chamadas), modos $($c.modos)" }
if ($want.K4) {
    foreach ($m in "troca_ab", "gatilho127", "info_sem_conectado", "old_nativo") {
        $c = Ct "k4_$m"; Check "K4 negativo do contrato acusado: $m" ([int]$c.divergencias -gt 0) "divergencias $($c.divergencias)"
    }
}
if ($want.K5) {
    $inc = Completas "k5", "k5b", "k6", "k6b"; if ($inc) { Check "K5 execucoes completas" $false ($inc -join "; ") }
    $r = Py "compara", "$S\k5.trace.tsv", "$S\k5b.trace.tsv", "--com-tempo"; Check "K5 determinismo do release (duas execucoes)" (-not $inc -and $r.Code -eq 0 -and (Same "k5.fr.tsv" "k5b.fr.tsv")) ($r.Out -split "`n")[0]
    $r = Py "compara", "$S\k6.trace.tsv", "$S\k6b.trace.tsv", "--com-tempo"; Check "K5 determinismo do build com mods (duas execucoes)" (-not $inc -and $r.Code -eq 0 -and (Same "k6.fr.tsv" "k6b.fr.tsv")) ($r.Out -split "`n")[0]
    $r = Py "compara", "$S\k5.trace.tsv", "$S\k6.trace.tsv", "--com-tempo"; Check "K5 paridade release x mods" (-not $inc -and $r.Code -eq 0 -and (Same "k5.fr.tsv" "k6.fr.tsv")) ($r.Out -split "`n")[0]
}
if ($want.K6) {
    $r = Py "limiares", "$S\k6.trace.tsv", "$S\limiares.tsv"; $c = Ct "k6"
    Check "K6 limiares do jogo (30 pontos)" ($r.Code -eq 0) (($r.Out -split "`n" | Where-Object { $_ -match "FALHA" }) -join "; ")
    Check "K6 contrato no gameplay" ($c.divergencias -eq "0") "chamadas $($c.chamadas)"
}
if ($want.K7) {
    $r = Py "conexao", "$S\k7.trace.tsv", "$S\conexao.tsv"; $c = Ct "k7"
    Check "K7 desconexao e reconexao no gameplay" ($r.Code -eq 0) (($r.Out -split "`n") -join "; ")
    Check "K7 contrato nas transicoes" ($c.divergencias -eq "0" -and [int]$c.conexoes -ge 2 -and [int]$c.desconexoes -ge 2) "conexoes $($c.conexoes), desconexoes $($c.desconexoes)"
}
if ($want.K8) { $r = Py "boot", "$S\k8.trace.tsv"; $c = Ct "k8"; Check "K8 porta vazia desde o boot" ($r.Code -eq 0 -and $c.divergencias -eq "0") (($r.Out -split "`n") -join "; ") }
if ($want.K9) { $r = Py "softreset", "$S\k9.trace.tsv", "$S\k9.out", "$S\k9c.out"; Check "K9 soft reset (referencia legada)" ($r.Code -eq 0) (($r.Out -split "`n") -join "; ") }
if ($want.K10) {
    $r = Py "compara", "$S\k7.trace.tsv", "$S\k10_ab.trace.tsv"; Check "K10 negativo acusado: A/B trocados" ($r.Code -ne 0) ($r.Out -split "`n")[0]
    $r = Py "compara", "$S\k6.trace.tsv", "$S\k10_passo.trace.tsv"; Check "K10 negativo acusado: amostra um passo adiantada" ($r.Code -ne 0) ($r.Out -split "`n")[0]
    $r = Py "compara", "$S\k10_vb.trace.tsv", "$S\k10_vbm.trace.tsv"; Check "K10 negativo acusado: amostra no VBlank-In (transicao injetada)" ($r.Code -ne 0) ($r.Out -split "`n")[0]
    $inc = Completas "k6", "k10_vbpasso"
    $r = Py "compara", "$S\k6.trace.tsv", "$S\k10_vbpasso.trace.tsv", "--com-tempo"; Check "K10 controle: VBlank-In com roteiro por passo da o mesmo traco" (-not $inc -and $r.Code -eq 0) ((@(($r.Out -split "`n")[0]) + $inc) -join "; ")
}
if ($want.K11) {
    foreach ($r in $assistidos) {
        $cmp = Py "compara", "$S\k11_$r.trace.tsv", "$S\k11m_$r.trace.tsv", "--com-tempo", "--ignora", "r,l,x2,y2,old,nome"
        $fr  = Same "k11_$r.fr.tsv" "k11m_$r.fr.tsv"
        $c0  = Ct "k11_$r"; $c1 = Ct "k11m_$r"
        $stq = Same "k11_$r.st.tsv" "k11m_$r.st.tsv"
        $inc = Completas "k11_$r", "k11m_$r"
        Check "K11 campos nao usados: $r" (-not $inc -and $cmp.Code -eq 0 -and $fr -and $stq) "quadros $(if ($fr) { 'iguais' } else { 'DIFERENTES' }), estado por passo $(if ($stq) { 'igual' } else { 'DIFERENTE' }); $(($cmp.Out -split "`n")[0])$(if ($inc) { '; INCOMPLETA: ' + ($inc -join '; ') })"
        Check "K11 contrato na rota: $r" ($c0.divergencias -eq "0" -and $c1.divergencias -eq "0" -and $c0.teclado -eq "0" -and (Out "k11_$r") -notmatch "erro:") "chamadas $($c0.chamadas)"
    }
}
if ($want.K12) {
    foreach ($k in "area1", "expl") {
        $cmp = Py "compara", "$S\k12_o_$k.trace.tsv", "$S\k12_c_$k.trace.tsv", "--com-tempo"
        $inc = Completas "k12_o_$k", "k12_c_$k"
        Check "K12 roteiro convertido igual ao original: $k" (-not $inc -and $cmp.Code -eq 0 -and (Same "k12_o_$k.fr.tsv" "k12_c_$k.fr.tsv")) ((@(($cmp.Out -split "`n")[0]) + $inc) -join "; ")
    }
}
if ($want.K13) {
    $inc = Completas "k13_novo_$($RotasK13[0])", "k13_traco"
    Check "K13 traco ligado nao muda os quadros" (-not $inc -and (Same "k13_novo_$($RotasK13[0]).fr.tsv" "k13_traco.fr.tsv")) ((@($RotasK13[0]) + $inc) -join "; ")
    foreach ($r in $RotasK13) {
        $inc = Completas "k13_novo_$r", "k13_ref_$r"
        Check "K13 padrao inalterado (quadros e leituras = referencia): $r" (-not $inc -and (Same "k13_novo_$r.fr.tsv" "k13_ref_$r.fr.tsv") -and (Same "k13_novo_$r.fl.tsv" "k13_ref_$r.fl.tsv")) ((@($ExeRef) + $inc) -join "; ")
    }
}
$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("test_controles: {0} casos, {1} falhas" -f $results.Count, $fail)
exit $(if ($fail) { 1 } else { 0 })
