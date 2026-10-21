# N4: saves nativos (docs/N4-PREPARACAO.md, "SAVE e LOAD de gameplay"). Headless, deterministico.
#   S1 SAVE de gameplay (build com mods, BS_SAVES_H2=7500: SO o save point e simulado; o resto e o fluxo
#      do jogo): roteiro passo_save_rcg02 ate "Save completed."; .bsv gravado, VMS valido (n4_vmu.py)
#   S2 determinismo: duas execucoes do S1 dao o mesmo VMS
#   S3 LOAD em processo novo (release, sem diagnostico): passo_load_slot1; a partida volta a RCG02 com a
#      vida e a posicao do momento do SAVE, continua jogavel (anda) e o arquivo nao muda
#   S4 sem save: o mesmo roteiro termina no titulo (nada carregado), sem erro
#   S5 --saves legado (fallback): sem VMU, o save nao e carregado e nenhuma pasta de saves e criada
#   S6 padrao (sem --saves): igual ao S3 (estado por passo)
#   S7 save corrompido: preservado como .corrompido, nada carregado, sem erro
#   S8 recuperacao pelo .bak: destino estragado + .bak valido -> LOAD do .bak, registrado no log
#   S9 temporario remanescente (.tmp de gravacao interrompida): descartado, o save valido carrega
#   S10 versao futura do envelope: preservada intacta, nao carregada, recusada pela importacao
#   S11 --saves-importar/--saves-exportar: ida e volta byte a byte; VMS adulterado e inexistente recusados
#   S12 Flycast -> Windows (com -VmuFlycast <vmu.bin> -PosFlycast "x,y,z"): o BLUESTIN.000 da VMU do
#      jogo original importado carrega com a posicao registrada no Flycast (senao SKIP)
#   S13 o produto nao usa diagnosticos: nenhuma linha BS_SAVES_H1/H2 nos casos sem eles
#   S14 --headless sem --saves-pasta: cartao temporario vazio do processo, fora dos saves do usuario, apagado no fim
#   S15 modo portatil pela instalacao (instalacao.toml do BlueStinger-Recompiler, sem opcao de linha de
#       comando): o executavel aberto direto carrega a partida de <instalacao>\saves
#   S16 a instalacao portatil movida para outra pasta continua lendo os proprios saves
#   S17 SAVE de gameplay numa instalacao portatil grava em <instalacao>\saves
#   S18 precedencia: --saves-pasta vence o instalacao.toml; sem ele (e no --headless) cartao temporario;
#       instalacao.toml invalido: aviso e o padrao
#   powershell -File tools/dist/test_saves.ps1 [-Exe <release>] [-ExeMods <mods>] [-Data <data>] [-Casos S1,...]
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\app\bluestinger.exe"),
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-mods\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [string]$VmuFlycast = "",
    [string]$PosFlycast = "",
    [string]$Saida = (Join-Path $env:TEMP "bs-saves-teste")
)
$ErrorActionPreference = "Stop"
foreach ($x in @($Exe, $ExeMods)) { if (-not (Test-Path -LiteralPath $x)) { Write-Host "SKIP: executavel ausente ($x)"; exit 0 } }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$Exe = (Resolve-Path $Exe).Path; $ExeMods = (Resolve-Path $ExeMods).Path; $Data = (Resolve-Path $Data).Path
$marker = Join-Path $Saida ".bs-saves-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
$rot = Join-Path $PSScriptRoot "roteiros"
$vmuTool = Join-Path $PSScriptRoot "..\dev\n4_vmu.py"

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
# Um caso = um processo headless; env so dele (cmd /c "set ...&& exe")
function Start-Case($nome, $exe, $roteiro, $vblanks, $argsExtra, $envExtra = @{}) {
    $d = Join-Path $Saida $nome
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    $envs = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = (Join-Path $rot $roteiro);
               BS_STATE_PASSO = (Join-Path $d "estado.tsv"); BS_STATE_PASSO_CADA = "10"; BS_FILE_LOG = (Join-Path $d "arquivos.tsv") }
    foreach ($k in $envExtra.Keys) { $envs[$k] = $envExtra[$k] }
    $sets = ($envs.GetEnumerator() | ForEach-Object { "set `"$($_.Key)=$($_.Value)`"" }) -join "&& "
    $line = "$sets&& `"$exe`" --data `"$Data`" --headless $vblanks $argsExtra > `"$(Join-Path $d 'saida.txt')`" 2>&1"
    $p = Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$line`"" -PassThru -WindowStyle Hidden
    return @{ Nome = $nome; Dir = $d; Proc = $p }
}
function Wait-Case($c) { $c.Proc.WaitForExit(); $c.Code = $c.Proc.ExitCode; $c.Out = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $c.Dir "saida.txt"); return $c }  # o jogo escreve caminhos em UTF-8
function State($c, [int]$passo) {
    foreach ($l in Get-Content -LiteralPath (Join-Path $c.Dir "estado.tsv")) {
        $f = $l.Split("`t"); if ($f[0] -eq "$passo") { return @{ Vida = [int]$f[3]; X = [double]$f[4]; Y = [double]$f[5]; Z = [double]$f[6]; Hash = $f[2] } }
    }
    return $null
}
function Near($a, $b) { return $a -and $b -and [math]::Abs($a.X - $b.X) -lt 0.01 -and [math]::Abs($a.Y - $b.Y) -lt 0.01 -and [math]::Abs($a.Z - $b.Z) -lt 0.01 }
function Rooms($c) { @(Get-Content -LiteralPath (Join-Path $c.Dir "arquivos.tsv") | ForEach-Object { $f = $_.Split("`t"); if ($f.Count -gt 5 -and $f[5] -eq "0" -and $f[4] -match '/(R[A-Z0-9]+)\.BIN$') { $Matches[1] } }) }
function ExeCli($argsCli) { $o = cmd /c "`"$Exe`" $argsCli 2>&1" | Out-String; return @{ Code = $LASTEXITCODE; Out = $o } }
function SavesDir($nome) { $p = Join-Path $Saida "saves_$nome"; New-Item -ItemType Directory -Force -Path $p | Out-Null; return $p }

# Orcamento da rota de SAVE (passo_save_rcg02, indexada por passo de jogo): com o video nativo (padrao desde o N5) o
# passo de jogo fica parado durante os filmes, que duram o tempo real: NEWGAME 452,8 s (27 146 VBlanks, no passo 1038) e
# E001 13,7 s (828 VBlanks, no passo 2146); fora deles a rota anda como antes (~2 VBlanks por passo). Os 17 100 VBlanks
# do Sofdec mais os dos filmes nativos: 46 000 (medido em 2026-10-21; o roteiro nao muda, so o orcamento).
$vbSave = 46000
# ---- S1/S2: dois SAVEs de gameplay em paralelo ----
$sv1 = SavesDir "s1"; $sv2 = SavesDir "s2"
$c1 = Start-Case "s1" $ExeMods "passo_save_rcg02.txt" $vbSave "--saves nativo --saves-pasta `"$sv1`"" @{ BS_SAVES_H2 = "7500" }
$c2 = Start-Case "s2" $ExeMods "passo_save_rcg02.txt" $vbSave "--saves nativo --saves-pasta `"$sv2`"" @{ BS_SAVES_H2 = "7500" }
$c1 = Wait-Case $c1; $c2 = Wait-Case $c2
$bsv = Join-Path $sv1 "card0\BLUESTIN.000.bsv"
$m = [regex]::Match($c1.Out, "SAVE (\d+) \(ok (\d+), falha (\d+)\)")
Check "S1 SAVE de gameplay concluido" ($c1.Code -eq 0 -and (Test-Path $bsv) -and $m.Success -and $m.Groups[2].Value -eq "1" -and $m.Groups[3].Value -eq "0") "rc=$($c1.Code) $($m.Value)"
Check "S1 save point simulado so pelo H2" ($c1.Out -match "ponto de save simulado") ""
$vms1 = Join-Path $Saida "s1.vms"; $vms2 = Join-Path $Saida "s2.vms"
$e1 = ExeCli "--saves-pasta `"$sv1`" --saves-exportar `"$vms1`""
$e2 = ExeCli "--saves-pasta `"$sv2`" --saves-exportar `"$vms2`""
$val = cmd /c "python -I `"$vmuTool`" valida `"$vms1`" --bluestinger 2>&1" | Out-String
Check "S1 VMS valido (cabecalho, CRC, 1200 B)" ($e1.Code -eq 0 -and $val -match "(?m)^VALIDO") ((($val -split "`n") | Where-Object { $_ -match "crc" }) -join " ")
$h1 = if (Test-Path $vms1) { (Get-FileHash $vms1).Hash } else { "-" }
$h2 = if (Test-Path $vms2) { (Get-FileHash $vms2).Hash } else { "+" }
Check "S2 determinismo do SAVE (VMS igual)" ($e2.Code -eq 0 -and $h1 -eq $h2) "$(if ($h1) { $h1.Substring(0, [Math]::Min(16, $h1.Length)) } else { 'sem VMS' })"
$atSave = State $c1 8000
$bsvHash = (Get-FileHash $bsv).Hash

# ---- casos de LOAD em paralelo (cada um com a sua pasta) ----
function Copy-Save($nome) { $d = SavesDir $nome; New-Item -ItemType Directory -Force -Path (Join-Path $d "card0") | Out-Null; Copy-Item $bsv (Join-Path $d "card0\BLUESTIN.000.bsv"); return $d }
$s3 = Copy-Save "s3"; $s4 = SavesDir "s4"; $s5 = SavesDir "s5"; $s6 = Copy-Save "s6"
$s7 = Copy-Save "s7"; $b7 = Join-Path $s7 "card0\BLUESTIN.000.bsv"; $bytes = [IO.File]::ReadAllBytes($b7); $bytes[700] = $bytes[700] -bxor 0xFF; [IO.File]::WriteAllBytes($b7, $bytes)
$s8 = Copy-Save "s8"; Move-Item (Join-Path $s8 "card0\BLUESTIN.000.bsv") (Join-Path $s8 "card0\BLUESTIN.000.bsv.bak"); Set-Content -LiteralPath (Join-Path $s8 "card0\BLUESTIN.000.bsv") -Value "lixo"
$s9 = Copy-Save "s9"; Copy-Item $bsv (Join-Path $s9 "card0\BLUESTIN.001.bsv.tmp")
$s10 = Copy-Save "s10"; $b10 = Join-Path $s10 "card0\BLUESTIN.000.bsv"; $bytes = [IO.File]::ReadAllBytes($b10); $bytes[4] = 9; [IO.File]::WriteAllBytes($b10, $bytes); $h10 = (Get-FileHash $b10).Hash
$cases = @(
    (Start-Case "s3" $Exe "passo_load_slot1.txt" 4700 "--saves nativo --saves-pasta `"$s3`""),
    (Start-Case "s4" $Exe "passo_load_slot1.txt" 4700 "--saves nativo --saves-pasta `"$s4`""),
    (Start-Case "s5" $Exe "passo_load_slot1.txt" 4700 "--saves legado --saves-pasta `"$s5\naocriar`""),
    (Start-Case "s6" $Exe "passo_load_slot1.txt" 4700 "--saves-pasta `"$s6`""),
    (Start-Case "s7" $Exe "passo_load_slot1.txt" 4700 "--saves nativo --saves-pasta `"$s7`""),
    (Start-Case "s8" $Exe "passo_load_slot1.txt" 4700 "--saves nativo --saves-pasta `"$s8`""),
    (Start-Case "s9" $Exe "passo_load_slot1.txt" 4700 "--saves nativo --saves-pasta `"$s9`""),
    (Start-Case "s10" $Exe "passo_load_slot1.txt" 4700 "--saves nativo --saves-pasta `"$s10`"")
)
$fly = $null
if ($VmuFlycast -and (Test-Path -LiteralPath $VmuFlycast)) {
    $s12 = SavesDir "s12"; $ext = Join-Path $Saida "flycast_vmu"
    cmd /c "python -I `"$vmuTool`" extrai `"$VmuFlycast`" `"$ext`" 2>&1" | Out-Null
    $imp = ExeCli "--saves-pasta `"$s12`" --saves-importar `"$(Join-Path $ext 'BLUESTIN.000')`""
    Check "S12 importacao do VMS do Flycast" ($imp.Code -eq 0) $imp.Out.Trim()
    $fly = Start-Case "s12" $Exe "passo_load_slot1.txt" 4700 "--saves nativo --saves-pasta `"$s12`""
}
$r = @{}; foreach ($c in $cases) { $r[$c.Nome] = Wait-Case $c }
if ($fly) { $fly = Wait-Case $fly }

$a3 = State $r.s3 2130; $w3 = State $r.s3 2250
Check "S3 LOAD em processo novo: posicao e vida do SAVE" ($r.s3.Code -eq 0 -and (Near $a3 $atSave) -and $a3.Vida -eq $atSave.Vida) "salvo ($($atSave.X), $($atSave.Y), $($atSave.Z)) vida $($atSave.Vida); carregado ($($a3.X), $($a3.Y), $($a3.Z)) vida $($a3.Vida)"
Check "S3 sala do SAVE carregada (RCG02)" ((Rooms $r.s3) -contains "RCG02") ((Rooms $r.s3) -join ",")
Check "S3 partida continua jogavel (anda depois do LOAD)" ($w3 -and [math]::Abs($w3.X - $a3.X) + [math]::Abs($w3.Z - $a3.Z) -gt 50) "($($w3.X), $($w3.Z))"
Check "S3 LOAD nao altera o arquivo" ((Get-FileHash (Join-Path $s3 "card0\BLUESTIN.000.bsv")).Hash -eq $bsvHash) ""
function Title($c) { $s = State $c 2250; return ($c.Code -eq 0 -and $s -and $s.X -eq 0 -and $s.Z -eq 0 -and -not ((Rooms $c) -contains "RCG02")) }
Check "S4 sem save: fica no titulo, sem erro" (Title $r.s4) "rc=$($r.s4.Code)"
# legado: sem VMU o menu segue outro caminho (os mesmos botoes caem no NEW GAME); o que importa e nao carregar
$st5 = @(Get-Content -LiteralPath (Join-Path $r.s5.Dir "estado.tsv") | Select-Object -Skip 1 | ForEach-Object { $f = $_.Split("`t"); @{ X = [double]$f[4]; Y = [double]$f[5]; Z = [double]$f[6] } })
$restored5 = @($st5 | Where-Object { Near $_ $atSave }).Count
Check "S5 legado: sem VMU, o save nao e carregado e nenhuma pasta de saves e criada" ($r.s5.Code -eq 0 -and $restored5 -eq 0 -and -not ((Rooms $r.s5) -contains "RCG02") -and -not (Test-Path (Join-Path $s5 "naocriar"))) "salas $((Rooms $r.s5) -join ',')"
$same6 = $true; foreach ($p in 1660, 2140, 2170, 2250) { $x = State $r.s3 $p; $y = State $r.s6 $p; if (-not $x -or -not $y -or $x.Hash -ne $y.Hash) { $same6 = $false } }
Check "S6 padrao sem --saves = nativo" ($r.s6.Code -eq 0 -and $same6) ""
Check "S7 corrompido: preservado, nada carregado" ((Title $r.s7) -and (Test-Path (Join-Path $s7 "card0\BLUESTIN.000.bsv.corrompido"))) ""
Check "S8 recuperacao pelo .bak" ($r.s8.Code -eq 0 -and (Near (State $r.s8 2130) $atSave) -and $r.s8.Out -match "recuperado pela copia anterior") ""
Check "S9 temporario descartado, save valido carrega" ($r.s9.Code -eq 0 -and (Near (State $r.s9 2130) $atSave) -and -not (Test-Path (Join-Path $s9 "card0\BLUESTIN.001.bsv.tmp"))) ""
Check "S10 versao futura: intacta e nao carregada" ((Title $r.s10) -and (Get-FileHash $b10).Hash -eq $h10) ""
$i10 = ExeCli "--saves-pasta `"$s10`" --saves-importar `"$vms1`""
Check "S10 importacao nao sobrescreve versao futura" ($i10.Code -ne 0 -and (Get-FileHash $b10).Hash -eq $h10) $i10.Out.Trim()

# ---- S11 importar/exportar ----
$s11 = SavesDir "s11"; $back = Join-Path $Saida "volta.vms"; $bad = Join-Path $Saida "ruim.vms"
$i = ExeCli "--saves-pasta `"$s11`" --saves-nome BLUESTIN.002 --saves-importar `"$vms1`""
$o = ExeCli "--saves-pasta `"$s11`" --saves-nome BLUESTIN.002 --saves-exportar `"$back`""
Check "S11 importar/exportar: ida e volta" ($i.Code -eq 0 -and $o.Code -eq 0 -and (Get-FileHash $back).Hash -eq $h1) ""
$bytes = [IO.File]::ReadAllBytes($vms1); $bytes[0x46] = $bytes[0x46] -bxor 0xFF; [IO.File]::WriteAllBytes($bad, $bytes)
$b = ExeCli "--saves-pasta `"$s11`" --saves-nome BLUESTIN.003 --saves-importar `"$bad`""
$n = ExeCli "--saves-pasta `"$s11`" --saves-importar `"$(Join-Path $Saida 'nao_existe.vms')`""
Check "S11 VMS adulterado e inexistente recusados" ($b.Code -ne 0 -and $n.Code -ne 0 -and -not (Test-Path (Join-Path $s11 "card0\BLUESTIN.003.bsv"))) ""

$own = Join-Path $s11 "card0\BLUESTIN.002.bsv"; $hOwn = (Get-FileHash $own).Hash
$x = ExeCli "--saves-pasta `"$s11`" --saves-nome BLUESTIN.002 --saves-exportar `"$own`""
$y = ExeCli "--saves-pasta `"$s11`" --saves-importar `"$vms1`" --saves-exportar `"$back`""
Check "S11 exportar sobre o proprio slot e importar+exportar juntos recusados" ($x.Code -ne 0 -and $y.Code -ne 0 -and (Get-FileHash $own).Hash -eq $hOwn) ""

# ---- S14 headless sem pasta: cartao temporario vazio do processo, apagado no fim ----
$c14 = Wait-Case (Start-Case "s14" $Exe "passo_load_slot1.txt" 1700 "")
$m14 = [regex]::Match($c14.Out, "\[saves\] nativo: (.+)")
$tmp14 = if ($m14.Success) { $m14.Groups[1].Value.Trim() } else { "" }
Check "S14 headless sem --saves-pasta: cartao temporario, apagado no fim" ($c14.Code -eq 0 -and $tmp14 -match "bs-saves-headless-\d+$" -and -not (Test-Path -LiteralPath $tmp14)) $tmp14

# ---- S15-S18: modo portatil pela instalacao (instalacao.toml), executavel aberto direto ----
function New-Install($nome, $exe, [string]$cfg) {
    $d = Join-Path $Saida $nome
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    Copy-Item -LiteralPath $exe -Destination (Join-Path $d "BlueStinger.exe")
    if ($cfg) { [IO.File]::WriteAllText((Join-Path $d "instalacao.toml"), $cfg) }
    return $d
}
$portatil = "[saves]`nlocal = `"portatil`"`n"
$i15 = New-Install "inst_p" $Exe $portatil
New-Item -ItemType Directory -Force -Path (Join-Path $i15 "saves\card0") | Out-Null
Copy-Item $bsv (Join-Path $i15 "saves\card0\BLUESTIN.000.bsv")
$i17 = New-Install "inst_s" $ExeMods $portatil
$x18 = SavesDir "s18"
$i18 = New-Install "inst_x" $Exe $portatil
$i18b = New-Install "inst_sem" $Exe ""
$i18c = New-Install "inst_inv" $Exe "isto [nao e toml"
$cs = @(
    (Start-Case "s15" (Join-Path $i15 "BlueStinger.exe") "passo_load_slot1.txt" 4700 ""),
    (Start-Case "s17" (Join-Path $i17 "BlueStinger.exe") "passo_save_rcg02.txt" $vbSave "" @{ BS_SAVES_H2 = "7500" }),
    (Start-Case "s18a" (Join-Path $i18 "BlueStinger.exe") "passo_load_slot1.txt" 1700 "--saves-pasta `"$x18`""),
    (Start-Case "s18b" (Join-Path $i18b "BlueStinger.exe") "passo_load_slot1.txt" 1700 ""),
    (Start-Case "s18c" (Join-Path $i18c "BlueStinger.exe") "passo_load_slot1.txt" 1700 "")
)
$rp = @{}; foreach ($c in $cs) { $rp[$c.Nome] = Wait-Case $c }
function Root($c) { $m = [regex]::Match($c.Out, "\[saves\] nativo: (.+)"); if ($m.Success) { $m.Groups[1].Value.Trim() } else { "" } }
$a15 = State $rp.s15 2130
Check "S15 instalacao portatil: o executavel aberto direto carrega de <instalacao>\saves" ($rp.s15.Code -eq 0 -and (Root $rp.s15) -eq (Join-Path $i15 "saves") -and $rp.s15.Out -match "modo portatil \(instalacao.toml\)" -and (Near $a15 $atSave)) "pasta $(Root $rp.s15)"
$i16 = Join-Path $Saida "inst_movida ç"
Move-Item -LiteralPath $i15 -Destination $i16
$c16 = Wait-Case (Start-Case "s16" (Join-Path $i16 "BlueStinger.exe") "passo_load_slot1.txt" 4700 "")
Check "S16 instalacao portatil movida: continua com os proprios saves" ($c16.Code -eq 0 -and (Root $c16) -eq (Join-Path $i16 "saves") -and (Near (State $c16 2130) $atSave)) "pasta $(Root $c16)"
$m17 = [regex]::Match($rp.s17.Out, "SAVE (\d+) \(ok (\d+), falha (\d+)\)")
Check "S17 SAVE de gameplay na instalacao portatil grava em <instalacao>\saves" ($rp.s17.Code -eq 0 -and (Test-Path (Join-Path $i17 "saves\card0\BLUESTIN.000.bsv")) -and $m17.Success -and $m17.Groups[2].Value -eq "1") "$($m17.Value)"
$okA = (Root $rp.s18a) -eq $x18
$okB = (Root $rp.s18b) -match "bs-saves-headless-\d+$" -and -not (Test-Path (Join-Path $i18b "saves"))
$okC = (Root $rp.s18c) -match "bs-saves-headless-\d+$" -and $rp.s18c.Out -match "instalacao.toml ilegivel"
Check "S18 precedencia: --saves-pasta > instalacao.toml; sem ele, cartao temporario; invalido avisa" ($okA -and $okB -and $okC -and $rp.s18a.Code -eq 0 -and $rp.s18b.Code -eq 0 -and $rp.s18c.Code -eq 0) "pasta=$okA sem=$okB invalido=$okC"

if ($fly) {
    $f = State $fly 2130; $fw = State $fly 2250; $pos = $PosFlycast.Split(",") | ForEach-Object { [double]$_ }
    $ok = $f -and $pos.Count -eq 3 -and [math]::Abs($f.X - $pos[0]) -lt 0.01 -and [math]::Abs($f.Y - $pos[1]) -lt 0.01 -and [math]::Abs($f.Z - $pos[2]) -lt 0.01
    Check "S12 Flycast -> Windows: LOAD com a posicao do Flycast" ($fly.Code -eq 0 -and $ok) "Flycast ($PosFlycast); Windows ($($f.X), $($f.Y), $($f.Z)) vida $($f.Vida)"
    Check "S12 partida do Flycast continua jogavel no Windows" ($fw -and [math]::Abs($fw.X - $f.X) + [math]::Abs($fw.Z - $f.Z) -gt 50) "($($fw.X), $($fw.Z))"
} else { Write-Host "[SKIP] S12 Flycast -> Windows (sem -VmuFlycast)" }

$diag = @($r.Values | Where-Object { $_.Out -match "BS_SAVES_H" })
Check "S13 produto sem diagnosticos BS_SAVES_H1/H2" ($diag.Count -eq 0) ""

$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("test_saves: {0} ok, {1} falhas" -f ($results.Count - $fail), $fail)
exit $(if ($fail) { 1 } else { 0 })
