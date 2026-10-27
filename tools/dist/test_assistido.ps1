# Cobertura de gameplay alem da primeira area, comparando --tempo nativo (estavel, padrao) e
# --tempo experimental (N2b-2) com roteiros indexados por PASSO DE JOGO (gravados no gameplay
# assistido, app/assist.h e tools/dev/assistido.py). Com o indice por passo a mesma entrada cai no
# mesmo ponto logico nos dois modos (as cargas nao contam passos), entao a comparacao e direta:
#   A1 quadros: o quadro exibido no fim de cada passo e igual nos dois modos (todos os passos)
#   A2 leituras: a mesma sequencia de blocos de arquivo (arquivo, deslocamento, tamanho, SHA-256)
#   A3 audio: a mesma sequencia de escritas da CPU na AICA (sem as copias de guarda do anel do
#      stream de video, como no N4 de test_n2b2.ps1)
#   A4 estado logico: a cada 10 passos (no inicio do passo), hash da regiao de variaveis do jogo,
#      vida e posicao do Eliot iguais
#   A5 progresso: os dois modos chegam ao ultimo passo do roteiro sem erro
#   A7 cargas de sala: cada R*.BIN lido no mesmo passo nos dois modos
#   A8 sessao ao vivo (BS_ASSISTIDO) = reproducao da sessao.txt; pad recusado com entradas futuras
#   A6 negativo: as entradas do menu atrasadas 3 passos so no experimental sao acusadas (quadros e
#      estado); A6b: um passo ausente de um log e acusado
# Roteiros (tools/dist/roteiros/passo_*.txt):
#   passo_cgate       NEW GAME, cais (RCG00), cenas e radio, porta do C-GATE, sala RCG02
#   passo_heliporto   corredor (RCG02), exterior (RCG03), menu, item, uso do item, combate, dano,
#                     morte, GAME OVER, volta ao titulo e NEW GAME
#   passo_combate     socos no monstro (dano no inimigo)
#   passo_load_opcoes LOAD GAME sem VMU e OPTIONS
#   powershell -File tools/dist/test_assistido.ps1 [-Exe <exe>] [-Data <data>] [-Roteiros a,b]
param(
    # N8.5: --video legado (o Sofdec original: os roteiros por passo destas suites foram gravados com ele) so existe no build de validacao
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-validacao\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [string[]]$Roteiros = @("passo_cgate", "passo_heliporto", "passo_combate", "passo_load_opcoes"),
    [int]$LimiteSegundos = 3600,
    [string]$Saida = (Join-Path $env:TEMP "bs-assistido-teste")
)
. (Join-Path $PSScriptRoot "valida_exe.ps1"); Assert-Validacao $Exe  # N8.5: o exe precisa ser de validacao
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "SKIP: executavel ausente ($Exe)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$py = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $py) { Write-Host "SKIP: python ausente (tools/dev/compara_modos.py)"; exit 0 }
$marker = Join-Path $Saida ".bs-assistido-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}

# VBlanks por roteiro: o ultimo passo do roteiro a 2 VBlanks por passo, com folga para as cargas do
# estavel (que duram VBlanks) e para o boot
function VBlanksFor($roteiro) {
    $last = 0
    foreach ($l in [IO.File]::ReadLines($roteiro)) {
        $t = $l.Split("#")[0].Trim()
        if ($t -match "^(\d+)\s") { $last = [int64]$Matches[1] }
    }
    return [int64]($last * 2 + 3000)
}

# video legado: os roteiros por passo foram gravados com ele. No filme nativo (N5) o filme roda dentro de UM passo de jogo:
# a entrada por passo nao o pula, o NEWGAME toca inteiro (452 s logicos) e o orcamento de VBlanks acabava dentro dele nos
# dois modos de tempo (A5 parado no passo ~1037; 2026-10-24). Os modos de tempo continuam comparados com o mesmo video.
function Launch($tag, $tempo, $roteiro, $n) {
    $p   = Join-Path $Saida $tag
    $all = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = $roteiro; BS_FILE_LOG = "${p}_fl.tsv"
              BS_FRAME_LOG = "${p}_fr.tsv"; BS_AICA_LOG = "${p}_au.tsv"; BS_AICA_LOG_TUDO = "1"; BS_STATE_PASSO = "${p}_st.tsv" }
    foreach ($k in $all.Keys) { Set-Item "env:$k" $all[$k] }
    try {
        $proc = Start-Process -FilePath "cmd.exe" -PassThru -WindowStyle Hidden -ArgumentList "/c `"`"$Exe`" --data `"$Data`" --tempo $tempo --video legado --headless $n > `"${p}_saida.txt`" 2>&1`""
        $null = $proc.Handle
    } finally { foreach ($k in $all.Keys) { Set-Item "env:$k" "" } }
    return @{ Tag = $tag; Proc = $proc; P = $p; N = $n }
}

$runs = @()
foreach ($r in $Roteiros) {
    $file = Join-Path $PSScriptRoot "roteiros\$r.txt"
    $n = VBlanksFor $file
    $runs += Launch "${r}_est" "nativo" $file $n
    $runs += Launch "${r}_exp" "experimental" $file $n
}
# A6 negativo (sensibilidade): o roteiro de LOAD GAME com as entradas depois do passo 1000
# atrasadas 3 passos, so no experimental; a comparacao com o estavel tem de acusar
$negSrc = Join-Path $PSScriptRoot "roteiros\passo_load_opcoes.txt"
$negFile = Join-Path $Saida "neg_roteiro.txt"
$negLines = foreach ($l in [IO.File]::ReadLines($negSrc)) {
    if ($l -match "^(\d+)\s+(\S+)" -and [int64]$Matches[1] -gt 1000) { "$([int64]$Matches[1] + 3) $($Matches[2])" } else { $l }
}
[IO.File]::WriteAllLines($negFile, [string[]]$negLines)
$runs += Launch "neg_exp" "experimental" $negFile (VBlanksFor $negSrc)
$t0 = Get-Date
foreach ($s in $runs) {
    if (-not $s.Proc.WaitForExit($LimiteSegundos * 1000)) { & taskkill /PID $s.Proc.Id /T /F 2>&1 | Out-Null }
}
Write-Host ("execucoes: {0:n0} s" -f ((Get-Date) - $t0).TotalSeconds)

$compara = Join-Path $PSScriptRoot "..\dev\compara_modos.py"
foreach ($r in $Roteiros) {
    $file = Join-Path $PSScriptRoot "roteiros\$r.txt"
    $lastStep = 0
    foreach ($l in [IO.File]::ReadLines($file)) { $t = $l.Split("#")[0].Trim(); if ($t -match "^(\d+)\s") { $lastStep = [int64]$Matches[1] } }
    $ok = $true
    foreach ($m in "est", "exp") {
        $out = Get-Content -Raw -LiteralPath (Join-Path $Saida "${r}_${m}_saida.txt")
        if ($out -notmatch "VBlanks: \d+;" -or $out -match "(?m)^erro:") { $ok = $false; Write-Host ($out -split "`n" | Select-String "erro" | Select-Object -First 3) }
    }
    $cmp = & cmd /c "python `"$compara`" `"$(Join-Path $Saida "${r}_est")`" `"$(Join-Path $Saida "${r}_exp")`" 2>&1"
    $cmp | Set-Content -LiteralPath (Join-Path $Saida "${r}_comparacao.txt")
    $res = ($cmp | Select-String "^RESULTADO").Line
    Write-Host "== $r"; $cmp | Where-Object { $_ -notmatch "^  \(" } | ForEach-Object { Write-Host "   $_" }
    $reached = 0; if ($res -match "ultimo_passo=(\d+)") { $reached = [int64]$Matches[1] }
    Check "A5 $r progresso" ($ok -and $reached -ge $lastStep) "ultimo passo comparado $reached (roteiro ate $lastStep)"
    Check "A1 $r quadros" ($res -match "quadros=ok")
    Check "A2 $r leituras" ($res -match "leituras=ok")
    Check "A3 $r audio" ($res -match "audio=ok")
    Check "A4 $r estado logico" ($res -match "estado=ok")
    Check "A7 $r cargas de sala no mesmo passo" ($res -match "cargas=ok")
}
if (Test-Path -LiteralPath (Join-Path $Saida "passo_load_opcoes_est_fr.tsv")) {
    $neg = & cmd /c "python `"$compara`" `"$(Join-Path $Saida "passo_load_opcoes_est")`" `"$(Join-Path $Saida "neg_exp")`" 2>&1"
    $negRes = ($neg | Select-String "^RESULTADO").Line
    Check "A6 negativo: entrada deslocada 3 passos e detectada" ($negRes -match "quadros=FALHA" -and $negRes -match "estado=FALHA") $negRes
    # A6b negativo: um passo (600) que some so de um log nao pode passar como igual
    $src = Join-Path $Saida "passo_load_opcoes_exp"; $hole = Join-Path $Saida "neg_buraco"
    [IO.File]::WriteAllLines("${hole}_fr.tsv", [string[]]([IO.File]::ReadAllLines("${src}_fr.tsv") | Where-Object { $_.Split("`t")[3] -ne "600" }))
    [IO.File]::WriteAllLines("${hole}_st.tsv", [string[]]([IO.File]::ReadAllLines("${src}_st.tsv") | Where-Object { -not $_.StartsWith("600`t") }))
    Copy-Item -LiteralPath "${src}_fl.tsv" -Destination "${hole}_fl.tsv"
    $h = & cmd /c "python `"$compara`" `"$(Join-Path $Saida "passo_load_opcoes_est")`" `"$hole`" 2>&1"
    $hRes = ($h | Select-String "^RESULTADO").Line
    Check "A6b negativo: passo ausente de um log e acusado" ($hRes -match "quadros=FALHA" -and $hRes -match "estado=FALHA") $hRes
}

# A8 sessao ao vivo (BS_ASSISTIDO) = reproducao da sessao.txt com BS_INPUT_SCRIPT (quadros VBlank a
# VBlank); no meio, pad com um roteiro de entradas futuras carregado tem de ser recusado
$live = Join-Path $Saida "ao_vivo"
$cli = Join-Path $PSScriptRoot "..\dev\assistido.py"
$fut = Join-Path $Saida "futuro.txt"
[IO.File]::WriteAllLines($fut, [string[]]@("indice passo", "1150 START", "1153 -"))
& cmd /c "python `"$cli`" iniciar `"$live`" --tempo experimental --exe `"$Exe`" --data `"$Data`" 2>&1" | Out-Null
$o = & cmd /c "python `"$cli`" cmd `"$live`" `"pad START`" `"passos 3`" `"pad -`" `"ate_passo 601`" `"pad START`" `"passos 3`" `"pad -`" `"ate_passo 801`" `"pad START`" `"passos 3`" `"pad -`" `"ate_passo 1100`" `"roteiro $fut`" `"pad A`" `"ate_passo 1160`" `"pad DOWN`" `"passos 5`" `"pad -`" `"passos 20`" 2>&1"
$refused = ($o | Select-String "erro o roteiro carregado tem entradas").Count -eq 1
& cmd /c "python `"$cli`" cmd `"$live`" fim 2>&1" | Out-Null
$t1 = Get-Date
while (((Get-Date) - $t1).TotalSeconds -lt 120 -and -not (Select-String -Quiet -Pattern "VBlanks:" -LiteralPath (Join-Path $live "saida.txt"))) { Start-Sleep -Milliseconds 500 }
$liveFr = [IO.File]::ReadAllLines((Join-Path $live "quadros.tsv"))
$lastV = [int64]($liveFr[-1].Split("`t")[0])
$rep = Launch "replay_exp" "experimental" (Join-Path $live "sessao.txt") $lastV
$rep.Proc.WaitForExit($LimiteSegundos * 1000) | Out-Null
$repFr = [IO.File]::ReadAllLines("$($rep.P)_fr.tsv")
$n = [Math]::Min($liveFr.Count, $repFr.Count) - 1
$equal = $n -gt 2000 -and ((($liveFr[0..$n]) -join "`n") -eq (($repFr[0..$n]) -join "`n"))
Check "A8 sessao ao vivo = reproducao da sessao.txt (quadros VBlank a VBlank)" $equal "$n VBlanks"
Check "A8b pad recusado com entradas futuras de roteiro" $refused
$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas" -f $results.Count, $fail)
exit $(if ($fail) { 1 } else { 0 })
