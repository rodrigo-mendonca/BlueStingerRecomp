# N2a: portoes do provedor de tempo nativo (docs/N2-PLANO.md, secao 5). Mesmo roteiro com
# --tempo legado e --tempo nativo, relogio de parede fixo (D5). Matriz:
#   3 000 VBlanks, arquivos nativo  -> A1, A5;
#   6 000 VBlanks, arquivos legado  -> A1, A5, A3 contra o quadro do N0 (B49D68E2165B8505);
#   8 000 VBlanks, arquivos nativo e legado -> A1, A5, A2, A3 contra os quadros do N1.
#   A1  trace de ordem (BS_EVENT_TRACE: VBlank, interrupcoes com SPC, trocas, ciclo) identico;
#   A2  fila nativa (BS_TIME_TRACE): o instante devido de cada VBlank e o cruzamento da linha do
#       SPG; nada entregue antes do devido; eventos simultaneos na ordem de prioridade; atraso
#       ate a entrega dentro do limite (a entrega acontece no ponto de poll seguinte, como no
#       legado, e A1 prova que e o mesmo ponto);
#   A3  quadro final identico entre os provedores de tempo e igual a referencia;
#   A5  leituras de TMU, SPG_STATUS, MDST, RTC e ISTNRM/EXT/ERR (BS_CLOCK_LOG) identicas.
#   Negativos: cada mutacao do provedor nativo tem de terminar normalmente (rc 0) e divergir da
#   referencia no artefato esperado (ordem ou leituras).
#   powershell -File tools/dist/test_tempo.ps1 [-Exe <bluestinger.exe>] [-Data <data>]
# video legado: estabelecida com o Sofdec original; o filme nativo (N5) dura o tempo real do filme e
# desloca os artefatos por VBlank depois de cada filme (a logica por passo e a mesma).
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\BlueStinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$AtrasoMaximo = 4000,  # ticks: quantum de poll (2 000) + trecho ate o proximo poll
    [int]$LimiteSegundos = 900,  # por execucao (8 000 VBlanks levam ~4 min)
    [string]$Saida = (Join-Path $env:TEMP "bs-tempo-teste")
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "SKIP: executavel ausente ($Exe)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$marker = Join-Path $Saida ".bs-tempo-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

# quadros de referencia: N0 (arquivos legado, 6 000) e N1 (test_arquivos, 8 000)
# saves legado: as referencias sao anteriores ao N4 (sem cartao: "VMU not found"); com o cartao nativo o
# boot monta e consulta o cartao e o quadro de 8 000 VBlanks sai com outra fase de animacao (mesma cena)
$refQuadro = @{ "legado_6000" = "B49D68E2165B8505"; "nativo_8000" = "F7818C7E18FA130C"; "legado_8000" = "F472ECC27954D27B" }
# prioridade dos tipos no mesmo instante (native::time::EventKind)
$prio = @{ VBlankIn = 0; VBlankOut = 1; RenderDone = 2; InputDone = 3; FileDone = 4; Timer = 5; AudioTick = 6 }

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function Run($tempo, $arquivos, $n, $mutacao = "") {
    $tag = "${tempo}_${arquivos}_${n}$(if ($mutacao) { "_$mutacao" })"
    $r = @{ Ev = Join-Path $Saida "ordem_$tag.tsv"; Cl = Join-Path $Saida "relogios_$tag.tsv"
            Tt = Join-Path $Saida "fila_$tag.tsv"; Bmp = Join-Path $Saida "quadro_$tag.bmp" }
    $env:BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\newgame_area1.txt")
    $env:BS_RELOGIO_FIXO = "1700000000"
    $env:BS_EVENT_TRACE = $r.Ev; $env:BS_CLOCK_LOG = $r.Cl; $env:BS_TEMPO_MUTACAO = $mutacao
    $env:BS_TIME_TRACE = $(if ($tempo -eq "nativo") { $r.Tt } else { "" })
    $log = Join-Path $Saida "saida_$tag.txt"
    try {
        # tempo-limite: um travamento (por exemplo uma mutacao que para o tempo) falha o caso em
        # vez de prender a suite
        $p = Start-Process -FilePath "cmd.exe" -PassThru -NoNewWindow -ArgumentList `
            "/c `"`"$Exe`" --data `"$Data`" --arquivos $arquivos --tempo $tempo --saves legado --video legado --headless $n --dump `"$($r.Bmp)`" > `"$log`" 2>&1`""
        $null = $p.Handle  # sem o handle guardado, ExitCode volta vazio depois de WaitForExit(ms)
        if (-not $p.WaitForExit($LimiteSegundos * 1000)) {
            & taskkill /PID $p.Id /T /F 2>&1 | Out-Null
            $r.Code = "tempo-limite"
        } else { $r.Code = $p.ExitCode }
    }
    finally {
        $env:BS_INPUT_SCRIPT = ""; $env:BS_RELOGIO_FIXO = ""; $env:BS_EVENT_TRACE = ""; $env:BS_CLOCK_LOG = ""
        $env:BS_TIME_TRACE = ""; $env:BS_TEMPO_MUTACAO = ""
    }
    $r.Out = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { "" }
    return $r
}
function Hash($p) { if (Test-Path -LiteralPath $p) { (Get-FileHash -LiteralPath $p).Hash } else { "-" } }
function Short($h) { if ($h.Length -ge 16) { $h.Substring(0, 16) } else { $h } }

# A2 sobre o trace da fila nativa
function CheckQueue($tt, $label) {
    $early = 0; $max = 0; $count = 0; $vb = 0; $offLine = 0; $simult = 0; $badOrder = 0
    $prevDue = -1; $prevKind = ""
    foreach ($line in [System.IO.File]::ReadLines($tt)) {
        if ($line.StartsWith("#") -or $line.StartsWith("seq")) { continue }
        $f = $line.Split("`t"); $k = $f[1]
        # RenderStart (N2b-0) nao e entrega: registra o STARTRENDER com o fim agendado como devido
        if ($k -eq "RenderStart") { continue }
        $d = [uint64]$f[2]; $e = [uint64]$f[3]; ++$count
        if ($e -lt $d) { ++$early } elseif ($e - $d -gt $max) { $max = $e - $d }
        if ($k -eq "VBlankIn" -or $k -eq "VBlankOut") { ++$vb; if ($f[4] -ne "1") { ++$offLine } }
        if ($d -eq $prevDue) { ++$simult; if ($prio[$k] -lt $prio[$prevKind]) { ++$badOrder } }
        $prevDue = $d; $prevKind = $k
    }
    Check "A2 VBlank devido exatamente na linha do SPG ($label)" ($vb -gt 0 -and $offLine -eq 0) "$vb VBlanks, $offLine fora da linha"
    Check "A2 nenhum evento antes do instante devido ($label)" ($count -gt 0 -and $early -eq 0) "$count entregas, $early antecipadas"
    Check "A2 simultaneos na ordem de prioridade ($label)" ($badOrder -eq 0) "$simult pares simultaneos, $badOrder fora de ordem"
    Check "A2 atraso maximo <= $AtrasoMaximo ticks ($label)" ($max -le $AtrasoMaximo) "maximo $max"
}

$matriz = @(@("nativo", 3000), @("legado", 6000), @("nativo", 8000), @("legado", 8000))
$ref3000 = $null
foreach ($m in $matriz) {
    $arq = $m[0]; $n = $m[1]; $label = "arquivos $arq, $n VBlanks"
    $leg = Run "legado" $arq $n
    $nat = Run "nativo" $arq $n
    if ($arq -eq "nativo" -and $n -eq 3000) { $ref3000 = $leg }
    Check "roteiro termina nos dois provedores de tempo ($label)" ($leg.Code -eq 0 -and $nat.Code -eq 0) "rc=$($leg.Code)/$($nat.Code)"
    $st = [regex]::Match($nat.Out, "\[tempo\] nativo: VBlank-In (\d+).*recuos do relogio (\d+)")
    Check "provedor nativo ativo e sem recuo do relogio ($label)" ($st.Success -and [int]$st.Groups[1].Value -ge $n -and $st.Groups[2].Value -eq "0") $st.Value
    $hl = Hash $leg.Ev; $hn = Hash $nat.Ev
    Check "A1 ordem de eventos identica ($label)" ($hl -eq $hn -and $hl -ne "-") "$(Short $hl)"
    $cl = Hash $leg.Cl; $cn = Hash $nat.Cl
    Check "A5 leituras de relogio e estado identicas ($label)" ($cl -eq $cn -and $cl -ne "-") "$(Short $cl)"
    $ql = Hash $leg.Bmp; $qn = Hash $nat.Bmp
    Check "A3 quadro identico entre os provedores de tempo ($label)" ($ql -eq $qn -and $ql -ne "-") "$(Short $qn)"
    $key = "${arq}_$n"
    if ($refQuadro.ContainsKey($key)) {
        Check "A3 quadro igual a referencia ($label)" ($qn.StartsWith($refQuadro[$key])) "esperado $($refQuadro[$key])"
    }
    if ($n -eq 8000) { CheckQueue $nat.Tt $label }
}

# Negativos: defeito -> artefato onde tem de aparecer
# ("ignorar_cancelamento" fica so no unitario n2_mutations_are_detected: o roteiro nao cancela
# nenhum evento, "cancelados 0" nas estatisticas, entao a mutacao nao tem efeito observavel aqui)
$negativos = [ordered]@{ perder_vblank_out = "leituras"; atrasar_render = "ordem"; deriva = "ordem"; relogio_degraus = "leituras" }
foreach ($mname in $negativos.Keys) {
    $mut = Run "nativo" "nativo" 3000 $mname
    $dif = @()
    if ((Hash $mut.Ev) -ne (Hash $ref3000.Ev)) { $dif += "ordem" }
    if ((Hash $mut.Cl) -ne (Hash $ref3000.Cl)) { $dif += "leituras" }
    if ((Hash $mut.Bmp) -ne (Hash $ref3000.Bmp)) { $dif += "quadro" }
    $esperado = $negativos[$mname]
    Check "negativo: mutacao $mname detectada em $esperado" ($mut.Code -eq 0 -and $ref3000.Code -eq 0 -and $dif -contains $esperado) "rc=$($mut.Code); diverge em: $($dif -join ',')"
}

$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas; saidas em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
