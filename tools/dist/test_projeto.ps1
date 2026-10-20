# Projeto C++ editavel e mods, pela distribuicao do Builder:
#   1. compila do CDI exportando o projeto (mapa de funcoes, fontes, C++ gerado, mods/);
#   2. adiciona um mod em C++, uma alteracao controlada no C++ gerado e um mod de arquivo;
#   3. recompila A PARTIR do projeto com o CDI inacessivel;
#   4. o jogo instalado mostra os tres efeitos (sem o CDI); 4b. B10 (N2b): mod que reescreve a
#      espera do VBlank, com bs::mods::wait (sem falta de progresso) e sem (falta acusada com PC);
#   5. mods de recursos (N1): maior que o original vale no provedor nativo; no legado, e para
#      recurso inexistente, mensagem clara;
#   6. reexportar preserva mods/ e guarda o generated/ editado.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_projeto.ps1 -Disc <imagem.cdi> [-Builder <pasta>]
param(
    [Parameter(Mandatory = $true)][string]$Disc,
    [string]$Builder = ""
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Builder) { $Builder = Join-Path $repo "dist\BlueStinger-Builder" }
$Disc = (Resolve-Path -LiteralPath $Disc).Path
$work = Join-Path $env:TEMP "bs-projeto-teste"
$marker = Join-Path $work ".bs-projeto-teste"
if (Test-Path -LiteralPath $work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $work existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $work
}
New-Item -ItemType Directory -Path $work | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function RunCmd($line) { return (cmd.exe /c "$line 2>&1" | Out-String) }  # nao chamar de Cmd: esconderia o cmd.exe  # stderr nativo sem abortar o script

$cli = Join-Path $Builder "bs-builder.exe"
$cdi = Join-Path $work "imagem ç.cdi"
cmd.exe /c "mklink /H `"$cdi`" `"$Disc`"" | Out-Null
if (-not (Test-Path -LiteralPath $cdi)) { Copy-Item -LiteralPath $Disc -Destination $cdi }
$env:BS_BUILDER_CACHE = Join-Path $work "cache"
$out = Join-Path $work "saída ç"
$proj = Join-Path $out "projeto"
$exe = Join-Path $out "BlueStinger\BlueStinger.exe"

# 1. compilacao do CDI exportando o projeto
$r = RunCmd "`"$cli`" --disc `"$cdi`" --out `"$out`" --sem-filmes --projeto `"$proj`""
$mapa = Join-Path $proj "mapa_funcoes.tsv"
$okFiles = (Test-Path -LiteralPath (Join-Path $proj "LEIA-ME-PROJETO.md")) -and (Test-Path -LiteralPath (Join-Path $proj "generated\funcs.h")) -and
           (Test-Path -LiteralPath (Join-Path $proj "mods\exemplos\exemplo_contador.cpp")) -and (Test-Path -LiteralPath (Join-Path $proj "third_party\SDL3\CMakeLists.txt"))
$okMap = (Test-Path -LiteralPath $mapa) -and ((Get-Content -LiteralPath $mapa | Select-String "^0x8C194580\tf_8C194580\tfuncs_\d+\.cpp\tpointer_table") -ne $null)
Check "exporta o projeto (fontes, gerado, mods, mapa)" ($LASTEXITCODE -eq 0 -and $okFiles -and $okMap) ""

# 2. mod em C++, alteracao no C++ gerado, mod de arquivo do mesmo tamanho
Copy-Item -LiteralPath (Join-Path $proj "mods\exemplos\exemplo_contador.cpp") -Destination (Join-Path $proj "mods\exemplo_contador.cpp")
$gen = (Get-ChildItem -LiteralPath (Join-Path $proj "generated") -Filter "funcs_*.cpp" | Select-String -Pattern "^void f_8C010000\(" -List | Select-Object -First 1).Path
$t = [IO.File]::ReadAllText($gen)
$t = $t.Replace("void f_8C010000(bs::Sh4Context* c) {`n    BS_FN_ENTER(0x8C010000u);", "void f_8C010000(bs::Sh4Context* c) {`n    BS_FN_ENTER(0x8C010000u);`n    std::fputs(`"[edicao controlada] entrada do jogo\n`", stderr);")
$t = $t.Replace("#include `"funcs.h`"`n", "#include `"funcs.h`"`n#include <cstdio>`n")
[IO.File]::WriteAllText($gen, $t)
$manifest = Get-Content -LiteralPath (Join-Path $out "BlueStinger\data\disc.manifest")
$small = $manifest | Where-Object { $_ -match '^file \d+ (\d+) \S+ (.+)$' -and [int]$Matches[1] -ge 64 -and [int]$Matches[1] -le 4096 } | Select-Object -First 1
$small -match '^file \d+ (\d+) \S+ (.+)$' | Out-Null
$assetRel = $Matches[2]; $assetSize = [int]$Matches[1]
$assetOrig = Join-Path $out ("BlueStinger\data\disc\" + $assetRel.Replace('/', '\'))
$assetMod = Join-Path $proj ("mods\assets\" + $assetRel.Replace('/', '\'))
New-Item -ItemType Directory -Force -Path (Split-Path $assetMod) | Out-Null
# copia identica: prova que o jogo le o arquivo do mod e segue normal (a troca do conteudo
# setor a setor e coberta por runtime_data_asset_overrides; mudar um arquivo de configuracao
# como CNF00.NT muda o comportamento do jogo)
Copy-Item -LiteralPath $assetOrig -Destination $assetMod
# B10 (N2b): a espera do VBlank do jogo (8C064A20) reescrita num mod, ativa so com
# BS_TESTE_ESPERA: "primitiva" usa bs::mods::wait em cada volta; "sem", o mesmo laco so com poll
# (o monitor de progresso tem de acusar a falta de progresso com a thread e o PC)
[IO.File]::WriteAllText((Join-Path $proj "mods\teste_espera.cpp"), @'
#include "cpu/sh4_mem.h"
#include "mods/mod_api.h"

#include <cstdlib>
#include <cstring>

static bool g_primitiva = true;

static void espera(bs::Sh4Context* c) {
    const uint32_t flag = bs::sh4_read32(c, 0x8C064A84) + uint32_t(int16_t(bs::sh4_read16(c, 0x8C064A80)));
    while (bs::sh4_read16(c, flag) == 0) {
        if (g_primitiva) bs::mods::wait(c, 0x8C064A2C, 0x8C064A26, 4);
        else bs::mods::poll(c, 0x8C064A26, 4);
    }
    if (g_primitiva) bs::mods::wait_end(c, 0x8C064A2C);
    bs::mods::call_original(0x8C064A20, c);  // bandeira ja acesa: o original so conclui
}

BS_MOD(teste_espera) {
    const char* v = std::getenv("BS_TESTE_ESPERA");
    if (!v || !*v) return;
    g_primitiva = std::strcmp(v, "sem") != 0;
    bs::mods::replace(0x8C064A20, espera);
}
'@)

# 3. recompila do projeto com o CDI inacessivel
Rename-Item -LiteralPath $cdi -NewName "imagem ç.cdi.fora"
try {
    $r = RunCmd "`"$cli`" --de-projeto `"$proj`" --out `"$out`" --sem-filmes"
    Check "recompila do projeto sem o CDI" ($LASTEXITCODE -eq 0 -and $r -match "instalado:") (($r -split "`n" | Where-Object { $_ -match "falhou" } | Select-Object -First 1))
    # 4. efeitos no jogo instalado (roteiro ate o NEW GAME)
    $env:BS_INPUT_SCRIPT = Join-Path $PSScriptRoot "roteiros\newgame_area1.txt"
    $g = RunCmd "`"$exe`" --headless 2300"
    $env:BS_INPUT_SCRIPT = ""
    Check "mod em C++ ativo (replace)" ($g -match "\[mods\] iniciando exemplo_contador" -and $g -match "\[mod exemplo\] primeira chamada de 8C194580") ""
    Check "alteracao no C++ gerado ativa" ($g -match "\[edicao controlada\] entrada do jogo") ""
    Check "mod de arquivo carregado" ($g -match [regex]::Escape("[mods] recurso substituido: $($assetRel.ToUpper())") -and $g -match "VBlanks: 2300;") $assetRel
    # 4b. B10: mod de funcao de espera, com e sem a primitiva (modo sombra: a falta e acusada)
    $env:BS_INPUT_SCRIPT = Join-Path $PSScriptRoot "roteiros\newgame_area1.txt"
    $env:BS_TESTE_ESPERA = "primitiva"
    $g = RunCmd "`"$exe`" --headless 2300"
    $env:BS_TESTE_ESPERA = "sem"
    $g2 = RunCmd "`"$exe`" --headless 2300"
    $env:BS_TESTE_ESPERA = ""; $env:BS_INPUT_SCRIPT = ""
    $ep = [regex]::Match($g, "\[progresso\] espera 8C064A2C: episodios (\d+).*evento esperado VBlankIn: com (\d+), sem (\d+)")
    Check "B10 mod de espera com a primitiva: roda, espera declarada, sem falta de progresso" `
        ($g -match "VBlanks: 2300;" -and $g -match "faltas de progresso 0 " -and $ep.Success -and [int]$ep.Groups[1].Value -gt 0 -and $ep.Groups[3].Value -eq "0") $ep.Value
    $msg = ($g2 -split "`n" | Where-Object { $_ -match "falta de progresso:" } | Select-Object -First 1)
    Check "B10 mod de espera sem a primitiva: falta de progresso com thread e PC" `
        ($g2 -match "VBlanks: 2300;" -and $msg -match "thread 0 \d+ \(maior lote em 8C064A26\)" -and $msg -match "bs::mods::wait") $msg
    # 5. mod maior que o original: o provedor nativo de arquivos (N1) aceita; o legado (setores
    #    de tamanho fixo) recusa com mensagem clara; mod sem recurso correspondente: erro claro
    $installedAsset = Join-Path $out ("BlueStinger\mods\assets\" + $assetRel.Replace('/', '\'))
    [IO.File]::WriteAllBytes($installedAsset, (New-Object byte[] ($assetSize + 4096)))
    $g = RunCmd "`"$exe`" --headless 10"
    Check "mod maior que o original: aceito no provedor nativo" ($LASTEXITCODE -eq 0 -and $g -match [regex]::Escape("($assetSize -> $($assetSize + 4096) bytes)")) (($g -split "`n" | Where-Object { $_ -match "erro|recurso" } | Select-Object -First 1))
    $g = RunCmd "`"$exe`" --arquivos legado --headless 10"
    Check "mod maior que o original: mensagem clara no legado" ($LASTEXITCODE -ne 0 -and $g -match "maior que o original") (($g -split "`n" | Where-Object { $_ -match "erro" } | Select-Object -First 1))
    $bogus = Join-Path $out "BlueStinger\mods\assets\BSDATA\NAO_EXISTE.BIN"
    [IO.File]::WriteAllText($bogus, "x")
    $g = RunCmd "`"$exe`" --headless 10"
    Check "mod sem recurso correspondente: mensagem clara" ($LASTEXITCODE -ne 0 -and $g -match "NAO_EXISTE.BIN: nao substitui nenhum recurso") (($g -split "`n" | Where-Object { $_ -match "erro" } | Select-Object -First 1))
    Remove-Item -LiteralPath $bogus
    # 5b. remover o mod e recompilar: o efeito some (o cache espelha mods/, nada sobra la)
    Remove-Item -LiteralPath (Join-Path $proj "mods\exemplo_contador.cpp")
    $r = RunCmd "`"$cli`" --de-projeto `"$proj`" --out `"$out`" --sem-filmes"
    $env:BS_INPUT_SCRIPT = Join-Path $PSScriptRoot "roteiros\newgame_area1.txt"
    $g = RunCmd "`"$exe`" --headless 2300"
    $env:BS_INPUT_SCRIPT = ""
    Check "mod removido: efeito some apos recompilar" ($r -match "instalado:" -and $g -notmatch "exemplo_contador" -and $g -match "\[edicao controlada\]" -and $g -match "VBlanks: 2300;") ""
    Copy-Item -LiteralPath (Join-Path $proj "mods\exemplos\exemplo_contador.cpp") -Destination (Join-Path $proj "mods\exemplo_contador.cpp")
} finally {
    Rename-Item -LiteralPath "$cdi.fora" -NewName "imagem ç.cdi"
}

# 6. reexportar: mods/ preservada, generated/ editado guardado, edicao (mais nova) em runtime/ mantida
$rtFile = Join-Path $proj "runtime\aica\aica_regs.cpp"
Add-Content -LiteralPath $rtFile -Value "// edicao do usuario no projeto (teste)"
$r = RunCmd "`"$cli`" --disc `"$cdi`" --out `"$out`" --sem-filmes --projeto `"$proj`""
$kept = Test-Path -LiteralPath (Join-Path $proj "mods\exemplo_contador.cpp")
$backup = @(Get-ChildItem -LiteralPath $proj -Directory -Filter "generated.anterior-*").Count -eq 1
$genEdit = Select-String -LiteralPath (Get-ChildItem -LiteralPath (Get-ChildItem -LiteralPath $proj -Directory -Filter "generated.anterior-*").FullName -Filter "funcs_*.cpp").FullName -Pattern "edicao controlada" -Quiet
Check "reexportar preserva mods e o gerado editado" ($LASTEXITCODE -eq 0 -and $kept -and $backup -and $genEdit) ""
Check "reexportar mantem edicao mais nova em runtime/" ((Get-Content -LiteralPath $rtFile -Raw) -match "edicao do usuario no projeto") ""

$env:BS_BUILDER_CACHE = ""
$fails = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} casos, {1} falhas" -f $results.Count, $fails)
if (-not $fails) { Remove-Item -Recurse -Force -LiteralPath $work }
exit $(if ($fails) { 1 } else { 0 })
