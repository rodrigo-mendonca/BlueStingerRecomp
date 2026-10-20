# Partida real, sem o CDI: NEW GAME -> video/prologo -> primeira area jogavel -> movimento.
# Usa uma instalacao gerada pelo Builder (padrao: dist/BlueStinger-Preview) com o roteiro de
# entrada versionado (roteiros/newgame_area1.txt) e confere quadros capturados:
#   - o jogo roda os VBlanks pedidos sem erro;
#   - a barra de vida do HUD ("ELIOT") aparece na area jogavel;
#   - a imagem muda com o movimento (personagem/camera respondem ao controle);
#   - o jogo responde ao controle: sem entrada depois do NEW GAME, a cena fica parada.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_gameplay.ps1 [-Install <pasta>]
param([string]$Install = "")
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Install) { $Install = Join-Path $repo "dist\BlueStinger-Preview" }
$exe = Join-Path $Install "BlueStinger.exe"
$script = Join-Path $PSScriptRoot "roteiros\newgame_area1.txt"
$work = Join-Path $env:TEMP "bs-gameplay-teste"
$marker = Join-Path $work ".bs-gameplay-teste"
if (Test-Path -LiteralPath $work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $work existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $work
}
New-Item -ItemType Directory -Path $work | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
Add-Type -AssemblyName System.Drawing

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function Play($scriptFile, $vblanks, $dir) {
    $env:BS_INPUT_SCRIPT = $scriptFile
    # via cmd: stderr nativo com ErrorActionPreference=Stop abortaria o script no PowerShell 5.1
    try { $out = cmd /c "`"$exe`" --video legado --headless $vblanks --dump-every 50 --capture `"$dir`" 2>&1" | Out-String }
    finally { $env:BS_INPUT_SCRIPT = "" }
    return @{ Code = $LASTEXITCODE; Out = $out }
}
function Frame($dir, $v) {
    $f = Get-ChildItem -LiteralPath $dir -Filter "v*.bmp" | Where-Object { [int]$_.BaseName.Substring(1) -ge $v } | Sort-Object Name | Select-Object -First 1
    return $f
}
# pixels verdes da barra de vida (canto inferior esquerdo, faixa y 405-420, x 170-345)
function HudGreen($file) {
    $b = New-Object Drawing.Bitmap $file.FullName
    $n = 0
    try { for ($y = 405; $y -le 420; $y++) { for ($x = 170; $x -le 345; $x += 2) { $c = $b.GetPixel($x, $y); if ($c.G -gt 150 -and $c.R -lt 120 -and $c.B -lt 160) { $n++ } } } }
    finally { $b.Dispose() }
    return $n
}
# diferenca media por pixel (nao chamar de Diff: e alias de Compare-Object) (amostrada) entre dois quadros
function FrameDiff($a, $b) {
    $x = New-Object Drawing.Bitmap $a.FullName; $y = New-Object Drawing.Bitmap $b.FullName; $s = 0; $n = 0
    try { for ($j = 0; $j -lt $x.Height; $j += 8) { for ($i = 0; $i -lt $x.Width; $i += 8) { $p = $x.GetPixel($i, $j); $q = $y.GetPixel($i, $j); $s += [math]::Abs($p.R - $q.R) + [math]::Abs($p.G - $q.G) + [math]::Abs($p.B - $q.B); $n++ } } }
    finally { $x.Dispose(); $y.Dispose() }
    return $s / $n
}

# 1. partida com movimento
$dir = Join-Path $work "com entrada"
$r = Play $script 7300 $dir
Check "partida roda 7300 VBlanks sem erro (sem CDI)" ($r.Code -eq 0 -and $r.Out -match "VBlanks: 7300;" -and $r.Out -notmatch "erro:") ($r.Out -split "`n" | Where-Object { $_ -match "erro:|VBlanks" } | Select-Object -First 1)
$hud = HudGreen (Frame $dir 7250)
Check "area jogavel: barra de vida do HUD" ($hud -gt 40) "$hud pixels verdes"
$d1 = FrameDiff (Frame $dir 5650) (Frame $dir 6050)
$d2 = FrameDiff (Frame $dir 6050) (Frame $dir 6850)
Check "movimento muda a cena" ($d1 -gt 10 -and $d2 -gt 10) ("diferencas {0:N1} e {1:N1}" -f $d1, $d2)

# 2. sem entrada depois do NEW GAME: a cena da area jogavel fica parada (o movimento vem do controle)
$still = Join-Path $work "roteiro_parado.txt"
Get-Content -LiteralPath $script | Where-Object { $_ -notmatch "^\s*(5[5-9]|6|7)\d\d\d? " } | Set-Content -Encoding ascii $still
$dir2 = Join-Path $work "sem entrada"
$r = Play $still 6900 $dir2
$d3 = FrameDiff (Frame $dir2 6050) (Frame $dir2 6850)
Check "sem controle: personagem parado" ($r.Code -eq 0 -and $d3 -lt $d2 / 2) ("diferenca {0:N1} (com entrada {1:N1})" -f $d3, $d2)

$fails = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas (quadros em {2})" -f $results.Count, $fails, $work)
exit $(if ($fails) { 1 } else { 0 })
