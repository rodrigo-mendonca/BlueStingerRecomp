# Regressao completa, sequencial e RETOMAVEL (so desenvolvimento).
#
#   powershell -File tools/dev/regressao.ps1 [-Disc <img.cdi>] [-Estado <pasta>] [-Novo] [-Desde <etapa>]
#                                            [-Ate <etapa>] [-Listar]
#
# Cada etapa concluida fica em <Estado>\estado.tsv com o codigo de saida, a duracao e a impressao
# digital do codigo-fonte (HEAD, diff dos arquivos versionados e conteudo dos nao versionados). Pode
# ser interrompida a qualquer momento (Ctrl+C, fechar a janela, desligar): rodar de novo continua da
# primeira etapa que nao terminou com sucesso. A etapa interrompida e refeita do inicio (as suites
# limpam a propria pasta de saida).
#   - fonte mudou desde o estado salvo: o estado e descartado e tudo roda de novo (os builds e as
#     suites dependem do codigo);
#   - etapa com falha: roda de novo na retomada (as seguintes tambem rodam);
#   - -Novo: descarta o estado; -Desde <etapa>: refaz a partir dela; -Ate <etapa>: para depois dela;
#   - -Listar: mostra as etapas e o estado salvo, sem rodar nada.
# Os builds rodam sempre (sao incrementais e baratos) e sao pre-requisito: se falharem, para.
# make_builder e make_preview tambem param a regressao se falharem (as suites dependem deles).
# Log legivel: <Estado>\regressao.log; saida completa de cada etapa: <Estado>\<etapa>.txt.
param(
    [string]$Disc = (Join-Path $PSScriptRoot "..\..\ISO\Blue Stinger.cdi"),
    [string]$Estado = (Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\regressao"),
    [switch]$Novo,
    [string]$Desde = "",
    [string]$Ate = "",
    [switch]$Listar
)
$ErrorActionPreference = "Continue"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$dist = Join-Path $repo "tools\dist"
New-Item -ItemType Directory -Force $Estado | Out-Null
$stateFile = Join-Path $Estado "estado.tsv"
$log       = Join-Path $Estado "regressao.log"

# Ambiente do Visual Studio para os builds e o ctest (o mesmo vcvars64 que o CLAUDE.md pede).
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars  = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -property installationPath 2>$null
    if ($vs) { $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat" }
}
if (-not $vcvars -or -not (Test-Path $vcvars)) {
    $vcvars = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio", "$env:ProgramFiles\Microsoft Visual Studio" -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue -Depth 6 |
              Select-Object -First 1 -ExpandProperty FullName
}

# Etapas: nome, tipo (build | cmd | suite), comando/argumentos, para a regressao se falhar
$steps = @(
    @{ N = "build_release"; T = "build"; C = "cmake --build --preset msvc-release"; Stop = $true },
    @{ N = "build_debug";   T = "build"; C = "cmake --build --preset msvc-debug"; Stop = $true },
    @{ N = "build_mods";    T = "build"; C = "cmake --build build\msvc-release-mods"; Stop = $true },
    @{ N = "build_watch";   T = "build"; C = "cmake --build build\msvc-release-watch --target bluestinger"; Stop = $true },
    @{ N = "unitarios";     T = "cmd";   C = "build\msvc-debug\tests\bs_unit_tests.exe" },
    @{ N = "ctest";         T = "cmd";   C = "ctest --preset msvc-debug" },
    @{ N = "ctest_disco";   T = "cmd";   C = "ctest --preset msvc-debug"; Disc = $true },
    @{ N = "make_builder";  T = "suite"; A = @("$dist\make_builder.ps1"); Stop = $true },
    @{ N = "make_preview";  T = "suite"; A = @("$dist\make_preview.ps1", "-Disc", $Disc); Stop = $true },
    @{ N = "test_preview";  T = "suite"; A = @("$dist\test_preview.ps1") },
    @{ N = "test_gameplay"; T = "suite"; A = @("$dist\test_gameplay.ps1") },
    @{ N = "test_builder";  T = "suite"; A = @("$dist\test_builder.ps1", "-Disc", $Disc) },
    @{ N = "test_projeto";  T = "suite"; A = @("$dist\test_projeto.ps1", "-Disc", $Disc) },
    @{ N = "test_arquivos"; T = "suite"; A = @("$dist\test_arquivos.ps1") },
    @{ N = "test_ordem";    T = "suite"; A = @("$dist\test_ordem.ps1") },
    @{ N = "test_tempo";    T = "suite"; A = @("$dist\test_tempo.ps1") },
    @{ N = "test_n2b";      T = "suite"; A = @("$dist\test_n2b.ps1") },
    @{ N = "test_cri";      T = "suite"; A = @("$dist\test_cri.ps1") },
    @{ N = "test_efeito";   T = "suite"; A = @("$dist\test_efeito.ps1") },
    @{ N = "test_n2b2";     T = "suite"; A = @("$dist\test_n2b2.ps1") },
    @{ N = "test_assistido"; T = "suite"; A = @("$dist\test_assistido.ps1") },
    @{ N = "test_controles"; T = "suite"; A = @("$dist\test_controles.ps1") },
    @{ N = "test_pd_nativo"; T = "suite"; A = @("$dist\test_pd_nativo.ps1") }
)
$names = $steps | ForEach-Object { $_.N }
foreach ($x in @($Desde, $Ate)) { if ($x -and $names -notcontains $x) { Write-Host "ERRO: etapa desconhecida '$x'. Etapas: $($names -join ', ')"; exit 2 } }

# Impressao digital do codigo-fonte: HEAD + diff dos versionados + conteudo dos nao versionados
# (respeita o .gitignore: generated/, analysis/, build/ ficam de fora). Documentacao (*.md) e este
# executor nao entram: nao mudam o que e testado, e editar o STATUS.md no meio nao invalida a regressao.
function Fingerprint {
    Push-Location $repo
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        $parts = New-Object System.Text.StringBuilder
        [void]$parts.AppendLine((git rev-parse HEAD))
        [void]$parts.AppendLine((git diff HEAD --binary -- . ":(exclude)*.md" ":(exclude)tools/dev/regressao.ps1" | Out-String))
        foreach ($f in (git ls-files --others --exclude-standard | Where-Object { $_ -notmatch "\.md$" -and $_ -ne "tools/dev/regressao.ps1" })) {
            $h = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash
            [void]$parts.AppendLine("$f $h")
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes($parts.ToString())
        return ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace "-", "").Substring(0, 16)
    } finally { Pop-Location }
}

function LoadState {
    $s = @{}
    if (Test-Path $stateFile) {
        foreach ($l in Get-Content $stateFile) {
            $c = $l.Split("`t")
            if ($c.Count -ge 5 -and $c[0] -ne "etapa") { $s[$c[0]] = @{ Rc = [int]$c[1]; Seg = $c[2]; Fim = $c[3]; Fp = $c[4] } }
        }
    }
    return $s
}
function SaveStep($name, $rc, $sec, $fp) {
    if (-not (Test-Path $stateFile)) { "etapa`trc`tsegundos`tfim`tfonte" | Out-File $stateFile -Encoding utf8 }
    # grava ao terminar a etapa: uma interrupcao no meio deixa a etapa sem registro (refeita)
    "$name`t$rc`t$sec`t$(Get-Date -Format s)`t$fp" | Out-File $stateFile -Append -Encoding utf8
}
function Log($text) { Write-Host $text; $text | Out-File $log -Append -Encoding utf8 }

$fp    = Fingerprint
$state = LoadState
if ($Listar) {
    Write-Host "fonte atual: $fp; estado: $stateFile"
    foreach ($st in $steps) {
        $e = $state[$st.N]
        $desc = if (-not $e) { "pendente" } elseif ($e.Fp -ne $fp) { "fonte diferente (refazer)" } elseif ($e.Rc -eq 0) { "ok ($($e.Seg)s, $($e.Fim))" } else { "FALHOU rc=$($e.Rc) ($($e.Fim))" }
        Write-Host ("  {0,-16} {1}" -f $st.N, $desc)
    }
    exit 0
}
$stale = @($state.Values | Where-Object { $_.Fp -ne $fp }).Count -gt 0
if ($Novo -or $stale) {
    if ($stale -and -not $Novo) { Log "fonte mudou desde o estado salvo: recomecando do zero" }
    Remove-Item -LiteralPath $stateFile -ErrorAction SilentlyContinue
    $state = @{}
}
if ($Desde) {
    $i = [array]::IndexOf($names, $Desde)
    foreach ($st in $steps[$i..($steps.Count - 1)]) { $state.Remove($st.N) }
}
if (Get-Process bluestinger -ErrorAction SilentlyContinue) {
    Log "AVISO: ha processos bluestinger rodando (de uma execucao interrompida?); as suites podem disputar CPU"
}
Log "== regressao $(Get-Date -Format s), fonte $fp"

$failed = 0
foreach ($st in $steps) {
    $prev = $state[$st.N]
    if ($st.T -ne "build" -and $prev -and $prev.Rc -eq 0) {
        Log ("-- {0} ja aprovada ({1}s, {2}): pulada" -f $st.N, $prev.Seg, $prev.Fim)
        if ($Ate -and $st.N -eq $Ate) { break }
        continue
    }
    $outFile = Join-Path $Estado "$($st.N).txt"
    $t0 = Get-Date
    if ($st.T -eq "suite") {
        $o  = & powershell -NoProfile -ExecutionPolicy Bypass -File @($st.A) 2>&1 | Out-String
        $rc = $LASTEXITCODE
    } else {
        $prefix = if ($st.Disc) { "set `"BS_DISC_IMAGE=$((Resolve-Path $Disc).Path)`"&& " } else { "" }
        $o  = cmd /c "chcp 65001 >nul & call `"$vcvars`" >nul && cd /d `"$repo`" && $prefix$($st.C) 2>&1" | Out-String
        $rc = $LASTEXITCODE
    }
    $sec = [int]((Get-Date) - $t0).TotalSeconds
    [IO.File]::WriteAllText($outFile, $o)
    $summary = ($o -split "`r?`n" | Where-Object { $_ -match "FALHA|FAIL|casos|falhas|SKIP|ERRO|tests passed|error |FAILED" } | Select-Object -Last 8) -join "`n"
    if ($st.N -eq "unitarios") { $summary = "ok=$(([regex]::Matches($o, '(?m)^\[ ok \]')).Count)" }
    Log ("== {0} rc={1} ({2}s)`n{3}" -f $st.N, $rc, $sec, $summary)
    if ($st.T -ne "build") { SaveStep $st.N $rc $sec $fp }
    if ($rc -ne 0) {
        $failed++
        if ($st.Stop) { Log "PARADO: $($st.N) falhou (pre-requisito das etapas seguintes)"; exit 1 }
    }
    if ($Ate -and $st.N -eq $Ate) { break }
}
Log "== fim $(Get-Date -Format s): $failed etapa(s) com falha"
exit $(if ($failed) { 1 } else { 0 })
