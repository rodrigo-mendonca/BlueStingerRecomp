# N7b: gera os cabecalhos SPIR-V dos shaders do backend Vulkan (native/render/vulkan/shaders ->
# native/render/vulkan/spirv/*.h). Os cabecalhos sao versionados: o build, o Builder e o usuario nao precisam
# do Vulkan SDK. Depois de editar um shader, rode isto e confira com -Conferir (falha se algum cabecalho
# versionado nao corresponde ao fonte).
#   powershell -File tools/dev/gera_spirv.ps1 [-Conferir] [-Sdk <VulkanSDK\versao>]
param(
    [switch]$Conferir,
    [string]$Sdk = (Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\sdk\VulkanSDK\1.4.363.0")
)
$ErrorActionPreference = "Stop"
$repo = Split-Path (Split-Path $PSScriptRoot)
$src = Join-Path $repo "native\render\vulkan\shaders"
$dst = Join-Path $repo "native\render\vulkan\spirv"
$glslang = Join-Path $Sdk "Bin\glslangValidator.exe"
$val = Join-Path $Sdk "Bin\spirv-val.exe"
if (-not (Test-Path $glslang)) { Write-Host "ERRO: falta $glslang"; exit 2 }
New-Item -ItemType Directory -Force $dst | Out-Null
$tmp = Join-Path $env:TEMP "bs_spirv"; New-Item -ItemType Directory -Force $tmp | Out-Null
# nome do cabecalho, fonte, variavel, definicoes
$jobs = @(
    @("pvr_vert.h", "pvr.vert", "kPvrVert", @()),
    @("pvr_frag.h", "pvr.frag", "kPvrFrag", @()),
    @("pvr_frag_autosort.h", "pvr.frag", "kPvrFragAutosort", @("-DAUTOSORT")),
    @("resolve_comp.h", "resolve.comp", "kResolveComp", @()),
    # o ambiente FP do software para dispositivos com VK_KHR_shader_float_controls: RTE (fc) e RTE + subnormais (fcd)
    @("pvr_frag_fc.h", "pvr.frag", "kPvrFragFc", @("-DFLOAT_CONTROLS")),
    @("pvr_frag_autosort_fc.h", "pvr.frag", "kPvrFragAutosortFc", @("-DAUTOSORT", "-DFLOAT_CONTROLS")),
    @("resolve_comp_fc.h", "resolve.comp", "kResolveCompFc", @("-DFLOAT_CONTROLS")),
    @("pvr_frag_fcd.h", "pvr.frag", "kPvrFragFcd", @("-DFLOAT_CONTROLS", "-DDENORM_PRESERVE")),
    @("pvr_frag_autosort_fcd.h", "pvr.frag", "kPvrFragAutosortFcd", @("-DAUTOSORT", "-DFLOAT_CONTROLS", "-DDENORM_PRESERVE")),
    @("resolve_comp_fcd.h", "resolve.comp", "kResolveCompFcd", @("-DFLOAT_CONTROLS", "-DDENORM_PRESERVE")),
    # N8.7: volumes modificadores (sombra por intensidade), com as tres variantes do ambiente FP
    @("volumes_comp.h", "volumes.comp", "kVolumesComp", @()),
    @("volumes_comp_fc.h", "volumes.comp", "kVolumesCompFc", @("-DFLOAT_CONTROLS")),
    @("volumes_comp_fcd.h", "volumes.comp", "kVolumesCompFcd", @("-DFLOAT_CONTROLS", "-DDENORM_PRESERVE")),
    # N7c: apresentacao direta e reducao da resolucao interna
    @("present_vert.h", "present.vert", "kPresentVert", @()),
    @("present_frag.h", "present.frag", "kPresentFrag", @()),
    @("downsample_comp.h", "downsample.comp", "kDownsampleComp", @()),
    # N7c secao 21: pos-processamento (o passe interno e o exemplo de mod, para os testes do validador)
    @("post_vert.h", "post.vert", "kPostVert", @()),
    @("post_color_frag.h", "post_color.frag", "kPostColorFrag", @()),
    @("post_tonemap_frag.h", "post_tonemap.frag", "kPostTonemapFrag", @()),
    @("post_sharpen_frag.h", "post_sharpen.frag", "kPostSharpenFrag", @()),
    @("post_bloom_h_frag.h", "post_bloom_h.frag", "kPostBloomHFrag", @()),
    @("post_bloom_v_frag.h", "post_bloom_v.frag", "kPostBloomVFrag", @()),
    @("post_crt_frag.h", "post_crt.frag", "kPostCrtFrag", @()),
    @("post_exemplo_frag.h", "..\..\..\..\mods-exemplos\shaders\cor-quente\shader.frag", "kPostExemploFrag", @()),
    # testes do validador: um passe que escreve num buffer e um com push constants grandes demais
    @("post_teste_buffer_frag.h", "teste\post_buffer.frag", "kPostTesteBufferFrag", @()),
    @("post_teste_push_frag.h", "teste\post_push.frag", "kPostTestePushFrag", @()),
    @("post_teste_laco_frag.h", "teste\post_laco.frag", "kPostTesteLacoFrag", @()),
    @("post_teste_3d_frag.h", "teste\post_3d.frag", "kPostTeste3dFrag", @())
)
$fail = 0
foreach ($j in $jobs) {
    $out = Join-Path $tmp $j[0]
    $spv = Join-Path $tmp ($j[0] + ".spv")
    $args = @("-V", "--target-env", "vulkan1.1") + $j[3] + @("-o", $spv, (Join-Path $src $j[1]))
    & $glslang @args | Out-Null
    if ($LASTEXITCODE) { Write-Host "ERRO: glslang $($j[1]) $($j[3])"; exit 1 }
    & $val --target-env vulkan1.1 $spv
    if ($LASTEXITCODE) { Write-Host "ERRO: spirv-val $($j[0])"; exit 1 }
    $bytes = [IO.File]::ReadAllBytes($spv)
    $words = for ($i = 0; $i -lt $bytes.Length; $i += 4) { "0x{0:x8}u" -f [BitConverter]::ToUInt32($bytes, $i) }
    $lines = for ($i = 0; $i -lt $words.Count; $i += 8) { "    " + (($words[$i..([Math]::Min($i + 7, $words.Count - 1))]) -join ", ") + "," }
    $text = "// Gerado por tools/dev/gera_spirv.ps1 de native/render/vulkan/shaders/$($j[1]) $($j[3] -join ' '): nao editar.`n" +
            "#pragma once`n#include <cstdint>`n`ninline constexpr uint32_t $($j[2])[] = {`n" + ($lines -join "`n") + "`n};`n"
    $target = Join-Path $dst $j[0]
    if ($Conferir) {
        $cur = if (Test-Path $target) { [IO.File]::ReadAllText($target) } else { "" }
        if ($cur -ne $text) { Write-Host "[DIVERGE] $($j[0])"; $fail++ } else { Write-Host "[ ok ] $($j[0])" }
    } else {
        [IO.File]::WriteAllText($target, $text)
        Write-Host "$($j[0]): $($words.Count) palavras"
    }
}
exit $(if ($fail) { 1 } else { 0 })
