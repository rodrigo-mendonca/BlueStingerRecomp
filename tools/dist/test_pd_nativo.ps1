# N3-2: provedor nativo do SDK de controles (--controles nativo) contra o legado (Maple), docs/N3-2-RELATORIO.md.
#   P1 equivalencia nas 5 rotas (passo_cgate, passo_heliporto, passo_combate, passo_load_opcoes,
#      newgame_area1), --tempo nativo e experimental: quadros, leituras, cargas, estado logico e audio
#      por passo (compara_modos.py) e o traco do PDS por passo COM as colunas de tempo (vblank e
#      leituras: o indice de amostras do nativo e o numero de GET_CONDITION do legado)
#   P2 fechamento (BS_PAD_FECHAMENTO): no nativo, 0 entradas no servidor, decodificador e auxiliares do
#      pd e no getter; 0 GET_CONDITION, 0 respostas do controle e 0 leituras da fonte no Maple; disparos
#      efetivos = DMAs; guarda da area nativa 0; no maximo uma passada do servidor por disparo (a regra
#      de coleta depende disso: servidores extras 0). O init 8C15C10C e o encerramento 8C15C686 do pd originais
#      rodam (cada um limpa as 24 unidades): entradas em 8C15C1A2 = 24 x (init + encerramento), nenhuma do servidor. No legado, o servidor roda (controle do teste)
#   P3 cenarios do N3-0 (conexao com A, porta vazia desde o boot, soft reset, limiares, matriz de
#      valores, transicao injetada), nos dois modos de tempo: traco com tempo e quadros iguais; e as
#      conferencias semanticas do pad_traco.py: conexao, boot e limiares no nativo; soft reset (bit,
#      syscall do menu do BIOS e o controle sem START) no legado e no nativo, nos dois modos
#   P4 determinismo: duas execucoes do nativo iguais (quadros e traco)
#   P5 build com mods (todos os slots): nativo igual ao release nativo
#   P6 negativos, todos acusados: press_on, release_zero, amostra_adiantada, info_sem_conectado
#      (traco), presenca_imediata (conexao no boot), maple (o controle volta ao Maple: o fechamento
#      acusa). old_host: o jogo nao reescreve `on` com controle compativel nas rotas, entao o traco nao
#      muda (controle registrado); a sensibilidade de old fica no unitario pad_adapter_mutations
#   P7 area nativa do BIOS HLE: no legado, 0x8C004000-0x8C0040FF continua 0xFF no fim das 5 rotas (o
#      jogo nao a usa); no nativo, a info la (0x78 bytes) e igual ao buffer da camada per do legado
#   P8 estatico: no C++ gerado, toda chamada a 8C15C4E6 e 8C0975C0 passa pelo slot (BSG_SLOT), nenhuma
#      direta a 8C15C4E6, 8C15C668 ou 8C0975C0; os tres estao em [native] slots
#   P9 remapeamento e perfis por GUID (N3-3), no nativo: o roteiro de limiares como CONTROLES FISICOS
#      (BS_CONTROLE_FISICO) da o traco e os quadros do roteiro logico com o mapa padrao; com A e B
#      trocados no roteiro e no [controle], e com a troca so num [perfis."<guid>"] e o controle com esse
#      GUID, tambem; negativo: o perfil de outro GUID nao vale (fallback para o [controle]) e o traco diverge
#   P10 padrao (fim do N3): sem --controles o provedor e o nativo: fechamento do nativo (modo=nativo, Maple
#      sem controle) e traco com tempo, quadros, leituras, estado e audio iguais aos do --controles nativo
#   powershell -File tools/dist/test_pd_nativo.ps1 [-Exe <release>] [-ExeMods <mods>] [-Casos P1,P2,...] [-Paralelo 8]
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\msvc-release\app\bluestinger.exe"),
    [string]$ExeMods = (Join-Path $PSScriptRoot "..\..\build\msvc-release-mods\app\bluestinger.exe"),
    [string]$Data = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview\data"),
    [int]$Paralelo = 8,
    [string[]]$Casos = @("P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9", "P10"),
    [string]$Saida = (Join-Path $env:TEMP "bs-pd-nativo-teste")
)
$ErrorActionPreference = "Stop"
$Casos = @($Casos | ForEach-Object { $_ -split "," } | Where-Object { $_ })
# sem artefatos a suite nao prova nada: erro, nunca sucesso (revisao Codex 2026-10-18)
foreach ($e in @($Exe, $ExeMods)) { if (-not (Test-Path -LiteralPath $e)) { Write-Host "ERRO: executavel ausente ($e)"; exit 2 } }
if (-not (Test-Path -LiteralPath (Join-Path $Data "disc.manifest"))) { Write-Host "ERRO: dados ausentes ($Data)"; exit 2 }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { Write-Host "ERRO: python ausente"; exit 2 }
$Exe = (Resolve-Path $Exe).Path; $ExeMods = (Resolve-Path $ExeMods).Path; $Data = (Resolve-Path $Data).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$rot  = Join-Path $repo "tools\dist\roteiros"
$traco = Join-Path $repo "tools\dev\pad_traco.py"; $compara = Join-Path $repo "tools\dev\compara_modos.py"
$marker = Join-Path $Saida ".bs-pd-nativo-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
$S = $Saida
python (Join-Path $repo "tools\dev\pad_roteiros.py") $S | Out-Null
$want = @{}; foreach ($c in $Casos) { $want[$c] = $true }

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function VBlanksFor($file) {
    $last = 0
    foreach ($l in [IO.File]::ReadLines($file)) { $t = $l.Split("#")[0].Trim(); if ($t -match "^(\d+)\s") { $last = [int64]$Matches[1] } }
    return [int64]($last * 2 + 3000)
}

# --- execucoes (paralelas, no maximo $Paralelo); prefixo P: P_fr, P_fl, P_st, P_au, P_trace, P_fech ---
$jobs = [System.Collections.ArrayList]::new()
$procs = @{}  # tag -> processo (codigo de saida, revisao Codex)
function ExitOk($tag) { $p = $script:procs[$tag]; return $p -and $p.HasExited -and $p.ExitCode -eq 0 -and (Test-Path (Join-Path $S "${tag}_trace.tsv")) -and (Get-Item (Join-Path $S "${tag}_trace.tsv")).Length -gt 0 }
function Diverges($x) { return $x.Out -match "diferentes (\d+)" -and [int]$Matches[1] -gt 0 }
function Job($tag, $exe, $script, $n, $controles, $tempo = "nativo", $envs = @{}, [switch]$Dump) {
    $p = Join-Path $S $tag
    $e = @{ BS_RELOGIO_FIXO = "1700000000"; BS_INPUT_SCRIPT = $script; BS_FRAME_LOG = "${p}_fr.tsv"; BS_FILE_LOG = "${p}_fl.tsv"
            BS_STATE_PASSO = "${p}_st.tsv"; BS_PAD_TRACE = "${p}_trace.tsv"; BS_PAD_FECHAMENTO = "${p}_fech.tsv"
            BS_AICA_LOG = "${p}_au.tsv"; BS_AICA_LOG_TUDO = "1" }
    if ($Dump) { $e.BS_RAM_DUMP = "$($n - 1):${p}_ram.bin" }
    foreach ($k in $envs.Keys) { $e[$k] = $envs[$k] }
    [void]$script:jobs.Add(@{ Tag = $tag; Exe = $exe; N = $n; Env = $e; Tempo = $tempo; Ctl = $controles; P = $p })
}
function RunAll() {
    $running = @()
    foreach ($j in $script:jobs) {
        while (@($running | Where-Object { -not $_.HasExited }).Count -ge $Paralelo) { Start-Sleep -Milliseconds 500 }
        $cmd = ""
        foreach ($k in $j.Env.Keys) { $cmd += "set `"$k=$($j.Env[$k])`"&& " }
        $cmd += "`"$($j.Exe)`" --data `"$Data`" --tempo $($j.Tempo)$(if ($j.Ctl) { " --controles $($j.Ctl)" }) --headless $($j.N) > `"$($j.P)_saida.txt`" 2>&1"
        $proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c $cmd" -WindowStyle Hidden -PassThru
        $null = $proc.Handle
        $script:procs[$j.Tag] = $proc
        $running += $proc
    }
    $running | Wait-Process -Timeout 7200
    $script:jobs.Clear()
}
function Fech($tag) {
    $h = @{}; $f = Join-Path $S "${tag}_fech.tsv"
    if (Test-Path $f) { foreach ($l in Get-Content $f) { $c = $l.Split("`t", 2); if ($c.Count -eq 2) { $h[$c[0]] = $c[1] } } }
    return $h
}
function Py($tool, [string[]]$a) { $o = python $tool @a 2>&1 | Out-String; return @{ Code = $LASTEXITCODE; Out = $o.Trim() } }
function Same($a, $b) { (Get-FileHash (Join-Path $S $a)).Hash -eq (Get-FileHash (Join-Path $S $b)).Hash }
function TraceDiff($a, $b) { Py $traco @("compara", (Join-Path $S "${a}_trace.tsv"), (Join-Path $S "${b}_trace.tsv"), "--com-tempo") }
function Pair($a, $b) {  # compara_modos + traco com tempo
    $m = Py $compara @((Join-Path $S $a), (Join-Path $S $b))
    $t = TraceDiff $a $b
    $res = ($m.Out -split "`n" | Where-Object { $_ -match "^RESULTADO" } | Select-Object -First 1)
    $ok = $res -and $res -notmatch "FALHA" -and $t.Code -eq 0
    $det = "$res | $(($t.Out -split "`n")[0])"
    if ($t.Code -ne 0) { $det += " | " + (($t.Out -split "`n") | Where-Object { $_ -match "primeira" } | Select-Object -First 1) }
    return @{ Ok = $ok; Det = $det }
}
$closureZero = @("f_8C15C4E6", "f_8C15C24C", "f_8C15C3D4", "f_8C15C490", "f_8C15C668", "f_8C17B60C",
                 "maple_respostas_controle", "maple_leituras_fonte", "maple_portaA_getcond", "pd_guarda", "pd_servidores_extras")
function ClosureOk($tag) {
    $f = Fech $tag
    if ($f.Count -eq 0) { return @{ Ok = $false; Det = "sem ${tag}_fech.tsv" } }
    $bad = @($closureZero | Where-Object { $f[$_] -ne "0" })
    $okTrig = $f["pd_disparos_efetivos"] -eq $f["maple_dmas"] -and [int64]$f["maple_dmas"] -gt 0
    $okInit = [int64]$f["f_8C15C1A2"] -eq 24 * ([int64]$f["f_8C15C10C"] + [int64]$f["f_8C15C686"]) -and $f["f_8C15C10C"] -eq "1"
    $ok = $bad.Count -eq 0 -and $okTrig -and $okInit -and $f["modo"] -eq "nativo" -and [int64]$f["pd_servidor"] -gt 0
    return @{ Ok = $ok; Det = ("servidor/decod/getter originais {0}/{1}/{2}; Maple GET_COND {3}, respostas {4}, leituras {5}; disparos {6} = DMAs {7}; init {8}x, encerramento {11}x, 8C15C1A2 {9}{10}" -f
        $f["f_8C15C4E6"], $f["f_8C15C24C"], $f["f_8C15C668"], $f["maple_portaA_getcond"], $f["maple_respostas_controle"],
        $f["maple_leituras_fonte"], $f["pd_disparos_efetivos"], $f["maple_dmas"], $f["f_8C15C10C"], $f["f_8C15C1A2"],
        $(if ($bad.Count) { "; NAO ZERO: " + ($bad -join ",") } else { "" }), $f["f_8C15C686"]) }
}

$rotas = @("passo_cgate", "passo_heliporto", "passo_combate", "passo_load_opcoes", "newgame_area1")
$tempos = @("nativo", "experimental")
$cen = @(@("conexao", "$S\passo_conexao.txt", (VBlanksFor "$S\passo_conexao.txt")), @("boot", "$S\pad_conexao_boot.txt", 1500),
         @("softreset", "$S\pad_softreset.txt", 1500), @("limiares", "$S\passo_limiares.txt", (VBlanksFor "$S\passo_limiares.txt")),
         @("matriz", "$S\pad_matriz.txt", 5200), @("transicao", "$S\pad_transicao.txt", 2500))
if ($want.P1 -or $want.P2 -or $want.P7) {
    foreach ($r in $rotas) {
        $n = if ($r -eq "newgame_area1") { 7300 } else { VBlanksFor "$rot\$r.txt" }
        foreach ($t in $tempos) {
            $dump = $want.P7 -and $t -eq "nativo"
            Job "${r}_${t}_leg" $Exe "$rot\$r.txt" $n "legado" $t -Dump:$dump
            Job "${r}_${t}_nat" $Exe "$rot\$r.txt" $n "nativo" $t -Dump:$dump
        }
    }
}
if ($want.P3 -or $want.P4 -or $want.P5 -or $want.P6) {
    foreach ($c in $cen) {
        foreach ($t in $tempos) {
            if (-not $want.P3 -and -not ($t -eq "nativo" -and $c[0] -in @("limiares", "boot"))) { continue }
            Job "c_$($c[0])_${t}_leg" $Exe $c[1] $c[2] "legado" $t
            Job "c_$($c[0])_${t}_nat" $Exe $c[1] $c[2] "nativo" $t
        }
    }
}
$nLim = VBlanksFor "$S\passo_limiares.txt"
if ($want.P3) { foreach ($t in $tempos) { foreach ($m in "leg", "nat") { Job "c_softcontrole_${t}_$m" $Exe "$S\pad_softreset_controle.txt" 600 $(if ($m -eq "leg") { "legado" } else { "nativo" }) $t } } }
if ($want.P4) { Job "det_nat" $Exe "$S\passo_limiares.txt" $nLim "nativo" }
if ($want.P5) { Job "mods_nat" $ExeMods "$S\passo_limiares.txt" $nLim "nativo" }
if ($want.P6) {
    foreach ($m in "press_on", "release_zero", "amostra_adiantada", "info_sem_conectado", "old_host") {
        Job "neg_$m" $Exe "$S\passo_limiares.txt" $nLim "nativo" "nativo" @{ BS_PAD_NATIVO_MUTACAO = $m }
    }
    Job "neg_presenca_imediata" $Exe "$S\pad_conexao_boot.txt" 1500 "nativo" "nativo" @{ BS_PAD_NATIVO_MUTACAO = "presenca_imediata" }
    Job "neg_maple" $Exe "$S\pad_conexao_boot.txt" 1500 "nativo" "nativo" @{ BS_PAD_NATIVO_MUTACAO = "maple" }
}
if ($want.P9) {
    $guid = "030000005e0400008e02000000007200"
    $swap = Join-Path $S "fisico_troca_ab.txt"
    $lines = foreach ($l in [IO.File]::ReadLines("$S\passo_limiares.txt")) {
        if ($l -match "^(\d+)\s+(\S+)(.*)$") { $st = ($Matches[2] -split "\+" | ForEach-Object { if ($_ -eq "A") { "B" } elseif ($_ -eq "B") { "A" } else { $_ } }) -join "+"; "$($Matches[1]) $st$($Matches[3])" } else { $l }
    }
    [IO.File]::WriteAllLines($swap, [string[]]$lines)
    [IO.File]::WriteAllText((Join-Path $S "troca_controle.toml"), "versao = 1`n[controle]`nA = `"b`"`nB = `"a`"`n")
    [IO.File]::WriteAllText((Join-Path $S "troca_perfil.toml"), "versao = 1`n[perfis.`"$guid`"]`nnome = `"teste`"`nA = `"b`"`nB = `"a`"`n")
    $fis = @{ BS_INPUT_SCRIPT = "" }
    Job "r_logico" $Exe "$S\passo_limiares.txt" $nLim "nativo"
    Job "r_identidade" $Exe "" $nLim "nativo" "nativo" @{ BS_INPUT_SCRIPT = ""; BS_CONTROLE_FISICO = "$S\passo_limiares.txt" }
    Job "r_controle" $Exe "" $nLim "nativo" "nativo" @{ BS_INPUT_SCRIPT = ""; BS_CONTROLE_FISICO = $swap; BS_CONTROLES_TOML = "$S\troca_controle.toml" }
    Job "r_perfil" $Exe "" $nLim "nativo" "nativo" @{ BS_INPUT_SCRIPT = ""; BS_CONTROLE_FISICO = $swap; BS_CONTROLES_TOML = "$S\troca_perfil.toml"; BS_CONTROLE_GUID = $guid }
    # conexao com perfil: DESCONECTADO tira o controle (o teclado fica: a porta do jogo fica neutra) e o
    # proximo estado o reconecta com o mesmo GUID; equivale ao roteiro logico com DESCONECTADO -> "-"
    $conSwap = Join-Path $S "fisico_conexao_troca_ab.txt"; $conNeutro = Join-Path $S "conexao_neutra.txt"
    $cl = foreach ($l in [IO.File]::ReadLines("$S\passo_conexao.txt")) { if ($l -match "^(\d+)\s+(\S+)(.*)$") { $st = ($Matches[2] -split "\+" | ForEach-Object { if ($_ -eq "A") { "B" } elseif ($_ -eq "B") { "A" } else { $_ } }) -join "+"; "$($Matches[1]) $st$($Matches[3])" } else { $l } }
    [IO.File]::WriteAllLines($conSwap, [string[]]$cl)
    $cn = foreach ($l in [IO.File]::ReadLines("$S\passo_conexao.txt")) { $l -replace "^(\d+)\s+DESCONECTADO", '$1 -' }
    [IO.File]::WriteAllLines($conNeutro, [string[]]$cn)
    $nCon2 = VBlanksFor "$S\passo_conexao.txt"
    Job "r_con_logico" $Exe $conNeutro $nCon2 "nativo"
    Job "r_con_perfil" $Exe "" $nCon2 "nativo" "nativo" @{ BS_INPUT_SCRIPT = ""; BS_CONTROLE_FISICO = $conSwap; BS_CONTROLES_TOML = "$S\troca_perfil.toml"; BS_CONTROLE_GUID = $guid }
    Job "r_perfil_outro" $Exe "" $nLim "nativo" "nativo" @{ BS_INPUT_SCRIPT = ""; BS_CONTROLE_FISICO = $swap; BS_CONTROLES_TOML = "$S\troca_perfil.toml"; BS_CONTROLE_GUID = "03000000aaaaaaaabbbbbbbb00000000" }
}
if ($want.P10) { Job "padrao" $Exe "$S\passo_limiares.txt" $nLim ""; Job "padrao_nat" $Exe "$S\passo_limiares.txt" $nLim "nativo" }  # "" = sem a opcao
$t0 = Get-Date
RunAll
Write-Host ("execucoes: {0:n0} s" -f ((Get-Date) - $t0).TotalSeconds)

if ($want.P1) {
    foreach ($r in $rotas) { foreach ($t in $tempos) { $x = Pair "${r}_${t}_leg" "${r}_${t}_nat"; Check "P1 legado = nativo: $r ($t)" $x.Ok $x.Det } }
}
if ($want.P2) {
    foreach ($r in $rotas) {
        foreach ($t in $tempos) {
            $x = ClosureOk "${r}_${t}_nat"; Check "P2 fechamento no nativo: $r ($t)" $x.Ok $x.Det
            $l = Fech "${r}_${t}_leg"
            Check "P2 controle: o legado usa o servidor e o Maple: $r ($t)" ([int64]$l["f_8C15C4E6"] -gt 0 -and [int64]$l["maple_respostas_controle"] -gt 0) "servidor $($l["f_8C15C4E6"]), respostas $($l["maple_respostas_controle"])"
        }
    }
}
if ($want.P3) {
    foreach ($c in $cen) {
        foreach ($t in $tempos) {
            $a = "c_$($c[0])_${t}_leg"; $b = "c_$($c[0])_${t}_nat"
            $tr = TraceDiff $a $b
            $fr = (Py $compara @((Join-Path $S $a), (Join-Path $S $b))).Out -split "`n" | Where-Object { $_ -match "^quadros:" } | Select-Object -First 1
            Check "P3 cenario $($c[0]) ($t): traco com tempo e quadros iguais" ($tr.Code -eq 0 -and $fr -match " 0 diferentes, 0 ausentes") "$(($tr.Out -split "`n")[0]) | $fr"
            $x = ClosureOk $b; Check "P3 fechamento: $($c[0]) ($t)" $x.Ok $x.Det
        }
    }
    $nat = "c_conexao_nativo_nat_trace.tsv"
    $k = Py $traco @("conexao", (Join-Path $S $nat), (Join-Path $S "conexao.tsv")); Check "P3 conferencia conexao (pad_traco) no nativo" ($k.Code -eq 0) (($k.Out -split "`n")[-1])
    $k = Py $traco @("boot", (Join-Path $S "c_boot_nativo_nat_trace.tsv")); Check "P3 conferencia porta vazia desde o boot no nativo" ($k.Code -eq 0) (($k.Out -split "`n")[-1])
    foreach ($t in $tempos) {
        foreach ($m in "leg", "nat") {
            $k = Py $traco @("softreset", (Join-Path $S "c_softreset_${t}_${m}_trace.tsv"), (Join-Path $S "c_softreset_${t}_${m}_saida.txt"), (Join-Path $S "c_softcontrole_${t}_${m}_saida.txt"))
            Check "P3 conferencia soft reset (pad_traco) $m ($t)" ($k.Code -eq 0) (($k.Out -split "`n")[-1])
        }
    }
    $k = Py $traco @("limiares", (Join-Path $S "c_limiares_nativo_nat_trace.tsv"), (Join-Path $S "limiares.tsv")); Check "P3 conferencia limiares no nativo" ($k.Code -eq 0) (($k.Out -split "`n")[-1])
}
if ($want.P4) {
    Check "P4 determinismo do nativo (duas execucoes)" ((Same "c_limiares_nativo_nat_fr.tsv" "det_nat_fr.tsv") -and (Same "c_limiares_nativo_nat_trace.tsv" "det_nat_trace.tsv")) ""
}
if ($want.P5) {
    $x = TraceDiff "c_limiares_nativo_nat" "mods_nat"
    Check "P5 build com mods: nativo igual ao release" ($x.Code -eq 0 -and (Same "c_limiares_nativo_nat_fr.tsv" "mods_nat_fr.tsv")) (($x.Out -split "`n")[0])
}
if ($want.P6) {
    foreach ($m in "press_on", "release_zero", "amostra_adiantada", "info_sem_conectado") {
        $x = TraceDiff "c_limiares_nativo_nat" "neg_$m"; Check "P6 negativo acusado: $m" ((ExitOk "neg_$m") -and (Diverges $x)) (($x.Out -split "`n")[0])
    }
    $x = TraceDiff "c_boot_nativo_nat" "neg_presenca_imediata"; Check "P6 negativo acusado: presenca_imediata" ((ExitOk "neg_presenca_imediata") -and (Diverges $x)) (($x.Out -split "`n")[0])
    $x = ClosureOk "neg_maple"; Check "P6 negativo acusado: maple (retorno ao Maple)" (-not $x.Ok) $x.Det
    $x = TraceDiff "c_limiares_nativo_nat" "neg_old_host"; Check "P6 controle old_host: o jogo nao reescreve on nas rotas (traco igual)" ($x.Code -eq 0) (($x.Out -split "`n")[0])
}
if ($want.P7) {
    foreach ($r in $rotas) {
        $leg = [IO.File]::ReadAllBytes((Join-Path $S "${r}_nativo_leg_ram.bin")); $nat = [IO.File]::ReadAllBytes((Join-Path $S "${r}_nativo_nat_ram.bin"))
        $free = $true; for ($i = 0x4000; $i -lt 0x4100; $i++) { if ($leg[$i] -ne 0xFF) { $free = $false } }
        $same = $true; for ($i = 0; $i -lt 0x78; $i++) { if ($leg[0x2CFFA8 + $i] -ne $nat[0x4000 + $i]) { $same = $false } }
        Check "P7 area nativa: livre no legado e info = buffer da per do legado: $r" ($free -and $same) "livre $free, info igual $same"
        Remove-Item (Join-Path $S "${r}_nativo_leg_ram.bin"), (Join-Path $S "${r}_nativo_nat_ram.bin")
    }
}
if ($want.P8) {
    $gen = Join-Path $repo "generated"
    $direct = @(Select-String -Path "$gen\funcs_*.cpp" -Pattern "BSG_FN\((8C15C4E6|8C15C668|8C0975C0)\)|[^_A-Za-z]f_(8C15C4E6|8C15C668|8C0975C0)\(c\)")
    $slotCalls = @(Select-String -Path "$gen\funcs_*.cpp" -Pattern "BSG_SLOT\((8C15C4E6|8C0975C0)\)")
    $cfg = Get-Content (Join-Path $repo "config\bluestinger_us.toml") -Raw
    $inCfg = ($cfg -match "0x8C15C4E6") -and ($cfg -match "0x8C15C668") -and ($cfg -match "0x8C0975C0")
    Check "P8 estatico: chamadas so pelo slot" ($direct.Count -eq 0 -and $slotCalls.Count -eq 3 -and $inCfg) "diretas $($direct.Count), pelo slot $($slotCalls.Count), config $inCfg"
}

if ($want.P9) {
    foreach ($c in "r_identidade", "r_controle", "r_perfil") {
        $x = TraceDiff "r_logico" $c
        Check "P9 remapeamento: $c = roteiro logico (traco com tempo e quadros)" ($x.Code -eq 0 -and (Same "r_logico_fr.tsv" "${c}_fr.tsv")) (($x.Out -split "`n")[0])
    }
    $x = TraceDiff "r_con_logico" "r_con_perfil"
    Check "P9 conexao com perfil: desconectar com botao, reconectar (mesmo GUID) = roteiro logico com a porta neutra" ($x.Code -eq 0 -and (Same "r_con_logico_fr.tsv" "r_con_perfil_fr.tsv")) (($x.Out -split "`n")[0])
    $bad = @("r_logico", "r_identidade", "r_controle", "r_perfil", "r_perfil_outro", "r_con_logico", "r_con_perfil" | Where-Object { -not (ExitOk $_) })
    Check "P9 execucoes terminaram sem erro e com traco" ($bad.Count -eq 0) ($bad -join ",")
    $x = TraceDiff "r_logico" "r_perfil_outro"; Check "P9 negativo: perfil de outro GUID nao vale (fallback para o [controle])" ((ExitOk "r_perfil_outro") -and (Diverges $x)) (($x.Out -split "`n")[0])
    $o = Get-Content (Join-Path $S "r_perfil_saida.txt") -Raw; Check "P9 o roteiro fisico passou pelo mapa e pelo GUID" ($o -match "roteiro fisico" -and $o -match [regex]::Escape($guid)) ""
}
if ($want.P10) {
    $x = ClosureOk "padrao"; Check "P10 sem --controles: fechamento do nativo (modo=nativo)" $x.Ok $x.Det
    $x = Pair "padrao_nat" "padrao"; Check "P10 sem --controles = --controles nativo (traco com tempo, quadros, leituras, estado, audio)" $x.Ok $x.Det
}
$fails = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("test_pd_nativo: {0} casos, {1} falhas" -f $results.Count, $fails)
exit $(if ($fails) { 1 } else { 0 })
