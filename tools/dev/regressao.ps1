# Regressao completa, sequencial e RETOMAVEL (so desenvolvimento).
#
#   powershell -File tools/dev/regressao.ps1 [-Disc <img.cdi>] [-Estado <pasta>] [-Novo] [-Desde <etapa>]
#                                            [-Ate <etapa>] [-Listar]
#
# Cada etapa concluida fica em <Estado>\estado.tsv com o codigo de saida, a duracao e a CHAVE da etapa:
# a impressao digital do codigo-fonte (HEAD, diff dos versionados e conteudo dos nao versionados) SEM os
# scripts das suites de teste, mais o conteudo do script da propria etapa. Pode ser interrompida a
# qualquer momento (Ctrl+C, fechar a janela, desligar): rodar de novo continua da primeira etapa que nao
# terminou com sucesso. A etapa interrompida e refeita do inicio (as suites limpam a propria pasta).
#   - chave da etapa mudou: a etapa roda de novo. Codigo, Python, roteiros, configuracao e os produtores
#     (make_builder/make_preview, que compilam o repositorio como kit) entram na chave de TODAS as etapas:
#     qualquer mudanca neles refaz tudo. Editar so o script de uma suite (test_*.ps1, n5_e001.ps1) refaz
#     so aquela suite (os scripts das suites nao se incluem uns aos outros);
#   - etapa com falha: roda de novo na retomada (as seguintes tambem rodam);
#   - -Novo: descarta o estado; -Desde <etapa>: refaz a partir dela; -Ate <etapa>: para depois dela;
#   - -Listar: mostra as etapas e o estado salvo, sem rodar nada.
# Uma regressao por vez: <Estado>\trava.txt guarda o PID; outra execucao com o mesmo estado e recusada
# enquanto esse processo viver. <Estado>\ativo.txt mostra a etapa em andamento (nome, inicio, PID).
# No fim, a tabela de duracoes das etapas que rodaram.
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
# (BS_REGRESSAO_ETAPAS=<arquivo.ps1> troca a lista: so para o teste sintetico do executor)
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
    @{ N = "test_pd_nativo"; T = "suite"; A = @("$dist\test_pd_nativo.ps1") },
    @{ N = "test_saves";    T = "suite"; A = @("$dist\test_saves.ps1") },
    # N5: o filme nativo no jogo (E001: conversao so dele, rotas legado x nativo, START medido)
    @{ N = "n5_e001";       T = "suite"; A = @("$repo\tools\dev\n5_e001.ps1") }
)
if ($env:BS_REGRESSAO_ETAPAS) { $steps = & $env:BS_REGRESSAO_ETAPAS $repo }
$names = $steps | ForEach-Object { $_.N }
foreach ($x in @($Desde, $Ate)) { if ($x -and $names -notcontains $x) { Write-Host "ERRO: etapa desconhecida '$x'. Etapas: $($names -join ', ')"; exit 2 } }

# Impressao digital do codigo-fonte: HEAD + diff dos versionados + conteudo dos nao versionados
# (respeita o .gitignore: generated/, analysis/, build/ ficam de fora). Documentacao (*.md) e este
# executor nao entram: nao mudam o que e testado, e editar o STATUS.md no meio nao invalida a regressao.
# Os scripts das suites consumidoras (etapas "suite" sem Stop) tambem ficam de fora: cada um entra so na
# chave da propria etapa (StepKey). HEAD entra inteiro: um commit refaz tudo.
function SuiteScripts {
    $list = @()
    foreach ($st in $steps) {
        if ($st.T -eq "suite" -and -not $st.Stop) {
            $full = (Resolve-Path -LiteralPath $st.A[0] -ErrorAction SilentlyContinue).Path
            if ($full -and $full.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
                $list += $full.Substring($repo.Length).TrimStart('\').Replace('\', '/')
            }
        }
    }
    return $list
}
function Fingerprint {
    Push-Location $repo
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        $parts = New-Object System.Text.StringBuilder
        $skip = @("tools/dev/regressao.ps1") + @(SuiteScripts)
        $excl = @($skip | ForEach-Object { ":(exclude)$_" })
        [void]$parts.AppendLine((git rev-parse HEAD))
        [void]$parts.AppendLine((git diff HEAD --binary -- . ":(exclude)*.md" @excl | Out-String))
        foreach ($f in (git ls-files --others --exclude-standard | Where-Object { $_ -notmatch "\.md$" -and $skip -notcontains $_ })) {
            $h = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash
            [void]$parts.AppendLine("$f $h")
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes($parts.ToString())
        return ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace "-", "").Substring(0, 16)
    } finally { Pop-Location }
}

# Chave da etapa: a base + o conteudo do script da propria suite (build/cmd so dependem da base)
function StepKey($st, $base) {
    if ($st.T -ne "suite") { return $base }
    $f = $st.A[0]
    $h = if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.Substring(0, 16) } else { "ausente" }
    return "$base-$h"
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
$keys  = @{}
foreach ($st in $steps) { $keys[$st.N] = StepKey $st $fp }
if ($Listar) {
    Write-Host "fonte atual (base): $fp; estado: $stateFile"
    foreach ($st in $steps) {
        $e = $state[$st.N]
        $desc = if (-not $e) { "pendente" } elseif ($e.Fp -ne $keys[$st.N]) { "chave diferente (refazer)" } elseif ($e.Rc -eq 0) { "ok ($($e.Seg)s, $($e.Fim))" } else { "FALHOU rc=$($e.Rc) ($($e.Fim))" }
        Write-Host ("  {0,-16} {1}" -f $st.N, $desc)
    }
    exit 0
}
# uma regressao por vez neste estado
$lock = Join-Path $Estado "trava.txt"
if (Test-Path $lock) {
    $other = (Get-Content $lock -ErrorAction SilentlyContinue | Select-Object -First 1) -as [int]
    $p = if ($other) { Get-Process -Id $other -ErrorAction SilentlyContinue } else { $null }
    if ($p -and $other -ne $PID) { Write-Host "ERRO: outra regressao (PID $other) usa $Estado; espere ou encerre-a"; exit 3 }
}
"$PID" | Out-File $lock -Encoding ascii
# etapas cuja chave mudou saem do estado (rodam de novo); as outras aprovadas continuam valendo
$changed = @($steps | Where-Object { $state[$_.N] -and $state[$_.N].Fp -ne $keys[$_.N] } | ForEach-Object { $_.N })
if ($Novo) {
    Remove-Item -LiteralPath $stateFile -ErrorAction SilentlyContinue
    $state = @{}
} elseif ($changed.Count) {
    Log "chave mudou (refazer): $($changed -join ', ')"
    foreach ($n in $changed) { $state.Remove($n) }
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
$durations = @()
$ativo = Join-Path $Estado "ativo.txt"
foreach ($st in $steps) {
    $prev = $state[$st.N]
    if ($st.T -ne "build" -and $prev -and $prev.Rc -eq 0) {
        Log ("-- {0} ja aprovada ({1}s, {2}): pulada" -f $st.N, $prev.Seg, $prev.Fim)
        if ($Ate -and $st.N -eq $Ate) { break }
        continue
    }
    $outFile = Join-Path $Estado "$($st.N).txt"
    $t0 = Get-Date
    "$($st.N)`t$(Get-Date -Format s)`tPID $PID" | Out-File $ativo -Encoding utf8
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
    $durations += ,@($st.N, $sec, $rc)
    if ($st.T -ne "build") { SaveStep $st.N $rc $sec $keys[$st.N] }
    if ($rc -ne 0) {
        $failed++
        if ($st.Stop) {
            Log "PARADO: $($st.N) falhou (pre-requisito das etapas seguintes)"
            Remove-Item -LiteralPath $ativo, $lock -ErrorAction SilentlyContinue
            exit 1
        }
    }
    if ($Ate -and $st.N -eq $Ate) { break }
}
Remove-Item -LiteralPath $ativo, $lock -ErrorAction SilentlyContinue
if ($durations.Count) {
    $tot = ($durations | ForEach-Object { $_[1] } | Measure-Object -Sum).Sum
    Log ("-- duracoes (s): " + (($durations | ForEach-Object { "{0}={1}{2}" -f $_[0], $_[1], $(if ($_[2]) { "(rc=$($_[2]))" } else { "" }) }) -join ", ") + "; total $tot")
}
Log "== fim $(Get-Date -Format s): $failed etapa(s) com falha"
exit $(if ($failed) { 1 } else { 0 })
