# N7 (pendencia AMD e caminho B): o JOGO com as camadas de validacao do Vulkan (validacao completa + sincronizacao).
#
#   powershell -File tools/dev/n7_validar_jogo.ps1 [-Exe build\n7-game\app\bluestinger.exe] [-Data <pasta data>]
#                                                  [-Sdk <VulkanSDK\versao>] [-Casos A1,N1,J1]
#
# PRECISA de um terminal SEM elevacao (nao "Executar como administrador"): o carregador do Vulkan ignora
# VK_ADD_LAYER_PATH num processo de integridade alta. Nada muda o ambiente global: as variaveis valem so aqui.
#
# Casos:
#   A1 AMD integrada, modo comparar (software + Vulkan por render), rota passo_combate ate o VBlank 1200: passa do render
#      371, onde a perda do dispositivo intermitente aparece. Registra VUIDs, SYNC-HAZARD e as recuperacoes.
#   N1 NVIDIA (ou a GPU padrao), renderizador nativo na resolucao original, a mesma rota.
#   J1 janela com a apresentacao direta (1080p interno, efeitos ligados), 25 s com o controle virtual: valida a
#      swapchain, as duas filas, os semaforos dos slots e a cadeia de pos-processamento.
# Resultado: por caso, o numero de mensagens de erro/aviso das camadas (tem de ser 0), e os logs em %TEMP%\bs_vk_jogo.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\n7-game\app\bluestinger.exe"),
    [string]$Data = "C:\Source\recomp-BStinger\dist\BlueStinger-Preview\data",
    [string]$Sdk = (Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\sdk\VulkanSDK\1.4.363.0"),
    [string]$Casos = "A1,N1,J1"
)
$ErrorActionPreference = "Continue"
$admin = [bool](& "$env:SystemRoot\System32\whoami.exe" /groups | Select-String "S-1-16-(12288|16384)")
if ($admin) { Write-Host "ERRO: terminal elevado; o carregador ignoraria as camadas do SDK. Abra um terminal normal."; exit 2 }
foreach ($p in @($Exe, "$Sdk\Bin\VkLayer_khronos_validation.json")) {
    if (-not (Test-Path $p)) { Write-Host "ERRO: falta $p"; exit 2 }
}
$repo = Split-Path (Split-Path $PSScriptRoot)
$out = Join-Path $env:TEMP "bs_vk_jogo"; New-Item -ItemType Directory -Force $out | Out-Null
$env:VK_ADD_LAYER_PATH = "$Sdk\Bin"
$env:VK_LOADER_LAYERS_DISABLE = "~implicit~"
$env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
$env:VK_KHRONOS_VALIDATION_VALIDATE_SYNC = "true"
$env:VK_KHRONOS_VALIDATION_REPORT_FLAGS = "error,warn"
$env:VK_KHRONOS_VALIDATION_DEBUG_ACTION = "VK_DBG_LAYER_ACTION_LOG_MSG"
$env:VK_LOADER_DEBUG = "layer"
$env:BS_RELOGIO_FIXO = "1700000000"
$fail = 0
function Run($id, [string[]]$a, [hashtable]$extra) {
    $log = Join-Path $out "$id.txt"
    foreach ($k in $extra.Keys) { Set-Item "env:$k" $extra[$k] }
    # O jogo e um executavel GUI: "& $Exe *> log" nao espera nem captura a saida no PowerShell 5.1.
    $argStr = ($a | ForEach-Object { "`"$_`"" }) -join " "
    $proc = Start-Process -FilePath "cmd.exe" -PassThru -Wait -NoNewWindow `
        -ArgumentList "/c `"`"$Exe`" $argStr > `"$log`" 2>&1`""
    $rc = $proc.ExitCode
    foreach ($k in $extra.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
    $t = Get-Content $log -Raw
    if (-not $t) { $t = "" }
    $active = $t -match 'Insert instance layer "VK_LAYER_KHRONOS_validation"'
    $msgs = @([regex]::Matches($t, "(?m)^.*(Validation (Error|Warning)|VUID-|SYNC-HAZARD).*$") | ForEach-Object Value)
    $lost = ([regex]::Matches($t, "recriando o dispositivo")).Count
    $ok = $active -and $msgs.Count -eq 0
    Write-Host ("{0} {1}: rc={2}, camada ativa={3}, mensagens={4}, dispositivo recriado={5}; log {6}" -f `
        $(if ($ok) { "[ ok ]" } else { "[FALHA]" }), $id, $rc, $active, $msgs.Count, $lost, $log)
    $msgs | Select-Object -First 5 | ForEach-Object { Write-Host "    $_" }
    if (-not $ok) { $script:fail++ }
}
$rota = Join-Path $repo "tools\dist\roteiros\passo_combate.txt"
$want = $Casos.Split(",")
if ($want -contains "A1") {
    Run "A1" @("--data", $Data, "--video", "legado", "--headless", "1200") @{ BS_INPUT_SCRIPT = $rota; BS_RENDER_IR = "comparar"; BS_VULKAN_GPU = "AMD" }
}
if ($want -contains "N1") {
    Run "N1" @("--data", $Data, "--video", "legado", "--render", "nativo", "--headless", "1200") @{ BS_INPUT_SCRIPT = $rota }
}
if ($want -contains "J1") {
    $pad = Join-Path $out "j1_controle.txt"
    "0 conectar`n25000 sair" | Set-Content -Encoding ascii $pad
    Run "J1" @("--data", $Data, "--render", "nativo", "--resolucao-interna", "1080") @{ BS_CONTROLE_VIRTUAL = $pad }
}
Write-Host $(if ($fail) { "FALHOU: $fail caso(s)" } else { "APROVADO" })
exit $(if ($fail) { 1 } else { 0 })
