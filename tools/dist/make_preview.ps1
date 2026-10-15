# Monta a distribuicao de teste local dist/BlueStinger-Preview com o proprio Builder
# (bs-builder, kit = este repositorio): extrai e confere os dados do CDI, gera o C++,
# compila, verifica o executavel SEM o CDI e troca a Preview anterior (que fica em
# dist/BlueStinger-Preview.anterior para rollback).
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/make_preview.ps1 -Disc <imagem.cdi> [-Out <pasta>] [-Clean]
#
# A Preview contem o executavel e a pasta data\ derivados da copia do usuario: e para uso
# local e nao deve ser publicada (dist/ esta no .gitignore). Depois de montada, nao precisa
# do CDI.
param(
    [Parameter(Mandatory = $true)][string]$Disc,
    [string]$Out = "",
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Out) { $Out = Join-Path $repo "dist\BlueStinger-Preview" }
$Out = [IO.Path]::GetFullPath($Out).TrimEnd('\')
# a pasta e trocada inteira: nunca uma raiz de volume, o repositorio ou um ancestral dele
if ([IO.Path]::GetPathRoot($Out).TrimEnd('\') -eq $Out -or
    ($repo.TrimEnd('\') + '\').StartsWith($Out + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Write-Host "ERRO: pasta de saida recusada: $Out" -ForegroundColor Red; exit 1
}
$Disc = (Resolve-Path -LiteralPath $Disc).Path
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if ($env:BS_VCVARS) { $vcvars = $env:BS_VCVARS }

function Fail($msg) { Write-Host "ERRO: $msg" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $vcvars)) { Fail "Visual Studio Build Tools nao encontrado ($vcvars). Defina BS_VCVARS." }
$envLines = cmd /c "`"$vcvars`" >nul 2>nul && set"
foreach ($l in $envLines) { if ($l -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] } }

Write-Host "== compilando o bs-builder (Release)"
Push-Location $repo
try {
    cmake --preset msvc-release | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "configuracao do CMake falhou" }
    cmake --build --preset msvc-release --target bs-builder
    if ($LASTEXITCODE -ne 0) { Fail "compilacao do bs-builder falhou" }
} finally { Pop-Location }
$sdl = Join-Path $repo "build\msvc-release\_deps\sdl3-src"
if (-not (Test-Path (Join-Path $sdl "CMakeLists.txt"))) { Fail "fontes do SDL3 nao encontradas em $sdl" }

Write-Host "== Builder: $Disc -> $Out"
$builderArgs = @("--disc", $Disc, "--out", (Split-Path $Out), "--name", (Split-Path $Out -Leaf), "--kit", $repo, "--sdl3", $sdl)
$label = (git -C $repo describe --always --dirty --abbrev=7 2>$null)
if ($label) { $builderArgs += @("--version-label", "$label") }
if ($Clean) { $builderArgs += "--clean" }
& (Join-Path $repo "build\msvc-release\builder\bs-builder.exe") @builderArgs
if ($LASTEXITCODE -ne 0) { Fail "o Builder falhou (codigo $LASTEXITCODE); a Preview anterior foi mantida" }

# nenhum caminho do ambiente de desenvolvimento nos binarios (os dados nao entram na busca)
foreach ($f in Get-ChildItem -LiteralPath $Out -File -Include *.exe, *.dll -Recurse | Where-Object { $_.FullName -notlike "*\data\*" }) {
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    foreach ($t in [Text.Encoding]::ASCII.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes)) {
        foreach ($p in @($repo, $env:USERPROFILE)) {
            if ($p -and ($t.IndexOf($p, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $t.IndexOf($p.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase) -ge 0)) {
                Fail "$($f.Name) contem o caminho de desenvolvimento '$p'"
            }
        }
    }
}
Write-Host "OK: $Out"
Get-Content -LiteralPath (Join-Path $Out "VERSAO.txt") | Select-Object -First 1
