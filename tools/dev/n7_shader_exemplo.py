"""N7c (docs/N7-PLANO.md secao 21): instala o pacote de exemplo de shader de pos-processamento (mods-exemplos/shaders/
cor-quente) numa pasta mods\\shaders do jogo, com o shader.spv escrito a partir do SPIR-V gerado e versionado
(native/render/vulkan/spirv/post_exemplo_frag.h, de tools/dev/gera_spirv.ps1): nao precisa do Vulkan SDK.

  python tools/dev/n7_shader_exemplo.py <pasta do jogo>              # instala mods\\shaders\\cor-quente
  python tools/dev/n7_shader_exemplo.py <pasta do jogo> --invalidos  # e os pacotes invalidos dos testes
  python tools/dev/n7_shader_exemplo.py <pasta do jogo> --remover    # remove os pacotes que este script instala

Os invalidos (testes da recusa): "teste-buffer" (escreve num buffer de armazenamento), "teste-corrompido" (SPIR-V
truncado) e "teste-manifesto" (parametro com faixa invalida).
"""
import argparse
import os
import re
import shutil
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPIRV = os.path.join(REPO, "native", "render", "vulkan", "spirv")
EXEMPLO = os.path.join(REPO, "mods-exemplos", "shaders", "cor-quente")
NOMES = ["cor-quente", "teste-buffer", "teste-corrompido", "teste-manifesto"]


def palavras(header):
    texto = open(os.path.join(SPIRV, header), encoding="utf-8").read()
    corpo = texto[texto.index("{") + 1:texto.rindex("}")]
    return [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)u", corpo)]


def escrever_spv(caminho, ws):
    with open(caminho, "wb") as f:
        f.write(struct.pack("<%dI" % len(ws), *ws))


def pacote(dest, nome, manifesto, ws):
    d = os.path.join(dest, nome)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "manifesto.toml"), "w", encoding="utf-8") as f:
        f.write(manifesto)
    escrever_spv(os.path.join(d, "shader.spv"), ws)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("jogo", help="pasta do executavel (mods\\shaders fica dentro dela)")
    ap.add_argument("--invalidos", action="store_true")
    ap.add_argument("--remover", action="store_true")
    a = ap.parse_args()
    dest = os.path.join(a.jogo, "mods", "shaders")
    if a.remover:
        for n in NOMES:
            shutil.rmtree(os.path.join(dest, n), ignore_errors=True)
        print("removidos de", dest)
        return 0
    os.makedirs(dest, exist_ok=True)
    manifesto = open(os.path.join(EXEMPLO, "manifesto.toml"), encoding="utf-8").read()
    d = pacote(dest, "cor-quente", manifesto, palavras("post_exemplo_frag.h"))
    shutil.copy2(os.path.join(EXEMPLO, "shader.frag"), d)  # o fonte junto, por transparencia
    print("instalado", d)
    if a.invalidos:
        simples = "formato = 1\nnome = \"%s\"\nshader = \"shader.spv\"\n"
        pacote(dest, "teste-buffer", simples % "teste-buffer", palavras("post_teste_buffer_frag.h"))
        ws = palavras("post_exemplo_frag.h")
        pacote(dest, "teste-corrompido", simples % "teste-corrompido", ws[: len(ws) // 3])
        pacote(dest, "teste-manifesto", simples % "teste-manifesto" +
               "[[parametros]]\nnome = \"x\"\nminimo = 1.0\nmaximo = 0.0\n", ws)
        print("instalados os invalidos de teste em", dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
