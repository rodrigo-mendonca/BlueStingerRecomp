# Testa a distribuicao do Builder fora do repositorio: copia dist/BlueStinger-Builder para um
# caminho com espacos e caracteres fora da pagina ANSI (o kit vai para o cache), usa a imagem
# por hard link num caminho especial e roda os casos de sucesso e de falha.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_builder.ps1 -Disc <imagem.cdi> [-Builder <pasta>] [-Work <pasta>]
#
# Codigo de saida 0 = todos os casos passaram. Leva alguns minutos (3 compilacoes completas).
param(
    [Parameter(Mandatory = $true)][string]$Disc,
    [string]$Builder = "",
    [string]$Work = ""
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Builder) { $Builder = Join-Path $repo "dist\BlueStinger-Builder" }
if (-not $Work) { $Work = $env:TEMP }
$Disc = (Resolve-Path -LiteralPath $Disc).Path

# tudo numa subpasta marcada por este script: so ela e apagada
$Work = Join-Path ([IO.Path]::GetFullPath($Work)) "bs-builder-teste"
$marker = Join-Path $Work ".bs-builder-teste"
if (Test-Path -LiteralPath $Work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Work existe e nao foi criada por este teste"; exit 2 }
    icacls "$Work" /reset /T /Q | Out-Null
    Remove-Item -Recurse -Force -LiteralPath $Work
}
New-Item -ItemType Directory -Path $Work | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
# N5: as compilacoes destes casos nao tratam dos filmes (--sem-filmes); o caso 13 os converte.
function Run($exe, [string[]]$argv, $timeoutMs = 1800000, [switch]$Filmes) {
    if (-not $Filmes -and (Split-Path -Leaf $exe) -eq "bs-builder.exe") { $argv = @($argv) + "--sem-filmes" }
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe
    $psi.Arguments = ($argv | ForEach-Object { if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [Text.Encoding]::UTF8
    $psi.WorkingDirectory = $env:SystemRoot
    $t0 = Get-Date
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($timeoutMs)) { $p.Kill(); return @{ Code = -1; Out = "tempo esgotado"; Secs = 0 } }
    return @{ Code = $p.ExitCode; Out = $o.Result + $e.Result; Secs = [int]((Get-Date) - $t0).TotalSeconds }
}
function FailLine($r) { ($r.Out -split "`n" | Where-Object { $_ -match "falhou" } | Select-Object -First 1) }
function Hash($f) { if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f).Hash } else { "" } }
function NoStage($out) { @(Get-ChildItem -LiteralPath $out -Directory -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "BlueStinger.new-*" -or $_.Name -like "BlueStinger.old-*" }).Count -eq 0 }

# --- preparacao ---
$app = Join-Path $Work "Builder ç 日本 (teste)"
Copy-Item -Recurse -LiteralPath $Builder -Destination $app
$cli = Join-Path $app "bs-builder.exe"
$gui = Join-Path $app "BlueStinger-Developer.exe"
$rec = Join-Path $app "BlueStinger-Recompiler.exe"
$discDir = Join-Path $Work "Jogos ç ã 日本"
New-Item -ItemType Directory -Path $discDir | Out-Null
$cdi = Join-Path $discDir "Blue Stinger (cópia) ç.cdi"
cmd /c "mklink /H `"$cdi`" `"$Disc`"" | Out-Null
if (-not (Test-Path -LiteralPath $cdi)) { Copy-Item -LiteralPath $Disc -Destination $cdi }
$env:BS_BUILDER_CACHE = Join-Path $Work "cache"  # cache proprio: compilacoes limpas e nada fora da pasta de teste
$out1 = Join-Path $Work "Saída ç 日本\BS"
$out2 = Join-Path $Work "outra saída"
$exe1 = Join-Path $out1 "BlueStinger\BlueStinger.exe"

# 1. compilacao completa a partir de uma saida nova (kit em caminho fora do ANSI -> copia no cache)
$r = Run $cli @("--disc", $cdi, "--out", $out1)
Check "compilacao completa (saida nova)" ($r.Code -eq 0 -and (Test-Path -LiteralPath $exe1) -and (NoStage $out1)) "codigo $($r.Code), $($r.Secs) s"
$log1 = [IO.File]::ReadAllText((Join-Path $out1 ".bs-build\builder.log"), [Text.Encoding]::UTF8)
Check "kit copiado para o cache (caminho fora do ANSI)" ($log1 -match "copiando o kit para") ""
$v = Run $exe1 @("--version")
Check "jogo instalado responde" ($v.Code -eq 0 -and $v.Out -match "Blue Stinger \(recompilado\)") $v.Out.Trim()
$installed = Hash $exe1

# 1b. independencia do CDI: imagem inacessivel (renomeada) e instalacao movida para outra pasta
$hidden = "$cdi.inacessivel"
Rename-Item -LiteralPath $cdi -NewName ([IO.Path]::GetFileName($hidden))
try {
    $h = Run $exe1 @("--headless", "300") 600000
    Check "sem o CDI: jogo instalado da boot (300 VBlanks)" ($h.Code -eq 0 -and $h.Out -match "VBlanks: 300;") ""
    $movedRoot = Join-Path $Work "movido ç 日本"
    New-Item -ItemType Directory -Path $movedRoot | Out-Null
    $moved = Join-Path $movedRoot "BlueStinger"
    Move-Item -LiteralPath (Join-Path $out1 "BlueStinger") -Destination $moved
    try {
        $h = Run (Join-Path $moved "BlueStinger.exe") @("--headless", "300") 600000
        $vd = Run (Join-Path $moved "BlueStinger.exe") @("--verify-data") 600000
        Check "instalacao movida, sem o CDI: boot e dados conferidos" ($h.Code -eq 0 -and $h.Out -match "VBlanks: 300;" -and $vd.Code -eq 0) "boot $($h.Code), verify-data $($vd.Code)"
        # nenhum caminho do CDI nem da maquina de desenvolvimento nos arquivos do jogo
        $needles = @($cdi, $Disc, $repo) | ForEach-Object { $_, $_.Replace('\', '/') }
        $leak = @()
        foreach ($f in Get-ChildItem -LiteralPath $moved -File) {
            $bytes = [IO.File]::ReadAllBytes($f.FullName)
            foreach ($t in [Text.Encoding]::UTF8.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes)) {
                foreach ($n in $needles) { if ($t.IndexOf($n, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $leak += "$($f.Name): $n" } }
            }
        }
        Check "nenhum caminho do CDI ou do desenvolvimento na instalacao" ($leak.Count -eq 0) ($leak | Select-Object -Unique | Select-Object -First 3)
        # arquivo de dados ausente: mensagem clara (qual arquivo, o que fazer)
        $victim = Join-Path $moved "data\disc\BSDATA\CDG000.NJ"
        Rename-Item -LiteralPath $victim -NewName "CDG000.NJ.fora"
        $h = Run (Join-Path $moved "BlueStinger.exe") @("--headless", "60") 120000
        Rename-Item -LiteralPath "$victim.fora" -NewName "CDG000.NJ"
        Check "dado ausente: mensagem clara" ($h.Code -ne 0 -and $h.Out -match "CDG000.NJ \(ausente\)" -and $h.Out -match "Builder") (($h.Out -split "`n" | Where-Object { $_ -match "erro" } | Select-Object -First 1))
    } finally {
        Move-Item -LiteralPath $moved -Destination (Join-Path $out1 "BlueStinger")
    }
} finally {
    Rename-Item -LiteralPath $hidden -NewName ([IO.Path]::GetFileName($cdi))
}

# 2. reprodutivel: outra saida nova gera o mesmo C++
$r = Run $cli @("--disc", $cdi, "--out", $out2)
$gen = @(Get-ChildItem -LiteralPath (Join-Path $env:BS_BUILDER_CACHE "build") -Directory | ForEach-Object { Join-Path $_.FullName "generated" })
$same = $gen.Count -eq 2 -and -not (Compare-Object (Get-ChildItem -LiteralPath $gen[0] -File | ForEach-Object { "$($_.Name) $((Get-FileHash -LiteralPath $_.FullName).Hash)" }) (Get-ChildItem -LiteralPath $gen[1] -File | ForEach-Object { "$($_.Name) $((Get-FileHash -LiteralPath $_.FullName).Hash)" }))
Check "segunda saida nova: sucesso e C++ identico" ($r.Code -eq 0 -and $same) "codigo $($r.Code), $($r.Secs) s, caches $($gen.Count)"

# 3. incremental: so o que mudou
$r = Run $cli @("--disc", $cdi, "--out", $out1)
$steps = @([IO.File]::ReadAllLines((Join-Path $out1 ".bs-build\builder.log"), [Text.Encoding]::UTF8) | Where-Object { $_ -match '^\[\d+/\d+\]' }).Count
Check "recompilacao incremental" ($r.Code -eq 0 -and $steps -le 5) "codigo $($r.Code), $($r.Secs) s, $steps passos do ninja"
$installed = Hash $exe1
[IO.File]::WriteAllText((Join-Path $out1 "BlueStinger\bluestinger.ini"), "cdi=preservado")

# 4. imagem invalida
$fake = Join-Path $discDir "falso ç.cdi"
[IO.File]::WriteAllBytes($fake, (New-Object byte[] 4096))
$r = Run $cli @("--disc", $fake, "--out", $out1)
Check "imagem invalida recusada, instalada intacta" ($r.Code -eq 1 -and $r.Out -match "nao pode ser usada" -and (Hash $exe1) -eq $installed -and (NoStage $out1)) (FailLine $r)

# 5. sem compilador
$r = Run $cli @("--disc", $cdi, "--out", $out1, "--vswhere", (Join-Path $Work "nao existe\vswhere.exe"))
Check "sem compilador: mensagem de instalacao" ($r.Code -eq 1 -and $r.Out -match "Build Tools" -and (Hash $exe1) -eq $installed) (FailLine $r)

# 6. saida sem permissao
$ro = Join-Path $Work "sem permissão"
New-Item -ItemType Directory -Path $ro | Out-Null
$sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
icacls "$ro" /deny "*${sid}:(OI)(CI)(W,AD,WD)" | Out-Null
$r = Run $cli @("--disc", $cdi, "--out", (Join-Path $ro "BS"))
icacls "$ro" /remove:d "*$sid" | Out-Null
Check "saida sem permissao recusada" ($r.Code -eq 1 -and $r.Out -match "permissao|criar a pasta") (FailLine $r)

# 7. caminho de saida longo demais
$long = Join-Path $Work ("x" * 120 + "\" + "y" * 60)
$r = Run $cli @("--disc", $cdi, "--out", $long)
Check "saida longa demais recusada" ($r.Code -eq 1 -and $r.Out -match "longo demais") (FailLine $r)

# 8. falha de compilacao (kit com erro de sintaxe): nada de sucesso, instalada intacta
$bad = Join-Path $Work "kit quebrado"
Copy-Item -Recurse -LiteralPath (Join-Path $app "kit") -Destination $bad
Add-Content -LiteralPath (Join-Path $bad "runtime\pvr\pvr_video.cpp") -Value "`nesta linha nao compila;`n"
$r = Run $cli @("--disc", $cdi, "--out", $out1, "--kit", $bad)
Check "falha de compilacao: erro, instalada intacta" ($r.Code -eq 1 -and $r.Out -match 'falhou em "compilando"' -and (Hash $exe1) -eq $installed -and (NoStage $out1)) (FailLine $r)

# 9. jogo aberto: a troca falha e a instalada continua
$game = Start-Process -FilePath $exe1 -ArgumentList "--headless 1000000" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$r = Run $cli @("--disc", $cdi, "--out", $out1)
Stop-Process -Id $game.Id -Force; $game.WaitForExit()
Check "jogo aberto: troca recusada, instalada intacta" ($r.Code -eq 1 -and $r.Out -match 'falhou em "instalando"' -and (Hash $exe1) -eq $installed -and (NoStage $out1)) (FailLine $r)

# 10. janela: cancelar no meio (fechar em modo --auto) -> codigo 3, instalada intacta
$p = Start-Process -FilePath $gui -ArgumentList "--disc `"$cdi`" --out `"$out1`" --auto --sem-filmes" -PassThru
$t0 = Get-Date; while ($p.MainWindowHandle -eq 0 -and ((Get-Date) - $t0).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 200; $p.Refresh() }
Start-Sleep -Seconds 3
$p.CloseMainWindow() | Out-Null
$closed = $p.WaitForExit(120000)
Check "janela: cancelar mantem a instalada" ($closed -and $p.ExitCode -eq 3 -and (Hash $exe1) -eq $installed -and (NoStage $out1)) "codigo $($p.ExitCode)"

# 11. Recompiler (janela simples) em modo --auto ate o fim, com o modo portatil; arquivos do usuario
# preservados e instalacao.toml gravado na instalacao (o jogo o le ao abrir direto)
$p = Start-Process -FilePath $rec -ArgumentList "--disc `"$cdi`" --out `"$out1`" --portatil --idioma en-US --auto --sem-filmes" -PassThru
$closed = $p.WaitForExit(1800000)
$ini = [IO.File]::ReadAllText((Join-Path $out1 "BlueStinger\bluestinger.ini"))
$cfgFile = Join-Path $out1 "BlueStinger\instalacao.toml"
$cfg = if (Test-Path -LiteralPath $cfgFile) { [IO.File]::ReadAllText($cfgFile) } else { "" }
Check "Recompiler: compilacao ate o fim, ini do usuario preservado, modo portatil gravado" ($closed -and $p.ExitCode -eq 0 -and $ini -eq "cdi=preservado" -and $cfg -match 'local = "portatil"') "codigo $($p.ExitCode)"
$installed = Hash $exe1  # a janela instalou uma versao nova

# 11b. Developer (sem escolha de modo) recompila: o instalacao.toml da instalacao fica como estava
$p = Start-Process -FilePath $gui -ArgumentList "--disc `"$cdi`" --out `"$out1`" --auto --sem-filmes" -PassThru
$closed = $p.WaitForExit(1800000)
$cfg = if (Test-Path -LiteralPath $cfgFile) { [IO.File]::ReadAllText($cfgFile) } else { "" }
Check "Developer: recompilar preserva o modo portatil da instalacao" ($closed -and $p.ExitCode -eq 0 -and $cfg -match 'local = "portatil"') "codigo $($p.ExitCode)"
$installed = Hash $exe1

# 12. sem o kit ao lado: recusa clara
$nokit = Join-Path $Work "sem kit"
New-Item -ItemType Directory -Path $nokit | Out-Null
Copy-Item (Join-Path $app "*.exe"), (Join-Path $app "*.dll") $nokit
$r = Run (Join-Path $nokit "bs-builder.exe") @("--disc", $cdi, "--out", $out1)
Check "sem kit: recusa clara" ($r.Code -eq 1 -and $r.Out -match "kit de fontes incompleto" -and (Hash $exe1) -eq $installed) (FailLine $r)

# 13. N5: filmes convertidos pelo Builder (data\video com o manifesto dos 17 IDs) e reaproveitados
# por SHA-256 na compilacao seguinte; o pacote os leva
$r = Run $cli @("--disc", $cdi, "--out", $out1) -Filmes
$man = Join-Path $out1 "BlueStinger\data\video\manifest.toml"
$blog = Join-Path $out1 ".bs-build\builder.log"  # a CLI so mostra o log com BS_BUILDER_VERBOSE
$conv = [regex]::Match([IO.File]::ReadAllText($blog), "filmes: (\d+) convertidos, (\d+) reaproveitados, (\d+) ausentes")
$mp4 = @(Get-ChildItem -LiteralPath (Join-Path $out1 "BlueStinger\data\video") -Filter *.mp4 -ErrorAction SilentlyContinue).Count
$ids = if (Test-Path -LiteralPath $man) { ([regex]::Matches([IO.File]::ReadAllText($man), "(?m)^id = ")).Count } else { 0 }
Check "filmes: 16 convertidos, OPENING ausente, manifesto com os 17 IDs no pacote" ($r.Code -eq 0 -and $conv.Success -and $conv.Groups[1].Value -eq "16" -and $conv.Groups[3].Value -eq "1" -and $mp4 -eq 16 -and $ids -eq 17) "$($conv.Value); mp4 no pacote: $mp4; IDs: $ids"
$r = Run $cli @("--disc", $cdi, "--out", $out1) -Filmes
$conv = [regex]::Match([IO.File]::ReadAllText($blog), "filmes: (\d+) convertidos, (\d+) reaproveitados")
Check "filmes: reaproveitados na compilacao seguinte (SHA-256)" ($r.Code -eq 0 -and $conv.Success -and $conv.Groups[1].Value -eq "0" -and $conv.Groups[2].Value -eq "16") $conv.Value
$r = Run $cli @("--disc", $cdi, "--out", $out1)
Check "--sem-filmes: os filmes da compilacao anterior saem do pacote" ($r.Code -eq 0 -and -not (Test-Path -LiteralPath $man)) (FailLine $r)

$env:BS_BUILDER_CACHE = ""
$fails = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas" -f $results.Count, $fails)
if (-not $fails) { icacls "$Work" /reset /T /Q | Out-Null; Remove-Item -Recurse -Force -LiteralPath $Work }
exit $(if ($fails) { 1 } else { 0 })
