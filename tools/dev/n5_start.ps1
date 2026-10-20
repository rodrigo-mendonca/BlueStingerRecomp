# N5: START durante o E001, separado em camadas (docs/N5-PLANO.md; referencia: O1b-B no Flycast).
# Nao forca nada no adaptador: mede o que acontece no jogo.
#   1. controle   o START do roteiro cai numa leitura do controle feita DURANTE o filme
#   2. estado     a palavra que o laco do filme le (0x8C24F878 = estrutura global 0x8C24D2CC + 0x25AC)
#                 muda depois do START (BS_RAM_WATCH, por VBlank)
#   3. condicao   8C08B544 sai pelo input com bit 4 ligado e bit 8 desligado (e ctl & 1; o E001 tem
#                 ctl=01); o bit 2 so com a flag 0 da tabela, que o E001 nao tem
#   4. parada     o adaptador recebe o stop do jogo antes do fim natural ("[video] parado pelo jogo")
#   5. retorno    a rota termina sem erro
#   6. gameplay   depois da janela do filme o jogo segue em passos seguintes, e (no nativo) a janela acaba
#                 ANTES da janela da execucao sem START (o filme foi de fato encurtado)
# Entrada igual a do O1b-B: a rota do oraculo (passo_newgame_area1 ate "2630 UP", UP mantido) e o START
# -StartAposLeituras (419 = 6672 - 6253 do O1b-B) depois da entrada do E001, por 18 leituras. Durante o filme
# o passo de jogo do nativo fica parado (o filme roda dentro de um passo) e o do oraculo (passada do pd)
# continua; por isso a rota com START e a mesma rota convertida para LEITURAS pelo log de quadros da execucao
# sem START (passo -> primeira leitura daquele passo), com o START no indice de leitura. As duas execucoes
# tem a mesma entrada ate o START, entao a mesma linha do tempo.
#   powershell -File tools/dev/n5_start.ps1 [-Exe <bluestinger.exe>] [-Data <data com data\video>] [-StartAposLeituras 419]
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [string]$Saida = (Join-Path $env:TEMP "bs-n5-start"),
    [int]$StartAposLeituras = 419,
    [int]$StartLeituras = 18,
    [int]$VBlanks = 33500  # NEWGAME nativo toca os 452 s inteiros: o E001 abre perto do VBlank 31 470
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Exe = (Resolve-Path $Exe).Path; $Data = (Resolve-Path $Data).Path
if (Test-Path -LiteralPath $Saida) { Remove-Item -Recurse -Force -LiteralPath $Saida }
New-Item -ItemType Directory -Path $Saida | Out-Null
$palavra = "8C24F878"

function Run($tag, $video, $rota) {
    $p = Join-Path $Saida $tag
    $env:BS_RELOGIO_FIXO = "1700000000"; $env:BS_INPUT_SCRIPT = $rota; $env:BS_FRAME_LOG = "${p}_fr.tsv"
    $env:BS_RAM_WATCH = "${p}_ram.tsv:$palavra"
    try { cmd /c "`"$Exe`" --data `"$Data`" --video $video --headless $VBlanks > `"${p}_saida.txt`" 2>&1" }
    finally { foreach ($k in "BS_RELOGIO_FIXO", "BS_INPUT_SCRIPT", "BS_FRAME_LOG", "BS_RAM_WATCH") { Remove-Item "env:$k" -ErrorAction SilentlyContinue } }
    return @{ P = $p; Out = (Get-Content -Raw -Encoding UTF8 -LiteralPath "${p}_saida.txt") }
}
function Window($p, $passo) {  # janela do filme: VBlanks do passo, leituras do controle
    $w = @(Import-Csv "${p}_fr.tsv" -Delimiter "`t" | Where-Object { $_.passo -eq "$passo" })
    if ($w.Count -eq 0) { return $null }
    return @{ V0 = [int64]$w[0].vblank; V1 = [int64]$w[-1].vblank; R0 = [int64]$w[0].entrada; R1 = [int64]$w[-1].entrada }
}

# 1) sem START, rota do oraculo (por passo): a janela do filme (passo do E001 e leituras)
$lin0 = @(Get-Content -LiteralPath (Join-Path $repo "tools\dist\roteiros\passo_newgame_area1.txt"))
$corte = [array]::IndexOf($lin0, "2630 UP")
if ($corte -lt 0) { Write-Host "ERRO: passo_newgame_area1.txt sem a linha '2630 UP' da rota do oraculo"; exit 2 }
$base = Join-Path $Saida "rota_oraculo_passo.txt"
$lin0[0..$corte] | Set-Content -LiteralPath $base
$r0n = Run "sem_nativo" "nativo" $base
$m = [regex]::Match($r0n.Out, "\[video\] E001\.SFD -> E001\.mp4 \(passo (\d+)\)")
if (-not $m.Success) { Write-Host "ERRO: o E001 nao abriu no nativo"; exit 2 }
$passo = [int64]$m.Groups[1].Value
$wn = Window $r0n.P $passo
$alvo = $wn.R0 + $StartAposLeituras
Write-Host "E001 no passo $passo; janela nativa: VBlanks $($wn.V0)-$($wn.V1), leituras $($wn.R0)-$($wn.R1); START na leitura $alvo"
if ($alvo -gt $wn.R1) { Write-Host "ERRO: o START ($alvo) cairia depois do fim do filme ($($wn.R1))"; exit 2 }

# 2) com START nos dois modos: a mesma rota em indice de leitura (passo -> primeira leitura dele) + START
$fr = @(Import-Csv "$($r0n.P)_fr.tsv" -Delimiter "`t")
$primeira = @{}
foreach ($q in $fr) { $k = [int64]$q.passo; if (-not $primeira.ContainsKey($k)) { $primeira[$k] = [int64]$q.entrada } }
$passos = @($primeira.Keys | Sort-Object)
$leit = @("# rota do oraculo convertida para leituras pelo log de quadros de sem_nativo (n5_start.ps1)", "indice leitura")
# linhas da rota com passo DEPOIS do passo do E001: no oraculo (indice = passada do pd, que corre durante os
# filmes) elas aconteceram ANTES do E001 (o "2630 UP" cai no NEWGAME); entram na 1a leitura do E001 (vale a ultima)
$noFilme = $null
foreach ($l in @($lin0[0..$corte] | Where-Object { $_ -match '^\d' })) {
    $c = $l.Split(" "); $pa = [int64]$c[0]
    if ($pa -gt $passo) { $noFilme = $c[1]; continue }
    $k = $passos | Where-Object { $_ -ge $pa } | Select-Object -First 1
    if ($null -eq $k) { Write-Host "ERRO: passo $pa da rota nao aparece no log de quadros"; exit 2 }
    $leit += "$([math]::Max(0, $primeira[$k])) $($c[1])"
}
if ($noFilme) { $leit += "$($wn.R0) $noFilme" }  # estado do oraculo na entrada do E001 (UP mantido)
$leit += "$alvo START", "$($alvo + $StartLeituras) -"  # como a rota do O1b-B: "6672 START", "6690 -"
$rota = Join-Path $Saida "start.txt"
$leit | Set-Content -LiteralPath $rota
$res = @{}
foreach ($mode in "nativo", "legado") {
    $r = Run "start_$mode" $mode $rota
    $w = Window $r.P $passo
    $ram = @(Import-Csv "$($r.P)_ram.tsv" -Delimiter "`t")
    $durante = @($ram | Where-Object { $w -and [int64]$_.vblank -ge $w.V0 -and [int64]$_.vblank -le $w.V1 })
    $bits = ($durante | ForEach-Object { $v = [Convert]::ToUInt32($_.valor, 16); "V$($_.vblank)=$($_.valor)(b4=$([int](($v -band 4) -ne 0)),b8=$([int](($v -band 8) -ne 0)))" }) -join " "
    $saiu = ($durante | Where-Object { $v = [Convert]::ToUInt32($_.valor, 16); ($v -band 4) -and -not ($v -band 8) }).Count -gt 0
    $res[$mode] = [pscustomobject]@{
        Modo       = $mode
        Controle   = ($w -and $alvo -ge $w.R0 -and $alvo -le $w.R1)
        Janela     = if ($w) { "V$($w.V0)-$($w.V1) leituras $($w.R0)-$($w.R1)" } else { "-" }
        Estado     = $durante.Count
        Condicao   = $saiu
        Parada     = $r.Out -match "\[video\] parado pelo jogo"
        Retorno    = $r.Out -match "VBlanks: $VBlanks;" -and $r.Out -notmatch "(?m)^erro:"
        Gameplay   = $w -and @(Import-Csv "$($r.P)_fr.tsv" -Delimiter "`t" | Where-Object { [int64]$_.passo -gt $passo }).Count -gt 0
        Encurtado  = if ($mode -eq "nativo" -and $w) { "fim V$($w.V1) x V$($wn.V1) sem START: $([bool]($w.V1 -lt $wn.V1))" } else { "- (Sofdec emulado acaba sozinho em ~152 VBlanks)" }
        Palavra    = $bits
    }
}
$res.Values | Format-List | Out-String | Write-Host
Write-Host "Leitura: 1=Controle (START dentro da janela), 2=Estado (mudancas da palavra no filme), 3=Condicao (bit4 sem bit8),"
Write-Host "4=Parada (so no nativo: o adaptador recebeu o stop antes do fim), 5=Retorno, 6=Gameplay/Encurtado."
Write-Host "Referencia: O1b-B no Flycast (analysis/n5/o1b/o1b_b.tsv)."

# Veredito (revisao Codex 2026-10-20): o comportamento ORIGINAL medido no O1b-B e que o START durante o E001
# NAO encerra o filme (1937 iteracoes com START, 1936 sem) e o jogo volta ao gameplay. Exige isso no NATIVO
# (comparado ao original pelo O1b-B). O legado NAO e referencia de START: o Sofdec emulado encerra o NEWGAME e o
# E001 sozinho (o E001 dele abre perto do VBlank 4480, nao 31466), entao o START na leitura do nativo cai fora
# da janela dele por construcao; do legado so se exige chegar ao fim sem erro e voltar ao gameplay.
# A mesma linha do tempo ate o START: a conversao passo -> leitura usa o log por VBlank, entao a prova e que a
# execucao com START e a sem START tem os MESMOS registros de quadro (todas as colunas) enquanto o controle
# servido nao passou do indice do START.
$falhas = @()
$semFr = @(Get-Content -LiteralPath "$($r0n.P)_fr.tsv")
$comFr = @(Get-Content -LiteralPath (Join-Path $Saida "start_nativo_fr.tsv") -ErrorAction SilentlyContinue)
$cab = $semFr[0].Split("`t"); $ie = [array]::IndexOf($cab, "entrada")
$antes = { param($linhas) @($linhas | Select-Object -Skip 1 | Where-Object { [int64]($_.Split("`t")[$ie]) -lt $alvo }) }
$a1 = & $antes $semFr; $a2 = & $antes $comFr
if ($ie -lt 0 -or $a1.Count -eq 0 -or ($a1 -join "`n") -ne ($a2 -join "`n")) {
    $falhas += "linha do tempo: os registros de quadro ate o START diferem entre sem_nativo e start_nativo ($($a1.Count) x $($a2.Count) linhas)"
} else { Write-Host "linha do tempo ate o START: $($a1.Count) registros de quadro identicos (sem START x com START)" }
$xn = $res["nativo"]
if (-not $xn.Controle) { $falhas += "nativo: o START nao caiu dentro da janela do filme" }
if ($xn.Condicao) { $falhas += "nativo: a condicao de saida por input apareceu (o original nao sai)" }
foreach ($mode in "nativo", "legado") {
    $x = $res[$mode]
    if (-not $x.Retorno) { $falhas += "${mode}: a execucao nao chegou ao fim sem erro" }
    if (-not $x.Gameplay) { $falhas += "${mode}: sem gameplay depois do filme" }
}
if ($res["nativo"].Parada) { $falhas += "nativo: o adaptador recebeu parada (o original nao encerra o filme)" }
$wsn = Window (Join-Path $Saida "start_nativo") $passo
if (-not $wsn -or $wsn.V1 -ne $wn.V1) { $falhas += "nativo: o fim do filme mudou com o START (V$($wsn.V1) x V$($wn.V1))" }
if ($falhas.Count) { $falhas | ForEach-Object { Write-Host "FALHA: $_" }; Write-Host "START: DIVERGE do original"; exit 1 }
Write-Host "START: igual ao original (O1b-B) no nativo: o filme nao e encurtado, mesmo fim, retorno ao gameplay; legado sem erro"
exit 0
