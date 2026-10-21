# N6 fase E (docs/N6-PLANO.md secao 16): falas pelo provedor nativo contra o legado no mesmo roteiro por passo.
# O legado nao serve de referencia para o FIM das falas (sem a AICA a posicao nao anda: a fala nunca chega ao estado 3),
# entao o criterio e:
#   - quadros iguais ao legado;
#   - estado do jogo igual ao legado ate o primeiro fim natural de uma fala nativa (a divergencia legitima: o original,
#     no Flycast, tambem termina a fala e o jogo libera a fala seguinte e devolve a musica);
#   - cada fala nativa com o contrato medido no Flycast: 3 passos em "preparando" e o estado 3 dois passos depois do
#     fim do audio; eventos de dialogo (inicio, fim, interrupcao); nenhuma chamada inesperada, de outro handle ou falha;
#   - a fala de dialogo E002A tocada (o legado nunca a toca: a fala anterior nunca acaba);
#   - com --legendas e um pacote de teste, a legenda ativa no log ("[legenda] voz/e012aenv").
#   powershell -File tools/dev/n6_falas.ps1 -Exe <bluestinger.exe com os slots das falas> [-VBlanks N] [-Data <data>]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Rota = "",
    [int64]$VBlanks = 8000,
    [string]$Data = "",
    [string]$Saida = (Join-Path $env:TEMP "bs-n6-falas")
)
$ErrorActionPreference = "Stop"
# padroes no corpo: no PowerShell 5.1 o $PSScriptRoot fica vazio nos valores padrao do param
if (-not $Rota) { $Rota = Join-Path $PSScriptRoot "..\dist\roteiros\passo_combate.txt" }
if (-not $Data) { $Data = Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data" }
New-Item -ItemType Directory -Force $Saida | Out-Null
$falhas = 0
function Check($nome, [bool]$ok, $det = "") { Write-Host ("[{0}] {1} {2}" -f $(if ($ok) { "ok" } else { "FALHA" }), $nome, $det); if (-not $ok) { $script:falhas++ } }

# pacote de legendas de teste ao lado do executavel (mods\<pacote>), removido no fim
$mods = Join-Path (Split-Path $Exe) "mods"
$pacote = Join-Path $mods "n6-falas-teste"
New-Item -ItemType Directory -Force (Join-Path $pacote "legendas") | Out-Null
[IO.File]::WriteAllText((Join-Path $pacote "mod.toml"), "formato = 1`nid = `"n6-falas-teste`"`nnome = `"teste das falas (N6)`"`nprioridade = 1`n")
[IO.File]::WriteAllText((Join-Path $pacote "legendas\falas.pt-BR.toml"), "[[fala]]`nvoz = `"voz/e012aenv`"`ninicio = 0.0`nfim = 5.0`ntexto = `"[teste]`"`n")
try {
    foreach ($m in "legado", "nativo") {
        Remove-Item "$Saida\$m.*" -ErrorAction SilentlyContinue
        $env:BS_RELOGIO_FIXO = "1700000000"; $env:BS_INPUT_SCRIPT = (Resolve-Path $Rota).Path
        $env:BS_FRAME_LOG = "$Saida\$m.fr.tsv"; $env:BS_STATE_PASSO = "$Saida\$m.st.tsv"; $env:BS_STATE_PASSO_CADA = "1"
        $env:BS_EFEITOS_LOG = "$Saida\$m.log.tsv"
        try {
            cmd /c "`"$Exe`" --data `"$Data`" --audio legado --audio-falas $m --legendas pt-BR --video legado --headless $VBlanks > `"$Saida\$m.out`" 2>&1"
            $rc = $LASTEXITCODE
        } finally {
            foreach ($k in "BS_RELOGIO_FIXO", "BS_INPUT_SCRIPT", "BS_FRAME_LOG", "BS_STATE_PASSO", "BS_STATE_PASSO_CADA", "BS_EFEITOS_LOG") { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
        }
        Check "$m termina sem erro" ($rc -eq 0) "rc=$rc"
    }
} finally {
    Remove-Item -Recurse -Force $pacote -ErrorAction SilentlyContinue
}

$resumo = (Select-String -Path "$Saida\nativo.out" -Pattern "\[audio\] falas nativas:" | Select-Object -Last 1).Line
$m = [regex]::Match("$resumo", "start (\d+).*ausentes (\d+).*falhas de leitura (\d+), outros handles (\d+), inesperadas (\d+)")
Check "nativo: falas tocadas, sem ausentes, falhas, outros handles ou chamadas inesperadas" ($m.Success -and [int]$m.Groups[1].Value -gt 0 -and
      [int]$m.Groups[2].Value -eq 0 -and [int]$m.Groups[3].Value -eq 0 -and [int]$m.Groups[4].Value -eq 0 -and [int]$m.Groups[5].Value -eq 0) "$resumo"
Check "legado: o provedor nativo nao foi instalado" (-not (Select-String -Path "$Saida\legado.out" -Pattern "falas nativas" -Quiet)) ""

# contrato por fala, pelo log nativo
$linhas = Get-Content "$Saida\nativo.log.tsv" | Select-Object -Skip 1 | ForEach-Object { $c = $_.Split("`t"); [pscustomobject]@{ passo = [int64]$c[0]; tipo = $c[2]; det = $c[3] } }
$cur = $null; $prep = @(); $fim = @(); $primeiroFim = $null; $nomes = @()
foreach ($l in $linhas) {
    if ($l.tipo -eq "fala-start") { $cur = @{ start = $l.passo; nome = $l.det.Split("=")[1] }; $nomes += $cur.nome }
    elseif ($l.tipo -eq "fala" -and $l.det.StartsWith("inicio")) { $prep += ($l.passo - $cur.start) }
    elseif ($l.tipo -eq "fala" -and $l.det.StartsWith("fim-fisico")) { $cur.fis = $l.passo }
    elseif ($l.tipo -eq "fala-estado" -and $l.det -match "\(3\)") { $fim += ($l.passo - $cur.fis); if (-not $primeiroFim) { $primeiroFim = $l.passo } }
}
Check "preparo de 3 passos em todas as falas (Flycast: 3)" ($prep.Count -gt 0 -and -not ($prep | Where-Object { $_ -ne 3 })) "($($prep -join ', '))"
Check "estado 3 dois passos depois do fim do audio (Flycast: 1,8 a 2,3)" ($fim.Count -gt 0 -and -not ($fim | Where-Object { $_ -ne 2 })) "($($fim -join ', '))"
Check "a fala de dialogo E002A tocou" ($nomes -contains "voz/e002a") "($($nomes -join ', '))"
Check "eventos de dialogo: inicio, fim e interrupcao" ((Select-String -Path "$Saida\nativo.log.tsv" -Pattern "`tfala`tinicio" -Quiet) -and
      (Select-String -Path "$Saida\nativo.log.tsv" -Pattern "`tfala`tfim-logico" -Quiet) -and (Select-String -Path "$Saida\nativo.log.tsv" -Pattern "`tfala`tinterrompida" -Quiet)) ""
Check "legenda de teste ativa no log" (Select-String -Path "$Saida\nativo.out" -Pattern "\[legenda\] .*voz/e012aenv" -Quiet) ""

$a = [IO.File]::ReadAllLines("$Saida\legado.fr.tsv"); $b = [IO.File]::ReadAllLines("$Saida\nativo.fr.tsv")
Check "quadros iguais ao legado" ($a.Count -eq $b.Count -and (Compare-Object $a $b -SyncWindow 0 | Measure-Object).Count -eq 0) "$($a.Count) linhas"
$a = [IO.File]::ReadAllLines("$Saida\legado.st.tsv"); $b = [IO.File]::ReadAllLines("$Saida\nativo.st.tsv")
$dif = $null
for ($i = 0; $i -lt [Math]::Min($a.Count, $b.Count); $i++) { if ($a[$i] -ne $b[$i]) { $dif = [int64]$a[$i].Split("`t")[0]; break } }
Check "estado igual ao legado ate o primeiro fim natural de fala (divergencia legitima)" ($primeiroFim -and $dif -and $dif -gt $primeiroFim) "primeira diferenca no passo $dif; primeiro estado 3 nativo no passo $primeiroFim"
Write-Host "$falhas falhas; saidas em $Saida"
exit $(if ($falhas) { 1 } else { 0 })
