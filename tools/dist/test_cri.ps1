# Pre-requisitos do N2b-2 (docs/N2b-BLOQUEIOS.md): ociosidade da thread 3 (servidor do CRI),
# oraculo de audio e B11. Precisa do build com mods (BS_CRI_DIAG); sem ele, SKIP explicito.
# Execucoes em paralelo, mesmo roteiro (NEW GAME), relogio fixo, tempo nativo (modo sombra):
#   C1  medicao nao invasiva: os arquivos brutos (trace de ordem com ciclos, leituras, relogios e
#       AICA, estado, quadro) da execucao com todos os diagnosticos = os da referencia sem nenhum
#   C2  gameplay ocioso: depois da ultima leitura de arquivo, nenhuma passada com trabalho; as
#       passadas "elegiveis" (ociosa antes, nenhum evento desde entao nem durante) que trabalharam
#       existem (o classificador ve o fenomeno) e sao todas transicoes de stream; com o TMU parado
#       por quadro (modelo da D7), nenhuma, com eventos durante as passadas observados; observador
#       independente: toda a RAM principal e a de som numa janela de gameplay, nenhuma passada
#       ociosa sem evento muda alguma palavra
#   C3  bloqueio ate o proximo evento (pular5) = referencia (fluxo) em TN normalizada (sem as trocas
#       causadas pela exclusao mutua: cessoes 8C17DDCC chamadas de 8C1588C2 e 8C158854, marcadas uma
#       a uma em BS_CRI_CESSAO_LOG; cada marca tem de ser uma troca), IRQF, CL, SL, AUD, quadro
#   C4  controle so de tempo (repetir5): TN, IRQF, CL, SL, AUD, quadro iguais
#   C5  negativo: condicao de ociosidade errada (pular: ignora o gerenciador de streams) e detectada
#   A1  oraculo de audio: AUD (escritas e leituras da CPU na RAM de som e nos registradores da
#       AICA, por VBlank) igual em C1, C3, C4 e com o tempo legado. Negativos: escrita perdida
#       (testa o comparador) e escrita registrada mas nao aplicada (so as leituras seguintes da
#       CPU podem acusar: testa a sensibilidade)
#   B11a medidores do CRI (0x8C1DD1C8-0x8C1DD23C): tempo legado = modo sombra
#   B11b TMU parado por quadro: TN, CL, SL, AUD, quadro iguais; medidores diferentes
#   B11c consumidor artificial dos medidores: legado = sombra (controle); com o TMU parado, um
#       artefato logico muda (negativo detectado)
#   powershell -File tools/dist/test_cri.ps1 [-ExeMods <exe com BS_MODS>] [-Data <data>]
# saves legado: estas suites investigam o tempo (N2b) no caminho SEM cartao, onde as hipoteses foram
# estabelecidas; com o cartao nativo (padrao desde o N4) o boot segue o codigo de cartao do jogo, cujo
# ritmo depende do tempo, e D7/controles so de tempo deixam de valer (achado registrado para o retorno
# ao N2: STATUS.md, fechamento do N4).
# controles legado: as hipoteses tambem foram estabelecidas com o controle pelo Maple (padrao ate o
# N3-2). Com o controle nativo (padrao desde b1c76ec) o DMA do Maple termina antes e a ordem das
# interrupcoes (IRQF) muda sob as perturbacoes de tempo; A/B de 2026-10-19: atual com --controles
# legado --saves legado = 33/33. Reavaliar no retorno ao N2.
# video legado: estabelecida com o Sofdec original; o filme nativo (N5) dura o tempo real do filme e
# desloca os artefatos por VBlank depois de cada filme (a logica por passo e a mesma).
# audio legado (A/B do N8.0, 2026-10-25, aprovado pelo mantenedor): o observavel central (streams ADX do CRI e
# escritas da CPU na AICA) so existe com o audio legado; com o nativo nao ha stream e os negativos ficam vazios. O
# audio nativo segue coberto pela bateria do N6 (n6_h.ps1, H9), test_tempo, test_assistido e test_pd_nativo.
param(
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-mods\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$VBlanks = 8000,
    [int]$VBlanksB11 = 5000,
    [int]$LimiteSegundos = 2400,
    [string]$Saida = (Join-Path $env:TEMP "bs-cri-teste")
)
. (Join-Path $PSScriptRoot "valida_exe.ps1"); Assert-Validacao $ExeMods  # N8.4: o exe precisa ser de validacao
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $ExeMods)) { Write-Host "SKIP: sem build com mods ($ExeMods)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$marker = Join-Path $Saida ".bs-cri-teste"
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

# ---- execucoes (em paralelo; cada uma com o proprio ambiente) ----
$casos = [ordered]@{
    ref       = @{ n = $VBlanks; tempo = "nativo"; env = @{} }
    fluxo     = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "fluxo"; BS_CRI_SNAP_VB = "6000" } }
    fluxo_tq  = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "fluxo"; BS_TMU_DIAG = "quadro"; BS_CRI_SNAP_VB = "6000" } }
    pular5    = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "pular5" } }
    repetir5  = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "repetir5" } }
    pular     = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "pular" } }
    b11_leg   = @{ n = $VBlanksB11; tempo = "legado"; env = @{ BS_CRI_DIAG = "contar" } }
    b11_nat   = @{ n = $VBlanksB11; tempo = "nativo"; env = @{ BS_CRI_DIAG = "contar" } }
    b11_tq    = @{ n = $VBlanksB11; tempo = "nativo"; env = @{ BS_CRI_DIAG = "contar"; BS_TMU_DIAG = "quadro" } }
    cons_leg  = @{ n = $VBlanksB11; tempo = "legado"; env = @{ BS_CRI_DIAG = "consumidor" } }
    cons_nat  = @{ n = $VBlanksB11; tempo = "nativo"; env = @{ BS_CRI_DIAG = "consumidor" } }
    cons_tq   = @{ n = $VBlanksB11; tempo = "nativo"; env = @{ BS_CRI_DIAG = "consumidor"; BS_TMU_DIAG = "quadro" } }
    # negativo do oraculo de audio: a escrita 990 000 na RAM de som (envio de bancos no video) se perde
    audneg    = @{ n = $VBlanksB11; tempo = "nativo"; env = @{ BS_CRI_DIAG = "contar"; BS_AICA_MUTACAO = "990000" } }
    # negativo de sensibilidade: o preenchimento do bloco de estado do driver (0x00813A00, lido a
    # cada quadro pela biblioteca de som) entra no log mas nao na RAM
    audreg    = @{ n = $VBlanksB11; tempo = "nativo"; env = @{ BS_CRI_DIAG = "contar"; BS_AICA_MUTACAO = "registrada:00813A00" } }
}
$procs = @{}
foreach ($tag in $casos.Keys) {
    $k = $casos[$tag]; $p = Join-Path $Saida $tag
    $all = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\newgame_area1.txt")
              BS_EVENT_TRACE = "${p}_ev.tsv"; BS_FILE_LOG = "${p}_fl.tsv"; BS_CLOCK_LOG = "${p}_cl.tsv"
              BS_STATE_LOG = "${p}_sl.tsv"; BS_CRI_MEDIDORES = "${p}_med.tsv"; BS_CRI_FLUXO_LOG = "${p}_fx.tsv"
              BS_CRI_CESSAO_LOG = "${p}_cessao.tsv" }
    if ($tag -eq "ref") { foreach ($e in @("BS_CRI_MEDIDORES", "BS_CRI_FLUXO_LOG", "BS_CRI_CESSAO_LOG")) { $all.Remove($e) } }  # sem diagnostico
    foreach ($e in $k.env.Keys) { $all[$e] = $k.env[$e] }
    foreach ($e in $all.Keys) { Set-Item "env:$e" $all[$e] }
    try {
        $procs[$tag] = Start-Process -FilePath "cmd.exe" -PassThru -NoNewWindow -ArgumentList `
            "/c `"`"$ExeMods`" --data `"$Data`" --tempo $($k.tempo) --saves legado --controles legado --video legado --audio legado --headless $($k.n) --dump `"${p}_q.bmp`" > `"${p}_saida.txt`" 2>&1`""
        $null = $procs[$tag].Handle
    } finally { foreach ($e in $all.Keys) { Set-Item "env:$e" "" } }
}
$fim = (Get-Date).AddSeconds($LimiteSegundos)
$r = @{}
foreach ($tag in $casos.Keys) {
    $pr = $procs[$tag]; $left = [math]::Max(1, ($fim - (Get-Date)).TotalMilliseconds)
    if (-not $pr.WaitForExit([int]$left)) { & taskkill /PID $pr.Id /T /F 2>&1 | Out-Null; $code = "tempo-limite" } else { $code = $pr.ExitCode }
    $p = Join-Path $Saida $tag
    $r[$tag] = @{ P = $p; Code = $code; Out = $(if (Test-Path -LiteralPath "${p}_saida.txt") { Get-Content -LiteralPath "${p}_saida.txt" -Raw } else { "" }) }
    Check "roteiro termina ($tag)" ($code -eq 0) "rc=$code"
}

# ---- artefatos normalizados (mesmas regras do test_n2b.ps1) ----
function ReadLines($f) { if (Test-Path -LiteralPath $f) { [IO.File]::ReadAllLines($f) } else { @() } }
function Art($x, $kind) {
    $p = $x.P
    switch ($kind) {
        "TN" { return ReadLines "${p}_ev.tsv" | Where-Object { $_ -match '^\d' } | ForEach-Object {
                $f = $_.Split("`t"); if ($f[2] -eq "I") { "I $($f[3])" } else { "$($f[2]) $($f[3]) $($f[4])" } } }
        "TNC" {  # TN sem as trocas causadas pela exclusao mutua (marcadas uma a uma)
            $marks = New-Object 'System.Collections.Generic.HashSet[uint64]'
            foreach ($m in (ReadLines "${p}_cessao.tsv")) { [void]$marks.Add([uint64]$m.Split("`t")[0]) }
            $o = New-Object System.Collections.Generic.List[string]; $script:tncRemoved = 0; $script:tncBad = 0
            foreach ($l in (ReadLines "${p}_ev.tsv")) {
                if ($l -notmatch '^\d') { continue }
                $f = $l.Split("`t")
                if ($marks.Contains([uint64]$f[0])) {
                    if ($f[2] -eq "T") { ++$script:tncRemoved; continue }
                    ++$script:tncBad  # a marca nao caiu numa troca: a premissa causal falhou
                }
                $o.Add($(if ($f[2] -eq "I") { "I $($f[3])" } else { "$($f[2]) $($f[3]) $($f[4])" }))
            }
            $script:tncBad += $marks.Count - $script:tncRemoved - $script:tncBad  # marcas fora do trace
            return $o }
        "IRQF" {
            $field = [math]::Floor(200000000 * 1001 / 60000); $acc = [ordered]@{}
            foreach ($l in (ReadLines "${p}_ev.tsv")) {
                if ($l -notmatch '^\d') { continue }
                $f = $l.Split("`t")
                if ($f[2] -eq "I") { $k = [string][math]::Floor([double]$f[1] / $field); $acc[$k] = "$($acc[$k]) $($f[3])" }
            }
            return @($acc.Keys | ForEach-Object { "$_ $($acc[$_])" }) }
        "CL" { return ReadLines "${p}_fl.tsv" | Where-Object { $_ -match "`tdado`t" } | ForEach-Object {
                $f = $_.Split("`t"); "$($f[4]) $($f[5]) $($f[7]) $($f[1])" } }
        "CK" { return ReadLines "${p}_cl.tsv" | Select-Object -Skip 1 | ForEach-Object {
                $f = $_.Split("`t"); "$($f[0]) $($f[3]) $($f[5]) $($f[6]) $($f[7]) $($f[8]) $($f[9]) $($f[10])" } }
        "AUD" {  # escritas (11, 12) e leituras (13, 14) da CPU na AICA, por VBlank
            return ReadLines "${p}_cl.tsv" | Select-Object -Skip 1 | ForEach-Object { $f = $_.Split("`t"); "$($f[0]) $($f[11]) $($f[12]) $($f[13]) $($f[14])" } }
        "AUDW" { return ReadLines "${p}_cl.tsv" | Select-Object -Skip 1 | ForEach-Object { $f = $_.Split("`t"); "$($f[0]) $($f[11]) $($f[12])" } }
        "AUDR" { return ReadLines "${p}_cl.tsv" | Select-Object -Skip 1 | ForEach-Object { $f = $_.Split("`t"); "$($f[0]) $($f[13]) $($f[14])" } }
        "SL" { return ReadLines "${p}_sl.tsv" }
        "MED" { return ReadLines "${p}_med.tsv" }
        "Q" { if (Test-Path -LiteralPath "${p}_q.bmp") { return @((Get-FileHash -LiteralPath "${p}_q.bmp").Hash) } else { return @() } }
    }
}
function Same($a, $b) { return (($a -join "`n") -eq ($b -join "`n")) -and @($a).Count -gt 0 }
function Differ($a, $b) { return @($a).Count -gt 0 -and @($b).Count -gt 0 -and (($a -join "`n") -ne ($b -join "`n")) }
function AllSame($a, $b, $kinds) {
    $bad = @($kinds | Where-Object { -not (Same (Art $a $_) (Art $b $_)) })
    return @{ Ok = $bad.Count -eq 0; Txt = $(if ($bad.Count) { "diferem: $($bad -join ',')" } else { "iguais: $($kinds -join ',')" }) }
}

# ---- C1..C5, A1 ----
function RawHash($x, $suf) { $f = "$($x.P)_$suf"; if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f).Hash } else { "-" } }
$rawBad = @("ev.tsv", "fl.tsv", "cl.tsv", "sl.tsv", "q.bmp") | Where-Object { (RawHash $r.ref $_) -ne (RawHash $r.fluxo $_) -or (RawHash $r.ref $_) -eq "-" }
Check "C1 medicao nao invasiva: arquivos brutos da referencia = com todos os diagnosticos" ($rawBad.Count -eq 0) "$(if ($rawBad.Count) { "diferem: $($rawBad -join ',')" } else { 'ev, fl, cl, sl, q identicos byte a byte' })"

$lastRead = (Art $r.ref "CL" | ForEach-Object { [int]($_.Split(" ")[-1]) } | Measure-Object -Maximum).Maximum
$fx = ReadLines "$($r.fluxo.P)_fx.tsv"
$late = @($fx | Where-Object { [int]($_.Split("`t")[0]) -gt $lastRead }).Count  # o ultimo trabalho cai no VBlank da ultima leitura
$viol = [regex]::Matches($r.fluxo.Out, "\[cri\] violacao V(\d+)")
$ring = [regex]::Matches($r.fluxo.Out, "\[cri\] violacao V\d+[^\n]*\n(?:\[cri\]   [^\n]*\n)*")
$classified = @($ring | Where-Object { $_.Value -match "(?m)^\[cri\]   V\d+ trabalho [0-9A-F]+ epoca \w+;[^\n]*(estado 0->2|estado 2->0|cheio 1->0)[^\n]*\n\z" }).Count
$st = [regex]::Match($r.fluxo.Out, "com stream (\d+) \(ociosas (\d+)")
Check "C2 gameplay: nenhuma passada com trabalho depois da ultima leitura (V$lastRead)" ($fx.Count -gt 0 -and $late -eq 0) "$late passadas; $($st.Value)"
Check "C2 violacoes da ociosidade existem e sao transicoes de stream" ($viol.Count -gt 0 -and $classified -eq $viol.Count) "$classified de $($viol.Count) (abrir/fechar/encher)"
$vtq = [regex]::Match($r.fluxo_tq.Out, "violacoes (\d+) \(primeira V\d+\), com evento durante a passada (\d+)")
Check "C2 com o TMU parado por quadro (D7): todo trabalho disparado por evento" ($vtq.Success -and $vtq.Groups[1].Value -eq "0" -and [int]$vtq.Groups[2].Value -gt 0) $vtq.Value
$ram = [regex]::Match($r.fluxo.Out, "RAM \(principal e de som\) em V\d+-\d+: (\d+) passadas, (\d+) ociosas, (\d+) ociosas com RAM mudada \((\d+) sem evento durante a passada; (\d+) so nos medidores\)")
Check "C2 observador independente (RAM principal e de som, gameplay): ociosa sem evento so muda medidores do TMU" `
    ($ram.Success -and [int]$ram.Groups[2].Value -gt 0 -and $ram.Groups[4].Value -eq "0") $ram.Value
$ramq = [regex]::Match($r.fluxo_tq.Out, "RAM \(principal e de som\) em V\d+-\d+: (\d+) passadas, (\d+) ociosas, (\d+) ociosas com RAM mudada \((\d+) sem evento durante a passada; (\d+) so nos medidores\)")
Check "C2 observador independente com o TMU parado (D7): ociosa sem evento nao muda nenhuma palavra" `
    ($ramq.Success -and [int]$ramq.Groups[2].Value -gt 0 -and $ramq.Groups[4].Value -eq "0" -and $ramq.Groups[5].Value -eq "0") $ramq.Value

$x = AllSame $r.fluxo $r.pular5 @("TNC", "IRQF", "CL", "SL", "AUD", "Q")
$null = Art $r.fluxo "TNC"; $remF = $script:tncRemoved; $badF = $script:tncBad
$null = Art $r.pular5 "TNC"; $remP = $script:tncRemoved; $badP = $script:tncBad
Check "C3 bloqueio ate o proximo evento (pular5) = referencia" $x.Ok "$($x.Txt); trocas da exclusao removidas: $remF / $remP"
Write-Host ("[info] C3 TN sem normalizacao, referencia x bloqueio: {0}" -f $(if (Same (Art $r.fluxo "TN") (Art $r.pular5 "TN")) { "igual" } else { "difere (so cessoes da exclusao, se a TNC e igual)" }))
Check "C3 marcas causais da exclusao: existem e cada uma e uma troca" ($remF -gt 0 -and $remP -eq $remF -and $badF -eq 0 -and $badP -eq 0) "referencia $remF (falhas $badF); bloqueio $remP (falhas $badP)"
$x = AllSame $r.ref $r.repetir5 @("TN", "IRQF", "CL", "SL", "AUD", "Q")
Check "C4 controle so de tempo (repetir5) = referencia" $x.Ok $x.Txt
$neg = @("TNC", "CL", "SL", "Q") | Where-Object { Differ (Art $r.fluxo $_) (Art $r.pular $_) }
Check "C5 negativo: ociosidade errada (pular, ignora streams) detectada" ($neg.Count -gt 0) "difere em: $($neg -join ',')"
$aud = @("fluxo", "pular5", "repetir5") | Where-Object { -not (Same (Art $r.ref "AUD") (Art $r[$_] "AUD")) }
Check "A1 oraculo de audio: AUD igual (fluxo, pular5, repetir5)" ($aud.Count -eq 0) "$(if ($aud.Count) { "difere: $($aud -join ',')" })"
Check "A1 oraculo de audio: tempo legado = modo sombra" (Same (Art $r.b11_leg "AUD") (Art $r.b11_nat "AUD")) ""
$neg = @("TN", "CL", "SL", "Q") | Where-Object { Differ (Art $r.b11_nat $_) (Art $r.audneg $_) }
Check "A1 negativo do comparador: escrita perdida (e fora do log) detectada no AUD" (Differ (Art $r.b11_nat "AUD") (Art $r.audneg "AUD")) "outros artefatos que mudam: $($neg -join ',')"
$neg = @("TN", "CL", "SL", "Q") | Where-Object { Differ (Art $r.b11_nat $_) (Art $r.audreg $_) }
# (o bloco de estado decide o comportamento da biblioteca de som: com ele zerado ela passa a mandar
# comandos e o video muda; as escritas mudam por consequencia, entao so as leituras sao exigidas)
Check "A1 negativo de sensibilidade: escrita registrada e nao aplicada muda as leituras seguintes e o comportamento" `
    ((Differ (Art $r.b11_nat "AUDR") (Art $r.audreg "AUDR")) -and $neg.Count -gt 0) "artefatos de comportamento que mudam: $($neg -join ',')"

# ---- B11 ----
Check "B11a medidores do CRI: tempo legado = modo sombra" (Same (Art $r.b11_leg "MED") (Art $r.b11_nat "MED")) "$(@(Art $r.b11_nat 'MED').Count) VBlanks"
$x = AllSame $r.b11_nat $r.b11_tq @("TN", "IRQF", "CL", "SL", "AUD", "Q")
Check "B11b TMU parado por quadro: logica igual" $x.Ok $x.Txt
Check "B11b TMU parado por quadro: medidores diferentes (medem tempo)" (Differ (Art $r.b11_nat "MED") (Art $r.b11_tq "MED")) ""
$x = AllSame $r.cons_leg $r.cons_nat @("TN", "IRQF", "CL", "SL", "Q")
Check "B11c consumidor artificial: legado = sombra (controle)" $x.Ok $x.Txt
$neg = @("TN", "CL", "SL", "Q") | Where-Object { Differ (Art $r.cons_nat $_) (Art $r.cons_tq $_) }
$hits = [regex]::Match($r.cons_nat.Out, "consumidor artificial (\d+)")
Check "B11c negativo: consumidor do TMU com o TMU parado e detectado" ($neg.Count -gt 0) "difere em: $($neg -join ','); $($hits.Value)"

$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas; saidas em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
