"""Camera Livre: segurar "frente" pelo analogico esquerdo atravessando uma troca de cenario (2026-10-23).

Defeito relatado: na area nova o Eliot andava para o lado ate o analogico ser solto. Causa: o desvio dos cortes (que
mantem o rumo num corte dentro da area) somava o salto da vista da troca de cenario. O teste:
  - roda `passo_heliporto` ate a porta do corredor RCG02 (D-pad, como gravado), gira a vista ~45 graus no corredor pelo
    analogico direito (roteiro da camera), segura o analogico esquerdo para a frente, aperta A na porta segurando e
    continua segurando no exterior RCG03 por ~150 passos;
  - mede, em cada trecho de 8 montagens depois da troca, o angulo entre o deslocamento real do Eliot e a frente da vista
    (o vetor da camera exibida para o Eliot, no plano), e o desvio guardado pelo movimento (BS_CAMERA_MOV_LOG).
Aprovado: a troca de cenario foi vista (cortes por troca >= 1), o desvio fica 0 depois dela e o Eliot anda para a frente
da vista (mediana do angulo <= 25 graus) sem soltar o analogico. Com --camera original nada disso e medido (o modo
Original nao e tocado: --original roda a mesma rota e confere so que nenhuma amostra foi girada).

    python tools/dev/n7_camera_transicao.py --exe <bluestinger.exe> [--data <data>] [--original]
"""
import argparse
import csv
import math
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROTA = os.path.join(REPO, "tools", "dist", "roteiros", "passo_heliporto.txt")


def tsv(p):
    with open(p, encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", default=os.path.join(REPO, "dist", "BlueStinger-Preview", "data"))
    ap.add_argument("--original", action="store_true")
    a = ap.parse_args()
    d = tempfile.mkdtemp(prefix="n7_camera_transicao_")
    base = [l.strip() for l in open(ROTA, encoding="utf-8") if re.match(r"^\d", l) and int(l.split()[0]) <= 7660]
    with open(os.path.join(d, "entrada.txt"), "w", encoding="utf-8") as f:
        f.write("indice passo\n" + "\n".join(base + ["7700 AX=128+AY=0", "7750 A+AX=128+AY=0", "7753 AX=128+AY=0",
                                                      "7900 -"]) + "\n")
    with open(os.path.join(d, "camera.txt"), "w", encoding="utf-8") as f:
        f.write("indice passo\n7680 1 0\n7691 0 0\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
    env.update({"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": os.path.join(d, "entrada.txt"),
                "BS_CAMERA_ROTEIRO": os.path.join(d, "camera.txt"), "BS_CAMERA_MOV_LOG": os.path.join(d, "mov.tsv"),
                "BS_CAMERA_LOG": os.path.join(d, "cam.tsv")})
    cmd = [a.exe, "--data", a.data, "--video", "legado", "--camera", "original" if a.original else "livre",
           "--saves-pasta", os.path.join(d, "saves"), "--headless", "16300"]
    with open(os.path.join(d, "jogo.txt"), "w", encoding="utf-8") as f:
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT, env=env)
    log = open(os.path.join(d, "jogo.txt"), encoding="utf-8", errors="replace").read()
    if a.original:
        girada = re.search(r"amostras giradas (\d+)", log)
        ok = rc == 0 and (girada is None or girada.group(1) == "0") and not os.path.exists(os.path.join(d, "mov.tsv"))
        print("[%s] Original: rc=%d, nenhuma amostra girada (%s)" % ("ok" if ok else "FALHA", rc, d))
        return 0 if ok else 1
    trocas = re.search(r"trocas de cenario (\d+)", log)
    cam, mov = tsv(os.path.join(d, "cam.tsv")), tsv(os.path.join(d, "mov.tsv"))
    # a troca de cenario da porta: o maior salto da camera depois do passo do A (VBlank ~15800)
    corte, contou = None, False
    for p, r in zip(cam, cam[1:]):
        if int(r["vblank"]) > 15700 and math.hypot(float(r["x"]) - float(p["x"]), float(r["z"]) - float(p["z"])) > 60:
            corte = int(r["vblank"])
            contou = int(r["salas"]) > int(p["salas"])  # a carga da sala nova contada exatamente nesta montagem
            break
    erros = []
    if corte is not None and not contou:
        erros.append("a carga da sala nao foi contada na montagem do corte da porta")
    if rc != 0:
        erros.append("rc=%d" % rc)
    if not trocas or int(trocas.group(1)) < 1:
        erros.append("nenhuma troca de cenario contada")
    if corte is None:
        erros.append("a troca de cenario da porta nao apareceu no log da camera")
        print("[FALHA] %s (%s)" % ("; ".join(erros), d))
        return 1
    depois = [m for m in mov if int(m["vblank"]) >= corte]
    desvios = sorted({int(m["desvio"]) for m in depois})
    if not depois:
        erros.append("nenhuma amostra girada depois da troca (o analogico nao chegou segurado)")
    elif desvios != [0]:
        erros.append("desvio depois da troca: %s" % desvios[:5])
    # o Eliot andando: deslocamento entre montagens a cada 8, comparado com a frente da vista (camera -> Eliot)
    pos = [r for r in cam if corte <= int(r["vblank"]) <= corte + 300]
    angulos = []
    for p, r in zip(pos[::8], pos[8::8]):
        mx, mz = float(r["px"]) - float(p["px"]), float(r["pz"]) - float(p["pz"])
        if math.hypot(mx, mz) < 5:
            continue  # parado (porta, parede)
        fx, fz = float(p["px"]) - (float(p["x"]) + float(p["dx"])), float(p["pz"]) - (float(p["z"]) + float(p["dz"]))
        a1, a2 = math.atan2(mx, mz), math.atan2(fx, fz)
        angulos.append(abs((math.degrees(a1 - a2) + 180) % 360 - 180))
    med = sorted(angulos)[len(angulos) // 2] if angulos else None
    if med is None:
        erros.append("o Eliot nao andou depois da troca")
    elif med > 25:
        erros.append("o Eliot nao anda para a frente da vista: mediana %.0f graus" % med)
    print("[%s] troca de cenario no VBlank %d; desvio depois %s; angulo deslocamento x frente da vista: mediana %s, trechos %s%s (%s)" % (
        "ok" if not erros else "FALHA", corte, desvios[:3], "%.0f" % med if med is not None else "-",
        [round(x) for x in angulos[:8]], ("; " + "; ".join(erros)) if erros else "", d))
    return 0 if not erros else 1


if __name__ == "__main__":
    sys.exit(main())
