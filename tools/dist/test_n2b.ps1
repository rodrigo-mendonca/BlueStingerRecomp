# N2b-0/N2b-1: criterios do modo sombra e comparadores B1-B11 com negativos
# (docs/N2b-PLANO.md, secao 4). Tudo no roteiro NEW GAME (relogio fixo); o modo sombra roda com
# --tempo nativo e o legado com --tempo legado. No N2b-1 a execucao nao muda: os criterios B
# comparam legado x sombra e, sobretudo, provam que cada comparador detecta o defeito que deve.
#   S1  trace de ordem completo (com ciclos) do modo sombra = legado, byte a byte
#   S2  toda saida de espera declarada teve evento entregue no episodio; liberada pelo evento certo
#   S3  0 faltas de progresso com o limite calibrado (D8) nas duas rotas
#   S4  chamadas por VBlank ao servidor do CRI (8C1586B8) e a rotina por ponteiro (8C174888), em
#       quantidade e ordem, sombra = legado; negativo BS_CRI_DIAG=menos1 (uma passada a menos por
#       VBlank). Precisa do build com mods (-ExeMods): sem ele, SKIP explicito
#   B1..B9 comparadores (TN, IRQ, CL, CK, provedores de arquivos e fusao, timeout, SL, liveness,
#       determinismo, desempenho) e uma mutacao por criterio que tem de ser detectada.
#   Parciais aqui: B7 fila vazia -> deadlock so no teste unitario n2b_progress_decisions_at_wait.
#   B10 (mod de funcao de espera) esta no test_projeto.ps1 (caso 4b); B11 completo (medidores do
#   CRI, consumidor artificial) e a ociosidade da thread 3 estao no test_cri.ps1
#   (docs/N2b-BLOQUEIOS.md).
#   Audio: o padrao (nativo), menos as duplicatas *_al com --audio legado que alimentam S2, B4 (fundidos), B11 e S4
#   (A/B do N8.0, 2026-10-25, aprovado pelo mantenedor; ver Run)
#   powershell -File tools/dist/test_n2b.ps1 [-Exe <exe>] [-ExeMods <exe com BS_MODS>] [-Data <data>]
# saves legado: estas suites investigam o tempo (N2b) no caminho SEM cartao, onde as hipoteses foram
# estabelecidas; com o cartao nativo (padrao desde o N4) o boot segue o codigo de cartao do jogo, cujo
# ritmo depende do tempo, e D7/controles so de tempo deixam de valer (achado registrado para o retorno
# ao N2: STATUS.md, fechamento do N4).
# video legado: estabelecida com o Sofdec original; o filme nativo (N5) dura o tempo real do filme e
# desloca os artefatos por VBlank depois de cada filme (a logica por passo e a mesma).
param(
    # N8.3: --arquivos legado (modelo HLE do GD-ROM) so existe no build de validacao; o produto o recusa
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-validacao\app\bluestinger.exe"),
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-mods\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$VBlanks = 8000,
    [int]$VBlanksNeg = 3000,
    [int]$LimiteSegundos = 900,
    [string]$Saida = (Join-Path $env:TEMP "bs-n2b-teste")
)
. (Join-Path $PSScriptRoot "valida_exe.ps1"); Assert-Validacao $Exe; Assert-Validacao $ExeMods  # N8.4: o exe precisa ser de validacao
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "SKIP: executavel ausente ($Exe)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$marker = Join-Path $Saida ".bs-n2b-teste"
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

# Uma execucao com todos os artefatos. $envs: variaveis extras (nome -> valor).
# $audio: "" = o padrao (nativo); "legado" nas duplicatas que alimentam S2, B4 (fundidos), B11 e S4 (ver abaixo)
function Run($tag, $tempo, $n, [hashtable]$envs = @{}, $arquivos = "nativo", $roteiro = "newgame_area1", $exe = $Exe, $audio = "") {
    $p = Join-Path $Saida $tag
    $all = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\$roteiro.txt")
              BS_EVENT_TRACE = "${p}_ev.tsv"; BS_FILE_LOG = "${p}_fl.tsv"; BS_CLOCK_LOG = "${p}_cl.tsv"
              BS_STATE_LOG = "${p}_sl.tsv" }
    if ($tempo -eq "nativo") { $all.BS_PROGRESS_TRACE = "${p}_pg.tsv" }
    foreach ($k in $envs.Keys) { $all[$k] = $envs[$k] }
    foreach ($k in $all.Keys) { Set-Item "env:$k" $all[$k] }
    $log = "${p}_saida.txt"
    try {
        $proc = Start-Process -FilePath "cmd.exe" -PassThru -NoNewWindow -ArgumentList `
            "/c `"`"$exe`" --data `"$Data`" --arquivos $arquivos --tempo $tempo --saves legado --video legado$(if ($audio) { " --audio $audio" }) --headless $n --dump `"${p}_q.bmp`" > `"$log`" 2>&1`""
        $null = $proc.Handle
        if (-not $proc.WaitForExit($LimiteSegundos * 1000)) { & taskkill /PID $proc.Id /T /F 2>&1 | Out-Null; $code = "tempo-limite" }
        else { $code = $proc.ExitCode }
    } finally { foreach ($k in $all.Keys) { Set-Item "env:$k" "" } }
    return @{ P = $p; Code = $code; Out = $(if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { "" }) }
}

# ---- artefatos normalizados (linhas) ----
function ReadLines($f) { if (Test-Path -LiteralPath $f) { [IO.File]::ReadAllLines($f) } else { @() } }
function Art($r, $kind) {
    $p = $r.P
    switch ($kind) {
        "EV" { return ReadLines "${p}_ev.tsv" }
        "TN" {  # sem ciclos; interrupcao sem o PC interrompido
            return ReadLines "${p}_ev.tsv" | Where-Object { $_ -match '^\d' } | ForEach-Object {
                $f = $_.Split("`t"); if ($f[2] -eq "I") { "I $($f[3])" } else { "$($f[2]) $($f[3]) $($f[4])" } } }
        "IRQ" {  # por VBlank: interrupcoes (INTEVT) em ordem
            $vb = 0; $acc = New-Object System.Collections.Generic.List[string]; $cur = ""
            foreach ($l in (ReadLines "${p}_ev.tsv")) {
                if ($l -notmatch '^\d') { continue }
                $f = $l.Split("`t")
                if ($f[2] -eq "V") { $acc.Add("$vb $cur"); $vb++; $cur = "" } elseif ($f[2] -eq "I") { $cur += " $($f[3])" }
            }
            $acc.Add("$vb $cur"); return $acc }
        "IRQF" {  # interrupcoes por campo logico (instante / duracao do campo): uma IRQ ausente,
                  # extra ou fundida aparece no campo, mesmo quando a sequencia por VBlank fica igual
            $field = [math]::Floor(200000000 * 1001 / 60000)  # 3 336 666, como kCompatTicksPerField
            $acc = [ordered]@{}
            foreach ($l in (ReadLines "${p}_ev.tsv")) {
                if ($l -notmatch '^\d') { continue }
                $f = $l.Split("`t")
                if ($f[2] -eq "I") { $k = [string][math]::Floor([double]$f[1] / $field); $acc[$k] = "$($acc[$k]) $($f[3])" }
            }
            return @($acc.Keys | ForEach-Object { "$_ $($acc[$_])" }) }
        "CL" { return ReadLines "${p}_fl.tsv" | Where-Object { $_ -match "`tdado`t" } | ForEach-Object {
                $f = $_.Split("`t"); "$($f[4]) $($f[5]) $($f[7]) $($f[1])" } }
        "CK" {  # SPG so a quantidade (o valor e a posicao do feixe: depende do tempo de execucao)
            return ReadLines "${p}_cl.tsv" | Select-Object -Skip 1 | ForEach-Object {
                $f = $_.Split("`t"); "$($f[0]) $($f[3]) $($f[5]) $($f[6]) $($f[7]) $($f[8]) $($f[9]) $($f[10])" } }
        "AUD" { return ReadLines "${p}_cl.tsv" | Select-Object -Skip 1 | ForEach-Object { $f = $_.Split("`t"); "$($f[0]) $($f[11]) $($f[12])" } }
        "SL" { return ReadLines "${p}_sl.tsv" }
        "Q" { if (Test-Path -LiteralPath "${p}_q.bmp") { return @((Get-FileHash -LiteralPath "${p}_q.bmp").Hash) } else { return @() } }
        "CALLS" {  # por VBlank: quantas chamadas a cada funcao rastreada e o hash da sequencia
            $f0 = "${p}_calls.tsv"; if (-not (Test-Path -LiteralPath $f0)) { return @() }
            $acc = New-Object System.Collections.Generic.List[string]; $vb = $null; $seq = New-Object System.Text.StringBuilder
            $na = 0; $nb = 0
            foreach ($l in [IO.File]::ReadLines($f0)) {
                $f = $l.Split("`t"); if ($f[0] -eq "seq") { continue }
                if ($f[9] -ne $vb) {
                    if ($null -ne $vb) { $acc.Add("$vb $na $nb $($seq.ToString().GetHashCode())") }
                    $vb = $f[9]; $na = 0; $nb = 0; [void]$seq.Clear()
                }
                if ($f[1] -eq "8C1586B8") { ++$na; [void]$seq.Append('P') } else { ++$nb; [void]$seq.Append('O') }
            }
            if ($null -ne $vb) { $acc.Add("$vb $na $nb $($seq.ToString().GetHashCode())") }
            return $acc }
    }
}
function Same($a, $b) { return (($a -join "`n") -eq ($b -join "`n")) -and $a.Count -gt 0 }
# diferente de verdade: os dois artefatos existem e nao sao iguais (um artefato vazio nao conta)
function Differ($a, $b) { return $a.Count -gt 0 -and $b.Count -gt 0 -and (($a -join "`n") -ne ($b -join "`n")) }
function Num($text, $re) { $m = [regex]::Match($text, $re); if ($m.Success) { [uint64]$m.Groups[1].Value } else { -1 } }
function Fused($r) { Num $r.Out "\[vblank\] fundidos (\d+)" }
function Collapsed($r) { Num $r.Out "VBlank-In colapsados (\d+)" }
# espera: episodios, deadlocks, saidas com/sem o evento esperado, maior episodio (ticks)
function WaitInfo($r, $pc) {
    $m = [regex]::Match($r.Out, "espera ${pc}: episodios (\d+).*deadlock (\d+);.*evento esperado \w+: com (\d+), sem (\d+); maior episodio (\d+) ticks")
    if (-not $m.Success) { return $null }
    return @{ Ep = [uint64]$m.Groups[1].Value; Dl = [uint64]$m.Groups[2].Value; Com = [uint64]$m.Groups[3].Value
              Sem = [uint64]$m.Groups[4].Value; Max = [uint64]$m.Groups[5].Value; Txt = $m.Value }
}
$kTimeoutTicks = 166000 * 200  # 0x28870 us (166 ms) da espera de render, em ticks de 200 MHz

# ================= referencias =================
$ref  = Run "ref_legado" "legado" $VBlanks
$somb = Run "sombra" "nativo" $VBlanks
$som2 = Run "sombra_2" "nativo" $VBlanks
$expl = Run "sombra_exploracao" "nativo" $VBlanks @{} "nativo" "newgame_exploracao"
$refL = Run "ref_arquivos_legado" "legado" $VBlanks @{} "legado"
$somL = Run "sombra_arquivos_legado" "nativo" $VBlanks @{} "legado"
# audio legado (A/B do N8.0, 2026-10-25, aprovado pelo mantenedor): S2, B4 (fundidos), B11 e S4 foram estabelecidos e
# calibrados com o audio legado (padrao de entao). Duplicatas com --audio legado alimentam esses casos; as execucoes
# acima (audio nativo) seguem com todos os demais, e o resultado nativo desses quatro fica informado ([info]).
$sombA = Run "sombra_al" "nativo" $VBlanks -audio "legado"
$explA = Run "sombra_exploracao_al" "nativo" $VBlanks @{} "nativo" "newgame_exploracao" -audio "legado"
$refA  = Run "ref_legado_al" "legado" $VBlanks -audio "legado"
$refLA = Run "ref_arquivos_legado_al" "legado" $VBlanks @{} "legado" -audio "legado"
$somLA = Run "sombra_arquivos_legado_al" "nativo" $VBlanks @{} "legado" -audio "legado"
foreach ($r in $ref, $somb, $som2, $expl, $refL, $somL, $sombA, $explA, $refA, $refLA, $somLA) { Check "roteiro termina ($(Split-Path -Leaf $r.P))" ($r.Code -eq 0) "rc=$($r.Code)" }

# ---- S1-S4 ----
function S2($vb, $rd) {
    return @{ V = [bool]($vb -and $vb.Ep -gt 0 -and $vb.Com -eq $vb.Ep -and $vb.Sem -eq 0)
              R = [bool]($rd -and $rd.Ep -gt 0 -and $rd.Com -eq $rd.Ep -and $rd.Sem -eq 0 -and $rd.Max -lt $kTimeoutTicks) }
}
foreach ($r in $sombA, $explA) {
    $lbl = Split-Path -Leaf $r.P
    $vb = WaitInfo $r "8C064A2C"; $rd = WaitInfo $r "8C173086"; $s2 = S2 $vb $rd
    Check "S2 espera do VBlank: toda saida com VBlank-In entregue no episodio (audio legado, $lbl)" $s2.V $(if ($vb) { $vb.Txt })
    Check "S2 espera do render: toda saida com fim de render entregue, sem estouro (audio legado, $lbl)" $s2.R $(if ($rd) { $rd.Txt })
}
Check "S1 trace de ordem completo do modo sombra = legado (byte a byte)" (Same (Art $ref "EV") (Art $somb "EV"))
Check "S1 idem com arquivos legado" (Same (Art $refL "EV") (Art $somL "EV"))
foreach ($r in $somb, $expl) {
    $o = $r.Out; $lbl = Split-Path -Leaf $r.P
    # evento esperado entregue de fato (depois do assinante) no episodio de cada saida
    $vb = WaitInfo $r "8C064A2C"; $rd = WaitInfo $r "8C173086"
    $s2 = S2 $vb $rd
    # a espera do render vale igual com o audio nativo (obrigatoria); a do VBlank fica informada: com o nativo a rota
    # termina no meio de uma espera (um episodio aberto no fim), A/B do N8.0
    Check "S2 espera do render: toda saida com fim de render entregue, sem estouro ($lbl)" $s2.R $(if ($rd) { $rd.Txt })
    Write-Host "[info] S2 espera do VBlank com o audio nativo ($lbl): $(if ($s2.V) { 'ok' } else { 'difere' }); $(if ($vb) { $vb.Txt })"
    Check "B7 0 deadlocks nas esperas ($lbl)" ($vb -and $rd -and $vb.Dl -eq 0 -and $rd.Dl -eq 0)
    Check "B4 nenhum VBlank-In colapsado no modo sombra ($lbl)" ((Collapsed $r) -eq 0) "colapsados $(Collapsed $r)"
    $st = [regex]::Match($o, "faltas de progresso (\d+) \(limite (\d+)\)")
    Check "S3 0 faltas de progresso com o limite calibrado ($lbl)" ($st.Success -and $st.Groups[1].Value -eq "0" -and [uint64]$st.Groups[2].Value -gt 0) $st.Value
}

# ---- B: comparadores legado x sombra ----
Check "B1 ordem logica (TN) legado = sombra" (Same (Art $ref "TN") (Art $somb "TN"))
Check "B2 interrupcoes por VBlank legado = sombra" (Same (Art $ref "IRQ") (Art $somb "IRQ"))
Check "B2 interrupcoes por campo logico legado = sombra" (Same (Art $ref "IRQF") (Art $somb "IRQF"))
Check "B3 leituras de arquivo legado = sombra" (Same (Art $ref "CL") (Art $somb "CL"))
Check "B3b leituras de relogio e estado legado = sombra" (Same (Art $ref "CK") (Art $somb "CK"))
Check "B6 estado logico (SL) legado = sombra" (Same (Art $ref "SL") (Art $somb "SL"))
Check "B6 quadro legado = sombra" (Same (Art $ref "Q") (Art $somb "Q"))
Check "B6 escritas de audio legado = sombra" (Same (Art $ref "AUD") (Art $somb "AUD"))
# B4: provedores de arquivos (legado x nativo, mesmo provedor de tempo). O plano pede contador de
# fundidos = 0; o legado tem 14 no boot (medido): o criterio aqui e "igual nos dois provedores e
# ao legado"; o desvio fica registrado no relatorio.
# A TN entre provedores de arquivos NAO e igual hoje: o provedor nativo muda o tempo de carga
# (eventos +2 VBlanks apos a tela-titulo, STATUS N1); o contrato de leituras P1 entre provedores
# e conferido pelo test_arquivos.ps1 (por grupo de arquivos que dividem setores). Aqui so se
# registra; a igualdade de TN entre provedores e meta do N2b-2.
foreach ($pair in @(@($ref, $refL, "tempo legado"), @($somb, $somL, "modo sombra"))) {
    $a = $pair[0]; $b = $pair[1]
    $eq = @("TN", "SL", "Q") | Where-Object { Same (Art $a $_) (Art $b $_) }
    Write-Host "[info] B4 arquivos legado x nativo ($($pair[2])): iguais em [$($eq -join ',')] de [TN,SL,Q] (diferenca de tempo de carga conhecida do N1)"
}
Check "B4 arquivos legado: modo sombra = tempo legado (TN, CL, SL, quadro)" `
    ((Same (Art $refL "TN") (Art $somL "TN")) -and (Same (Art $refL "CL") (Art $somL "CL")) -and (Same (Art $refL "SL") (Art $somL "SL")) -and (Same (Art $refL "Q") (Art $somL "Q")))
$fr = Fused $refA; $fs = Fused $sombA; $frL = Fused $refLA; $fsL = Fused $somLA
Check "B4 VBlanks fundidos iguais nos dois provedores e no legado (audio legado)" ($fr -ge 0 -and $fr -eq $fs -and $fr -eq $frL -and $fr -eq $fsL) "fundidos $fr / $fs / $frL / $fsL"
Write-Host "[info] B4 fundidos com o audio nativo (tempo legado / sombra / arquivos legado: tempo legado / sombra): $(Fused $ref) / $(Fused $somb) / $(Fused $refL) / $(Fused $somL)"
Check "B8 determinismo: duas execucoes do modo sombra iguais (TN, CL, CK, SL, quadro)" `
    ((Same (Art $somb "EV") (Art $som2 "EV")) -and (Same (Art $somb "CL") (Art $som2 "CL")) -and (Same (Art $somb "CK") (Art $som2 "CK")) -and (Same (Art $somb "SL") (Art $som2 "SL")) -and (Same (Art $somb "Q") (Art $som2 "Q")))

# ---- negativos: cada defeito tem de ser detectado pelo comparador do seu criterio ----
$negRef = Run "neg_ref" "nativo" $VBlanksNeg
function Neg($nome, $envs, $kinds, [scriptblock]$extra = $null) {
    $m = Run "neg_$nome" "nativo" $VBlanksNeg $envs
    $dif = @($kinds | Where-Object { Differ (Art $negRef $_) (Art $m $_) })
    $ok = $m.Code -eq 0 -and $dif.Count -eq $kinds.Count
    if ($extra) { $ok = $ok -and (& $extra $m) }
    Check "negativo: $nome detectado em $($kinds -join '+')" $ok "rc=$($m.Code); difere em: $($dif -join ',')"
}
# (a deriva de tempo nao muda a ordem logica, por construcao: o negativo de B1 e um evento a mais,
# que muda a contagem de VBlanks que o jogo ve)
Neg "vblank extra na ordem logica (B1)" @{ BS_TEMPO_MUTACAO = "vblank_extra" } @("TN")
Neg "irq ausente (B2)" @{ BS_TEMPO_MUTACAO = "perder_vblank_in" } @("IRQF")
Neg "irq extra (B2)" @{ BS_TEMPO_MUTACAO = "vblank_extra" } @("IRQF")
Neg "leitura corrompida (B3)" @{ BS_ARQUIVOS_MUTACAO = "corromper_bloco" } @("CL")
Neg "relogio em degraus (B3b)" @{ BS_TEMPO_MUTACAO = "relogio_degraus" } @("CK")
# fusao provocada: a ordem por campo muda e o contador (Holly ou colapso no adaptador) acusa
Neg "vblank fundido (B4)" @{ BS_TEMPO_MUTACAO = "fundir_vblank" } @("IRQF") {
    param($m) ((Collapsed $m) -gt (Collapsed $negRef)) -or ((Fused $m) -gt (Fused $negRef)) }
# render que nao chega: a espera sai sem o fim de render entregue, depois do estouro de 166 ms
# logicos (caminho de timeout), e o jogo segue (roteiro termina)
Neg "render perdido (B5)" @{ BS_TEMPO_MUTACAO = "render_perdido" } @("TN") {
    param($m)
    $w = WaitInfo $m "8C173086"; $w0 = WaitInfo $negRef "8C173086"
    $w -and $w0 -and $w.Sem -ge 1 -and $w0.Sem -eq 0 -and $w.Max -ge [uint64]($kTimeoutTicks * 0.9) -and $w0.Max -lt [uint64]($kTimeoutTicks * 0.9) }
Neg "estado alterado (B6)" @{ BS_RAM_MUTACAO = "1000:0x24F000" } @("SL")
Neg "espera ignorada (B7)" @{ BS_TEMPO_MUTACAO = "sem_espera"; BS_PROGRESSO_LIMITE = "1000000" } @() {
    param($m) [regex]::Match($m.Out, "faltas de progresso ([1-9]\d*)").Success }
# B11 (parcial): os consumidores do TMU nao mudam a ordem logica com o TMU parado por quadro
# (hipotese D7); o comparador acusa quando o relogio inteiro anda errado (relogio_degraus, acima)
# Com o audio legado (A/B do N8.0); com o nativo a D7 nao vale num ponto (uma IRQ 0x320 antes da 0x360 no VBlank 2114),
# pendencia do N8.1 (medidores do CRI e TMU), informada abaixo.
$negRefA = Run "neg_ref_al" "nativo" $VBlanksNeg -audio "legado"
function B11($ref0, $t) { return [bool]($ref0.Code -eq 0 -and $t.Code -eq 0 -and (Same (Art $ref0 "TN") (Art $t "TN")) -and (Same (Art $ref0 "CL") (Art $t "CL")) -and (Same (Art $ref0 "Q") (Art $t "Q"))) }
foreach ($v in "quadro", "agendado") {
    $t = Run "tmu_${v}_al" "nativo" $VBlanksNeg @{ BS_TMU_DIAG = $v } -audio "legado"
    Check "B11 TMU parado ($v, audio legado): ordem logica, leituras de arquivo e quadro iguais" (B11 $negRefA $t) "rc=$($t.Code)"
    $tn = Run "tmu_$v" "nativo" $VBlanksNeg @{ BS_TMU_DIAG = $v }
    Write-Host "[info] B11 TMU parado ($v) com o audio nativo: $(if (B11 $negRef $tn) { 'iguais' } else { 'difere (pendencia D7 do N8.1)' })"
}
# B9: desempenho (trabalho medio por campo, 3 alternadas); negativo com "lento"
function Work($r) { $m = [regex]::Match($r.Out, "media ([\d.]+) ms"); if ($m.Success) { [double]$m.Groups[1].Value } else { 1e9 } }
$wl = @(); $wn = @()
for ($i = 0; $i -lt 3; ++$i) { $wl += Work (Run "perf_l$i" "legado" $VBlanksNeg); $wn += Work (Run "perf_n$i" "nativo" $VBlanksNeg) }
$ml = ($wl | Measure-Object -Average).Average; $mn = ($wn | Measure-Object -Average).Average
Check "B9 desempenho: sombra <= legado x 1,05" ($mn -le $ml * 1.05) ("legado {0:N2} ms, sombra {1:N2} ms" -f $ml, $mn)
$slow = Work (Run "perf_lento" "nativo" $VBlanksNeg @{ BS_TEMPO_MUTACAO = "lento" })
Check "negativo: lentidao (B9) detectada" ($slow -gt $ml * 1.05) ("lento {0:N2} ms" -f $slow)

# S4: chamadas da thread 3 por VBlank (build com mods: o rastreio envolve a passada pelos slots)
if (Test-Path -LiteralPath $ExeMods) {
    function RunCalls($tag, $tempo, $envs = @{}, $audio = "") {
        $e = @{ BS_CALL_TRACE = "$(Join-Path $Saida $tag)_calls.tsv;8C1586B8,8C174888" }
        foreach ($k in $envs.Keys) { $e[$k] = $envs[$k] }
        Run $tag $tempo $VBlanksNeg $e "nativo" "newgame_area1" $ExeMods $audio
    }
    # audio legado: a cobertura (VBlanks com passadas >= 2990) foi calibrada com os streams ADX do audio legado; com o
    # nativo ha menos passadas do CRI (sem streams) e a cobertura cai, com as chamadas iguais entre os tempos (informado)
    $cl = RunCalls "s4_legado_al" "legado" @{} "legado"
    $cs = RunCalls "s4_sombra_al" "nativo" @{} "legado"
    $cm = RunCalls "s4_menos1_al" "nativo" @{ BS_CRI_DIAG = "menos1" } "legado"
    $al = Art $cl "CALLS"; $as = Art $cs "CALLS"
    $passes = ($al | ForEach-Object { [int]($_.Split(' ')[1]) } | Measure-Object -Sum).Sum
    Check "S4 chamadas por VBlank ao servidor do CRI e a rotina por ponteiro: sombra = legado (audio legado)" `
        ($cl.Code -eq 0 -and $cs.Code -eq 0 -and $al.Count -ge $VBlanksNeg - 10 -and (Same $al $as)) "VBlanks $($al.Count), passadas $passes"
    Check "negativo: uma passada a menos por VBlank (S4) detectada (audio legado)" ($cm.Code -eq 0 -and (Differ $as (Art $cm "CALLS")))
    $cln = RunCalls "s4_legado" "legado"; $csn = RunCalls "s4_sombra" "nativo"
    $nl = Art $cln "CALLS"; $ns = Art $csn "CALLS"
    Write-Host "[info] S4 com o audio nativo: chamadas sombra $(if (Same $nl $ns) { '=' } else { '<>' }) legado; VBlanks com passadas $($nl.Count)"
    # o negativo vale igual com o audio nativo (obrigatorio)
    $cmn = RunCalls "s4_menos1" "nativo" @{ BS_CRI_DIAG = "menos1" }
    Check "negativo: uma passada a menos por VBlank (S4) detectada" ($csn.Code -eq 0 -and $cmn.Code -eq 0 -and (Differ $ns (Art $cmn "CALLS")))
} else { Write-Host "SKIP: S4 (sem build com mods: $ExeMods)" }

$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas; saidas em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
