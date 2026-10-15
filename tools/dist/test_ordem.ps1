# N0: trace de ordem do runtime legado (docs/PLANO-PORT-NATIVO.md secao 5).
# Roda o mesmo roteiro duas vezes com BS_EVENT_TRACE e exige traces identicos (a ordem de
# VBlank, interrupcoes, continuacoes, trocas/criacao de threads e longjmp e deterministica),
# mais propriedades estruturais. O trace aceito fica em -Saida como referencia para o N2.
#   powershell -File tools/dist/test_ordem.ps1 [-Exe <bluestinger.exe>] [-Data <data>] [-VBlanks 3000]
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$VBlanks = 3000,
    [string]$Saida = (Join-Path $env:TEMP "bs-ordem-teste")
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "SKIP: executavel ausente ($Exe)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$marker = Join-Path $Saida ".bs-ordem-teste"
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
function Run($n) {
    $trace = Join-Path $Saida "ordem_$n.tsv"
    $env:BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\newgame_area1.txt")
    $env:BS_EVENT_TRACE = $trace
    try { $out = cmd /c "`"$Exe`" --data `"$Data`" --headless $VBlanks 2>&1" | Out-String }
    finally { $env:BS_INPUT_SCRIPT = ""; $env:BS_EVENT_TRACE = "" }
    return @{ Code = $LASTEXITCODE; Trace = $trace; Out = $out }
}

$a = Run 1
$b = Run 2
Check "execucao 1 termina" ($a.Code -eq 0) "rc=$($a.Code)"
Check "execucao 2 termina" ($b.Code -eq 0) "rc=$($b.Code)"
Check "trace gravado" ((Test-Path -LiteralPath $a.Trace) -and (Test-Path -LiteralPath $b.Trace))
if (Test-Path -LiteralPath $a.Trace) {
    $ha = (Get-FileHash -LiteralPath $a.Trace -Algorithm SHA256).Hash
    $hb = if (Test-Path -LiteralPath $b.Trace) { (Get-FileHash -LiteralPath $b.Trace -Algorithm SHA256).Hash } else { "" }
    Check "ordem identica nas duas execucoes" ($ha -eq $hb) $ha.Substring(0, 16)
    $rows = Get-Content -LiteralPath $a.Trace | Where-Object { $_ -notmatch "^(#|seq)" } | ForEach-Object { $_.Split("`t") }
    $lines = Get-Content -LiteralPath $a.Trace | Where-Object { $_ -notmatch "^(#|seq)" }
    $kinds = @{}
    foreach ($l in $lines) { $k = $l.Split("`t")[2]; $kinds[$k] = 1 + [int]$kinds[$k] }
    Check "um V por VBlank" ([int]$kinds["V"] -ge $VBlanks - 2 -and [int]$kinds["V"] -le $VBlanks + 2) "V=$($kinds['V'])"
    Check "interrupcoes registradas" ([int]$kinds["I"] -gt [int]$kinds["V"]) "I=$($kinds['I'])"
    Check "threads do guest criadas" ([int]$kinds["N"] -ge 1) "N=$($kinds['N']) T=$($kinds['T']) R=$($kinds['R']) J=$($kinds['J'])"
    $cycles = $lines | ForEach-Object { [uint64]$_.Split("`t")[1] }
    $mono = $true
    for ($i = 1; $i -lt $cycles.Count; ++$i) { if ($cycles[$i] -lt $cycles[$i - 1]) { $mono = $false; break } }
    Check "ciclos nao decrescentes" $mono
}
$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas; traces em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
