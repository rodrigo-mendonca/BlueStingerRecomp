<#
.SYNOPSIS
  Revisao independente pelo Codex CLI (somente leitura). Ver CLAUDE.md, secao "Revisao com Codex".

.EXAMPLE
  powershell -File tools/dev/codex_review.ps1 -Scope "M3: semantica SH-4 (sh4_ops.h) e analisador" -Base HEAD
#>
param(
    [Parameter(Mandatory = $true)][string]$Scope,  # o que foi feito e o que revisar
    [string]$Base = "HEAD",                        # ref git de comparacao
    [string]$Model = ""                            # opcional: modelo do Codex
)
# Continue: no PowerShell 5.1, stderr de programas nativos com 2>&1 vira erro.
# Os erros sao verificados por $LASTEXITCODE.
$ErrorActionPreference = "Continue"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path

if (-not (Get-Command codex -ErrorAction SilentlyContinue)) { throw "Codex CLI nao encontrado no PATH." }
$login = cmd /c "codex login status 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $login -notmatch "Logged in") { throw "Codex CLI nao autenticado: $login" }

$diffstat  = (git -C $repo diff $Base --stat | Out-String).Trim()
$untracked = (git -C $repo ls-files --others --exclude-standard | Out-String).Trim()
if (-not $diffstat)  { $diffstat = "(nenhuma alteracao em arquivos versionados)" }
if (-not $untracked) { $untracked = "(nenhum)" }

$prompt = Get-Content "$PSScriptRoot\codex_review_prompt.md" -Raw -Encoding UTF8
$prompt = $prompt.Replace("{{SCOPE}}", $Scope).Replace("{{BASE}}", $Base).Replace("{{DIFFSTAT}}", $diffstat).Replace("{{UNTRACKED}}", $untracked)

$outDir = Join-Path $repo "reviews"
New-Item -ItemType Directory -Force $outDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$out   = Join-Path $outDir "codex-$stamp.md"

$cargs = @("exec", "--sandbox", "read-only", "--ephemeral", "-C", $repo, "-o", $out)
if ($Model) { $cargs += @("-m", $Model) }
$cargs += "-"
$prompt | codex @cargs | Out-Null
if ($LASTEXITCODE -ne 0) { throw "codex exec falhou (codigo $LASTEXITCODE)" }
Write-Output $out
