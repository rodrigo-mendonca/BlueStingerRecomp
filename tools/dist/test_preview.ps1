# Valida a distribuicao Preview fora do repositorio: copia a pasta para um caminho com
# espacos e caracteres especiais, usa a imagem por um caminho tambem especial (hard link,
# sem copiar) e roda o executavel com o diretorio de trabalho em outro lugar.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_preview.ps1 -Disc <imagem.cdi> [-Preview <pasta>] [-Work <pasta>]
#
# Codigo de saida 0 = todos os casos passaram.
param(
    [Parameter(Mandatory = $true)][string]$Disc,
    [string]$Preview = "",
    [string]$Work = ""
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Preview) { $Preview = Join-Path $repo "dist\BlueStinger-Preview" }
if (-not $Work) { $Work = Join-Path $env:TEMP "bs preview teste" }
$Disc = (Resolve-Path -LiteralPath $Disc).Path
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
    $found = $null
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

# --- preparacao: Preview copiada para um caminho especial, fora do repositorio ---
# Tudo fica numa subpasta propria de -Work, marcada por este script: so ela e apagada, e so
# se tiver a marca (um -Work errado nunca vira alvo de remocao; revisao Codex 2026-10-14).
$Work = Join-Path ([IO.Path]::GetFullPath($Work)) "bs-preview-teste"
$marker = Join-Path $Work ".bs-preview-teste"
if (Test-Path -LiteralPath $Work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Work existe e nao foi criada por este teste"; exit 2 }
    icacls "$Work" /reset /T /Q | Out-Null  # sobras (inclusive ACL de negacao) de uma execucao interrompida
    Remove-Item -Recurse -Force -LiteralPath $Work
}
$special = Join-Path $Work "Prévia Ação ç 日本 (teste)"
New-Item -ItemType Directory -Force -Path $special | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
Copy-Item -Recurse -LiteralPath $Preview -Destination (Join-Path $special "BlueStinger-Preview")
$app = Join-Path $special "BlueStinger-Preview"
$exe = Join-Path $app "BlueStinger.exe"
$discDir = Join-Path $Work "Jogos ç ã 日本"
New-Item -ItemType Directory -Force -Path $discDir | Out-Null
$discLink = Join-Path $discDir "Blue Stinger (cópia) ç.cdi"
cmd /c "mklink /H `"$discLink`" `"$Disc`"" | Out-Null
if (-not (Test-Path -LiteralPath $discLink)) { Copy-Item -LiteralPath $Disc -Destination $discLink }  # outro volume
$cwd = $env:SystemRoot  # diretorio de trabalho em outro lugar
$out = Join-Path $Work "saida ç"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# 1. versao
$r = Run $exe "--version" $cwd 30000
Check "versao" ($r.Code -eq 0 -and $r.Out -match "Blue Stinger \(recompilado\)") $r.Out.Trim()

# 2. headless com caminhos especiais: chega a tela VMU e grava o quadro
$dump = Join-Path $out "quadro ç.bmp"
$r = Run $exe "`"$discLink`" --headless 400 --dump `"$dump`"" $cwd
Check "headless com caminhos especiais" ($r.Code -eq 0 -and (Test-Path -LiteralPath $dump) -and (Get-Item -LiteralPath $dump).Length -gt 900000) ($r.Out.Trim() -split "`n" | Select-Object -First 1)

# 3. imagem invalida e inexistente: falha com mensagem, sem sucesso aparente
$fake = Join-Path $discDir "falso ç.cdi"
[IO.File]::WriteAllBytes($fake, (New-Object byte[] 4096))
$r = Run $exe "`"$fake`" --headless 10" $cwd 60000
Check "imagem invalida recusada" ($r.Code -ne 0 -and $r.Err -match "imagem invalida") (($r.Err -split "`n" | Where-Object { $_ -match "erro" } | Select-Object -First 1))
$r = Run $exe "`"$(Join-Path $discDir 'nao existe ç.cdi')`" --headless 10" $cwd 60000
Check "imagem inexistente recusada" ($r.Code -ne 0 -and $r.Err -match "erro") (($r.Err -split "`n" | Where-Object { $_ -match "erro" } | Select-Object -First 1))

# 4. janela real com a imagem na linha de comando: abre, apresenta, fecha limpo; log ao lado
$psi = New-Object System.Diagnostics.ProcessStartInfo $exe, "`"$discLink`""
$psi.UseShellExecute = $true; $psi.WorkingDirectory = $cwd
$p = [System.Diagnostics.Process]::Start($psi)
$t0 = Get-Date
while ($p.MainWindowHandle -eq 0 -and ((Get-Date) - $t0).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 200; $p.Refresh() }
Start-Sleep -Seconds 10; $p.Refresh()
$title = $p.MainWindowTitle
[W]::PostMessage($p.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
$closed = $p.WaitForExit(15000)
$log = Join-Path $app "bluestinger.log"
Check "janela abre, roda e fecha" ($closed -and $p.ExitCode -eq 0 -and $title -match "campos/s") "fechou: $closed; codigo: $(if ($closed) { $p.ExitCode }); titulo: $title"
Check "log ao lado do executavel" ((Test-Path -LiteralPath $log) -and ((Get-Content -LiteralPath $log -Raw) -match "desempenho")) $log
Check "ini lembra a imagem" ((Get-Content -LiteralPath (Join-Path $app "bluestinger.ini") -Raw -Encoding UTF8) -match [regex]::Escape("ç.cdi")) ""

# 5. sem imagem: dialogo de arquivo na inicializacao; cancelar encerra sem erro
$psi = New-Object System.Diagnostics.ProcessStartInfo $exe, ""
$psi.UseShellExecute = $true; $psi.WorkingDirectory = $cwd
$p = [System.Diagnostics.Process]::Start($psi)
$t0 = Get-Date; $dlg = [IntPtr]::Zero
while ($dlg -eq [IntPtr]::Zero -and ((Get-Date) - $t0).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 300; $dlg = DialogOf $p.Id }
if ($dlg -ne [IntPtr]::Zero) { [W]::PostMessage($dlg, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
$closed = $p.WaitForExit(15000)
if (-not $closed) { $p.Kill() }
Check "dialogo de arquivo sem imagem; cancelar sai" ($dlg -ne [IntPtr]::Zero -and $closed -and $p.ExitCode -eq 0) ""

# 6. pasta somente leitura: log e ini vao para %LOCALAPPDATA%\BlueStingerRecomp
$ro = Join-Path $special "somente leitura"
Copy-Item -Recurse -LiteralPath $app -Destination $ro
foreach ($n in "bluestinger.log", "bluestinger.ini") { $f = Join-Path $ro $n; if (Test-Path -LiteralPath $f) { Remove-Item -Force -LiteralPath $f } }
$sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
icacls "$ro" /deny "*${sid}:(W,AD,WD)" | Out-Null
$localLog = Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\bluestinger.log"
if (Test-Path -LiteralPath $localLog) { Remove-Item -Force -LiteralPath $localLog }
$psi = New-Object System.Diagnostics.ProcessStartInfo (Join-Path $ro "BlueStinger.exe"), "`"$fake`""
$psi.UseShellExecute = $true; $psi.WorkingDirectory = $cwd
$p = [System.Diagnostics.Process]::Start($psi)
Start-Sleep -Seconds 4
$box = DialogOf $p.Id   # mensagem de imagem invalida
if ($box -ne [IntPtr]::Zero) { [W]::PostMessage($box, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
$closed = $p.WaitForExit(15000); if (-not $closed) { $p.Kill() }
$roLogOk = (Test-Path -LiteralPath $localLog) -and -not (Test-Path -LiteralPath (Join-Path $ro "bluestinger.log"))
$badCode = $p.ExitCode
# imagem valida na mesma pasta: o ini (com o caminho especial, em UTF-8) vai para LOCALAPPDATA
$localIni = Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\bluestinger.ini"
$iniBefore = if (Test-Path -LiteralPath $localIni) { [IO.File]::ReadAllBytes($localIni) } else { $null }
if (Test-Path -LiteralPath $localIni) { Remove-Item -Force -LiteralPath $localIni }
$psi = New-Object System.Diagnostics.ProcessStartInfo (Join-Path $ro "BlueStinger.exe"), "`"$discLink`""
$psi.UseShellExecute = $true; $psi.WorkingDirectory = $cwd
$p2 = [System.Diagnostics.Process]::Start($psi)
$t0 = Get-Date
while ($p2.MainWindowHandle -eq 0 -and ((Get-Date) - $t0).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 200; $p2.Refresh() }
Start-Sleep -Seconds 3
[W]::PostMessage($p2.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
$closed2 = $p2.WaitForExit(15000); if (-not $closed2) { $p2.Kill() }
$iniText = if (Test-Path -LiteralPath $localIni) { [IO.File]::ReadAllText($localIni, [Text.Encoding]::UTF8) } else { "" }
if ($null -ne $iniBefore) { [IO.File]::WriteAllBytes($localIni, $iniBefore) } elseif (Test-Path -LiteralPath $localIni) { Remove-Item -Force -LiteralPath $localIni }
icacls "$ro" /remove:d "*$sid" | Out-Null
Check "pasta somente leitura: log em LOCALAPPDATA" $roLogOk $localLog
Check "imagem invalida na janela: mensagem e codigo de erro" ($box -ne [IntPtr]::Zero -and $closed -and $badCode -ne 0) "codigo $badCode"
Check "pasta somente leitura: ini em LOCALAPPDATA (UTF-8)" ($closed2 -and $p2.ExitCode -eq 0 -and $iniText.Contains($discLink) -and -not (Test-Path -LiteralPath (Join-Path $ro "bluestinger.ini"))) $iniText.Trim()

$fails = @($results | Where-Object { -not $_.Ok }).Count
$results | Format-Table -AutoSize -Wrap | Out-String -Width 400 | Set-Content -Encoding utf8 (Join-Path $Work "resultado.txt")  # diagnostico de falhas intermitentes
Write-Host ("{0} casos, {1} falhas" -f $results.Count, $fails)
exit $(if ($fails) { 1 } else { 0 })
