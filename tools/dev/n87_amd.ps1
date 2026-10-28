# N8.7 (docs/N8-7-DIAGNOSTICO.md secao 1): experimentos controlados da perda do dispositivo Vulkan na GPU AMD integrada
# (APU). Repete a rota A1 (passo_combate ate o VBlank 1200, relogio fixo) N vezes por configuracao e conta as falhas:
#   iso      GPU isolada: --render nativo (sem o software), sem carga
#   seq      modo comparar com o software sequencial (BS_RENDER_SEQ=1)
#   parK     modo comparar com o software paralelo restrito a K processadores logicos (afinidade do processo: o pool
#            do std::execution::par so usa esses); par16 = todos (o caso historico)
#   cargaK   --render nativo com uma carga sintetica independente em K threads (outro processo, a rota inteira)
#   seqcargaK  modo comparar sequencial + a carga sintetica (separa "o software rodando" de "a CPU ocupada")
# Uma configuracao por vez, nada em paralelo com ela. Nao muda nenhum arquivo do repositorio; logs em -Saida.
#   powershell -File tools/dev/n87_amd.ps1 [-Exe <exe>] [-Data <data>] [-Casos iso,seq,par16] [-N 30] [-Saida <dir>]
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [string]$Casos = "iso,seq,par16",
    [int]$N = 30,
    [int]$VBlanks = 1200,
    [string]$Gpu = "AMD",
    [string]$Saida = (Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\n8-0\n87_amd")
)
$ErrorActionPreference = "Continue"
$Exe  = (Resolve-Path $Exe).Path
$Data = (Resolve-Path $Data).Path
$rota = (Resolve-Path (Join-Path $PSScriptRoot "..\dist\roteiros\passo_combate.txt")).Path
New-Item -ItemType Directory -Force $Saida | Out-Null
$resumo = Join-Path $Saida "resumo.tsv"
if (-not (Test-Path $resumo)) { "caso`trodada`trc`tfalhas`trecriado`tquadros`tdiferentes`tsegundos`tlog" | Set-Content -Encoding utf8 $resumo }
$ncpu = [Environment]::ProcessorCount

# carga sintetica: K processos de laco aritmetico (nao toca disco nem GPU), encerrados no fim da rodada
function Start-Carga([int]$k) {
    $ps = @()
    for ($i = 0; $i -lt $k; ++$i) {
        $ps += Start-Process powershell -ArgumentList "-NoProfile", "-Command", "`$x = 0.5; while (`$true) { for (`$j = 0; `$j -lt 100000; ++`$j) { `$x = `$x * 1.0000001 + 0.0000001 } }" -PassThru -WindowStyle Hidden
    }
    Start-Sleep -Milliseconds 500
    return $ps
}

function Rodada([string]$caso, [int]$i) {
    $log = Join-Path $Saida ("{0}_{1:D2}.txt" -f $caso, $i)
    $env:BS_INPUT_SCRIPT = $rota
    $env:BS_RELOGIO_FIXO = "1700000000"
    $env:BS_VULKAN_GPU   = $Gpu
    Remove-Item Env:\BS_RENDER_IR, Env:\BS_RENDER_SEQ -ErrorAction SilentlyContinue
    $args_ = "--data `"$Data`" --headless $VBlanks"
    $mask = $null; $carga = @()
    switch -Regex ($caso) {
        '^iso$'          { $args_ += " --render nativo" }
        '^seq$'          { $env:BS_RENDER_IR = "comparar"; $env:BS_RENDER_SEQ = "1" }
        '^par(\d+)$'     { $env:BS_RENDER_IR = "comparar"; $k = [int]$Matches[1]; if ($k -lt $ncpu) { $mask = [Convert]::ToString(([int64]1 -shl $k) - 1, 16) } }
        '^carga(\d+)$'   { $args_ += " --render nativo"; $carga = Start-Carga ([int]$Matches[1]) }
        '^seqcarga(\d+)$' { $env:BS_RENDER_IR = "comparar"; $env:BS_RENDER_SEQ = "1"; $carga = Start-Carga ([int]$Matches[1]) }
        default          { Write-Host "caso desconhecido: $caso"; exit 2 }
    }
    $aff = if ($mask) { "/affinity $mask " } else { "" }
    $t0 = Get-Date
    $p = Start-Process cmd.exe -ArgumentList "/c start `"`" /b /wait $aff`"$Exe`" $args_ > `"$log`" 2>&1" -NoNewWindow -PassThru -Wait
    $seg = [int]((Get-Date) - $t0).TotalSeconds
    foreach ($c in $carga) { Stop-Process -Id $c.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item Env:\BS_RENDER_IR, Env:\BS_RENDER_SEQ, Env:\BS_INPUT_SCRIPT, Env:\BS_RELOGIO_FIXO, Env:\BS_VULKAN_GPU -ErrorAction SilentlyContinue
    $t = Get-Content $log -Raw -ErrorAction SilentlyContinue
    if (-not $t) { $t = "" }
    $vk = [regex]::Match($t, "\[render-vk\] [^:\r\n]+: quadros[^\r\n]*")  # o resumo (BS_VK_LENTOS tambem escreve [render-vk])
    $fal = [regex]::Match($vk.Value, "falhas (\d+)").Groups[1].Value
    $rec = [regex]::Match($vk.Value, "recriado (\d+)").Groups[1].Value
    $qd  = [regex]::Match($vk.Value, "quadros (\d+)").Groups[1].Value
    $dif = [regex]::Match($vk.Value, "pixels diferentes ([0-9.]+)%").Groups[1].Value
    $dev = if ($vk.Value -match "\[render-vk\] ([^:]+):") { $Matches[1] } else { "?" }
    "$caso`t$i`t$($p.ExitCode)`t$fal`t$rec`t$qd`t$dif`t$seg`t$log" | Add-Content -Encoding utf8 $resumo
    $falha = $vk.Value -eq "" -or [int]("0$fal") -gt 0
    Write-Host ("{0} {1} #{2}: rc={3} dispositivo={4} quadros={5} falhas={6} recriado={7} diferentes={8}% {9}s{10}" -f `
        $(if ($falha) { "[FALHA]" } else { "[ ok ]" }), $caso, $i, $p.ExitCode, $dev, $qd, $fal, $rec, $dif, $seg,
        $(if ($falha) { " ($((Select-String -Path $log -Pattern 'falha no render|DEVICE_LOST|falha do dispositivo' | Select-Object -First 1).Line))" } else { "" }))
    return $falha
}

foreach ($caso in ($Casos -split ",")) {
    $f = 0
    for ($i = 1; $i -le $N; ++$i) { if (Rodada $caso $i) { ++$f } }
    Write-Host ("== {0}: {1} de {2} rodadas com falha" -f $caso, $f, $N)
}
