# N8.1: verificacoes nativas S2n e S4n (aprovadas pelo mantenedor em 2026-10-25), com o audio nativo (padrao), antes e
# depois de cada mudanca da migracao. A conferencia dos artefatos e do tools/dev/n8_nativo.py (testes sem o jogo em
# tools/dev/test_n8_nativo.py). Provedores de saves e video como no test_n2b (--saves legado --video legado: o filme
# nativo do NEWGAME dura 27 146 VBlanks; o cartao e o filme tem as suas suites).
#   S2n espera do VBlank (8C064A2C), nos dois modos de tempo e em duas rotas: saidas sem evento ou por outro evento = 0,
#       episodios fechados pelo VBlank-In, no maximo um aberto e so se a execucao termina dentro dele; execucao e
#       trace completos; negativo: a espera nao declarada (BS_TEMPO_MUTACAO=sem_espera) reprova
#   S4n sequencia do servidor do CRI (8C1586B8 e a rotina por ponteiro 8C174888): tempo legado = modo sombra linha a
#       linha (funcao, chamador, r4-r7, r0, pr, VBlank), execucoes completas e trace ate o ultimo VBlank; negativo
#       BS_CRI_DIAG=menos1 detectado. Precisa do build com mods (-ExeMods)
#   T   TMU logico (N8.1): todo acesso dentro do contrato; o timeout de 166 ms do render pelo TMU vence com o render
#       perdido e nao vence com BS_TMU_MUTACAO=parado (negativo)
#   D   determinismo: duas execucoes experimentais com o mesmo trace de progresso
#   R   referencia pre-migracao (-Referencia): as assinaturas de cada S2n, do S4n e da sequencia do CRI no experimental
#       iguais as gravadas antes do N8.1 (-Capturar grava; a pasta fica fora do git, em analysis/)
#   powershell -File tools/dist/test_n8_nativo.ps1 [-Exe <exe>] [-ExeMods <exe>] [-Referencia <pasta>] [-Capturar]
param(
    # N8.4: --controles legado e --saves legado (modelo do Maple) so existem no build de validacao; o codigo nativo nele e o do produto
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-validacao\app\bluestinger.exe"),
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-mods\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [string]$Referencia = (Join-Path $PSScriptRoot "..\..\analysis\n8\pre-n8-1"),
    [switch]$Capturar,
    [int]$VBlanks = 8000,
    [int]$VBlanksS4 = 3000,
    [int]$LimiteSegundos = 1800,
    [string]$Saida = (Join-Path $env:TEMP "bs-n8-nativo-teste")
)
. (Join-Path $PSScriptRoot "valida_exe.ps1"); Assert-Validacao $Exe; Assert-Validacao $ExeMods  # N8.4: o exe precisa ser de validacao
$ErrorActionPreference = "Stop"
foreach ($x in $Exe, $ExeMods) { if (-not (Test-Path -LiteralPath $x)) { Write-Host "ERRO: executavel ausente ($x)"; exit 2 } }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "ERRO: dados ausentes ($Data)"; exit 2 }
if ($Capturar -and (Test-Path -LiteralPath $Referencia)) { Write-Host "ERRO: a referencia ja existe ($Referencia): nada sobrescrito"; exit 2 }
if (-not $Capturar -and -not (Test-Path -LiteralPath (Join-Path $Referencia "manifesto.json"))) { Write-Host "ERRO: sem a referencia pre-migracao ($Referencia); -Capturar para gravar"; exit 2 }
$marker = Join-Path $Saida ".bs-n8-nativo-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
$py = Join-Path $PSScriptRoot "..\dev\n8_nativo.py"

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function Caso($tag, $exe, $tempo, $n, $roteiro, [hashtable]$env = @{}, [switch]$calls) {
    return @{ tag = $tag; exe = $exe; tempo = $tempo; n = $n; roteiro = $roteiro; env = $env; calls = [bool]$calls }
}
function Launch($c) {
    $p = Join-Path $Saida $c.tag
    $all = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\$($c.roteiro).txt")
              BS_PROGRESS_TRACE = "${p}_pg.tsv" }
    if ($c.calls) { $all.BS_CALL_TRACE = "${p}_calls.tsv;8C1586B8,8C174888" }
    foreach ($k in $c.env.Keys) { $all[$k] = $c.env[$k] }
    foreach ($k in $all.Keys) { Set-Item "env:$k" $all[$k] }
    try {
        $proc = Start-Process -FilePath "cmd.exe" -PassThru -WindowStyle Hidden -ArgumentList `
            "/c `"`"$($c.exe)`" --data `"$Data`" --tempo $($c.tempo) --saves legado --video legado --headless $($c.n) > `"${p}_saida.txt`" 2>&1`""
        $null = $proc.Handle
    } finally { foreach ($k in $all.Keys) { Set-Item "env:$k" "" } }
    return @{ C = $c; Proc = $proc; P = $p }
}
$casos = @(
    (Caso "s2n_nat_area1" $Exe "nativo" $VBlanks "newgame_area1"),
    (Caso "s2n_nat_expl" $Exe "nativo" $VBlanks "newgame_exploracao"),
    (Caso "s2n_exp_area1" $Exe "experimental" $VBlanks "newgame_area1"),
    (Caso "s2n_exp_area1_b" $Exe "experimental" $VBlanks "newgame_area1"),
    (Caso "s2n_exp_expl" $Exe "experimental" $VBlanks "newgame_exploracao"),
    (Caso "s2n_neg" $Exe "nativo" $VBlanksS4 "newgame_area1" @{ BS_TEMPO_MUTACAO = "sem_espera" }),
    (Caso "s4n_leg" $ExeMods "legado" $VBlanksS4 "newgame_area1" -calls),
    (Caso "s4n_nat" $ExeMods "nativo" $VBlanksS4 "newgame_area1" -calls),
    (Caso "s4n_neg" $ExeMods "nativo" $VBlanksS4 "newgame_area1" @{ BS_CRI_DIAG = "menos1" } -calls),
    (Caso "s4n_exp" $ExeMods "experimental" $VBlanksS4 "newgame_area1" -calls),
    # T (N8.1, TMU logico): o timeout de 166 ms da espera do render vem do TMU; com o render perdido ele tem de vencer, e
    # com o TMU logico parado (BS_TMU_MUTACAO=parado) nao pode vencer
    (Caso "tmu_render" $Exe "experimental" $VBlanks "newgame_area1" @{ BS_TEMPO_MUTACAO = "render_perdido" }),
    (Caso "tmu_parado" $Exe "experimental" $VBlanks "newgame_area1" @{ BS_TEMPO_MUTACAO = "render_perdido"; BS_TMU_MUTACAO = "parado" })
)
$runs = @{}
foreach ($s in @($casos | ForEach-Object { Launch $_ })) {
    if (-not $s.Proc.WaitForExit($LimiteSegundos * 1000)) { & taskkill /PID $s.Proc.Id /T /F 2>&1 | Out-Null; $code = "tempo-limite" }
    else { $code = $s.Proc.ExitCode }
    $runs[$s.C.tag] = @{ P = $s.P; Code = $code; N = $s.C.n }
}
foreach ($t in $runs.Keys | Sort-Object) { if ($t -ne "s2n_neg" -and $t -ne "tmu_parado") { Check "execucao termina ($t)" ($runs[$t].Code -eq 0) "rc=$($runs[$t].Code)" } }

# T: TMU logico. Todo acesso dentro do contrato (0 entregues ao modelo) nas execucoes experimentais; o timeout pelo TMU
# vence com o render perdido e nao vence com o TMU parado
function SaidaDe($t) { Get-Content -Raw -LiteralPath "$($runs[$t].P)_saida.txt" -ErrorAction SilentlyContinue }
foreach ($t in @($casos | Where-Object { $_.tempo -eq "experimental" } | ForEach-Object { $_.tag } | Sort-Object)) {  # pelo modo, nao pelo nome
    $m = [regex]::Match((SaidaDe $t), "tmu logico: acessos atendidos (\d+), fora do contrato \(entregues ao modelo\) (\d+)")
    Check "T TMU logico: acessos dentro do contrato ($t)" ($m.Success -and [uint64]$m.Groups[1].Value -gt 0 -and $m.Groups[2].Value -eq "0") $m.Value
}
function RenderSem($t) {
    $m = [regex]::Match((SaidaDe $t), "espera 8C173086:.*evento esperado RenderDone: com (\d+), sem (\d+)")
    if ($m.Success) { [int]$m.Groups[2].Value } else { -1 }
}
$sr = RenderSem "tmu_render"; $sp = RenderSem "tmu_parado"
Check "T timeout pelo TMU logico: render perdido sai pelo timeout" ($runs.tmu_render.Code -eq 0 -and $sr -ge 1) "saidas sem o fim de render: $sr"
Check "negativo: TMU logico parado -> o timeout nao vence" ($sp -eq 0 -or ($runs.tmu_parado.Code -ne 0 -and $sp -lt 1)) "saidas sem o fim de render: $sp; rc=$($runs.tmu_parado.Code)"

# conferencia pelo n8_nativo.py; com -Capturar grava o json na referencia, senao compara com ela
$refDir = $Referencia
if ($Capturar) { New-Item -ItemType Directory -Path $refDir | Out-Null }
function Py([string[]]$a, $nome) {
    $j = Join-Path $Saida "$nome.json"
    $a += @("--json", $j)
    if (-not $Capturar -and $nome) { $a += @("--ref", (Join-Path $refDir "$nome.json")) }
    $out = & python $py @a 2>&1 | Out-String
    $rc = $LASTEXITCODE
    if ($Capturar -and $rc -eq 0 -and $nome) { Copy-Item $j (Join-Path $refDir "$nome.json") }
    return @{ Code = $rc; Out = $out.Trim() }
}
function S2n($tag) {
    $r = $runs[$tag]
    return Py @("s2n", "--trace", "$($r.P)_pg.tsv", "--saida", "$($r.P)_saida.txt", "--vblanks", "$($r.N)") $tag
}
foreach ($t in "s2n_nat_area1", "s2n_nat_expl", "s2n_exp_area1", "s2n_exp_expl") {
    $x = S2n $t
    Check "S2n espera do VBlank ($t)" ($runs[$t].Code -eq 0 -and $x.Code -eq 0) (($x.Out -split "`n")[-1])
    if ($x.Code) { Write-Host $x.Out }
}
$neg = Py @("s2n", "--trace", "$($runs.s2n_neg.P)_pg.tsv", "--saida", "$($runs.s2n_neg.P)_saida.txt", "--vblanks", "$VBlanksS4") ""
Check "negativo: espera do VBlank nao declarada (sem_espera) reprova o S2n" ($neg.Code -ne 0 -and $neg.Out -match "nenhum episodio") (($neg.Out -split "`n")[0])

$b = Py @("s2n", "--trace", "$($runs.s2n_exp_area1_b.P)_pg.tsv", "--saida", "$($runs.s2n_exp_area1_b.P)_saida.txt", "--vblanks", "$VBlanks") ""
$ha = (Get-FileHash "$($runs.s2n_exp_area1.P)_pg.tsv").Hash; $hb = (Get-FileHash "$($runs.s2n_exp_area1_b.P)_pg.tsv").Hash
Check "D determinismo: duas execucoes experimentais com o mesmo trace de progresso" ($b.Code -eq 0 -and $runs.s2n_exp_area1.Code -eq 0 -and $runs.s2n_exp_area1_b.Code -eq 0 -and $ha -eq $hb) "pg.tsv $($ha.Substring(0, 12)) / $($hb.Substring(0, 12))"

$p = { param($t) $runs[$t].P }
$s4 = Py @("s4n", "--legado", "$(& $p s4n_leg)_calls.tsv", "--sombra", "$(& $p s4n_nat)_calls.tsv", "--saida-legado", "$(& $p s4n_leg)_saida.txt",
           "--saida-sombra", "$(& $p s4n_nat)_saida.txt", "--vblanks", "$VBlanksS4", "--neg", "$(& $p s4n_neg)_calls.tsv", "--saida-neg", "$(& $p s4n_neg)_saida.txt") "s4n"
Check "S4n sequencia do servidor do CRI: tempo legado = modo sombra linha a linha; negativo menos1 detectado" ($runs.s4n_leg.Code -eq 0 -and $runs.s4n_nat.Code -eq 0 -and $runs.s4n_neg.Code -eq 0 -and $s4.Code -eq 0) (($s4.Out -split "`n")[-1])
if ($s4.Code) { Write-Host $s4.Out }
$se = Py @("s4seq", "--calls", "$(& $p s4n_exp)_calls.tsv", "--saida", "$(& $p s4n_exp)_saida.txt", "--vblanks", "$VBlanksS4") "s4n_exp"
Check "S4n sequencia do servidor do CRI no experimental (trace completo$(if (-not $Capturar) { ', = referencia' }))" ($runs.s4n_exp.Code -eq 0 -and $se.Code -eq 0) (($se.Out -split "`n")[-1])
if ($se.Code) { Write-Host $se.Out }

$exeSha = (Get-FileHash -LiteralPath $Exe).Hash; $modsSha = (Get-FileHash -LiteralPath $ExeMods).Hash
if ($Capturar) {
    $fail0 = @($results | Where-Object { -not $_.Ok }).Count
    if ($fail0) { Remove-Item -Recurse -Force -LiteralPath $refDir; Write-Host "referencia NAO gravada: $fail0 falhas" }
    else {
        @{ data = (Get-Date -Format s); exe_sha256 = $exeSha; exe_mods_sha256 = $modsSha; vblanks = $VBlanks; vblanks_s4 = $VBlanksS4 } |
            ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $refDir "manifesto.json")
        Write-Host "referencia pre-migracao gravada em $refDir"
    }
} else {
    $m = Get-Content (Join-Path $refDir "manifesto.json") -Raw | ConvertFrom-Json
    Write-Host "[info] referencia de $($m.data): exe $($m.exe_sha256.Substring(0, 12)) (agora $($exeSha.Substring(0, 12))), mods $($m.exe_mods_sha256.Substring(0, 12)) (agora $($modsSha.Substring(0, 12)))"
}
$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("test_n8_nativo: {0} casos, {1} falhas; saidas em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
