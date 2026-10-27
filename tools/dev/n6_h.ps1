# N6 fase H (docs/N6-PLANO.md secao 20): bateria nivel 2 para promover o audio nativo a padrao. Nao promove nada: roda
# o jogo com --audio nativo|legado (os tres provedores de uma vez) e reaproveita os testes dirigidos das fases D/E/F.
# Casos (-Casos H1,H2,...):
#   H1 unitarios do audio, da localizacao, da interface e do video, normal e ASan (-BuildAsan), com os dados do jogo
#   H2 efeitos x legado (n6_efeitos.ps1) nas rotas de combate e do heliporto: quadros e estado por passo iguais
#   H3 falas x legado (n6_falas.ps1): contrato do Flycast, eventos de dialogo, legenda
#   H4 tudo nativo x tudo legado nas rotas LOAD do save g1 (-SaveG1) e heliporto: estado por passo igual (ou, com as
#      falas nativas, igual ate o primeiro fim natural de fala: a divergencia legitima de n6_falas), nenhuma fonte acima
#      de 16 bits na mistura (o clipping de soma do modelo da AICA e informado), nenhuma falha dos tres provedores; na LOAD, key-on da musica x oraculo do Flycast
#      (-OraculoMusica, n6_musica_compara.py): >= 99 % pareados no criterio mais estrito, 0 inversoes, vozes extras <= 2 %
#   H5 selecao e fallback: sem opcao = os tres nativos (padrao desde a promocao); --audio legado = nenhum nativo (fallback);
#      --audio nativo --audio-musica legado; BS_AUDIO=legado e BS_AUDIO=nativo; opcao especifica sobre BS_AUDIO; valores
#      invalidos recusados
#   H6 dublagens de mod em WAV, FLAC e OGG (tom sintetico gerado aqui; FLAC/OGG pelo ffmpeg) com legenda
#   H7 estabilidade offline: as 11 musicas x 300 s no bs-musica (notas e vozes > 0; recusadas, tom saturado, falhas de
#      amostra, saturacao, nota ligada roubada e MIXS acima de 20 bits = 0; "sem camada" informado: dado da musica)
#   H8 outras frentes com o audio nativo (BS_AUDIO=nativo): controles (test_pd_nativo P1,P2), filme E001 (n5_e001) e
#      saves (test_saves.ps1, exige o build com mods em -ExeMods)
#   H9 tempo real: janela (XAudio2) no modo espectador da rota LOAD, tudo nativo: faltas de dados, erros de escrita e
#      perdas do dispositivo = 0
# Uma prova sem pre-requisito (dados, save g1, oraculo, ffmpeg, ASan, build com mods) REPROVA a bateria; -Parcial aceita
# o skip (execucao de desenvolvimento, nao vale para a promocao). O ambiente BS_* de quem chama e retirado durante a
# bateria (BS_SEM_DSP, BS_AUDIO etc. mudariam os casos) e devolvido no fim.
#   N8.5: os casos que usam --video legado (H4, H5, H6) ou outros provedores legados exigem o -Exe do build de validacao
#   (o produto os recusa, saida 2); o H9 (tudo nativo) roda na Preview.
#   powershell -File tools/dev/n6_h.ps1 -Exe <bluestinger.exe> [-Casos H1,H5] [-Parcial] [-Build <dir>] [-BuildAsan <dir>]
#              [-ExeMods <exe com mods>] [-OraculoMusica <tsv>] [-SaveG1 <BLUESTIN.000>]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Data = "",
    [string[]]$Casos = @("H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8", "H9"),
    [switch]$Parcial,
    [string]$Build = "",
    [string]$BuildAsan = "",
    [string]$ExeMods = "",
    [string]$SaveG1 = (Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\n6f\jogo\g1\BLUESTIN.000"),
    [string]$OraculoMusica = (Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\n6_o6\musica_m2a.tsv"),
    [string]$GameData = "",
    [int]$JanelaSegundos = 150,
    [string]$Saida = (Join-Path $env:TEMP "bs-n6-h")
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Data) { $Data = Join-Path $repo "dist\BlueStinger-Preview\data" }
if (-not $Build) { $Build = Join-Path $repo "build\n6" }
if (-not $GameData) { $GameData = @((Join-Path $repo "game-data"), (Join-Path $repo "..\recomp-BStinger\game-data")) | Where-Object { Test-Path $_ } | Select-Object -First 1 }
$Exe = (Resolve-Path $Exe).Path
$Casos = @($Casos | ForEach-Object { $_ -split "," } | Where-Object { $_ })
New-Item -ItemType Directory -Force $Saida | Out-Null
$falhas = 0
$resultados = [ordered]@{}
function Check($caso, $nome, [bool]$ok, $det = "") {
    Write-Host ("[{0}] {1} {2} {3}" -f $(if ($ok) { "ok" } else { "FALHA" }), $caso, $nome, $det)
    if (-not $ok) { $script:falhas++; $script:resultados[$caso] = "FALHA" } elseif (-not $script:resultados.Contains($caso)) { $script:resultados[$caso] = "ok" }
}
function Skip($caso, $porque) {
    if ($Parcial) { Write-Host "[skip] $caso $porque"; if (-not $script:resultados.Contains($caso)) { $script:resultados[$caso] = "skip: $porque" } }
    else { Check $caso "pre-requisito ausente (prova obrigatoria; -Parcial para pular)" $false $porque }
}
# ambiente isolado: nenhuma variavel BS_* de quem chama vale dentro da bateria
$ambienteOriginal = @{}
Get-ChildItem env: | Where-Object { $_.Name -like "BS_*" } | ForEach-Object { $ambienteOriginal[$_.Name] = $_.Value; Remove-Item "env:$($_.Name)" }
if ($ambienteOriginal.Count) { Write-Host "(ambiente BS_* retirado durante a bateria: $($ambienteOriginal.Keys -join ', '))" }
function LimparAmbiente { Get-ChildItem env: | Where-Object { $_.Name -like "BS_*" } | ForEach-Object { Remove-Item "env:$($_.Name)" } }
# Roda o jogo com variaveis de ambiente proprias; devolve o codigo de saida (saida e erro em <nome>.out)
function Jogo($nome, $argumentos, [hashtable]$amb = @{}) {
    LimparAmbiente
    $env:BS_RELOGIO_FIXO = "1700000000"
    foreach ($k in $amb.Keys) { Set-Item "env:$k" $amb[$k] }
    try {
        cmd /c "`"$Exe`" --data `"$Data`" $argumentos > `"$Saida\$nome.out`" 2>&1"
        return $LASTEXITCODE
    } finally { LimparAmbiente }
}
function Filho($arquivo, $script, $argumentos) {
    cmd /c "powershell -NoProfile -File `"$script`" $argumentos > `"$arquivo`" 2>&1"
    return $LASTEXITCODE
}
function Linha($nome, $padrao) { (Select-String -Path "$Saida\$nome.out" -Pattern $padrao | Select-Object -Last 1).Line }
function Num($linha, $rotulo) { $m = [regex]::Match("$linha", "$rotulo (\d+)"); if ($m.Success) { [int64]$m.Groups[1].Value } else { -1 } }
function VBlanksDaRota($rota) {  # como o test_pd_nativo: ultimo indice x 2 + 3000
    $last = 0
    foreach ($l in [IO.File]::ReadLines($rota)) { $t = $l.Split("#")[0].Trim(); if ($t -match "^(\d+)\s") { $last = [int64]$Matches[1] } }
    return $last * 2 + 3000
}
# o cartao do g1 numa pasta propria por execucao (o jogo grava nela; o arquivo do usuario nunca e tocado)
function CartaoG1($nome) {
    $d = "$Saida\$nome.saves"
    Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $d | Out-Null
    cmd /c "`"$Exe`" --data `"$Data`" --saves-pasta `"$d`" --saves-importar `"$SaveG1`" --saves-nome BLUESTIN.000 > `"$Saida\$nome.imp.out`" 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "importar o g1 falhou ($nome)" }
    return $d
}
$rotaLoad = Join-Path $repo "tools\dist\roteiros\passo_load_slot1.txt"
$rotaHeli = Join-Path $repo "tools\dist\roteiros\passo_heliporto.txt"

try {
if ($Casos -contains "H1") {
    foreach ($par in @(@("normal", $Build), @("ASan", $BuildAsan))) {
        if (-not $par[1]) { Skip "H1" "sem -BuildAsan (ASan)"; continue }
        if (-not $GameData) { Skip "H1" "game-data ausente (testes com dados do jogo)"; continue }
        foreach ($t in "bs_audio_tests", "bs_localization_tests", "bs_ui_tests", "bs_video_tests") {
            $e = Join-Path $par[1] "tests\$t.exe"
            if (-not (Test-Path $e)) { Skip "H1" "$t ausente em $($par[1])"; continue }
            $env:BS_GAME_DATA = $GameData
            $pathAntes = $env:PATH
            if ($par[0] -eq "ASan") {  # a DLL do ASan do MSVC (o vcvars a poe no PATH)
                $dll = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll",
                    "$env:ProgramFiles\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" -ErrorAction SilentlyContinue | Select-Object -Last 1
                if ($dll) { $env:PATH = "$($dll.DirectoryName);$env:PATH" }
            }
            $o = & $e 2>&1 | Out-String
            $rc = $LASTEXITCODE
            $env:PATH = $pathAntes
            LimparAmbiente
            $o | Out-File "$Saida\h1_$($par[0])_$t.txt"
            $m = [regex]::Match($o, "(\d+) testes, (\d+) falhas")
            $sk = [regex]::Match($o, "(\d+) sem dados")
            Check "H1" "$t ($($par[0]))" ($rc -eq 0 -and $m.Success -and $m.Groups[2].Value -eq "0" -and $sk.Groups[1].Value -eq "0") "$($m.Value), $($sk.Value)"
        }
    }
}
if ($Casos -contains "H2") {
    foreach ($r in @(@("combate", "passo_combate.txt"), @("heliporto", "passo_heliporto.txt"))) {
        LimparAmbiente
        $rc = Filho "$Saida\h2_$($r[0]).txt" (Join-Path $PSScriptRoot "n6_efeitos.ps1") "-Exe `"$Exe`" -Data `"$Data`" -Rota `"$(Join-Path $repo "tools\dist\roteiros\$($r[1])")`" -Saida `"$Saida\h2_$($r[0])`""
        $o2 = Get-Content "$Saida\h2_$($r[0]).txt"
        Check "H2" "efeitos x legado, rota $($r[0]) (n6_efeitos.ps1)" ($rc -eq 0) (($o2 | Select-Object -Last 1) -join "")
    }
}
if ($Casos -contains "H3") {
    LimparAmbiente
    $rc = Filho "$Saida\h3.txt" (Join-Path $PSScriptRoot "n6_falas.ps1") "-Exe `"$Exe`" -Data `"$Data`" -Saida `"$Saida\h3`""
    $o3 = Get-Content "$Saida\h3.txt"
    Check "H3" "falas x legado (n6_falas.ps1)" ($rc -eq 0) (($o3 | Select-Object -Last 1) -join "")
}
if ($Casos -contains "H4") {
    $rotas4 = @()
    if (Test-Path $SaveG1) { $rotas4 += , @("load", $rotaLoad, 9000, $true) } else { Skip "H4" "save g1 ausente ($SaveG1)" }
    $rotas4 += , @("heliporto", $rotaHeli, (VBlanksDaRota $rotaHeli), $false)
    foreach ($r in $rotas4) {
        $n = $r[0]
        foreach ($m in "legado", "nativo") {
            $cart = if ($r[3]) { "--saves-pasta `"$(CartaoG1 "h4_${n}_$m")`"" } else { "" }
            $rc = Jogo "h4_${n}_$m" "$cart --audio $m --video legado --headless $($r[2])" @{
                BS_INPUT_SCRIPT = $r[1]; BS_STATE_PASSO = "$Saida\h4_${n}_$m.st.tsv"; BS_STATE_PASSO_CADA = "1"
                BS_MUSICA_VOZES = "$Saida\h4_${n}_$m.vozes.tsv"; BS_EFEITOS_LOG = "$Saida\h4_${n}_$m.log.tsv" }
            Check "H4" "$n $m termina sem erro" ($rc -eq 0) "rc=$rc"
        }
        # estado logico por PASSO (hash, vida, posicao), nos passos comuns; o VBlank de cada passo fica de fora (o audio
        # nativo nao espera as cargas para a RAM de som: o mesmo passo chega 1 a 2 VBlanks antes, a diferenca de tempo do N2,
        # N2-PLANO 11.1) e e informado a parte, como o deslocamento de VBlank
        $sem = { param($l) $c = $l.Split("`t"); (@($c[0]) + $c[2..($c.Count - 1)]) -join "`t" }
        $a = @([IO.File]::ReadAllLines("$Saida\h4_${n}_legado.st.tsv") | Select-Object -Skip 1)
        $b = @([IO.File]::ReadAllLines("$Saida\h4_${n}_nativo.st.tsv") | Select-Object -Skip 1)
        $comuns = [Math]::Min($a.Count, $b.Count)
        $dif = $null; $desloc = @{}
        for ($i = 0; $i -lt $comuns; $i++) {
            if ((& $sem $a[$i]) -ne (& $sem $b[$i])) { $dif = [int64]$a[$i].Split("`t")[0]; break }
            $d = [int64]$b[$i].Split("`t")[1] - [int64]$a[$i].Split("`t")[1]; $desloc[$d] = 1 + [int]$desloc[$d]
        }
        Write-Host ("    {0}: deslocamento de VBlank nativo - legado por passo: {1}" -f $n, (($desloc.Keys | Sort-Object | ForEach-Object { "$_ ($($desloc[$_]) passos)" }) -join ", "))
        # primeiro fim natural de fala nativa (estado 3): dali em diante o original tambem libera o jogo (n6_falas)
        $fimFala = $null
        if (Test-Path "$Saida\h4_${n}_nativo.log.tsv") {
            $l = Select-String -Path "$Saida\h4_${n}_nativo.log.tsv" -Pattern "`tfala-estado`t.*\(3\)" | Select-Object -First 1
            if ($l) { $fimFala = [int64]$l.Line.Split("`t")[0] }
        }
        $igual = -not $dif -and [Math]::Abs($a.Count - $b.Count) -le 2  # no mesmo numero de VBlanks, +-1 passo pelo deslocamento
        Check "H4" "$n estado por passo x legado (igual, ou igual ate o 1o fim natural de fala)" ($comuns -gt 3000 -and ($igual -or ($fimFala -and $dif -gt $fimFala))) $(if ($igual) { "$comuns passos comuns iguais ($($a.Count) legado, $($b.Count) nativo)" } else { "1a diferenca no passo $dif; 1o fim natural de fala no passo $fimFala" })
        $nome = "h4_${n}_nativo"
        $mix = Linha $nome "\[audio\] mistura:"
        # Saturacao pelo modelo da AICA (docs/N6-PLANO.md 19.3 e 20.2): a saida de cada canal e de 16 bits e so a SOMA
        # dos canais e cortada no fim; com os niveis de cada canal iguais aos do original (H2, H3 e o key-on x Flycast),
        # o clipping de soma e o esperado. Reprova o clipping introduzido pelo backend: uma fonte so acima de 16 bits
        # (ganho acima de 0 dB, DSP fora da faixa). A saturacao fica informada; nao e prova de equivalencia acustica.
        $fonte = Num $mix "fonte acima de 16 bits"
        Check "H4" "$n mistura: nenhuma fonte acima de 16 bits (clipping so de soma), audivel" ($fonte -eq 0 -and (Num $mix "pico") -gt 1000) "fonte acima de 16 bits $fonte; saturadas (soma) $(Num $mix 'saturadas'); MIXS acima de 20 bits $(Num $mix '20 bits')"
        $ef = Linha $nome "\[audio\] efeitos nativos:"; $mu = Linha $nome "\[audio\] musica nativa:"; $fa = Linha $nome "\[audio\] falas nativas:"
        Check "H4" "$n efeitos: tocados, nenhum ausente, recusado ou com falha" ((Num $ef "ausentes") -eq 0 -and (Num $ef "recusadas") -eq 0 -and (Num $ef "decodificacao") -eq 0 -and (Num $ef "tocar") -gt 0) "$ef"
        Check "H4" "$n musica: notas, sem erro do driver, carga recusada, voz sem camada ou recusada" ((Num $mu "notas") -gt 1000 -and $mu -match "\(erros 0\)" -and $mu -match "recusadas 0, outros" -and (Num $mu "camada") -eq 0 -and $mu -match "recusadas 0$") "$mu"
        Check "H4" "$n falas: sem falha, ausente, chamada inesperada ou outro handle" ((Num $fa "inesperadas") -eq 0 -and $fa -match "falhas 0\)" -and (Num $fa "handles") -eq 0 -and (Num $fa "ausentes") -eq 0 -and (Num $fa "leitura") -eq 0) "$fa"
        Check "H4" "$n legado: nenhum provedor nativo" (-not (Select-String -Path "$Saida\h4_${n}_legado.out" -Pattern "\[audio\] nativo:" -Quiet)) ""
        if ($n -eq "load") {
            if (-not (Test-Path $OraculoMusica)) { Skip "H4" "oraculo da musica ausente ($OraculoMusica)" } else {
                $c = python -I (Join-Path $PSScriptRoot "n6_musica_compara.py") $OraculoMusica "$Saida\h4_load_nativo.vozes.tsv" 2>&1 | Out-String
                $rcc = $LASTEXITCODE
                $c | Out-File "$Saida\h4_compara.txt"
                # o criterio mais estrito (a ultima linha "um a um"): pareados, vozes nativas extras e inversoes. As extras
                # nao sao zero no resultado aceito (docs/N6-PLANO.md 18: a 1a leitura do oraculo 736 ms depois do tocar e o
                # recalculo estereo por controle 7): o teto e 2 % dos pareados
                $m = [regex]::Matches($c, "(\d+)/(\d+) pareados, (\d+) sem voz nativa, (\d+) vozes nativas sem key-on no trecho, (\d+) inversoes") | Select-Object -Last 1
                $ok = $rcc -eq 0 -and $m -and [double]$m.Groups[1].Value / [double]$m.Groups[2].Value -ge 0.99 -and $m.Groups[5].Value -eq "0" -and [double]$m.Groups[4].Value -le 0.02 * [double]$m.Groups[1].Value
                Check "H4" "key-on da musica x oraculo do Flycast (>= 99 %, 0 inversoes, extras <= 2 %)" $ok "$($m.Value)"
            }
        }
    }
}
if ($Casos -contains "H5") {
    $casos5 = @(
        @("padrao", "", @{}, "efeitos e falas e musica"),
        @("audio_legado", "--audio legado", @{}, ""),
        @("audio_nativo", "--audio nativo", @{}, "efeitos e falas e musica"),
        @("misto", "--audio nativo --audio-musica legado", @{}, "efeitos e falas \("),
        @("legado_e_opcao", "--audio legado --audio-falas nativo", @{}, "falas \("),
        @("ambiente_legado", "", @{ BS_AUDIO = "legado" }, ""),
        @("ambiente_legado_e_opcao", "--audio-musica nativo", @{ BS_AUDIO = "legado" }, "musica \("),
        @("ambiente", "", @{ BS_AUDIO = "nativo" }, "efeitos e falas e musica"),
        @("ambiente_e_opcao", "--audio-falas legado", @{ BS_AUDIO = "nativo" }, "efeitos e musica"),
        @("opcao_sobre_ambiente", "--audio nativo", @{ BS_AUDIO = "legado" }, "efeitos e falas e musica"))
    foreach ($c in $casos5) {
        $rc = Jogo "h5_$($c[0])" "$($c[1]) --video legado --headless 900" $c[2]
        $l = Linha "h5_$($c[0])" "\[audio\] nativo:"
        $ok = $rc -eq 0 -and $(if ($c[3]) { "$l" -match "nativo: $($c[3])" } else { -not $l })
        Check "H5" "$($c[0]): $(if ($c[3]) { $c[3] } else { 'nenhum nativo' })" $ok "$l"
    }
    # valor invalido: o jogo recusa e mostra o uso (no stderr e numa caixa de mensagem, que fica esperando um clique; aqui
    # basta o texto no stderr e o processo nao seguir para o jogo; a caixa e fechada encerrando o processo)
    foreach ($inv in @(@("opcao", "--audio talvez --headless 10", $null), @("ambiente", "--headless 10", "talvez"))) {
        LimparAmbiente
        if ($inv[2]) { $env:BS_AUDIO = $inv[2] }
        $p = Start-Process -FilePath $Exe -ArgumentList "--data `"$Data`" $($inv[1])" -PassThru -NoNewWindow `
            -RedirectStandardError "$Saida\h5_invalido_$($inv[0]).err" -RedirectStandardOutput "$Saida\h5_invalido_$($inv[0]).out"
        LimparAmbiente
        $saiu = $p.WaitForExit(15000)
        if (-not $saiu) { $p.Kill(); $p.WaitForExit() }
        $err = Get-Content "$Saida\h5_invalido_$($inv[0]).err" -Raw
        Check "H5" "valor invalido recusado ($($inv[0])): uso, sem iniciar o jogo" ("$err" -match "uso: bluestinger" -and "$err" -notmatch "\[audio\]|\[tempo\]") $(if ($saiu) { "rc=$($p.ExitCode)" } else { "caixa de uso aberta (fechada pelo teste)" })
    }
}
if ($Casos -contains "H6") {
    $modsAudio = Join-Path (Split-Path $Exe) "mods\audio"
    $ff = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if (Test-Path $modsAudio) { Skip "H6" "$modsAudio ja existe (arquivos do usuario: nao mexer)" } else {
        $pac = Join-Path (Split-Path $Exe) "mods\n6-h-legendas"
        try {
            New-Item -ItemType Directory -Force "$modsAudio\pt-br\voz", "$pac\legendas" | Out-Null
            [IO.File]::WriteAllText("$pac\mod.toml", "formato = 1`nid = `"n6-h-legendas`"`nnome = `"teste da fase H`"`nprioridade = 1`n")
            [IO.File]::WriteAllText("$pac\legendas\falas.pt-BR.toml", "[[fala]]`nvoz = `"voz/e012aenv`"`ninicio = 0.0`nfim = 5.0`ntexto = `"[teste H]`"`n")
            $wav = "$Saida\tom.wav"
            python -I -c "import wave,math,struct,sys;w=wave.open(sys.argv[1],'wb');w.setnchannels(1);w.setsampwidth(2);w.setframerate(22050);w.writeframes(b''.join(struct.pack('<h',int(8000*math.sin(2*math.pi*440*i/22050))) for i in range(22050*6)));w.close()" $wav
            foreach ($ext in "wav", "flac", "ogg") {
                Remove-Item "$modsAudio\pt-br\voz\*" -ErrorAction SilentlyContinue
                $alvo = "$modsAudio\pt-br\voz\e012aenv.$ext"
                if ($ext -eq "wav") { Copy-Item $wav $alvo } elseif (-not $ff) { Skip "H6" "ffmpeg ausente ($ext)"; continue } else {
                    $cod = if ($ext -eq "ogg") { "-c:a libvorbis -q:a 4" } else { "" }
                    cmd /c "ffmpeg -hide_banner -loglevel error -y -i `"$wav`" $cod `"$alvo`""
                    if (-not (Test-Path $alvo)) { Check "H6" "gerar $ext com o ffmpeg" $false ""; continue }
                }
                $rc = Jogo "h6_$ext" "--audio nativo --dublagem pt-BR --legendas pt-BR --video legado --headless 8000" @{ BS_INPUT_SCRIPT = (Join-Path $repo "tools\dist\roteiros\passo_combate.txt") }
                $fa = Linha "h6_$ext" "\[audio\] falas nativas:"
                Check "H6" "dublagem $ext tocada, sem falha de leitura" ($rc -eq 0 -and (Num $fa "dublagens") -ge 1 -and (Num $fa "leitura") -eq 0) "$fa"
                Check "H6" "legenda da dublagem $ext ativa" (Select-String -Path "$Saida\h6_$ext.out" -Pattern "\[legenda\] .*voz/e012aenv" -Quiet) ""
            }
        } finally {
            Remove-Item -Recurse -Force $modsAudio, $pac -ErrorAction SilentlyContinue
        }
    }
}
if ($Casos -contains "H7") {
    $musica = Join-Path $Build "builder\bs-musica.exe"
    if (-not (Test-Path $musica)) { Skip "H7" "bs-musica ausente em $Build" } else {
        $bsdata = Join-Path $Data "disc\BSDATA"
        $esperadas = "M10", "M2A", "M2B", "M2C", "M3", "M4", "M5", "M6", "M7", "M9", "M99"
        $nomes = @(Get-ChildItem $bsdata -Filter "M*.MSB" | ForEach-Object { $_.BaseName } | Where-Object { Test-Path "$bsdata\$_.MPB" } | Sort-Object)
        Check "H7" "inventario: as 11 musicas do disco" ((Compare-Object ($esperadas | Sort-Object) $nomes | Measure-Object).Count -eq 0) "($($nomes -join ' '))"
        foreach ($n in $nomes) {
            $o = & $musica $bsdata $n 300 "$Saida\h7_$n.wav" 2>&1 | Out-String
            $rc = $LASTEXITCODE
            $o | Out-File "$Saida\h7_$n.txt"
            Remove-Item "$Saida\h7_$n.wav" -ErrorAction SilentlyContinue
            # "sem camada" nao entra: notas com tecla fora de todas as camadas, que o driver tambem nao toca (0x0B78;
            # M2B, M3, M4 e M7, docs/N6-PLANO.md 18.5); e informado
            $zeros = "recusadas", "tom saturado", "falhas de amostra", "saturadas", "com a nota ligada", "MIXS acima de 20 bits"
            $ruins = @($zeros | Where-Object { (Num $o $_) -ne 0 })
            Check "H7" "$n 300 s" ($rc -eq 0 -and (Num $o "notas") -gt 0 -and (Num $o "vozes") -gt 0 -and $ruins.Count -eq 0) "notas $(Num $o 'notas'), vozes $(Num $o 'vozes'), sem camada $(Num $o 'sem camada'), pico $(Num $o 'pico'); nao zero: $($ruins -join ', ')"
        }
    }
}
if ($Casos -contains "H8") {
    LimparAmbiente
    $env:BS_AUDIO = "nativo"
    try {
        $rc = Filho "$Saida\h8_pd.txt" (Join-Path $repo "tools\dist\test_pd_nativo.ps1") "-Exe `"$Exe`" -ExeMods `"$Exe`" -Data `"$Data`" -Casos P1,P2 -Paralelo 2 -Saida `"$Saida\h8_pd`""
        $o8 = Get-Content "$Saida\h8_pd.txt"
        Check "H8" "controles com o audio nativo (test_pd_nativo P1,P2)" ($rc -eq 0) (($o8 | Select-Object -Last 1) -join "")
        $probe = Join-Path $Build "builder\bs-video-probe.exe"
        $rc = Filho "$Saida\h8_e001.txt" (Join-Path $PSScriptRoot "n5_e001.ps1") "-Exe `"$Exe`" -Probe `"$probe`" -Preview `"$Data`" -Saida `"$Saida\h8_e001`""
        $o8b = Get-Content "$Saida\h8_e001.txt"
        Check "H8" "filme E001 com o audio nativo (n5_e001)" ($rc -eq 0) (($o8b | Select-Object -Last 1) -join "")
        if (-not $ExeMods -or -not (Test-Path $ExeMods)) { Skip "H8" "test_saves exige o build com mods (-ExeMods)" } else {
            $rc = Filho "$Saida\h8_saves.txt" (Join-Path $repo "tools\dist\test_saves.ps1") "-Exe `"$Exe`" -ExeMods `"$ExeMods`" -Data `"$Data`" -Saida `"$Saida\h8_saves`""
            $o8c = Get-Content "$Saida\h8_saves.txt"
            Check "H8" "saves com o audio nativo (test_saves.ps1)" ($rc -eq 0) (($o8c | Select-Object -Last 1) -join "")
        }
    } finally { LimparAmbiente }
}
if ($Casos -contains "H9") {
    if (-not (Test-Path $SaveG1)) { Skip "H9" "save g1 ausente" } else {
        $cart = CartaoG1 "h9"
        [IO.File]::WriteAllText("$Saida\h9_sair.txt", "$($JanelaSegundos * 1000) sair`n")
        $rc = Jogo "h9" "--saves-pasta `"$cart`" --audio nativo" @{ BS_INPUT_SCRIPT = $rotaLoad; BS_CONTROLE_VIRTUAL = "$Saida\h9_sair.txt" }
        $mix = Linha "h9" "\[audio\] mistura:"
        Check "H9" "janela: termina sem erro, saida XAudio2 usada" ($rc -eq 0 -and (Num $mix "blocos") -gt 1000) "$mix"
        Check "H9" "janela: nenhuma falta de dados, erro de escrita ou perda do dispositivo" ((Num $mix "dados") -eq 0 -and (Num $mix "escrita") -eq 0 -and (Num $mix "dispositivo") -eq 0) "faltas $(Num $mix 'dados'), erros $(Num $mix 'escrita'), perdas $(Num $mix 'dispositivo')"
        Check "H9" "janela: mistura sem saturacao" ((Num $mix "saturadas") -eq 0) ""
    }
}
} finally {
    LimparAmbiente
    foreach ($k in $ambienteOriginal.Keys) { Set-Item "env:$k" $ambienteOriginal[$k] }
}
Write-Host ""
foreach ($k in $resultados.Keys) { Write-Host ("{0}: {1}" -f $k, $resultados[$k]) }
Write-Host "$falhas falhas$(if ($Parcial) { ' (execucao parcial: nao vale para a promocao)' }); saidas em $Saida"
exit $(if ($falhas) { 1 } else { 0 })
