"""N7c: a imagem APRESENTADA na janela (docs/N7-PLANO.md secao 20) com gameplay real, por tamanho e modo de tela.

Reproduz uma gravacao (tools/dev/gravacoes.py) na janela (modo espectador: o roteiro controla) com a resolucao
interna, o modo de tela e a apresentacao pedidos, e captura a imagem apresentada (BS_JANELA_CAPTURA: jogo, legendas e
menu como o jogador ve) durante as falas. Confere em cada captura:
  - o tamanho e o da saida (janela, ou a tela inteira sem bordas);
  - a area do jogo e o maior 4:3 centrado: as faixas fora dela sao pretas e a imagem dentro dela nao e;
  - nas capturas de uma fala, a legenda aparece na faixa de baixo da area (texto claro com contorno escuro); fora das
    falas, nao;
  - no log, a apresentacao usada e quantos quadros vieram da GPU na resolucao interna.

  python tools/dev/n7_janela.py --exe <bluestinger.exe> [--sessao sessao-20261021-145006] [--resolucao 1080]
                                [--janela 1280x960 | --sem-bordas] [--apresentacao vulkan|sdl] [--render nativo|software]

O graficos.toml da pasta do executavel e trocado durante o teste e restaurado no fim. Saida 1 se algum criterio falhar.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gravacoes  # noqa: E402
import n7_legendas  # noqa: E402  (ler_bmp, faixas_do_log)

GRAFICOS = """preset = "personalizado"
[imagem]
resolucao = "{res}"
escala = 100
[tela]
modo = "{modo}"
largura = {w}
altura = {h}
vsync = true
renderizador = "{render}"
[efeitos]
{efeitos}
"""


def area43(w, h):
    aw = min(w, h * 4 / 3)
    ah = aw * 3 / 4
    return int((w - aw) // 2), int((h - ah) // 2), int(aw), int(ah)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--sessao", default="sessao-20261021-145006")
    ap.add_argument("--resolucao", default="1080", choices=["original", "720", "1080", "1440", "2160"])
    ap.add_argument("--janela", default="1280x960")
    ap.add_argument("--sem-bordas", action="store_true")
    ap.add_argument("--apresentacao", default="vulkan", choices=["vulkan", "sdl"])
    ap.add_argument("--render", default="nativo", choices=["nativo", "software"])
    ap.add_argument("--cada", type=int, default=45)
    ap.add_argument("--manter", action="store_true")
    ap.add_argument("--efeitos", default="", help='chaves de [efeitos] separadas por virgula: "tonemap=true,exposicao=1.0"')
    a = ap.parse_args()

    exe = os.path.abspath(a.exe)
    sess = gravacoes.pasta(a.sessao)
    s = gravacoes.ler_sessao(a.sessao)
    log_ant = open(os.path.join(sess, "reproducao", "jogo.txt"), encoding="utf-8", errors="replace").read()
    faixas = n7_legendas.faixas_do_log(log_ant)
    if not faixas:
        raise SystemExit("a reproducao anterior da sessao nao tem falas no log")
    ini, fim = faixas[0][0] - 60, (faixas[min(3, len(faixas) - 1)][1] or faixas[0][0] + 600) + 60  # as primeiras falas

    tmp = tempfile.mkdtemp(prefix="n7_janela_")
    cap = os.path.join(tmp, "cap")
    shutil.copytree(os.path.join(sess, "saves_inicio"), os.path.join(tmp, "saves"))
    gfile = os.path.join(os.path.dirname(exe), "graficos.toml")
    backup = gfile + ".antes_n7_janela"
    if os.path.exists(gfile):
        shutil.copy2(gfile, backup)
    w, h = (int(x) for x in a.janela.split("x"))
    res = {"original": "original", "720": "720p", "1080": "1080p", "1440": "1440p", "2160": "2160p"}[a.resolucao]
    with open(gfile, "w", encoding="utf-8") as f:
        f.write(GRAFICOS.format(res=res, modo="sem_bordas" if a.sem_bordas else "janela", w=0 if a.sem_bordas else w,
                                h=0 if a.sem_bordas else h, render="vulkan" if a.render == "nativo" else "software",
                                efeitos="\n".join(x.strip() for x in a.efeitos.split(",") if x.strip())))
    env = dict(os.environ)
    for k in ("BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_ASSISTIDO", "BS_RENDER", "BS_APRESENTACAO"):
        env.pop(k, None)
    env.update({"BS_RELOGIO_FIXO": gravacoes.RELOGIO, "BS_INPUT_SCRIPT": os.path.join(sess, "entrada.txt"),
                "BS_JANELA_CAPTURA": cap, "BS_JANELA_CAPTURA_CADA": str(a.cada),
                "BS_JANELA_CAPTURA_VBLANKS": "%d-%d" % (ini, fim)})
    cmd = [exe, "--data", s.get("data", ""), "--saves-pasta", os.path.join(tmp, "saves"), "--apresentacao", a.apresentacao]
    cmd += s["args"].split()
    if a.resolucao != "original":
        cmd += ["--resolucao-interna", a.resolucao]
    log = os.path.join(tmp, "jogo.txt")
    falhas = []
    try:
        with open(log, "w", encoding="utf-8") as out:
            p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=os.path.dirname(exe))
            # tempo real: ~60 VBlanks por segundo; espera passar do fim da faixa
            limite = time.time() + fim / 50 + 60
            while p.poll() is None and time.time() < limite:
                caps = os.listdir(cap) if os.path.isdir(cap) else []
                if any(int(re.match(r"janela_v(\d+)_", c).group(1)) >= fim - a.cada for c in caps):
                    break
                time.sleep(2)
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(20)
                except subprocess.TimeoutExpired:
                    p.kill()
        texto = open(log, encoding="utf-8", errors="replace").read()
        caps = sorted(os.listdir(cap)) if os.path.isdir(cap) else []
        if not caps:
            falhas.append("nenhuma captura")
        apres = re.findall(r"\[apresentacao\].*", texto)
        print("\n".join(apres[:3]) if apres else "(sem linha da apresentacao)")
        faixas_agora = n7_legendas.faixas_do_log(texto) or faixas
        com = sem = 0
        for c in caps:
            m = re.match(r"janela_v(\d+)_(\d+)x(\d+)\.bmp", c)
            v, cw, ch = int(m.group(1)), int(m.group(2)), int(m.group(3))
            iw, ih, px = n7_legendas.ler_bmp(os.path.join(cap, c))
            if (iw, ih) != (cw, ch):
                falhas.append("%s: imagem %dx%d" % (c, iw, ih))
                continue
            if not a.sem_bordas and (cw, ch) != (w, h):
                falhas.append("%s: saida %dx%d em vez de %dx%d" % (c, cw, ch, w, h))
            x0, y0, aw, ah = area43(cw, ch)

            def lum(x, y):
                r, g, b = px[y][x]
                return (r + g + b) / 3

            # faixas pretas fora da area (amostras)
            fora = [(x, y) for x in range(0, x0, max(1, x0 // 8) or 1) for y in range(0, ch, ch // 16)] if x0 > 2 else []
            fora += [(x, y) for y in range(0, y0, max(1, y0 // 8) or 1) for x in range(0, cw, cw // 16)] if y0 > 2 else []
            if any(lum(x, y) > 8 for x, y in fora):
                falhas.append("%s: faixa fora da area 4:3 nao e preta" % c)
            # a imagem do jogo dentro da area
            dentro = [lum(x0 + aw * i // 20, y0 + ah * j // 20) for i in range(1, 20) for j in range(1, 20)]
            media = sum(dentro) / len(dentro)
            if max(dentro) < 10:
                falhas.append("%s: area do jogo preta" % c)
            # legenda: na faixa de baixo da area, texto claro (>= 235) colado a contorno escuro (<= 40)
            fala = any(i <= v < (f if f else i + 600) for i, f, _ in faixas_agora)
            claros = 0
            for y in range(y0 + ah * 78 // 100, y0 + ah * 97 // 100, 2):
                for x in range(x0 + aw // 10, x0 + aw * 9 // 10, 2):
                    if lum(x, y) >= 235 and min(lum(x + dx, y) for dx in (-3, 3)) <= 40:
                        claros += 1
            tem = claros >= 25
            print("%s: %s, luma media %.1f, texto na faixa da legenda: %d amostras%s" % (c, "fala" if fala else "sem fala", media, claros,
                                                                        "" if tem == fala else "  <-- DIVERGE"))
            if fala and not tem:
                falhas.append("%s: fala sem legenda na imagem" % c)
            com += fala
            sem += not fala
        print("%d capturas (%d com fala, %d sem)" % (len(caps), com, sem))
        if not com:
            falhas.append("nenhuma captura durante uma fala")
    finally:
        if os.path.exists(backup):
            shutil.move(backup, gfile)
        elif os.path.exists(gfile):
            os.remove(gfile)
        if a.manter:
            print("capturas em", cap)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    print("FALHOU: " + "; ".join(falhas[:8]) if falhas else "APROVADO")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
