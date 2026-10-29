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
# Limite de tempo por etapa (Max, segundos; padrao: build 1 h, cmd 30 min, suite 2 h, as 3 suites mais longas
# 3 h, cerca de 3x a maior duracao medida): estourou, a arvore de processos e encerrada e a etapa falha com
# rc=124 (refeita na retomada). Durante a etapa, <etapa>.txt cresce e ativo.txt traz o tempo e o tamanho da
# saida a cada 30 s (batimento). BS_REGRESSAO_LIMITE_S troca todos os limites (teste sintetico).
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
    # N8.2: o build de validacao (legado, modelos e sondas; as suites do legado e a bateria H usam este exe)
    @{ N = "build_validacao"; T = "build"; C = "cmake --build --preset msvc-validacao"; Stop = $true },
    @{ N = "build_mods";    T = "build"; C = "cmake --build build\msvc-release-mods"; Stop = $true },
    @{ N = "build_watch";   T = "build"; C = "cmake --build build\msvc-release-watch --target bluestinger"; Stop = $true },
    @{ N = "unitarios";     T = "cmd";   C = "build\msvc-debug\tests\bs_unit_tests.exe" },
    @{ N = "ctest";         T = "cmd";   C = "ctest --preset msvc-debug" },
    @{ N = "ctest_disco";   T = "cmd";   C = "ctest --preset msvc-debug"; Disc = $true },
    @{ N = "make_builder";  T = "suite"; A = @("$dist\make_builder.ps1"); Stop = $true },
    @{ N = "make_preview";  T = "suite"; A = @("$dist\make_preview.ps1", "-Disc", $Disc); Stop = $true },
    @{ N = "test_preview";  T = "suite"; A = @("$dist\test_preview.ps1") },
    # N8.0: as 16 referencias no exe da Preview (com mods\ ao lado), com as 179 excecoes de SPC aprovadas
    # N8.7: as referencias do N8.0 sao anteriores a fog por tabela e aos volumes: conferidas com os dois desligados; os
    # quadros com os efeitos tem o conjunto proprio (referencias_n87, capturado com o exe do N8.7, sem excecoes)
    @{ N = "referencias";   T = "cmd";   C = "python tools\dev\n8_referencias.py conferir --exe dist\BlueStinger-Preview\BlueStinger.exe --ref analysis\n8\referencias --excecoes config\n8_1_excecoes_spc.toml --paralelo 4 --ambiente BS_RENDER_SEM_FOG_VOLUMES=1"; Max = 7200 },
    @{ N = "referencias_n87"; T = "cmd"; C = "python tools\dev\n8_referencias.py conferir --exe dist\BlueStinger-Preview\BlueStinger.exe --ref analysis\n8\referencias_n87 --paralelo 4"; Max = 7200 },
    @{ N = "test_gameplay"; T = "suite"; A = @("$dist\test_gameplay.ps1") },
    @{ N = "test_builder";  T = "suite"; A = @("$dist\test_builder.ps1", "-Disc", $Disc) },
    @{ N = "test_projeto";  T = "suite"; A = @("$dist\test_projeto.ps1", "-Disc", $Disc) },
    @{ N = "test_arquivos"; T = "suite"; A = @("$dist\test_arquivos.ps1") },
    @{ N = "test_ordem";    T = "suite"; A = @("$dist\test_ordem.ps1") },
    @{ N = "test_tempo";    T = "suite"; A = @("$dist\test_tempo.ps1") },
    @{ N = "test_n2b";      T = "suite"; A = @("$dist\test_n2b.ps1"); Max = 10800 },
    @{ N = "test_cri";      T = "suite"; A = @("$dist\test_cri.ps1") },
    @{ N = "test_efeito";   T = "suite"; A = @("$dist\test_efeito.ps1") },
    @{ N = "test_n2b2";     T = "suite"; A = @("$dist\test_n2b2.ps1") },
    @{ N = "test_assistido"; T = "suite"; A = @("$dist\test_assistido.ps1") },
    @{ N = "test_controles"; T = "suite"; A = @("$dist\test_controles.ps1"); Max = 10800 },
    @{ N = "test_pd_nativo"; T = "suite"; A = @("$dist\test_pd_nativo.ps1"); Max = 10800 },
    @{ N = "test_saves";    T = "suite"; A = @("$dist\test_saves.ps1") },
    # N5: o filme nativo no jogo (E001: conversao so dele, rotas legado x nativo, START medido)
    @{ N = "n5_e001";       T = "suite"; A = @("$repo\tools\dev\n5_e001.ps1") },
    # N8: produto nativo x validacao (test_n8_nativo) e a bateria H do N6: a validacao nos casos com o legado, o H4 em
    # etapa propria e o H9 (tudo nativo, em tempo real) na Preview
    @{ N = "test_n8_nativo"; T = "suite"; A = @("$dist\test_n8_nativo.ps1"); Max = 10800 },
    @{ N = "n6_h";          T = "suite"; A = @("$repo\tools\dev\n6_h.ps1", "-Exe", "$repo\build\msvc-validacao\app\bluestinger.exe", "-ExeMods", "$repo\build\msvc-release-mods\app\bluestinger.exe", "-Data", "$repo\dist\BlueStinger-Preview\data", "-Build", "$repo\build\msvc-release", "-Casos", "H2,H3,H5,H6,H7,H8"); Max = 10800 },
    @{ N = "n6_h4";         T = "suite"; A = @("$repo\tools\dev\n6_h.ps1", "-Exe", "$repo\build\msvc-validacao\app\bluestinger.exe", "-Data", "$repo\dist\BlueStinger-Preview\data", "-Build", "$repo\build\msvc-release", "-Casos", "H4", "-Saida", "$env:TEMP\bs-n6-h4") },
    @{ N = "n6_h9";         T = "suite"; A = @("$repo\tools\dev\n6_h.ps1", "-Exe", "$repo\dist\BlueStinger-Preview\BlueStinger.exe", "-Casos", "H9", "-Saida", "$env:TEMP\bs-n6-h9") }
)
if ($env:BS_REGRESSAO_ETAPAS) { $steps = & $env:BS_REGRESSAO_ETAPAS $repo $Disc }
# revisao Codex 20261028-012302: esquema das etapas (T/C/N texto, A lista de textos, Disc/Stop booleanos, Max inteiro);
# um "False" ou "0" textual mudaria o comportamento sem mudar a chave
foreach ($st in $steps) {
    $bad = @()
    foreach ($k in "N", "T", "C") { if ($null -ne $st[$k] -and $st[$k] -isnot [string]) { $bad += $k } }
    if ($st.T -notin "build", "cmd", "suite") { $bad += "T" }
    if ($null -ne $st.A -and @($st.A | Where-Object { $_ -isnot [string] }).Count) { $bad += "A" }
    foreach ($k in "Disc", "Stop") { if ($null -ne $st[$k] -and $st[$k] -isnot [bool]) { $bad += $k } }
    if ($null -ne $st.Max -and $st.Max -isnot [int]) { $bad += "Max" }
    if ($bad.Count) { Write-Host "ERRO: etapa '$($st.N)' com tipo invalido em: $($bad -join ', ')"; exit 2 }
}
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

# Chave da etapa: a base + a definicao da etapa (tipo, comando, argumentos, disco, limite, parada; revisao Codex
# 20261028-011732: etapas que usam o mesmo script com argumentos diferentes, ou um argumento editado aqui, nao reaproveitam
# a aprovacao) + o conteudo do script da propria suite
# Forma canonica com tipo e comprimento (revisao Codex 20261028-012302): $null, $false e "False" nao colidem
function Canon($v) {
    if ($null -eq $v) { return "n" }
    if ($v -is [bool]) { return "b:$v" }
    if ($v -is [int]) { return "i:$v" }
    if ($v -is [string]) { return "s$($v.Length):$v" }
    $xs = @($v | ForEach-Object { Canon $_ })
    return "a$($xs.Count):[" + ($xs -join [char]31) + "]"
}
# Identidade da midia (revisao Codex 20261028-012302): caminho resolvido e SHA-256 do conteudo, calculada uma vez, entra
# na chave das etapas que usam a imagem (Disc, ou o caminho nos argumentos). Trocar o caminho ou o conteudo refaz a etapa.
$script:midia = $null
function MediaId {
    if ($null -eq $script:midia) {
        $r = (Resolve-Path -LiteralPath $Disc -ErrorAction SilentlyContinue).Path
        $script:midia = if ($r) { "$($r.ToLowerInvariant())|$((Get-FileHash -LiteralPath $r -Algorithm SHA256).Hash)" } else { "ausente:$Disc" }
    }
    return $script:midia
}
function StepKey($st, $base) {
    $def = "T=$(Canon $st.T)|C=$(Canon $st.C)|A=$(Canon @($st.A))|Disc=$(Canon $st.Disc)|Max=$(Canon $st.Max)|Stop=$(Canon $st.Stop)"
    if ($st.Disc -or (@($st.A) -contains $Disc)) { $def += "|Midia=$(MediaId)" }
    $sha = [Security.Cryptography.SHA256]::Create()
    $d = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($def))) -replace "-", "").Substring(0, 8)
    if ($st.T -ne "suite") { return "$base-$d" }
    $f = $st.A[0]
    $h = if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.Substring(0, 16) } else { "ausente" }
    return "$base-$h-$d"
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
    # a saida vai direto para <etapa>.txt (acompanhavel durante a etapa); limite de tempo por etapa: um
    # processo travado (p. ex. janela modal de assert) vira falha rc=124 em vez de prender a regressao
    if ($st.T -eq "suite") {
        $line = "powershell -NoProfile -ExecutionPolicy Bypass -File " + (($st.A | ForEach-Object { "`"$_`"" }) -join " ")
    } else {
        $prefix = if ($st.Disc) { "set `"BS_DISC_IMAGE=$((Resolve-Path $Disc).Path)`"&& " } else { "" }
        $line = "call `"$vcvars`" >nul && cd /d `"$repo`" && $prefix$($st.C)"
    }
    $max = if ($env:BS_REGRESSAO_LIMITE_S) { [int]$env:BS_REGRESSAO_LIMITE_S } elseif ($st.Max) { $st.Max } elseif ($st.T -eq "suite") { 7200 } elseif ($st.T -eq "build") { 3600 } else { 1800 }
    Remove-Item -LiteralPath $outFile -ErrorAction SilentlyContinue
    $p = Start-Process cmd.exe -ArgumentList "/d /s /c `"chcp 65001 >nul & $line > `"$outFile`" 2>&1`"" -NoNewWindow -PassThru
    [void]$p.Handle  # guarda o handle: sem isso o ExitCode pode vir vazio
    "$($st.N)`t$($t0.ToString('s'))`tPID $PID`tfilho $($p.Id)`t0s de ${max}s" | Out-File $ativo -Encoding utf8
    $rc = $null
    while (-not $p.WaitForExit(30000)) {
        $el = [int]((Get-Date) - $t0).TotalSeconds
        $sz = if (Test-Path -LiteralPath $outFile) { (Get-Item -LiteralPath $outFile).Length } else { 0 }
        $lw = if ($sz) { (Get-Item -LiteralPath $outFile).LastWriteTime.ToString("s") } else { "-" }
        "$($st.N)`t$($t0.ToString('s'))`tPID $PID`tfilho $($p.Id)`t${el}s de ${max}s`tsaida $sz bytes (ultima escrita $lw)" | Out-File $ativo -Encoding utf8
        if ($el -ge $max) {
            & taskkill.exe /T /F /PID $p.Id 2>&1 | Out-Null
            [void]$p.WaitForExit(10000)
            "`nTEMPO ESGOTADO: ${max}s (regressao.ps1 encerrou a arvore de processos)" | Out-File $outFile -Append -Encoding utf8
            $rc = 124
            break
        }
    }
    if ($null -eq $rc) { $rc = $p.ExitCode }
    $sec = [int]((Get-Date) - $t0).TotalSeconds
    $o = if (Test-Path -LiteralPath $outFile) { Get-Content -LiteralPath $outFile -Raw -Encoding UTF8 } else { "" }
    if ($null -eq $o) { $o = "" }
    $summary = ($o -split "`r?`n" | Where-Object { $_ -match "FALHA|FAIL|casos|falhas|SKIP|ERRO|tests passed|error |FAILED|TEMPO ESGOTADO" } | Select-Object -Last 8) -join "`n"
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
