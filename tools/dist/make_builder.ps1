# Monta a distribuicao do Builder em dist/BlueStinger-Builder: BlueStingerBuilder.exe (janela),
# bs-builder.exe (linha de comando), runtime do MSVC, licencas, instrucoes e o kit de fontes
# (runtime, app, leitor de disco, config, SDL3) que o Builder compila na maquina do usuario.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/make_builder.ps1 [-Out <pasta>] [-NoBuild]
#
# A distribuicao do Builder NAO contem nada do jogo (nem codigo gerado, nem executavel do
# jogo, nem dados do disco); a montagem confere isso antes de trocar a pasta. Uma tentativa
# que falha deixa a anterior intacta.
param(
    [string]$Out = "",
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Out) { $Out = Join-Path $repo "dist\BlueStinger-Builder" }
$Out = [IO.Path]::GetFullPath($Out).TrimEnd('\')
if ([IO.Path]::GetPathRoot($Out).TrimEnd('\') -eq $Out -or
    ($repo.TrimEnd('\') + '\').StartsWith($Out + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Write-Host "ERRO: pasta de saida recusada: $Out" -ForegroundColor Red; exit 1
}
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if ($env:BS_VCVARS) { $vcvars = $env:BS_VCVARS }

function Fail($msg) { Write-Host "ERRO: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "== $msg" }

if (-not (Test-Path $vcvars)) { Fail "Visual Studio Build Tools nao encontrado ($vcvars). Defina BS_VCVARS." }
$envLines = cmd /c "`"$vcvars`" >nul 2>nul && set"
foreach ($l in $envLines) { if ($l -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] } }
if (-not $env:VCToolsRedistDir) { Fail "VCToolsRedistDir nao definido pelo vcvars" }

$build = Join-Path $repo "build\msvc-release"
if (-not $NoBuild) {
    Step "compilando o Builder (Release)"
    Push-Location $repo
    try {
        cmake --preset msvc-release | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "configuracao do CMake falhou" }
        cmake --build --preset msvc-release --target BlueStingerBuilder bs-builder
        if ($LASTEXITCODE -ne 0) { Fail "compilacao falhou" }
    } finally { Pop-Location }
}
$sdl = Join-Path $build "_deps\sdl3-src"
foreach ($f in "builder\BlueStingerBuilder.exe", "builder\bs-builder.exe", "_deps\sdl3-src\CMakeLists.txt", "_deps\tomlplusplus-src\LICENSE") {
    if (-not (Test-Path (Join-Path $build $f))) { Fail "falta $build\$f" }
}

$stage = "$Out.tmp"
if (Test-Path -LiteralPath $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
New-Item -ItemType Directory -Force -Path $stage, "$stage\licencas", "$stage\kit" | Out-Null
Step "montando $stage"
Copy-Item (Join-Path $build "builder\BlueStingerBuilder.exe") $stage
Copy-Item (Join-Path $build "builder\bs-builder.exe") $stage
$crt = Get-ChildItem -Directory (Join-Path $env:VCToolsRedistDir "x64") -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
if (-not $crt) { Fail "runtime do MSVC (Microsoft.VC*.CRT) nao encontrado" }
Copy-Item (Join-Path $crt.FullName "*.dll") $stage

# kit: as mesmas entradas de kKitEntries (builder_core.cpp) + VERSION + SDL3 (sem o que a
# compilacao no Windows nao usa)
$kit = Join-Path $stage "kit"
# allowlist: so arquivos que o git versiona ou versionaria (rastreados + novos nao ignorados).
# O que o .gitignore exclui (game-data, generated, ISO, ...) nunca entra (revisao Codex).
$kitEntries = @("CMakeLists.txt", "LICENSE", "app", "runtime", "tools/extractor", "tools/dist/LEIA-ME.txt", "config",
                "tools/dcmap", "native", "legacy", "transitional", "tools/build")
$kitList = @(git -C $repo -c core.quotepath=off ls-files --cached --others --exclude-standard -- @kitEntries)
if ($LASTEXITCODE -ne 0 -or $kitList.Count -lt 20) { Fail "git ls-files falhou ou listou poucos arquivos ($($kitList.Count))" }
foreach ($rel in $kitList) {
    $src = Join-Path $repo $rel
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { continue }  # apagado na arvore de trabalho
    $dst = Join-Path $kit $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dst
}
$version = (git -C $repo describe --always --dirty --abbrev=7 2>$null)
if (-not $version) { $version = "desconhecida" }
[IO.File]::WriteAllText((Join-Path $kit "VERSION"), "$version`n")
$sdlKit = Join-Path $kit "third_party\SDL3"
New-Item -ItemType Directory -Force -Path $sdlKit | Out-Null
$skip = @(".git", ".github", "android-project", "android-project-ant", "Xcode", "VisualC", "VisualC-GDK", "VisualC-WinRT",
          "test", "examples", "docs")
Get-ChildItem -LiteralPath $sdl -Force | Where-Object { $skip -notcontains $_.Name } | ForEach-Object {
    Copy-Item -Recurse -LiteralPath $_.FullName -Destination (Join-Path $sdlKit $_.Name)
}

# toml++ (MIT, N3-1: o jogo le controles.toml): sem testes, documentacao e exemplos
$tomlSrc = Join-Path $build "_deps\tomlplusplus-src"
$tomlKit = Join-Path $kit "third_party\tomlplusplus"
New-Item -ItemType Directory -Force -Path $tomlKit | Out-Null
$tomlSkip = @(".git", ".github", ".circleci", "docs", "tests", "examples", "toml-test", "vendor")
Get-ChildItem -LiteralPath $tomlSrc -Force | Where-Object { $tomlSkip -notcontains $_.Name } | ForEach-Object {
    Copy-Item -Recurse -LiteralPath $_.FullName -Destination (Join-Path $tomlKit $_.Name)
}

# licencas e textos
Copy-Item -LiteralPath (Join-Path $repo "LICENSE") -Destination "$stage\licencas\BlueStingerRecompiled-MIT.txt"
Copy-Item -LiteralPath (Join-Path $sdl "LICENSE.txt") -Destination "$stage\licencas\SDL3-zlib.txt"
Copy-Item -LiteralPath (Join-Path $build "_deps\tomlplusplus-src\LICENSE") -Destination "$stage\licencas\tomlplusplus-MIT.txt"
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "LEIA-ME-BUILDER.txt") -Destination "$stage\LEIA-ME.txt"

# dependencias dos executaveis
foreach ($exe in "BlueStingerBuilder.exe", "bs-builder.exe") {
    $deps = (dumpbin /nologo /dependents (Join-Path $stage $exe)) | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] }
    foreach ($d in $deps) {
        if ($d -match '^api-ms-win-' -or (Test-Path (Join-Path $stage $d))) { continue }
        if ((Test-Path (Join-Path $env:WINDIR "System32\$d")) -and $d -notmatch '^(msvcp|vcruntime|concrt)') { continue }
        Fail "$exe depende de $d, que nao esta na distribuicao"
    }
}

# nada do jogo: nomes proibidos, assinatura do IP.BIN e trechos do 1ST_READ.BIN local
$forbidden = Get-ChildItem -LiteralPath $stage -Recurse -Force | Where-Object {
    $_.Name -match '^(1ST_READ\.BIN|IP\.BIN|generated|game-data|analysis|snapshots)$' -or $_.Extension -in ".cdi", ".gdi", ".chd", ".iso"
}
if ($forbidden) { Fail ("conteudo do jogo na distribuicao: " + ($forbidden.FullName -join ", ")) }
# (o texto "SEGA SEGAKATANA" nao serve: e o identificador de formato que o leitor de IP.BIN
# confere, presente no codigo do Builder)
$needles = @()
foreach ($spec in @(@("game-data\1ST_READ.BIN", 0x1000, 0x31000, 8), @("game-data\IP.BIN", 0x400, 0xC00, 6))) {
    $file = Join-Path $repo $spec[0]
    if (-not (Test-Path $file)) { Write-Host "aviso: sem $($spec[0]) local; conferencia so por nomes"; continue }
    $fb = [IO.File]::ReadAllBytes($file); $got = 0
    # amostras de 48 bytes com variedade (trecho de zeros ou repetido daria falso positivo)
    for ($off = $spec[1]; $off + 48 -le $fb.Length -and $got -lt $spec[3]; $off += $spec[2]) {
        $s = $fb[$off..($off + 47)]
        if (@($s | Select-Object -Unique).Count -ge 20) { $needles += ,$s; $got++ }
    }
    if ($got -lt 3) { Fail "amostras insuficientes de $($spec[0]) para a conferencia" }
}
$latin1 = [Text.Encoding]::GetEncoding(28591)
foreach ($f in Get-ChildItem -LiteralPath $stage -Recurse -File) {
    $text = $latin1.GetString([IO.File]::ReadAllBytes($f.FullName))
    foreach ($n in $needles) {
        if ($text.Contains($latin1.GetString([byte[]]$n))) { Fail "$($f.FullName) contem bytes do jogo" }
    }
}
# nenhum caminho do ambiente de desenvolvimento nos binarios
foreach ($f in Get-ChildItem $stage -File -Include *.exe, *.dll -Recurse) {
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    foreach ($t in [Text.Encoding]::ASCII.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes)) {
        foreach ($p in @($repo, $env:USERPROFILE)) {
            if ($p -and ($t.IndexOf($p, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $t.IndexOf($p.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase) -ge 0)) {
                Fail "$($f.Name) contem o caminho de desenvolvimento '$p'"
            }
        }
    }
}

# responde como esperado (sem argumentos: uso, codigo 2)
cmd /c "`"$(Join-Path $stage 'bs-builder.exe')`" >nul 2>nul"  # stderr nativo + Stop abortaria o script
if ($LASTEXITCODE -ne 2) { Fail "bs-builder.exe montado nao respondeu (codigo $LASTEXITCODE)" }
$kitFiles = Get-ChildItem -LiteralPath $kit -Recurse -File
@"
Blue Stinger Builder $version
Distribuicao: Builder (sem conteudo do jogo)
Montada em: $(Get-Date -Format 'yyyy-MM-dd HH:mm')
Runtime do MSVC: $($crt.Name) ($([IO.Path]::GetFileName($env:VCToolsRedistDir.TrimEnd('\'))))
Kit de fontes: $($kitFiles.Count) arquivos, $([math]::Round((($kitFiles | Measure-Object Length -Sum).Sum / 1MB), 1)) MB (SDL3 release-3.2.30)
"@ | Set-Content -Encoding utf8 "$stage\VERSAO.txt"

# troca segura (como em make_preview.ps1)
Step "instalando em $Out"
$old = "$Out.old"
if ((Test-Path -LiteralPath $old) -and -not (Test-Path -LiteralPath $Out)) { Rename-Item -LiteralPath $old -NewName ([IO.Path]::GetFileName($Out)) }
$backup = "$Out.old-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$hadOld = Test-Path -LiteralPath $Out
try {
    if ($hadOld) { Rename-Item -LiteralPath $Out -NewName ([IO.Path]::GetFileName($backup)) }
    Rename-Item -LiteralPath $stage -NewName ([IO.Path]::GetFileName($Out))
} catch {
    if ($hadOld -and -not (Test-Path -LiteralPath $Out) -and (Test-Path -LiteralPath $backup)) {
        Rename-Item -LiteralPath $backup -NewName ([IO.Path]::GetFileName($Out))
    }
    Fail "nao foi possivel instalar a nova distribuicao ($($_.Exception.Message)); a anterior foi mantida"
}
foreach ($d in @($backup, $old)) {
    if (Test-Path -LiteralPath $d) { try { Remove-Item -Recurse -Force -LiteralPath $d } catch { Write-Host "aviso: nao foi possivel apagar $d" } }
}
Write-Host "OK: $Out ($version)"
