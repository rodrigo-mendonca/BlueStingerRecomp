# N6 fase D (docs/N6-PLANO.md secao 14): efeitos sonoros pelo provedor nativo contra o legado (SDK de som original na
# AICA emulada), no mesmo roteiro por passo. Criterio: quadros e estado do jogo IGUAIS por passo (o bit "canal livre"
# da consulta governa a prioridade e o rodizio do gerenciador do jogo, entao o fim nativo de cada efeito tem de cair
# na mesma passada), com efeitos tocados de fato pelo nativo, nenhum ausente e nenhuma falha de decodificacao.
#   powershell -File tools/dev/n6_efeitos.ps1 -Exe <bluestinger.exe com os slots do som> [-Rota <passo_*.txt>]
#              [-VBlanks N] [-Data <data>] [-Saida <pasta>]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Rota = "",
    [int64]$VBlanks = 0,
    [string]$Data = "",
    [string]$Saida = (Join-Path $env:TEMP "bs-n6-efeitos")
)
$ErrorActionPreference = "Stop"
# padroes no corpo: no PowerShell 5.1 o $PSScriptRoot fica vazio nos valores padrao do param
if (-not $Rota) { $Rota = Join-Path $PSScriptRoot "..\dist\roteiros\passo_combate.txt" }
if (-not $Data) { $Data = Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data" }
if ($VBlanks -le 0) {  # como o test_pd_nativo: ultimo indice x 2 + 3000
    $last = 0
    foreach ($l in [IO.File]::ReadLines($Rota)) { $t = $l.Split("#")[0].Trim(); if ($t -match "^(\d+)\s") { $last = [int64]$Matches[1] } }
    $VBlanks = $last * 2 + 3000
}
New-Item -ItemType Directory -Force $Saida | Out-Null
$falhas = 0
function Check($nome, [bool]$ok, $det = "") { Write-Host ("[{0}] {1} {2}" -f $(if ($ok) { "ok" } else { "FALHA" }), $nome, $det); if (-not $ok) { $script:falhas++ } }
foreach ($m in "legado", "nativo") {
    Remove-Item "$Saida\$m.*" -ErrorAction SilentlyContinue
    $env:BS_RELOGIO_FIXO = "1700000000"; $env:BS_INPUT_SCRIPT = (Resolve-Path $Rota).Path
    $env:BS_FRAME_LOG = "$Saida\$m.fr.tsv"; $env:BS_STATE_PASSO = "$Saida\$m.st.tsv"; $env:BS_STATE_PASSO_CADA = "1"
    try {
        cmd /c "`"$Exe`" --data `"$Data`" --audio-efeitos $m --video legado --headless $VBlanks > `"$Saida\$m.out`" 2>&1"
        $rc = $LASTEXITCODE
    } finally {
        foreach ($k in "BS_RELOGIO_FIXO", "BS_INPUT_SCRIPT", "BS_FRAME_LOG", "BS_STATE_PASSO", "BS_STATE_PASSO_CADA") { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
    }
    Check "$m termina sem erro" ($rc -eq 0) "rc=$rc"
}
$resumo = (Select-String -Path "$Saida\nativo.out" -Pattern "\[audio\] efeitos nativos: parar" | Select-Object -Last 1).Line
$m = [regex]::Match("$resumo", "tocar (\d+).*consultas (\d+); efeitos ausentes (\d+), vozes recusadas (\d+), falhas de decodificacao (\d+)")
Check "nativo: efeitos tocados, nenhum ausente, recusado ou com falha" ($m.Success -and [int]$m.Groups[1].Value -gt 0 -and
      [int]$m.Groups[3].Value -eq 0 -and [int]$m.Groups[4].Value -eq 0 -and [int]$m.Groups[5].Value -eq 0) "$resumo"
$leg = (Select-String -Path "$Saida\legado.out" -Pattern "\[audio\] efeitos nativos: parar" -Quiet)
Check "legado: o provedor nativo nao foi instalado" (-not $leg) ""
foreach ($f in "fr", "st") {
    $a = "$Saida\legado.$f.tsv"; $b = "$Saida\nativo.$f.tsv"
    $ok = (Test-Path $a) -and (Test-Path $b) -and (Get-FileHash $a).Hash -eq (Get-FileHash $b).Hash
    Check $(if ($f -eq "fr") { "quadros iguais ao legado" } else { "estado do jogo por passo igual ao legado" }) $ok $(if (Test-Path $a) { "$((Get-Content $a).Count) linhas" } else { "sem arquivo" })
}
Write-Host "$falhas falhas; saidas em $Saida"
exit $(if ($falhas) { 1 } else { 0 })
