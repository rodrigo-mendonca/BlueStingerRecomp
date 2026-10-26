# N2b-2 (--tempo experimental): criterios e negativos (docs/N2b2-RELATORIO.md). O modo
# experimental tira os ciclos estimados do tempo: o instante logico so anda nos pontos de espera
# declarados, ate o proximo evento. O estavel (--tempo nativo) continua o padrao e nao pode mudar.
# Roteiros NEW GAME (relogio fixo), com o estavel como referencia.
#   N1  progresso: o experimental completa a exploracao (16 000 VBlanks) sem erro, com a thread 3
#       bloqueando nas passadas ociosas
#   N2  leituras: a mesma sequencia (arquivo, deslocamento, tamanho, SHA-256) que o estavel
#   N3  quadros: a mesma sequencia de quadros distintos, do boot ate o primeiro botao do gameplay
#       (titulo, menus, os dois videos, as cargas); so os quadros intermediarios do boot ficam de fora
#   N4  audio: a mesma sequencia de escritas da CPU na AICA ate o gameplay, sem as escritas de
#       espera do stream (8 palavras zeradas por VBlank enquanto o video enche o buffer)
#   N5  eventos: os padroes por VBlank (interrupcoes, trocas de thread, longjmp) do experimental
#       existem no estavel (>= 99% dos VBlanks); a thread e identificada pela funcao de entrada
#   N6  provedores de arquivos (B4, D9a): no experimental, legado = nativo VBlank a VBlank
#       (quadros, leituras, audio) ate o gameplay
#   N7  timeout (B5): render que nunca termina sai pelo timeout de 166 ms (TMU = instante logico)
#   N8  liveness (B7), negativos: espera do VBlank nao declarada, thread do CRI que nunca bloqueia e
#       espera sincrona que da volta viram erro com thread, PC e espera; o estavel com a mesma
#       mutacao do hardware roda (controle)
#   N9  detector da thread 3: bloquear depois de toda passada muda leituras ou quadros (o detector
#       e necessario). Ignorar so a RAM: informado (nao sensivel na rota, ver o relatorio)
#   N10 determinismo (B8): duas execucoes experimentais identicas
#   N11 estavel inalterado: --tempo nativo do executavel novo = executavel anterior ao N2b-2, byte a
#       byte (trace com ciclos, leituras, relogio, estado, tempo, quadro). Sem -ExeAntigo: SKIP
#   N12 desempenho (B9): trabalho medio por campo do experimental <= estavel x 1,05 (3 rodadas
#       alternadas, sequenciais)
#   N13 mods (B10): o build com mods no experimental = build sem mods (leituras e quadros)
#   Audio: o padrao (nativo) em tudo, menos o N3b (duplicatas est_ex_al/exp_exd_al) e o lado novo do N11, com o audio
#   legado (A/B do N8.0, 2026-10-25, aprovado pelo mantenedor; ver Caso)
#   powershell -File tools/dist/test_n2b2.ps1 [-Exe <exe>] [-ExeMods <exe>] [-ExeAntigo <exe>] [-Data <data>]
# video legado: estabelecida com o Sofdec original; o filme nativo (N5) dura o tempo real do filme e
# desloca os artefatos por VBlank depois de cada filme (a logica por passo e a mesma).
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\app\bluestinger.exe"),
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-mods\app\bluestinger.exe"),
    # N8.3: --arquivos legado (modelo HLE do GD-ROM) so existe no build de validacao (caso exp_lg do N6)
    [string]$ExeValidacao = (Join-Path $PSScriptRoot "..\..\build\msvc-validacao\app\bluestinger.exe"),
    [string]$ExeAntigo = (Join-Path $PSScriptRoot "..\..\analysis\rollback-antes-n2b2\exe\bluestinger-release.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$VBlanksEx = 16000,
    [int]$VBlanks = 8000,
    [int]$VBlanksNeg = 3000,
    [int]$LimiteSegundos = 1800,
    [string]$Saida = (Join-Path $env:TEMP "bs-n2b2-teste")
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "SKIP: executavel ausente ($Exe)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$marker = Join-Path $Saida ".bs-n2b2-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

$results = @()
$skips = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}

# Execucoes em paralelo, cada uma com o proprio ambiente. Caso: tag, exe, tempo, vblanks, roteiro,
# arquivos, variaveis extras, artefatos (ev = trace de ordem e de tempo, relogio e estado).
function Launch($c) {
    $p   = Join-Path $Saida $c.tag
    $all = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\$($c.roteiro).txt")
              BS_FILE_LOG = "${p}_fl.tsv"; BS_FRAME_LOG = "${p}_fr.tsv"; BS_AICA_LOG = "${p}_au.tsv" }
    if ($c.ev) { $all.BS_EVENT_TRACE = "${p}_ev.tsv"; $all.BS_CLOCK_LOG = "${p}_cl.tsv"; $all.BS_STATE_LOG = "${p}_sl.tsv"; $all.BS_TIME_TRACE = "${p}_tt.tsv" }
    if ($c.audioAll) { $all.BS_AICA_LOG_TUDO = "1" }
    foreach ($k in $c.env.Keys) { $all[$k] = $c.env[$k] }
    foreach ($k in $all.Keys) { Set-Item "env:$k" $all[$k] }
    $args2 = "--data `"$Data`" --tempo $($c.tempo) --arquivos $($c.arquivos) --headless $($c.n)"
    if ($c.ev) { $args2 += " --dump `"${p}_q.bmp`"" }
    if ($c.controles) { $args2 += " --controles $($c.controles)" }
    if ($c.saves) { $args2 += " --saves $($c.saves)" }
    if ($c.saves -eq "legado") { $args2 += " --video legado" }  # escopo historico: Sofdec original (N5)
    if ($c.audio) { $args2 += " --audio $($c.audio)" }
    try {
        $proc = Start-Process -FilePath "cmd.exe" -PassThru -WindowStyle Hidden -ArgumentList "/c `"`"$($c.exe)`" $args2 > `"${p}_saida.txt`" 2>&1`""
        $null = $proc.Handle
    } finally { foreach ($k in $all.Keys) { Set-Item "env:$k" "" } }
    return @{ C = $c; Proc = $proc; P = $p }
}
function RunBatch($casos) {
    $runs = @{}
    $started = foreach ($c in $casos) { Launch $c }
    foreach ($s in $started) {
        if (-not $s.Proc.WaitForExit($LimiteSegundos * 1000)) { & taskkill /PID $s.Proc.Id /T /F 2>&1 | Out-Null; $code = "tempo-limite" }
        else { $code = $s.Proc.ExitCode }
        $log = "$($s.P)_saida.txt"
        $runs[$s.C.tag] = @{ P = $s.P; Code = $code; Out = $(if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { "" }) }
    }
    return $runs
}
# saves legado em todos os casos: a suite (N3/N3b quadros entre modos, N8b numero da thread do CRI) foi
# estabelecida no caminho sem cartao; com o cartao nativo o codigo de cartao do jogo depende do tempo
# (STATUS.md, fechamento do N4). Reavaliar no retorno ao N2.
# $audio: "" = o padrao (nativo); "legado" so nas execucoes do N3b e do N11 (A/B do N8.0, 2026-10-25, aprovado pelo
# mantenedor): o alinhamento fixo do N3b foi medido com o audio legado e o N11 compara com um executavel anterior ao N6.
function Caso($tag, $exe, $tempo, $n, $roteiro, [hashtable]$env = @{}, [switch]$ev, [switch]$audioAll, $arquivos = "nativo", $controles = "", $saves = "legado", $audio = "") {
    return @{ tag = $tag; exe = $exe; tempo = $tempo; n = $n; roteiro = $roteiro; env = $env; ev = [bool]$ev; audioAll = [bool]$audioAll; arquivos = $arquivos; controles = $controles; saves = $saves; audio = $audio }
}

function ReadLines($f) { if (Test-Path -LiteralPath $f) { [IO.File]::ReadAllLines($f) } else { @() } }
# completa: rc 0, o orcamento inteiro e nenhuma linha "erro:" (o rc, revisao Codex 2026-10-25)
function Ended($r, $n) { return $r.Code -eq 0 -and $r.Out -match "VBlanks: $n;" -and $r.Out -notmatch "(?m)^erro:" }
function Err($r) { $m = [regex]::Match($r.Out, "(?m)^erro: .*$"); if ($m.Success) { $m.Value } else { "" } }
# leituras sem o VBlank (arquivo, deslocamento, tamanho, SHA-256)
function Reads($r) { ReadLines "$($r.P)_fl.tsv" | Select-Object -Skip 1 | ForEach-Object { ($_.Split("`t") | Select-Object -Skip 2) -join " " } }
# quadros distintos: (VBlank da troca, hash)
function Frames($r) {
    $prev = ""; $out = New-Object System.Collections.Generic.List[object]
    foreach ($l in (ReadLines "$($r.P)_fr.tsv" | Select-Object -Skip 1)) {
        $f = $l.Split("`t")
        if ($f[1] -ne $prev) { $out.Add(@([int64]$f[0], $f[1])); $prev = $f[1] }
    }
    return , $out
}
# escritas na AICA, com o VBlank (so com BS_AICA_LOG_TUDO), sem as copias de guarda do anel do
# stream de video: 8 palavras depois do fim de cada canal (009FBF80, 009FFFC0) que repetem o inicio
# do anel e sao regravadas a cada VBlank enquanto o video enche o buffer. Fica o preenchimento do
# boot (41474553), que e o mesmo nos dois modos.
function AudioWrites($r) {
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($l in [IO.File]::ReadLines("$($r.P)_au.tsv")) {
        if (-not $l.StartsWith("w")) { continue }
        $f = $l.Split("`t")
        $a = [Convert]::ToUInt32($f[1], 16)
        if ($f[2] -ne "41474553" -and (($a -ge 0x009FFFC0 -and $a -le 0x009FFFDC) -or ($a -ge 0x009FBF80 -and $a -le 0x009FBF9C))) { continue }
        $out.Add(@([int64]$f[4], "$($f[1]) $($f[2]) $($f[3])"))
    }
    return , $out
}
# padroes de eventos por VBlank (sem ciclos nem PC interrompido). A thread e identificada pela funcao de entrada (do
# evento N), nao pelo indice: o indice depende da ordem de criacao, que muda entre os modos com o audio nativo (no
# experimental a thread do CRI, 8C158840, nasce primeiro; A/B do N8.0). A thread 0 (principal) nao tem evento N.
function Signatures($r) {
    $cur = ""; $out = New-Object System.Collections.Generic.List[string]
    $ent = @{ "00000000" = "principal" }
    $id = { param($i) if ($ent.ContainsKey($i)) { $ent[$i] } else { "t$i" } }
    foreach ($l in [IO.File]::ReadLines("$($r.P)_ev.tsv")) {
        if ($l -notmatch '^\d') { continue }
        $f = $l.Split("`t")
        switch ($f[2]) {
            "V" { $out.Add($cur); $cur = "" }
            "I" { $cur += " I$($f[3])" }
            "N" { $ent[$f[3]] = $f[4]; $cur += " N$($f[4])" }
            "T" { $cur += " T$(& $id $f[3])-$(& $id $f[4])" }
            "J" { $cur += " J$($f[3])-$(& $id $f[4])" }
            default { $cur += " $($f[2])$($f[3])-$($f[4])" }
        }
    }
    $out.Add($cur); return , $out
}
# indice da thread criada com a funcao de entrada $entry (evento N do trace), ou "" se nao ha
function ThreadOf($evFile, $entry) {
    foreach ($l in (ReadLines $evFile)) { $f = $l.Split("`t"); if ($f.Count -ge 5 -and $f[2] -eq "N" -and $f[4] -eq $entry) { return [string][Convert]::ToInt32($f[3], 16) } }
    return ""
}
function Hash($f) { if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f).Hash } else { "ausente" } }

$hasMods = Test-Path -LiteralPath $ExeMods
$hasOld  = Test-Path -LiteralPath $ExeAntigo
$kGameplay = 5600  # primeiro botao do gameplay nos roteiros (VBlank do estavel)

# ---- lote 1: rotas longas, determinismo, provedores ----
$lote1 = @(
    (Caso "est_ex" $Exe "nativo" $VBlanksEx "newgame_exploracao" -ev -audioAll),
    (Caso "exp_ex" $Exe "experimental" $VBlanksEx "newgame_exploracao" -ev -audioAll),
    (Caso "exp_ex2" $Exe "experimental" $VBlanksEx "newgame_exploracao" -ev),
    (Caso "exp_exd" $Exe "experimental" $VBlanksEx "newgame_exploracao_n2b2" -audioAll),
    (Caso "exp_lg" $ExeValidacao "experimental" $VBlanks "newgame_area1" -audioAll -arquivos "legado"),
    (Caso "exp_a1" $Exe "experimental" $VBlanks "newgame_area1" -audioAll),
    (Caso "est_novo" $Exe "nativo" $VBlanks "newgame_area1" -ev -audioAll),
    # N3b com o audio legado (duplicatas de est_ex e exp_exd; as originais seguem no nativo para N2-N5)
    (Caso "est_ex_al" $Exe "nativo" $VBlanksEx "newgame_exploracao" -audio "legado"),
    (Caso "exp_exd_al" $Exe "experimental" $VBlanksEx "newgame_exploracao_n2b2" -audio "legado")
)
# (o executavel antigo nao tem --saves: e sem cartao por construcao, como o lado novo com --saves legado)
# N11 compara com ciclos contra o executavel anterior ao N2b-2 (Maple com controle): o lado novo usa o
# provedor legado de controles, porque no nativo o DMA do Maple termina ~9 600 ciclos antes (N3-2), e o audio legado,
# porque o antigo e anterior ao N6 (so tem o audio legado).
if ($hasOld) { $lote1 += (Caso "est_antigo" $ExeAntigo "nativo" $VBlanks "newgame_area1" -ev -saves ""), (Caso "est_n11" $Exe "nativo" $VBlanks "newgame_area1" -ev -controles "legado" -saves "legado" -audio "legado") }
if ($hasMods) { $lote1 += (Caso "exp_mods" $ExeMods "experimental" $VBlanks "newgame_area1") }
$r = RunBatch $lote1

# ---- lote 2: negativos ----
$lote2 = @(
    (Caso "neg_sem_espera" $Exe "experimental" $VBlanksNeg "newgame_area1" @{ BS_TEMPO_MUTACAO = "sem_espera" }),
    (Caso "neg_cri_nunca" $Exe "experimental" $VBlanksNeg "newgame_area1" @{ BS_TEMPO_MUTACAO = "cri_nunca_ocioso"; BS_EVENT_TRACE = (Join-Path $Saida "neg_cri_nunca_ev.tsv") }),
    (Caso "neg_ffst_exp" $Exe "experimental" $VBlanksNeg "newgame_area1" @{ BS_SB_MUTACAO = "ffst_ocupado" }),
    (Caso "neg_ffst_est" $Exe "nativo" $VBlanksNeg "newgame_area1" @{ BS_SB_MUTACAO = "ffst_ocupado" }),
    (Caso "neg_cri_sempre" $Exe "experimental" $VBlanks "newgame_area1" @{ BS_TEMPO_MUTACAO = "cri_sempre_ocioso" }),
    (Caso "neg_cri_ram" $Exe "experimental" $VBlanks "newgame_area1" @{ BS_TEMPO_MUTACAO = "cri_ignora_ram" }),
    (Caso "neg_render" $Exe "experimental" $VBlanks "newgame_area1" @{ BS_TEMPO_MUTACAO = "render_perdido" })
)
$r2 = RunBatch $lote2
foreach ($k in $r2.Keys) { $r[$k] = $r2[$k] }

# ---- N1: progresso ----
$blocks = [regex]::Match($r.exp_ex.Out, "ociosas \(bloqueios\) (\d+)")
$adv    = [regex]::Match($r.exp_ex.Out, "avancos (\d+) \(linha do SPG (\d+)\)")
Check "N1 progresso: exploracao completa no experimental, thread 3 bloqueando" ((Ended $r.exp_ex $VBlanksEx) -and $blocks.Success -and [int64]$blocks.Groups[1].Value -gt 0 -and $adv.Success) `
    "$(Err $r.exp_ex) bloqueios da thread 3: $($blocks.Groups[1].Value); avancos do tempo: $($adv.Groups[1].Value) (linha do SPG $($adv.Groups[2].Value))"

# ---- N2: leituras ----
$re = Reads $r.est_ex; $rx = Reads $r.exp_ex
Check "N2 leituras: mesma sequencia (arquivo, deslocamento, tamanho, SHA-256)" ($re.Count -gt 0 -and (($re -join "`n") -eq ($rx -join "`n"))) "estavel $($re.Count), experimental $($rx.Count)"

# ---- N3: quadros ate o gameplay (mesmo roteiro) ----
# Excecao documentada (relatorio 4.4): o estavel mostra UM quadro intermediario do boot (estado
# parcial durante as cargas, entre V4 e V13) que o experimental nao chega a mostrar.
$kBootFrame = "F1211C1A34398D17"
$fe = Frames $r.est_ex; $fx = Frames $r.exp_ex
$fe2 = New-Object System.Collections.Generic.List[object]
$bootOnly = @()
foreach ($q in $fe) { if ($q[0] -lt 40 -and $q[1] -eq $kBootFrame) { $bootOnly += "V$($q[0])" } else { $fe2.Add($q) } }
# no log bruto do estavel, o quadro de boot ocupa exatamente V4-V13, entre o primeiro quadro (V1-V3)
# e o do titulo da Sega (V14), que sao os dois primeiros do experimental
$raw = @{}; foreach ($l in (ReadLines "$($r.est_ex.P)_fr.tsv" | Select-Object -Skip 1)) { $f = $l.Split("`t"); $raw[[int64]$f[0]] = $f[1] }
$bootVbs = @($raw.Keys | Where-Object { $raw[$_] -eq $kBootFrame } | Sort-Object)
$bootOk = ($bootVbs -join ",") -eq "4,5,6,7,8,9,10,11,12,13" -and $raw[[int64]3] -eq $fx[0][1] -and $raw[[int64]14] -eq $fx[1][1]
$k = 0
while ($k -lt $fe2.Count -and $k -lt $fx.Count -and $fe2[$k][1] -eq $fx[$k][1]) { $k++ }
$stopVb = if ($k -lt $fe2.Count) { $fe2[$k][0] } else { [int64]::MaxValue }
Check "N3 quadros: mesma sequencia do boot ao gameplay (titulo, menus, videos, cargas)" `
    ((Ended $r.est_ex $VBlanksEx) -and (Ended $r.exp_ex $VBlanksEx) -and $bootOnly.Count -eq 1 -and $bootOk -and $k -gt 2000 -and $stopVb -ge $kGameplay) `
    "trocas iguais: $k (ate V$stopVb do estavel; gameplay a partir de V$kGameplay); quadro so do boot no estavel: V$($bootVbs -join ',')"

# ---- N3b: gameplay com as entradas alinhadas por passo de jogo ----
# O indice do roteiro e o numero de leituras do controle (coluna "entrada" do BS_FRAME_LOG); no mesmo
# passo de jogo da area 1 o estavel fez 26 leituras a mais (cargas mais longas). Com
# newgame_exploracao_n2b2.txt (indices do gameplay -26), cada troca de quadro e comparada no mesmo
# ponto logico (leitura alinhada = leitura do estavel - 26 = leitura do experimental), da entrada na
# area 1 ate o ultimo ponto logico comum as duas execucoes. A unica excecao e o menu de itens, entre
# o START que o abre (leitura 5974) e o que o fecha (6174): ele carrega modelos, e no estavel a carga
# dura VBlanks de CPU (D11).
$kAlign = 26; $kFrom = 4674; $kMenuOpen = 5974; $kMenuClose = 6174
function FramesSeq($r, $shift) {
    $prev = ""; $out = New-Object System.Collections.Generic.List[object]
    foreach ($l in (ReadLines "$($r.P)_fr.tsv" | Select-Object -Skip 1)) {
        $f = $l.Split("`t")
        if ($f[1] -ne $prev) { $out.Add(@(([int64]$f[2] - $shift), [int64]$f[0], $f[1])); $prev = $f[1] }
    }
    return , $out
}
# O alinhamento de 26 leituras foi medido com o audio legado; com o nativo as cargas D11 do estavel duram outro tempo
# (25 ate a leitura 5780, 27 depois; A/B do N8.0). O caso roda com o audio legado (est_ex_al, exp_exd_al); o par com o
# audio nativo (est_ex, exp_exd) fica informado, e o N3c (mesmas leituras) segue nele.
function AlignedFrames($est, $exp) {
    $se1 = FramesSeq $est $kAlign; $sx1 = FramesSeq $exp 0
    $ge = @($se1 | Where-Object { $_[0] -ge $kFrom }); $gx = @($sx1 | Where-Object { $_[0] -ge $kFrom })
    $hi = [math]::Min(($ge | ForEach-Object { $_[0] } | Measure-Object -Maximum).Maximum, ($gx | ForEach-Object { $_[0] } | Measure-Object -Maximum).Maximum)
    $outE = @($ge | Where-Object { $_[0] -le $hi -and ($_[0] -lt $kMenuOpen -or $_[0] -gt $kMenuClose) } | ForEach-Object { "$($_[0]) $($_[2])" })
    $outX = @($gx | Where-Object { $_[0] -le $hi -and ($_[0] -lt $kMenuOpen -or $_[0] -gt $kMenuClose) } | ForEach-Object { "$($_[0]) $($_[2])" })
    $inE = @($ge | Where-Object { $_[0] -ge $kMenuOpen -and $_[0] -le $kMenuClose }).Count
    $inX = @($gx | Where-Object { $_[0] -ge $kMenuOpen -and $_[0] -le $kMenuClose }).Count
    return @{ Ok = ((Ended $est $VBlanksEx) -and (Ended $exp $VBlanksEx) -and $outE.Count -gt 4000 -and (($outE -join "`n") -eq ($outX -join "`n")))
              Txt = "trocas fora do menu: $($outE.Count) (leituras alinhadas $kFrom-$hi); no menu ($kMenuOpen-$kMenuClose): estavel $inE, experimental $inX" }
}
$n3b = AlignedFrames $r.est_ex_al $r.exp_exd_al
Check "N3b gameplay com entradas alinhadas (audio legado): quadros iguais em cada ponto logico, fora o menu de itens" $n3b.Ok $n3b.Txt
$n3bn = AlignedFrames $r.est_ex $r.exp_exd
Write-Host "[info] N3b com o audio nativo (alinhamento fixo medido no legado): $(if ($n3bn.Ok) { 'iguais' } else { 'diferem' }); $($n3bn.Txt)"
$re2 = Reads $r.exp_exd
Check "N3c gameplay com entradas alinhadas: mesmas leituras" ((Ended $r.exp_exd $VBlanksEx) -and $re2.Count -gt 0 -and (($re -join "`n") -eq ($re2 -join "`n"))) "$($re2.Count) leituras"

# ---- N4: audio (rota inteira) ----
$ae = AudioWrites $r.est_ex; $ax = AudioWrites $r.exp_ex; $ad = AudioWrites $r.exp_exd
function SameWrites($x, $y) {
    if ($x.Count -ne $y.Count -or $x.Count -eq 0) { return $false }
    for ($i = 0; $i -lt $x.Count; $i++) { if ($x[$i][1] -ne $y[$i][1]) { return $false } }
    return $true
}
$a1e = AudioWrites $r.est_novo; $a1x = AudioWrites $r.exp_a1
Check "N4 audio: mesma sequencia de escritas na AICA na rota inteira (sem as copias de guarda do anel)" `
    ((Ended $r.est_ex $VBlanksEx) -and (Ended $r.exp_ex $VBlanksEx) -and (Ended $r.exp_exd $VBlanksEx) -and (Ended $r.est_novo $VBlanks) -and
     (Ended $r.exp_a1 $VBlanks) -and (SameWrites $ae $ax) -and (SameWrites $ae $ad) -and (SameWrites $a1e $a1x)) `
    "exploracao: estavel $($ae.Count), experimental $($ax.Count), com entradas alinhadas $($ad.Count); area 1: estavel $($a1e.Count), experimental $($a1x.Count)"

# ---- N5: eventos por VBlank ----
$se = Signatures $r.est_ex; $sx = Signatures $r.exp_ex
$set = New-Object System.Collections.Generic.HashSet[string]
foreach ($s in $se) { [void]$set.Add($s) }
$inSet = @($sx | Where-Object { $set.Contains($_) }).Count
$frac = if ($sx.Count) { $inSet / $sx.Count } else { 0 }
# as duas execucoes completas e os dois traces com todos os VBlanks (uma assinatura por VBlank + a do fim): trace vazio
# ou truncado nao passa pela fracao (revisao Codex 2026-10-25)
$n5Ok = (Ended $r.est_ex $VBlanksEx) -and (Ended $r.exp_ex $VBlanksEx) -and $se.Count -eq $VBlanksEx + 1 -and $sx.Count -eq $VBlanksEx + 1
Check "N5 eventos: padroes por VBlank do experimental existem no estavel (>= 99%)" ($n5Ok -and $frac -ge 0.99) ("{0:N2}% de {1} VBlanks; padroes distintos: estavel {2}, experimental {3}; assinaturas: estavel {4}, experimental {5}" -f (100 * $frac), $sx.Count, $set.Count, (@($sx | Sort-Object -Unique).Count), $se.Count, $sx.Count)

# ---- N6: provedores no experimental ----
$fl = ReadLines "$($r.exp_lg.P)_fr.tsv" | Select-Object -Skip 1 | Where-Object { [int64]($_.Split("`t")[0]) -lt 5500 }
$fn = ReadLines "$($r.exp_a1.P)_fr.tsv" | Select-Object -Skip 1 | Where-Object { [int64]($_.Split("`t")[0]) -lt 5500 }
$ll = ReadLines "$($r.exp_lg.P)_fl.tsv" | Select-Object -Skip 1 | Where-Object { [int64]($_.Split("`t")[1]) -lt 5500 } | ForEach-Object { ($_.Split("`t") | Select-Object -Skip 1) -join " " }
$ln = ReadLines "$($r.exp_a1.P)_fl.tsv" | Select-Object -Skip 1 | Where-Object { [int64]($_.Split("`t")[1]) -lt 5500 } | ForEach-Object { ($_.Split("`t") | Select-Object -Skip 1) -join " " }
# audio escrita a escrita, com o VBlank (BS_AICA_LOG_TUDO): linhas "w endereco valor tamanho vblank"
$al = ReadLines "$($r.exp_lg.P)_au.tsv" | Where-Object { $_.StartsWith("w") -and [int64]($_.Split("`t")[4]) -lt 5500 }
$an = ReadLines "$($r.exp_a1.P)_au.tsv" | Where-Object { $_.StartsWith("w") -and [int64]($_.Split("`t")[4]) -lt 5500 }
# leituras de dado pelo conteudo (deslocamento, tamanho, SHA-256), sem o nome nem o VBlank: o legado
# identifica o arquivo pelo setor (IWP15.NT e IWP20.NT sao o mesmo arquivo no disco, N1) e conclui a
# leitura do stream na propria carga, enquanto o nativo a conclui na passada seguinte do servidor
$ll2 = $ll | Where-Object { $_.Split(" ")[2] -eq "dado" } | ForEach-Object { $f = $_.Split(" "); "$($f[4]) $($f[5]) $($f[6])" }
$ln2 = $ln | Where-Object { $_.Split(" ")[2] -eq "dado" } | ForEach-Object { $f = $_.Split(" "); "$($f[4]) $($f[5]) $($f[6])" }
Check "N6 provedores (B4, D9a): no experimental, legado = nativo VBlank a VBlank ate o gameplay" `
    ((Ended $r.exp_lg $VBlanks) -and (Ended $r.exp_a1 $VBlanks) -and $fl.Count -gt 5000 -and $al.Count -gt 0 -and $ll2.Count -gt 0 -and (($fl -join "`n") -eq ($fn -join "`n")) -and (($al -join "`n") -eq ($an -join "`n")) -and (($ll2 -join "`n") -eq ($ln2 -join "`n"))) `
    "quadros $($fl.Count), escritas de audio $($al.Count), leituras de dado $($ll2.Count); $(Err $r.exp_lg)"

# ---- N7: timeout ----
$rw = [regex]::Match($r.neg_render.Out, "espera 8C173086:.*evento esperado RenderDone: com (\d+), sem (\d+); maior episodio (\d+) ticks")
Check "N7 timeout (B5): render perdido sai pelo timeout de 166 ms logicos, sem travar" ((Ended $r.neg_render $VBlanks) -and $rw.Success -and [int]$rw.Groups[2].Value -ge 1) `
    "saidas sem o fim de render: $($rw.Groups[2].Value); maior episodio $($rw.Groups[3].Value) ticks (166 ms ~ 54 M)"

# ---- N8: liveness ----
$e1 = Err $r.neg_sem_espera; $e2 = Err $r.neg_cri_nunca; $e3 = Err $r.neg_ffst_exp
Check "N8a liveness: espera do VBlank nao declarada -> erro com thread e PC" ($e1 -match "falta de progresso" -and $e1 -match "thread corrente 0 em 8C064A2C") $e1.Substring(0, [math]::Min(160, $e1.Length))
# a thread do CRI pela entrada (8C158840, evento N do trace): o indice depende da ordem de criacao (ver Signatures)
$tCri = ThreadOf (Join-Path $Saida "neg_cri_nunca_ev.tsv") "8C158840"
Check "N8b liveness: thread do CRI (entrada 8C158840) que nunca bloqueia -> erro com essa thread e a espera 8C158860" `
    ($tCri -ne "" -and $e2 -match "falta de progresso" -and $e2 -match "thread $tCri \d+ \(maior lote em [0-9A-F]{8}; ultima espera 8C158860\)") "thread do CRI: $(if ($tCri) { $tCri } else { 'sem evento N' }); $($e2.Substring(0, [math]::Min(160, $e2.Length)))"
Check "N8c espera sincrona que da volta -> erro explicito; estavel com a mesma mutacao roda" ($e3 -match "espera sincrona 8C0972(54|64)" -and (Ended $r.neg_ffst_est $VBlanksNeg)) $e3.Substring(0, [math]::Min(160, $e3.Length))
$skips += "N8 espera sincrona do DMA ch2 (8C1C1DC6, BS_SB_MUTACAO=c2dst_ocupado): o laco nao e alcancado nesta rota (negativo vazio)"

# ---- N9: detector da thread 3 ----
$ra = Reads $r.exp_a1; $fa = ReadLines "$($r.exp_a1.P)_fr.tsv"
$rs = Reads $r.neg_cri_sempre; $fs = ReadLines "$($r.neg_cri_sempre.P)_fr.tsv"
# as duas execucoes completas e com dados: um mutante sem artefatos nao conta como diferenca (revisao Codex 2026-10-25)
$n9Ok = (Ended $r.exp_a1 $VBlanks) -and (Ended $r.neg_cri_sempre $VBlanks) -and $ra.Count -gt 0 -and $rs.Count -gt 0 -and $fa.Count -gt 1 -and $fs.Count -gt 1
Check "N9 detector necessario: bloquear depois de toda passada muda leituras ou quadros" ($n9Ok -and ((($ra -join "`n") -ne ($rs -join "`n")) -or (($fa -join "`n") -ne ($fs -join "`n")))) "leituras $($ra.Count)/$($rs.Count), quadros $($fa.Count)/$($fs.Count); $(Err $r.neg_cri_sempre)"
$rr = Reads $r.neg_cri_ram; $fr2 = ReadLines "$($r.neg_cri_ram.P)_fr.tsv"
if ((($ra -join "`n") -eq ($rr -join "`n")) -and (($fa -join "`n") -eq ($fr2 -join "`n"))) {
    $skips += "N9 detector que ignora a RAM (cri_ignora_ram): sem diferenca observavel nesta rota (o componente de RAM e conservador aqui)"
} else { Write-Host "[info] N9 detector que ignora a RAM: diferenca observavel (negativo sensivel nesta rota)" }

# ---- N10: determinismo ----
$d = @("fl", "fr", "ev", "tt", "cl", "sl", "au") | Where-Object { (Hash "$($r.exp_ex.P)_$_.tsv") -ne (Hash "$($r.exp_ex2.P)_$_.tsv") -and $_ -ne "au" }
Check "N10 determinismo (B8): duas execucoes experimentais identicas" ((Ended $r.exp_ex2 $VBlanksEx) -and $d.Count -eq 0) "diferem: $($d -join ',')"

# ---- N11: estavel inalterado ----
if ($hasOld) {
    # artefatos selecionados (trace de ordem com ciclos, leituras, relogio, estado, tempo, quadro final),
    # todos presentes e nao vazios, e as duas execucoes terminadas
    $arts = @("ev.tsv", "fl.tsv", "cl.tsv", "sl.tsv", "tt.tsv", "q.bmp")
    $miss = $arts | Where-Object { -not (Test-Path -LiteralPath "$($r.est_n11.P)_$_") -or (Get-Item -LiteralPath "$($r.est_n11.P)_$_").Length -lt 64 -or
                                   -not (Test-Path -LiteralPath "$($r.est_antigo.P)_$_") -or (Get-Item -LiteralPath "$($r.est_antigo.P)_$_").Length -lt 64 }
    $d = $arts | Where-Object { (Hash "$($r.est_n11.P)_$_") -ne (Hash "$($r.est_antigo.P)_$_") }
    Check "N11 estavel inalterado: --tempo nativo (--controles legado --saves legado) = executavel anterior ao N2b-2 (artefatos byte a byte)" `
        ((Ended $r.est_n11 $VBlanks) -and (Ended $r.est_antigo $VBlanks) -and $miss.Count -eq 0 -and $d.Count -eq 0) "diferem: $($d -join ','); ausentes ou vazios: $($miss -join ',')"
} else { $skips += "N11 sem o executavel anterior ($ExeAntigo)" }

# ---- N13: mods ----
if ($hasMods) {
    $rm = Reads $r.exp_mods; $fm = ReadLines "$($r.exp_mods.P)_fr.tsv"
    Check "N13 mods (B10): build com mods no experimental = build sem mods" ((Ended $r.exp_mods $VBlanks) -and (($rm -join "`n") -eq ($ra -join "`n")) -and (($fm -join "`n") -eq ($fa -join "`n"))) $(Err $r.exp_mods)
} else { $skips += "N13 sem o build com mods ($ExeMods)" }

# ---- N12: desempenho (sequencial, alternado, sem artefatos) ----
$work = @{ nativo = @(); experimental = @() }
foreach ($i in 1..3) {
    foreach ($m in "nativo", "experimental") {
        $env:BS_RELOGIO_FIXO = "1700000000"; $env:BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\newgame_area1.txt")
        $o = cmd /c "`"$Exe`" --data `"$Data`" --tempo $m --video legado --headless 3000 2>&1" | Out-String  # escopo historico (Sofdec)
        $env:BS_RELOGIO_FIXO = ""; $env:BS_INPUT_SCRIPT = ""
        $w = [regex]::Match($o, "media ([\d.]+) ms")
        if ($w.Success) { $work[$m] += [double]::Parse($w.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) }
    }
}
$wn = ($work.nativo | Measure-Object -Average).Average; $wx = ($work.experimental | Measure-Object -Average).Average
Check "N12 desempenho (B9): experimental <= estavel x 1,05" ($work.nativo.Count -eq 3 -and $work.experimental.Count -eq 3 -and $wx -le $wn * 1.05) `
    ("trabalho medio por campo: estavel {0:N2} ms ({1}), experimental {2:N2} ms ({3})" -f $wn, ($work.nativo -join " "), $wx, ($work.experimental -join " "))

$fail = @($results | Where-Object { -not $_.Ok }).Count
foreach ($s in $skips) { Write-Host "[SKIP] $s" }
Write-Host ("{0} casos, {1} falhas; saidas em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
