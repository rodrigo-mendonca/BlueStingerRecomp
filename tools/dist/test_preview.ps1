# Valida a Preview fora do repositorio e SEM o CDI: copia a pasta (exe + data\) para um
# caminho com espacos e caracteres especiais e roda o executavel com o diretorio de
# trabalho em outro lugar. O CDI nao e usado em nenhum caso.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_preview.ps1 [-Preview <pasta>] [-Work <pasta>]
#
# Codigo de saida 0 = todos os casos passaram. Resultado tambem em <Work>\bs-preview-teste\resultado.txt.
param(
    [string]$Preview = "",
    [string]$Work = ""
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Preview) { $Preview = Join-Path $repo "dist\BlueStinger-Preview" }
if (-not $Work) { $Work = Join-Path $env:TEMP "bs preview teste" }
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok; Detalhe = $detail }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function Run($exe, $argList, $cwd, $timeoutMs = 600000) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe, $argList
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $psi.WorkingDirectory = $cwd
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($timeoutMs)) { $p.Kill(); return @{ Code = -1; Out = ""; Err = "tempo esgotado" } }
    return @{ Code = $p.ExitCode; Out = $o.Result; Err = $e.Result }
}
function DialogOf($procId) {
    $cb = [W+EnumProc]{ param($h, $l)
        $wpid = 0; [W]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
        if ($wpid -eq $procId -and [W]::IsWindowVisible($h)) {
            $sb = New-Object System.Text.StringBuilder 64; [W]::GetClassName($h, $sb, 64) | Out-Null
            if ($sb.ToString() -eq "#32770") { $script:dlg = $h }
        }
        return $true }
    $script:dlg = [IntPtr]::Zero
    [W]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:dlg
}
function StartWindow($exe, $argList) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe, $argList
    $psi.UseShellExecute = $true; $psi.WorkingDirectory = $env:SystemRoot
    return [System.Diagnostics.Process]::Start($psi)
}

# --- preparacao: tudo numa subpasta marcada por este script (so ela e apagada) ---
$Work = Join-Path ([IO.Path]::GetFullPath($Work)) "bs-preview-teste"
$marker = Join-Path $Work ".bs-preview-teste"
if (Test-Path -LiteralPath $Work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Work existe e nao foi criada por este teste"; exit 2 }
    icacls "$Work" /reset /T /Q | Out-Null
    Remove-Item -Recurse -Force -LiteralPath $Work
}
$special = Join-Path $Work "Prévia Ação ç 日本 (teste)"
New-Item -ItemType Directory -Force -Path $special | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
$app = Join-Path $special "BlueStinger-Preview"
Copy-Item -Recurse -LiteralPath $Preview -Destination $app
foreach ($n in "bluestinger.log", "bluestinger.ini") { $f = Join-Path $app $n; if (Test-Path -LiteralPath $f) { Remove-Item -Force -LiteralPath $f } }
$exe = Join-Path $app "BlueStinger.exe"
$cwd = $env:SystemRoot
$out = Join-Path $Work "saida ç"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# 1. versao
$r = Run $exe "--version" $cwd 30000
Check "versao" ($r.Code -eq 0 -and $r.Out -match "Blue Stinger \(recompilado\)") $r.Out.Trim()
# N8.2 (D-N8-3): a Preview e o produto: as mutacoes e sondas exclusivas de teste so existem no build de validacao e a
# variavel e recusada (nunca ignorada: um teste nao pode passar no vazio com este executavel)
foreach ($v in "BS_CICLOS_RUIDO", "BS_HW_PROBE", "BS_CRI_DIAG") {
    Set-Item "env:$v" "1"
    try { $r = Run $exe "--version" $cwd 30000 } finally { Remove-Item "env:$v" }
    Check "produto recusa $v (so no build de validacao)" ($r.Code -eq 2 -and $r.Err -match "so existe no build de validacao") ($r.Err.Trim())
}

# 2. dados conferidos (SHA-256 de todos os arquivos)
$r = Run $exe "--verify-data" $cwd
Check "dados conferidos (--verify-data)" ($r.Code -eq 0 -and $r.Out -match "conferidos") ""

# 3. headless sem o CDI, caminhos especiais: chega a tela VMU e grava o quadro
$dump = Join-Path $out "quadro ç.bmp"
$r = Run $exe "--headless 400 --dump `"$dump`"" $cwd
Check "headless sem CDI, caminhos especiais" ($r.Code -eq 0 -and (Test-Path -LiteralPath $dump) -and (Get-Item -LiteralPath $dump).Length -gt 900000) ($r.Out.Trim() -split "`n" | Select-Object -First 1)

# 4. um CDI na linha de comando: recusado com explicacao (o jogo nao le mais o CDI)
$r = Run $exe "`"C:\qualquer\Blue Stinger.cdi`" --headless 10" $cwd 60000
Check "CDI na linha de comando: explicacao" ($r.Code -eq 2 -and $r.Err -match "nao le mais o CDI") ""

# 5. janela: abre, roda, fecha limpo; log ao lado
$p = StartWindow $exe ""
$t0 = Get-Date
while ($p.MainWindowHandle -eq 0 -and ((Get-Date) - $t0).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 200; $p.Refresh() }
Start-Sleep -Seconds 10; $p.Refresh()
$title = $p.MainWindowTitle
[W]::PostMessage($p.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
$closed = $p.WaitForExit(15000)
$log = Join-Path $app "bluestinger.log"
Check "janela abre, roda e fecha" ($closed -and $p.ExitCode -eq 0 -and $title -match "campos/s") "fechou: $closed; codigo: $(if ($closed) { $p.ExitCode }); titulo: $title"
Check "log ao lado do executavel" ((Test-Path -LiteralPath $log) -and ((Get-Content -LiteralPath $log -Raw -Encoding UTF8) -match "desempenho")) $log

# 6. dados ausentes: janela mostra a mensagem e sai com erro; headless explica
$dataDir = Join-Path $app "data"
Rename-Item -LiteralPath $dataDir -NewName "data.fora"
try {
    $r = Run $exe "--headless 10" $cwd 60000
    Check "sem data\: mensagem clara (headless)" ($r.Code -eq 1 -and $r.Err -match "nao encontrados" -and $r.Err -match "Builder") (($r.Err -split "`n" | Where-Object { $_ -match "erro" } | Select-Object -First 1))
    $p = StartWindow $exe ""
    Start-Sleep -Seconds 3
    $box = DialogOf $p.Id
    if ($box -ne [IntPtr]::Zero) { [W]::PostMessage($box, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
    $closed = $p.WaitForExit(15000); if (-not $closed) { $p.Kill() }
    Check "sem data\: caixa de mensagem e codigo de erro (janela)" ($box -ne [IntPtr]::Zero -and $closed -and $p.ExitCode -eq 1) "codigo $($p.ExitCode)"
} finally {
    Rename-Item -LiteralPath (Join-Path $app "data.fora") -NewName "data"
}

# 7. pasta somente leitura: log em %LOCALAPPDATA%\BlueStingerRecomp; o jogo roda
$sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$localLog = Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\bluestinger.log"
if (Test-Path -LiteralPath $localLog) { Remove-Item -Force -LiteralPath $localLog }
icacls "$app" /deny "*${sid}:(W,AD,WD)" | Out-Null
try {
    $p = StartWindow $exe ""
    $t0 = Get-Date
    while ($p.MainWindowHandle -eq 0 -and ((Get-Date) - $t0).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 200; $p.Refresh() }
    Start-Sleep -Seconds 3
    [W]::PostMessage($p.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $closed = $p.WaitForExit(15000); if (-not $closed) { $p.Kill() }
} finally {
    icacls "$app" /remove:d "*$sid" | Out-Null
}
Check "pasta somente leitura: roda, log em LOCALAPPDATA" ($closed -and $p.ExitCode -eq 0 -and (Test-Path -LiteralPath $localLog)) $localLog

$fails = @($results | Where-Object { -not $_.Ok }).Count
$results | Format-Table -AutoSize -Wrap | Out-String -Width 400 | Set-Content -Encoding utf8 (Join-Path $Work "resultado.txt")
Write-Host ("{0} casos, {1} falhas" -f $results.Count, $fails)
exit $(if ($fails) { 1 } else { 0 })
