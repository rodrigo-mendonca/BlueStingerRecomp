# Testa a troca da Preview em make_preview.ps1 (sem recompilar: -NoBuild usa o Release ja
# compilado). Casos: instalacao nova; troca bloqueada (arquivo aberto na Preview anterior)
# falha com codigo 1 e mantem a anterior; sobra .old de uma troca interrompida volta a ser
# a Preview; saida recusada (raiz de volume, repositorio).
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_make_preview.ps1
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$make = Join-Path $PSScriptRoot "make_preview.ps1"
$work = Join-Path $env:TEMP "bs-make-preview-teste"
$marker = Join-Path $work ".bs-make-preview-teste"
if (Test-Path -LiteralPath $work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $work existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $work
}
New-Item -ItemType Directory -Path $work | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
$out = Join-Path $work "Prévia ç"

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function Make($o) {
    $null = & powershell -NoProfile -ExecutionPolicy Bypass -File $make -NoBuild -Out $o 2>&1
    return $LASTEXITCODE
}

# 1. instalacao nova
$rc = Make $out
Check "instalacao nova" ($rc -eq 0 -and (Test-Path -LiteralPath (Join-Path $out "BlueStinger.exe"))) "codigo $rc"

# 2. troca bloqueada: um arquivo aberto impede renomear a Preview anterior
$sentinel = Join-Path $out "sentinela.txt"
Set-Content -LiteralPath $sentinel -Value "anterior"
$h = [IO.File]::Open($sentinel, 'Open', 'Read', 'None')
try { $rc = Make $out } finally { $h.Close() }
$left = @(Get-ChildItem -LiteralPath $work -Directory | Where-Object { $_.Name -ne "Prévia ç" })
Check "troca bloqueada falha e mantem a anterior" ($rc -ne 0 -and (Test-Path -LiteralPath $sentinel) -and (Test-Path -LiteralPath (Join-Path $out "BlueStinger.exe"))) "codigo $rc; sobras: $($left.Name -join ', ')"

# 3. sobra .old sem a Preview (troca interrompida): volta a ser a Preview e a nova entra
Rename-Item -LiteralPath $out -NewName "Prévia ç.old"
$rc = Make $out
Check "sobra .old recuperada" ($rc -eq 0 -and (Test-Path -LiteralPath (Join-Path $out "BlueStinger.exe")) -and -not (Test-Path -LiteralPath "$out.old") -and -not (Test-Path -LiteralPath $sentinel)) "codigo $rc"

# 4. saidas recusadas antes de qualquer remocao
$rc = Make ([IO.Path]::GetPathRoot($repo))
Check "raiz de volume recusada" ($rc -ne 0) "codigo $rc"
$rc = Make $repo
Check "repositorio recusado" ($rc -ne 0 -and (Test-Path -LiteralPath (Join-Path $repo "CMakeLists.txt"))) "codigo $rc"

Remove-Item -Recurse -Force -LiteralPath $work
$fails = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas" -f $results.Count, $fails)
exit $(if ($fails) { 1 } else { 0 })
