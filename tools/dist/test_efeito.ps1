# N2b-2, P1 (docs/N2b-PROPOSTA.md): ociosidade da thread 3 pelo detector de EFEITO LIQUIDO da
# passada (RAM principal fora da pilha morta, MMIO/VRAM/SQ/AICA, escritas do host, estado do
# GD-ROM), inclusive com Sofdec/ADX ativos. Precisa do build de investigacao
# (cmake -DBS_MODS=ON -DBS_WRITE_WATCH=ON -DBS_BUILD_GAME=ON, em build\msvc-release-watch);
# sem ele, SKIP explicito. Nunca e o produto.
# Execucoes em paralelo, NEW GAME (area 1), relogio fixo, tempo nativo:
#   E1  medicao nao invasiva: arquivos brutos com BS_CRI_DIAG=efeito = referencia sem diagnostico
#   E2  passadas sem evento e sem efeito liquido: com Sofdec/ADX ativos, ao menos 99%; violacoes
#       (efeito sem evento depois de passada sem efeito) = 0, com e sem o TMU parado
#   E3  bloqueio depois de passada sem efeito ate o proximo evento (pular6) = referencia em TN,
#       IRQF, CL, SL, AUD e quadro
#   E4  controle so de tempo (repetir6) = referencia
#   E5  negativo: detector que ignora a RAM (pular6r) e detectado
#   E7  negativo: detector que ignora a descarga de store queue (pular6s); se nenhuma passada tem
#       efeito so por ela, o negativo e vazio nesta rota e isso e informado
#   E6  com o TMU parado (D7) e o bloqueio: TN = TN da referencia sem as cessoes da exclusao mutua
#       (marcadas causalmente), IRQF, CL, SL, AUD e quadro iguais
#   powershell -File tools/dist/test_efeito.ps1 [-ExeMods <exe do build de investigacao>] [-Data <data>]
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
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-watch\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$VBlanks = 8000,
    [int]$LimiteSegundos = 2400,
    [string]$Saida = (Join-Path $env:TEMP "bs-efeito-teste"),
    # U7 (retorno ao N2, 2026-10-23): outra rota (ex.: roteiros\passo_heliporto.txt, troca de sala e combate) e os
    # provedores atuais ("--video legado", com cartao e controle nativos). O padrao e o estabelecido no N2b.
    [string]$Roteiro = (Join-Path $PSScriptRoot "roteiros\newgame_area1.txt"),
    [string]$Provedores = "--saves legado --controles legado --video legado --audio legado"
)
. (Join-Path $PSScriptRoot "valida_exe.ps1"); Assert-Validacao $ExeMods  # N8.4: o exe precisa ser de validacao
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $ExeMods)) { Write-Host "SKIP: sem o build de investigacao BS_WRITE_WATCH ($ExeMods)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$marker = Join-Path $Saida ".bs-efeito-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

$results = @()
$skips = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}

# ---- execucoes (em paralelo; cada uma com o proprio ambiente) ----
$casos = [ordered]@{
    ref      = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "contar" } }
    efeito   = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "efeito" } }
    efeito_tq = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "efeito"; BS_TMU_DIAG = "quadro" } }
    pular6   = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "pular6" } }
    repetir6 = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "repetir6" } }
    pular6r  = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "pular6r" } }
    pular6s  = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "pular6s" } }
    pular6_tq = @{ n = $VBlanks; tempo = "nativo"; env = @{ BS_CRI_DIAG = "pular6"; BS_TMU_DIAG = "quadro" } }
    bruto    = @{ n = $VBlanks; tempo = "nativo"; env = @{} }
}
$procs = @{}
foreach ($tag in $casos.Keys) {
    $k = $casos[$tag]; $p = Join-Path $Saida $tag
    $all = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = $Roteiro
              BS_EVENT_TRACE = "${p}_ev.tsv"; BS_FILE_LOG = "${p}_fl.tsv"; BS_CLOCK_LOG = "${p}_cl.tsv"
              BS_STATE_LOG = "${p}_sl.tsv"; BS_CRI_MEDIDORES = "${p}_med.tsv"; BS_CRI_FLUXO_LOG = "${p}_fx.tsv"
              BS_CRI_CESSAO_LOG = "${p}_cessao.tsv" }
    if ($tag -eq "bruto") { foreach ($e in @("BS_CRI_MEDIDORES", "BS_CRI_FLUXO_LOG", "BS_CRI_CESSAO_LOG")) { $all.Remove($e) } }  # sem diagnostico
    foreach ($e in $k.env.Keys) { $all[$e] = $k.env[$e] }
    foreach ($e in $all.Keys) { Set-Item "env:$e" $all[$e] }
    try {
        $procs[$tag] = Start-Process -FilePath "cmd.exe" -PassThru -NoNewWindow -ArgumentList `
            "/c `"`"$ExeMods`" --data `"$Data`" --tempo $($k.tempo) $Provedores --headless $($k.n) --dump `"${p}_q.bmp`" > `"${p}_saida.txt`" 2>&1`""
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

# ---- E1..E6 ----
function RawHash($x, $suf) { $f = "$($x.P)_$suf"; if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f).Hash } else { "-" } }
$rawBad = @("ev.tsv", "fl.tsv", "cl.tsv", "sl.tsv", "q.bmp") | Where-Object { (RawHash $r.bruto $_) -ne (RawHash $r.efeito $_) -or (RawHash $r.bruto $_) -eq "-" }
Check "E1 medicao nao invasiva: arquivos brutos sem diagnostico = com o detector de efeito" ($rawBad.Count -eq 0) "$(if ($rawBad.Count) { "diferem: $($rawBad -join ',')" } else { 'ev, fl, cl, sl, q identicos' })"
$re = "sem evento (\d+) \(com efeito (\d+), sem efeito (\d+),.*?com MW/ADX e sem evento (\d+) \(sem efeito (\d+)\).*?violacoes (\d+)"
foreach ($t in @("efeito", "efeito_tq")) {
    $m = [regex]::Match($r[$t].Out, $re)
    $ok = $m.Success -and [int64]$m.Groups[4].Value -gt 0 -and [double]$m.Groups[5].Value -ge 0.99 * [double]$m.Groups[4].Value -and $m.Groups[6].Value -eq "0"
    Check "E2 ($t) Sofdec/ADX sem evento: >= 99% sem efeito liquido; 0 violacoes" $ok $(if ($m.Success) { "sem evento $($m.Groups[1].Value) (com efeito $($m.Groups[2].Value)); MW/ADX sem evento $($m.Groups[4].Value) (sem efeito $($m.Groups[5].Value)); violacoes $($m.Groups[6].Value)" })
}
$x = AllSame $r.ref $r.pular6 @("TN", "IRQF", "CL", "SL", "AUD", "Q")
$pp = [regex]::Match($r.pular6.Out, "passadas (\d+); sem evento")
Check "E3 bloqueio depois de passada sem efeito (pular6) = referencia" $x.Ok "$($x.Txt); passadas executadas: $($pp.Groups[1].Value)"
$x = AllSame $r.ref $r.repetir6 @("TN", "IRQF", "CL", "SL", "AUD", "Q")
Check "E4 controle so de tempo (repetir6) = referencia" $x.Ok $x.Txt
$neg = @("TN", "CL", "SL", "AUD", "Q") | Where-Object { Differ (Art $r.ref $_) (Art $r.pular6r $_) }
Check "E5 negativo: detector que ignora a RAM (pular6r) detectado" ($neg.Count -gt 0) "difere em: $($neg -join ',')"
$sqo = [regex]::Match($r.efeito.Out, "descarga de SQ (\d+) \(so ela (\d+)\)")
$negS = @("TN", "CL", "SL", "AUD", "Q") | Where-Object { Differ (Art $r.ref $_) (Art $r.pular6s $_) }
if (-not $sqo.Success) {
    Check "E7 resumo da store queue presente na saida do detector" $false
} elseif ([int64]$sqo.Groups[2].Value -gt 0) {
    Check "E7 negativo: detector que ignora a store queue (pular6s) detectado" ($negS.Count -gt 0) "passadas so com descarga de SQ: $($sqo.Groups[2].Value); difere em: $($negS -join ',')"
} else {
    # Negativo nao exercitado nesta rota: nao conta como aprovado. Exige so que o detector cego a
    # store queue se comporte como o completo (pular6s = pular6), ja que nada os distingue aqui.
    $d6 = @("TN", "CL", "SL", "AUD", "Q") | Where-Object { -not (Same (Art $r.pular6 $_) (Art $r.pular6s $_)) }
    Check "E7 pular6s = pular6 quando nenhuma passada tem efeito so pela store queue" ($d6.Count -eq 0) "diferentes ou vazios: $($d6 -join ',')"
    $script:skips += "E7 negativo da store queue NAO EXERCITADO nesta rota (passadas com descarga: $($sqo.Groups[1].Value), so ela: 0)"
}
$tnc = Art $r.ref "TNC"; $rem = $script:tncRemoved; $bad = $script:tncBad
$x = AllSame $r.ref $r.pular6_tq @("IRQF", "CL", "SL", "AUD", "Q")
Check "E6 TMU parado (D7) + bloqueio: TN = referencia sem as cessoes marcadas; demais iguais" `
    ((Same $tnc (Art $r.pular6_tq "TN")) -and $rem -gt 0 -and $bad -eq 0 -and $x.Ok) "cessoes removidas da referencia: $rem (falhas $bad); $($x.Txt)"

$fail = @($results | Where-Object { -not $_.Ok }).Count
foreach ($k in $skips) { Write-Host "[SKIP] $k" }
Write-Host ("{0} casos, {1} falhas; saidas em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
