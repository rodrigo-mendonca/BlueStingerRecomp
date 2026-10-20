# Teste sintetico do executor de regressao (tools/dev/regressao.ps1): repositorio git temporario com
# etapas falsas (BS_REGRESSAO_ETAPAS), sem builds nem jogo. Confere quando cada etapa e reaproveitada.
#   powershell -File tools/dev/test_regressao.ps1
$ErrorActionPreference = "Continue"
$src  = Join-Path $PSScriptRoot "regressao.ps1"
$root = Join-Path $env:TEMP ("bs_test_regressao_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$est  = Join-Path $root "estado"
New-Item -ItemType Directory -Force "$root\repo\tools\dev", "$root\repo\tools\dist", "$root\repo\src" | Out-Null
$repo = "$root\repo"
Copy-Item $src "$repo\tools\dev\regressao.ps1"
$marks = "$root\execucoes.txt"
# cada script falso anota que rodou; test_b falha se existir o arquivo "falhar"
foreach ($n in "make_x", "test_a", "test_b") {
    $fail = if ($n -eq "test_b") { "if (Test-Path '$root\falhar') { exit 1 }" } else { "" }
    "Add-Content '$marks' '$n'`n$fail`nexit 0" | Out-File "$repo\tools\dist\$n.ps1" -Encoding utf8
}
"codigo 1" | Out-File "$repo\src\code.txt" -Encoding utf8
"doc" | Out-File "$repo\LEIA.md" -Encoding utf8
@'
param($repo)
@(
    @{ N = "make_x"; T = "suite"; A = @("$repo\tools\dist\make_x.ps1"); Stop = $true },
    @{ N = "test_a"; T = "suite"; A = @("$repo\tools\dist\test_a.ps1") },
    @{ N = "test_b"; T = "suite"; A = @("$repo\tools\dist\test_b.ps1") }
)
'@ | Out-File "$root\etapas.ps1" -Encoding utf8
Push-Location $repo
git init -q; git -c user.email=t@t -c user.name=t add -A; git -c user.email=t@t -c user.name=t commit -qm base
Pop-Location
$env:BS_REGRESSAO_ETAPAS = "$root\etapas.ps1"

$fails = 0
function Run([string]$id, [string]$expect, [int]$expectRc = 0) {
    Remove-Item $marks -ErrorAction SilentlyContinue
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\dev\regressao.ps1" -Estado $est *> (Join-Path $root "$($id.Split(' ')[0]).txt")
    $rc  = $LASTEXITCODE
    $ran = if (Test-Path $marks) { (Get-Content $marks) -join "," } else { "" }
    $ok  = $ran -eq $expect -and $rc -eq $expectRc
    Write-Host ("{0} {1}: rodaram [{2}] (esperado [{3}]), rc={4}" -f $(if ($ok) { "[ ok ]" } else { "[FALHA]" }), $id, $ran, $expect, $rc)
    if (-not $ok) { $script:fails++ }
}
Run "R1 primeira execucao"             "make_x,test_a,test_b"
Run "R2 nada mudou"                    ""
Add-Content "$repo\tools\dist\test_a.ps1" "# editado"
Run "R3 so o script de test_a mudou"   "test_a"
Add-Content "$repo\LEIA.md" "mais doc"
Run "R4 so documentacao"               ""
Add-Content "$repo\src\code.txt" "codigo 2"
Run "R5 codigo mudou: tudo"            "make_x,test_a,test_b"
Add-Content "$repo\tools\dist\make_x.ps1" "# produtor editado"
Run "R6 produtor mudou: tudo"          "make_x,test_a,test_b"
"novo" | Out-File "$repo\src\novo.txt" -Encoding utf8
Run "R7 arquivo nao versionado novo"   "make_x,test_a,test_b"
New-Item "$root\falhar" -ItemType File | Out-Null
Add-Content "$repo\tools\dist\test_b.ps1" "# b editado"
Run "R8 test_b falha"                  "test_b" 1
Run "R9 falha roda de novo"            "test_b" 1
Remove-Item "$root\falhar"
Run "R10 corrigida"                    "test_b"
Push-Location $repo; git -c user.email=t@t -c user.name=t add -A; git -c user.email=t@t -c user.name=t commit -qm c2; Pop-Location
Run "R11 commit (HEAD mudou): tudo"    "make_x,test_a,test_b"
# trava: um processo vivo com o PID da trava recusa a segunda execucao
$sleeper = Start-Process powershell -ArgumentList "-NoProfile", "-Command", "Start-Sleep 60" -PassThru -WindowStyle Hidden
"$($sleeper.Id)" | Out-File "$est\trava.txt" -Encoding ascii
Add-Content "$repo\tools\dist\test_a.ps1" "# de novo"
Run "R12 trava de outra regressao viva" "" 3
Stop-Process -Id $sleeper.Id -Force
Run "R13 trava de processo morto"      "test_a"
$lockLeft = Test-Path "$est\trava.txt"; $ativoLeft = Test-Path "$est\ativo.txt"
$dur = Select-String -Path "$est\regressao.log" -Pattern "duracoes" -Quiet
$ok = -not $lockLeft -and -not $ativoLeft -and $dur
Write-Host ("{0} R14 trava e ativo removidos no fim; duracoes no log" -f $(if ($ok) { "[ ok ]" } else { "[FALHA]" }))
if (-not $ok) { $fails++ }
Remove-Item Env:\BS_REGRESSAO_ETAPAS
if ($fails -eq 0) { Remove-Item -Recurse -Force $root } else { Write-Host "artefatos em $root" }
Write-Host "falhas: $fails"
exit $fails
