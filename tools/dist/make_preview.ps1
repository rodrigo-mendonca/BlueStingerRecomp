# Monta a distribuicao de teste dist/BlueStinger-Preview a partir do codigo gerado local
# (generated/, produzido por bs-recomp a partir da copia do usuario) e do runtime.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/make_preview.ps1 [-Out <pasta>] [-NoBuild]
#
# Etapas: compila o alvo bluestinger em Release; monta uma pasta temporaria com o
# executavel, o runtime do MSVC (instalacao local permitida pela Microsoft), licencas e
# instrucoes; confere as dependencias com dumpbin e roda --version; so entao troca a
# Preview anterior. Uma tentativa que falha deixa a anterior intacta.
#
# O executavel contem codigo derivado do 1ST_READ.BIN do usuario: a pasta e para uso
# local e nao deve ser publicada (dist/ esta no .gitignore).
param(
    [string]$Out = "",
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Out) { $Out = Join-Path $repo "dist\BlueStinger-Preview" }
$Out = [IO.Path]::GetFullPath($Out).TrimEnd('\')
# a pasta de saida e trocada inteira: nunca uma raiz de volume, o repositorio ou um ancestral dele
if ([IO.Path]::GetPathRoot($Out).TrimEnd('\') -eq $Out -or
    ($repo.TrimEnd('\') + '\').StartsWith($Out + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Write-Host "ERRO: pasta de saida recusada: $Out" -ForegroundColor Red; exit 1
}
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if ($env:BS_VCVARS) { $vcvars = $env:BS_VCVARS }

function Fail($msg) { Write-Host "ERRO: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "== $msg" }

if (-not (Test-Path (Join-Path $repo "generated\generated.cmake"))) {
    Fail "falta generated/ (codigo gerado). Rode antes: bs-extract <imagem.cdi> game-data e bs-recomp config/bluestinger_us.toml"
}
if (-not (Test-Path $vcvars)) { Fail "Visual Studio Build Tools nao encontrado ($vcvars). Defina BS_VCVARS." }

# ambiente do compilador (vcvars) em variaveis desta sessao
$envLines = cmd /c "`"$vcvars`" >nul 2>nul && set"
foreach ($l in $envLines) { if ($l -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] } }
if (-not $env:VCToolsRedistDir) { Fail "VCToolsRedistDir nao definido pelo vcvars" }

if (-not $NoBuild) {
    Step "compilando (Release)"
    Push-Location $repo
    try {
        cmake --preset msvc-release -DBS_BUILD_GAME=ON | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "configuracao do CMake falhou" }
        cmake --build --preset msvc-release --target bluestinger
        if ($LASTEXITCODE -ne 0) { Fail "compilacao falhou" }
    } finally { Pop-Location }
}
$exe = Join-Path $repo "build\msvc-release\app\bluestinger.exe"
if (-not (Test-Path $exe)) { Fail "executavel nao encontrado: $exe" }

$stage = "$Out.tmp"
if (Test-Path $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
New-Item -ItemType Directory -Force -Path $stage, "$stage\licencas" | Out-Null

Step "montando $stage"
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage "BlueStinger.exe")
$crt = Get-ChildItem -Directory (Join-Path $env:VCToolsRedistDir "x64") -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
if (-not $crt) { Fail "runtime do MSVC (Microsoft.VC*.CRT) nao encontrado em $env:VCToolsRedistDir" }
Copy-Item (Join-Path $crt.FullName "*.dll") $stage

# dependencias: toda DLL que nao e do sistema precisa estar na pasta
$deps = (dumpbin /nologo /dependents (Join-Path $stage "BlueStinger.exe")) | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] }
$missing = @()
foreach ($d in $deps) {
    if ($d -match '^api-ms-win-') { continue }  # UCRT do Windows 10/11
    if (Test-Path (Join-Path $stage $d)) { continue }
    if (Test-Path (Join-Path $env:WINDIR "System32\$d")) {
        if ($d -match '^(msvcp|vcruntime|concrt)') { $missing += $d }  # runtime do MSVC: precisa ir junto
        continue
    }
    $missing += $d
}
if ($missing.Count) { Fail ("dependencias ausentes na distribuicao: " + ($missing -join ", ")) }

# nenhum caminho do ambiente de desenvolvimento dentro dos binarios (ASCII e UTF-16)
$leaks = @($repo, (Join-Path $repo "build"), $env:USERPROFILE) | Where-Object { $_ } | Select-Object -Unique
foreach ($f in Get-ChildItem $stage -File -Include *.exe, *.dll -Recurse) {
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    $texts = @([Text.Encoding]::ASCII.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes))
    foreach ($p in $leaks) {
        foreach ($t in $texts) {
            if ($t.IndexOf($p, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
                $t.IndexOf($p.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                Fail "$($f.Name) contem o caminho de desenvolvimento '$p'"
            }
        }
    }
}

# versao
$ver = (& (Join-Path $stage "BlueStinger.exe") --version 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $ver) { Fail "o executavel montado nao respondeu a --version" }
$sdlLicense = Join-Path $repo "build\msvc-release\_deps\sdl3-src\LICENSE.txt"
Copy-Item -LiteralPath (Join-Path $repo "LICENSE") -Destination "$stage\licencas\BlueStingerRecompiled-MIT.txt"
if (Test-Path $sdlLicense) { Copy-Item -LiteralPath $sdlLicense -Destination "$stage\licencas\SDL3-zlib.txt" }
@"
$ver
Distribuicao: Preview (testes locais)
Montada em: $(Get-Date -Format 'yyyy-MM-dd HH:mm')
Runtime do MSVC: $($crt.Name) ($([IO.Path]::GetFileName($env:VCToolsRedistDir.TrimEnd('\'))))
Arquivos: $((Get-ChildItem $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stage.Length + 1) }) -join ', ')
"@ | Set-Content -Encoding utf8 "$stage\VERSAO.txt"
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "LEIA-ME.txt") -Destination "$stage\LEIA-ME.txt"

# troca: a Preview anterior so sai depois que a nova esta instalada. Uma sobra .old de
# uma troca interrompida (sem $Out) e a ultima copia valida: volta a ser $Out antes de tudo.
Step "instalando em $Out"
$old = "$Out.old"
if ((Test-Path -LiteralPath $old) -and -not (Test-Path -LiteralPath $Out)) {
    Rename-Item -LiteralPath $old -NewName ([IO.Path]::GetFileName($Out))
}
$backup = "$Out.old-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$hadOld = Test-Path -LiteralPath $Out
try {
    if ($hadOld) { Rename-Item -LiteralPath $Out -NewName ([IO.Path]::GetFileName($backup)) }
    Rename-Item -LiteralPath $stage -NewName ([IO.Path]::GetFileName($Out))
} catch {
    if ($hadOld -and -not (Test-Path -LiteralPath $Out) -and (Test-Path -LiteralPath $backup)) {
        Rename-Item -LiteralPath $backup -NewName ([IO.Path]::GetFileName($Out))  # restaura a anterior
    }
    Fail "nao foi possivel instalar a nova Preview ($($_.Exception.Message)); a anterior foi mantida"
}
# a nova esta no lugar: so agora as copias antigas podem sair (falha aqui nao perde nada)
foreach ($d in @($backup, $old)) {
    if (Test-Path -LiteralPath $d) {
        try { Remove-Item -Recurse -Force -LiteralPath $d } catch { Write-Host "aviso: nao foi possivel apagar $d" }
    }
}
Write-Host "OK: $Out"
Write-Host $ver
