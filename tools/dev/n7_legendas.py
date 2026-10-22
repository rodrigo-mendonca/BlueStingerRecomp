"""N7c: legendas das falas NA IMAGEM durante o gameplay real (docs/N7-PLANO.md secao 18).

Reproduz uma gravacao de gameplay (tools/dev/gravacoes.py) com o executavel dado e captura os quadros apresentados
(com a legenda composta, como o jogador ve) nas faixas de VBlank das falas que o N6 anuncia no log ("[legenda]
VBlank N: voz/... | nenhuma"). Criterio de aceitacao: o evento no log NAO basta; em cada quadro capturado
  - com uma fala ativa, a legenda tem de estar na imagem: o quadro composto difere do quadro cru (gravado ao lado
    com BS_CAPTURA_CRUA) e so na faixa de baixo da area do jogo;
  - sem fala ativa (antes, entre e depois das falas), nada sobre a imagem: o jogo publica legenda vazia e a captura
    e o proprio quadro (sem legenda persistente).

  python tools/dev/n7_legendas.py [--sessao sessao-20261021-145006] [--exe <bluestinger.exe>] [--render software|nativo]
                                  [--margem 60] [--cada 3]

O executavel precisa do pacote de legendas na pasta dele (mods\\legendas-en): sem ele o N6 nao anuncia falas e o teste
falha. As capturas ficam numa pasta temporaria (apagada no fim, salvo --manter).
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gravacoes  # noqa: E402  (config, pasta, ler_sessao, rodape, jogo)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def ler_bmp(path):
    """(largura, altura, linhas de pixels (r, g, b) de cima para baixo) de um BMP de 24 ou 32 bits."""
    with open(path, "rb") as f:
        d = f.read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0] // 8
    row = (w * bpp + 3) & ~3
    linhas = []
    for y in range(abs(h)):
        yy = (abs(h) - 1 - y) if h > 0 else y
        base = off + yy * row
        linhas.append([(d[base + x * bpp + 2], d[base + x * bpp + 1], d[base + x * bpp]) for x in range(w)])
    return w, abs(h), linhas


def faixas_do_log(texto):
    """[(inicio, fim, voz)] das falas ativas pelo log do N6 (fim = VBlank do proximo anuncio)."""
    ev = [(int(m.group(1)), m.group(2).strip()) for m in re.finditer(r"\[legenda\] VBlank (\d+): (.*)", texto)]
    out = []
    for i, (v, oque) in enumerate(ev):
        if oque != "nenhuma":
            fim = ev[i + 1][0] if i + 1 < len(ev) else None
            out.append((v, fim, oque))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sessao", default="sessao-20261021-145006")
    ap.add_argument("--exe", default=os.path.join(REPO, "build", "n7-game", "app", "bluestinger.exe"))
    ap.add_argument("--render", default="software", choices=["software", "nativo"])
    ap.add_argument("--margem", type=int, default=60, help="VBlanks capturados antes e depois de cada fala")
    ap.add_argument("--cada", type=int, default=3, help="captura 1 quadro a cada N VBlanks")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()

    if not os.path.isdir(os.path.join(os.path.dirname(a.exe), "mods", "legendas-en")):
        raise SystemExit("falta o pacote de legendas em %s" % os.path.join(os.path.dirname(a.exe), "mods", "legendas-en"))
    c = gravacoes.config()
    c["exe"] = a.exe
    s = gravacoes.ler_sessao(a.sessao)
    d = gravacoes.pasta(a.sessao)
    fimrod = gravacoes.rodape(os.path.join(d, "entrada.txt"))
    if not fimrod:
        raise SystemExit("a sessao nao tem rodape (gravacao incompleta)")
    tmp = tempfile.mkdtemp(prefix="bs_n7_legendas_")
    try:
        # 1) as faixas das falas: a reproducao anterior (mesma entrada, mesmo relogio) ja as tem no log; se nao houver,
        #    uma passada sem capturas
        log_ant = os.path.join(d, "reproducao", "jogo.txt")
        texto = open(log_ant, encoding="utf-8", errors="replace").read() if os.path.exists(log_ant) else ""
        faixas = faixas_do_log(texto)
        if not faixas:
            raise SystemExit("o log da reproducao anterior nao tem falas ([legenda] VBlank ...): reproduza a sessao antes")
        rangos = ",".join("%d-%d" % (max(0, i - a.margem), (f if f else i) + a.margem) for i, f, _ in faixas)
        # 2) a reproducao com capturas so nessas faixas
        saves = os.path.join(tmp, "saves")
        shutil.copytree(os.path.join(d, "saves_inicio"), saves)
        cap = os.path.join(tmp, "cap")
        os.makedirs(cap)
        env = {"BS_INPUT_SCRIPT": os.path.join(d, "entrada.txt"), "BS_CAPTURA_VBLANKS": rangos, "BS_CAPTURA_CRUA": "1"}
        args = ["--saves-pasta", saves] + s["args"].split() + ["--render", a.render, "--headless", str(fimrod[1]),
                                                                  "--dump-every", str(a.cada), "--capture", cap]
        rc = gravacoes.jogo(c, args, env, os.path.join(tmp, "jogo"))
        texto = open(os.path.join(tmp, "jogo.txt"), encoding="utf-8", errors="replace").read()
        faixas = faixas_do_log(texto)  # as desta execucao (a sincronizacao e a do N6, aqui so conferida)
        # 3) cada captura contra as faixas
        ativas = lambda v: any(i <= v < (f if f is not None else 1 << 62) for i, f, _ in faixas)
        ok = True
        n_com = n_sem = 0
        problemas = []
        for nome in sorted(os.listdir(cap)):
            m = re.match(r"v(\d+)\.bmp$", nome)
            if not m:
                continue
            v = int(m.group(1))
            crua = os.path.join(cap, "v%06d_crua.bmp" % v)
            if ativas(v):
                n_com += 1
                if not os.path.exists(crua):
                    problemas.append("VBlank %d: fala ativa, mas o quadro nao tem legenda" % v)
                    continue
                w, h, comp = ler_bmp(os.path.join(cap, nome))
                _, _, cru = ler_bmp(crua)
                dif = [(x, y) for y in range(h) for x in range(w) if comp[y][x] != cru[y][x]]
                brancos = sum(1 for (x, y) in dif if min(comp[y][x]) >= 0xE0)
                if not dif:
                    problemas.append("VBlank %d: fala ativa, legenda invisivel (imagem igual ao quadro)" % v)
                elif min(y for _, y in dif) < h // 2:
                    problemas.append("VBlank %d: legenda fora da faixa de baixo (linha %d)" % (v, min(y for _, y in dif)))
                elif brancos < 50:
                    problemas.append("VBlank %d: texto da legenda quase sem pixels (%d)" % (v, brancos))
            else:
                n_sem += 1
                if os.path.exists(crua):
                    problemas.append("VBlank %d: legenda na imagem sem fala ativa (persistente?)" % v)
        ok = rc == 0 and not problemas and n_com > 0 and n_sem > 0
        res = {"sessao": a.sessao, "render": a.render, "rc": rc, "falas": [[i, f, voz] for i, f, voz in faixas],
               "quadros_com_fala": n_com, "quadros_sem_fala": n_sem, "problemas": problemas[:50],
               "veredito": "APROVADO" if ok else "REPROVADO"}
        print(json.dumps(res, indent=2, ensure_ascii=False))
        if a.manter:
            print("capturas em", cap)
        return 0 if ok else 1
    finally:
        if not a.manter:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
