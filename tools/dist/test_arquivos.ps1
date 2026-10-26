# N1: portoes do sistema de arquivos nativo (docs/N1-PLANO.md, secao 4).
# Roda o mesmo roteiro com --arquivos legado e --arquivos nativo (BS_FILE_LOG) e confere:
#   P1  mesmas leituras de dado de arquivo: (arquivo, deslocamento, SHA-256) por arquivo;
#       conjunto total e ordem global comparados e relatados;
#   P2  no modo nativo, nenhum setor de dado de arquivo lido pelo GD-ROM HLE;
#   P3  o modo nativo termina o roteiro sem erro (quadro final registrado);
#   P8  o modo legado (rollback) termina o roteiro sem erro.
#   powershell -File tools/dist/test_arquivos.ps1 [-Exe <bluestinger.exe>] [-Data <data>] [-VBlanks 8000]
param(
    # N8.3: --arquivos legado (modelo HLE do GD-ROM) so existe no build de validacao; o produto o recusa
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-validacao\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$VBlanks = 8000,
    [string]$Saida = (Join-Path $env:TEMP "bs-arquivos-teste")
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "SKIP: executavel ausente ($Exe)"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "SKIP: dados ausentes ($Data)"; exit 0 }
$marker = Join-Path $Saida ".bs-arquivos-teste"
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
function Run($prov) {
    $log = Join-Path $Saida "leituras_$prov.tsv"
    $bmp = Join-Path $Saida "quadro_$prov.bmp"
    $env:BS_INPUT_SCRIPT = (Join-Path $PSScriptRoot "roteiros\newgame_area1.txt")
    $env:BS_FILE_LOG = $log
    try { $out = cmd /c "`"$Exe`" --data `"$Data`" --arquivos $prov --video legado --headless $VBlanks --dump `"$bmp`" 2>&1" | Out-String }
    finally { $env:BS_INPUT_SCRIPT = ""; $env:BS_FILE_LOG = "" }
    return @{ Code = $LASTEXITCODE; Log = $log; Bmp = $bmp; Out = $out }
}
# Arquivos com o mesmo conteudo no disco (mesmo LBA e tamanho: 88 grupos, p.ex. IWP15.NT e
# IWP20.NT) sao indistinguiveis no log do legado, que identifica o arquivo pelo setor. A
# comparacao usa o grupo (o primeiro nome do grupo), nao o nome adivinhado pelo setor.
$alias = @{}
$byLoc = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $Data "disc.manifest")) {
    if ($line -match '^file (\d+) (\d+) \S+ (.+)$') {
        $loc = "$($Matches[1]):$($Matches[2])"; $name = $Matches[3].ToUpper()
        if (-not $byLoc.ContainsKey($loc)) { $byLoc[$loc] = $name }
        $alias[$name] = $byLoc[$loc]
    }
}
# blocos de dado: grupo do arquivo, deslocamento, sha
function Blocks($log, $origem) {
    Get-Content -LiteralPath $log | Select-Object -Skip 1 | ForEach-Object {
        $f = $_.Split("`t")
        if ($f[3] -eq "dado" -and $f[2] -eq $origem) {
            $g = if ($alias.ContainsKey($f[4])) { $alias[$f[4]] } else { $f[4] }
            "{0}`t{1}`t{2}" -f $g, $f[5], $f[7]
        }
    }
}

$leg = Run "legado"
$nat = Run "nativo"
Check "P8 legado termina o roteiro" ($leg.Code -eq 0) "rc=$($leg.Code)"
Check "P3 nativo termina o roteiro" ($nat.Code -eq 0) "rc=$($nat.Code)"
$opens = [regex]::Match($nat.Out, "\[arquivos\] nativo: (\d+) aberturas, (\d+) leituras, (\d+) bytes, (\d+) nao encontrados")
Check "nativo: arquivos abertos pelo provedor nativo" ($opens.Success -and [int]$opens.Groups[1].Value -gt 0 -and [int]$opens.Groups[4].Value -eq 0) $opens.Value

$L = @(Blocks $leg.Log "gdrom")
$N = @(Blocks $nat.Log "nativo")
$NG = @(Blocks $nat.Log "gdrom")
Check "P2 nativo sem setor de arquivo pelo GD-ROM" ($NG.Count -eq 0) "setores de arquivo pelo GD-ROM no modo nativo: $($NG.Count)"
# P1 por arquivo
$byL = @{}; $byN = @{}
foreach ($b in $L) { $k = $b.Split("`t")[0]; if (-not $byL.ContainsKey($k)) { $byL[$k] = New-Object System.Collections.Generic.List[string] }; $byL[$k].Add($b) }
foreach ($b in $N) { $k = $b.Split("`t")[0]; if (-not $byN.ContainsKey($k)) { $byN[$k] = New-Object System.Collections.Generic.List[string] }; $byN[$k].Add($b) }
$same = 0; $diff = @(); $onlyL = @(); $onlyN = @()
foreach ($k in $byL.Keys) {
    if (-not $byN.ContainsKey($k)) { $onlyL += $k }
    elseif (($byL[$k] -join "|") -eq ($byN[$k] -join "|")) { ++$same }
    else { $diff += $k }
}
foreach ($k in $byN.Keys) { if (-not $byL.ContainsKey($k)) { $onlyN += $k } }
Check "P1 mesmas leituras nos arquivos lidos pelos dois" ($diff.Count -eq 0) "iguais=$same diferentes=$($diff.Count) $($diff -join ',')"
# revisao Codex: o conjunto de arquivos tambem tem de ser o mesmo (um lado vazio nao passa)
Check "P1 mesmo conjunto de arquivos e de blocos" ($onlyL.Count -eq 0 -and $onlyN.Count -eq 0 -and $L.Count -eq $N.Count -and $L.Count -gt 0) "so legado: $($onlyL -join ','); so nativo: $($onlyN -join ','); blocos $($L.Count)/$($N.Count)"
$prefix = 0
for ($i = 0; $i -lt [math]::Min($L.Count, $N.Count); ++$i) { if ($L[$i] -ne $N[$i]) { break }; ++$prefix }
Write-Host ("     blocos: legado={0} nativo={1}; ordem global igual ate o bloco {2}; so legado: {3}; so nativo: {4}" -f `
    $L.Count, $N.Count, $prefix, ($onlyL -join ","), ($onlyN -join ","))
$hL = if (Test-Path $leg.Bmp) { (Get-FileHash $leg.Bmp).Hash.Substring(0, 16) } else { "-" }
$hN = if (Test-Path $nat.Bmp) { (Get-FileHash $nat.Bmp).Hash.Substring(0, 16) } else { "-" }
Write-Host "     quadro final: legado=$hL nativo=$hN (diferenca de tempo de carga: ver docs/N1-RELATORIO.md)"

$fail = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas; logs em {2}" -f $results.Count, $fail, $Saida)
exit $(if ($fail) { 1 } else { 0 })
