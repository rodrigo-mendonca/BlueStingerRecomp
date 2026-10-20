# N5: primeiro filme nativo no jogo (E001, docs/N5-INTEGRACAO.md). Executavel com os 8 slots MW
# (codigo gerado de novo com [native] slots). Nao toca a instalacao da Preview: monta uma pasta de
# dados propria com hard links de dist\BlueStinger-Preview\data e converte so o E001.
#
#   powershell -File tools/dev/n5_e001.ps1 [-Exe <bluestinger.exe>] [-Probe <bs-video-probe.exe>] [-Saida <dir>]
#              [-StartAposMs 3000]
#
# Etapas:
#   1. data de teste: hard links da Preview + data\video com o E001 (bs-video-probe --builder sobre um
#      BSDATA so com o E001.SFD: manifesto dos 17 IDs, os outros ausentes)
#   2. rota passo_newgame_area1 (7300 VBlanks, headless) com --video legado e com --video nativo
#   3. rota com START durante o E001 (ctl=01: o START pode encerrar o filme), no passo do inicio do
#      filme da execucao 2 + o equivalente a -StartAposMs
# Verificacoes (V1-V8, abaixo). O NEWGAME (ctl=00) nao e convertido aqui: no nativo fica ausente e o
# estado de ausente e o PENDENTE do O1b (hipotese 4, como o Flycast observou).
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\app\bluestinger.exe"),
    [string]$Probe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\builder\bs-video-probe.exe"),
    [string]$Preview = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [string]$Saida = (Join-Path $env:TEMP "bs-n5-e001"),
    [int]$VBlanks = 7300,
    [int]$StartAposMs = 3000
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
foreach ($f in $Exe, $Probe, (Join-Path $Preview "disc.manifest")) {
    if (-not (Test-Path -LiteralPath $f)) { Write-Host "ERRO: falta $f"; exit 2 }
}
$marker = Join-Path $Saida ".bs-n5-e001"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este script"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}

# ---- 1. dados de teste ----
$data = Join-Path $Saida "data"
$src = (Resolve-Path $Preview).Path
Get-ChildItem -LiteralPath $src -Recurse -File | ForEach-Object {
    $to = Join-Path $data $_.FullName.Substring($src.Length + 1)
    New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
    New-Item -ItemType HardLink -Path $to -Target $_.FullName | Out-Null
}
if (Test-Path -LiteralPath (Join-Path $data "video")) { Remove-Item -Recurse -Force -LiteralPath (Join-Path $data "video") }
$bsd = Join-Path $Saida "BSDATA_E001"
New-Item -ItemType Directory -Path $bsd | Out-Null
New-Item -ItemType HardLink -Path (Join-Path $bsd "E001.SFD") -Target (Join-Path $src "disc\BSDATA\E001.SFD") | Out-Null
$conv = & $Probe --builder $bsd (Join-Path $data "video") 2>&1 | Out-String
$conv | Set-Content -LiteralPath (Join-Path $Saida "conversao.txt")
Check "V0 E001 convertido (manifesto dos 17 IDs)" ($LASTEXITCODE -eq 0 -and $conv -match "ok: 1 convertidos") (($conv -split "`n" | Where-Object { $_ -match "convertidos" }) -join " ")

# ---- 2. rotas ----
function Run($tag, $video, $script) {
    $p = Join-Path $Saida $tag
    $env:BS_RELOGIO_FIXO = "1700000000"; $env:BS_INPUT_SCRIPT = $script
    $env:BS_FRAME_LOG = "${p}_fr.tsv"; $env:BS_FILE_LOG = "${p}_fl.tsv"; $env:BS_STATE_PASSO = "${p}_st.tsv"; $env:BS_STATE_PASSO_CADA = "1"
    try {
        cmd /c "`"$Exe`" --data `"$data`" --video $video --headless $VBlanks --dump `"${p}_q.bmp`" > `"${p}_saida.txt`" 2>&1"
    } finally {
        foreach ($k in "BS_RELOGIO_FIXO", "BS_INPUT_SCRIPT", "BS_FRAME_LOG", "BS_FILE_LOG", "BS_STATE_PASSO", "BS_STATE_PASSO_CADA") { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
    }
    return @{ P = $p; Out = (Get-Content -LiteralPath "${p}_saida.txt" -Raw) }
}
$rota = Join-Path $repo "tools\dist\roteiros\passo_newgame_area1.txt"
$leg = Run "legado" "legado" $rota
$nat = Run "nativo" "nativo" $rota
$ended = { param($r) $r.Out -match "VBlanks: $VBlanks;" -and $r.Out -notmatch "(?m)^erro:" }
Check "V1 as duas rotas terminam sem erro" ((& $ended $leg) -and (& $ended $nat))

$start = [regex]::Match($nat.Out, "\[video\] E001\.SFD -> E001\.mp4 \(passo (\d+)\)")
$fim   = [regex]::Match($nat.Out, "\[video\] fim natural: (\d+) ms logicos, (\d+) quadros \((\d+) pulados\) \(passo (\d+)\)")
Check "V2 E001 aberto pelo recurso convertido (ID 3 pelo manifesto)" $start.Success $(if ($start.Success) { "passo $($start.Groups[1].Value)" })
# 411 quadros a 30 fps = 13,70 s (matriz do N5); o relogio logico do jogo conduz o filme
$okSync = $fim.Success -and [math]::Abs([int]$fim.Groups[1].Value - 13700) -le 100 -and ([int]$fim.Groups[2].Value + [int]$fim.Groups[3].Value) -ge 404
Check "V3 sincronia: fim natural no tempo do filme, quadros exibidos + pulados ~ 411" $okSync $(if ($fim.Success) { "$($fim.Groups[1].Value) ms, $($fim.Groups[2].Value) quadros, $($fim.Groups[3].Value) pulados, passo $($fim.Groups[4].Value)" })
$sum = [regex]::Match($nat.Out, "\[video\] nativo: create (\d+) \(falhas (\d+)\).*chamadas de outros players (\d+), entrada exposta ao servidor (\d+)")
Check "V4 nenhum Sofdec no filme nativo: entrada nunca exposta ao servidor do MW" ($sum.Success -and $sum.Groups[4].Value -eq "0" -and $sum.Groups[2].Value -eq "0") $sum.Value
$sfdLeg = @(Get-Content -LiteralPath "$($leg.P)_fl.tsv" | Where-Object { $_ -match "E001\.SFD" }).Count
$sfdNat = @(Get-Content -LiteralPath "$($nat.P)_fl.tsv" | Where-Object { $_ -match "E001\.SFD" }).Count
Check "V5 o nativo nao le o E001.SFD do disco (o legado le)" ($sfdLeg -gt 0 -and $sfdNat -eq 0) "leituras do SFD: legado $sfdLeg, nativo $sfdNat"
# retorno ao gameplay: depois do fim do filme, mesma sequencia de salas e estado por passo
$de = if ($fim.Success) { [int]$fim.Groups[4].Value + 1 } else { 0 }
$cmp = & python -I (Join-Path $repo "tools\dev\compara_modos.py") $leg.P $nat.P --de $de 2>&1 | Out-String
$cmp | Set-Content -LiteralPath (Join-Path $Saida "compara_pos_filme.txt")
Write-Host $cmp
Check "V6 retorno ao gameplay: mesma sala final e rota ate o fim (relatorio compara_pos_filme.txt)" ((& $ended $nat) -and $fim.Success) "comparacao por passo a partir do passo $de (medida, nao criterio)"
# janela do filme no log de quadros (vblank, quadro, entrada=leituras do controle, passo)
$fr = @(Get-Content -LiteralPath "$($nat.P)_fr.tsv" | Select-Object -Skip 1 | ForEach-Object { , $_.Split("`t") })
$win = @()
if ($start.Success -and $fim.Success) {
    $p0 = [int64]$start.Groups[1].Value; $p1 = [int64]$fim.Groups[4].Value
    $win = @($fr | Where-Object { [int64]$_[3] -ge $p0 -and [int64]$_[3] -le $p1 })
}
$trocas = 0; for ($i = 1; $i -lt $win.Count; $i++) { if ($win[$i][1] -ne $win[$i - 1][1]) { $trocas++ } }
# quadros do filme na tela: ~411 trocas em ~822 VBlanks (30 fps sobre 59,94 Hz); o PVR parado nao troca
Check "V7 quadro do filme exibido no lugar do PVR" ($trocas -ge 380) "trocas de quadro na janela do filme: $trocas em $($win.Count) VBlanks"

# ---- 3. START durante o E001 (ctl=01) ----
# Se o laco do filme roda dentro de um so passo de jogo, um START por passo nao cai no meio dele: o
# START entra pelo roteiro por leitura do controle (newgame_area1.txt), no indice das leituras do
# inicio do filme + StartAposMs (uma leitura por iteracao do laco do filme, ~30 por segundo).
if ($win.Count -gt 0) {
    $passosNoFilme = [int64]$win[-1][3] - [int64]$win[0][3]
    $leituras0 = [int64]$win[0][2]
    $leituras1 = [int64]$win[-1][2]
    Write-Host "janela do filme: passos $($win[0][3])-$($win[-1][3]), leituras do controle $leituras0-$leituras1"
    $rotaStart = Join-Path $Saida "e001_start.txt"
    if ($passosNoFilme -ge 60) {
        $base = @(Get-Content -LiteralPath $rota); $alvo = [int64]$win[0][3] + [int]([math]::Round($StartAposMs / 1000.0 * ($passosNoFilme / 13.7)))
    } else {
        $base = @(Get-Content -LiteralPath (Join-Path $repo "tools\dist\roteiros\newgame_area1.txt"))
        $alvo = $leituras0 + [int]([math]::Round($StartAposMs / 1000.0 * (($leituras1 - $leituras0) / 13.7)))
        if ($leituras1 -le $leituras0) { Write-Host "o jogo nao le o controle durante o filme: START impossivel nesta rota" }
    }
    $cab = @($base | Where-Object { $_ -notmatch '^\d' })
    $corpo = @($base | Where-Object { $_ -match '^\d' }) + "$alvo START", "$($alvo + 4) -"
    ($cab + ($corpo | Sort-Object { [int64]($_.Split(" ")[0]) })) | Set-Content -LiteralPath $rotaStart
    $st = Run "start" "nativo" $rotaStart
    $par = [regex]::Match($st.Out, "\[video\] parado pelo jogo em (\d+) ms logicos")
    # Se o START deve encerrar o E001 aqui e PENDENTE do O1b (Flycast): o laco do filme le uma palavra de
    # estado do jogo (@(r14+0x25AC), bits 4/8), nao o controle; o legado nao serve de referencia (encerra o
    # filme sozinho em ~152 VBlanks). Medida, nao criterio: o jogo segue sem erro com o START no meio do
    # filme, e o resultado (parado ou fim natural) e comparado ao Flycast no O1b.
    Check "V8 START durante o E001: o jogo segue sem erro (efeito no filme: PENDENTE O1b)" (& $ended $st) $(if ($par.Success) { "parado pelo jogo em $($par.Groups[1].Value) ms" } else { "o filme foi ate o fim natural (START na leitura $alvo)" })
}
$f = @($results | Where-Object { -not $_.Ok }).Count
Write-Host "$($results.Count) casos, $f falhas; saidas em $Saida"
exit $(if ($f) { 1 } else { 0 })
