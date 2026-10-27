# N8.4 (revisao Codex): as suites que dependem dos modelos do legado (HLE do sistema, Maple, bu original) exigem um
# executavel do build de validacao. Ausente ou do produto, a suite reprova (nunca SKIP: o produto recusa esses modelos
# e a suite passaria a medir outra coisa). Uso: . (Join-Path $PSScriptRoot "valida_exe.ps1"); Assert-Validacao $Exe
function Assert-Validacao([string]$exe) {
    if (-not (Test-Path -LiteralPath $exe)) { Write-Host "ERRO: executavel de validacao ausente ($exe): compile com o preset msvc-validacao (ou msvc-release-mods/-watch)"; exit 2 }
    $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $null = cmd.exe /c "`"$exe`" --version --sistema hle 2>&1"
    $rc = $LASTEXITCODE; $ErrorActionPreference = $old
    if ($rc -ne 0) { Write-Host "ERRO: $exe nao e do build de validacao (recusou --sistema hle, rc=$rc)"; exit 2 }
}
