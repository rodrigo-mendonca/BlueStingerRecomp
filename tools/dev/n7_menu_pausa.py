"""N7c: o menu interno pausa o jogo e o retoma ao fechar, por qualquer caminho e depois de qualquer mudanca.

Defeito de 2026-10-23: fechar o menu com B/Esc na pagina principal ou com "Voltar ao jogo" depois de visitar uma pagina
(o caminho de quem muda uma configuracao) fechava o modelo sem retomar o jogo; so o botao View/Back retomava. Cada caso
roda o jogo na janela com o controle virtual (BS_CONTROLE_VIRTUAL), muda algo de video, audio, controles ou legendas
(isolado e em sequencia), fecha por um dos caminhos e confere no log:
  - cada "[menu] aberto" tem o seu "[menu] fechado";
  - depois de fechar o jogo anda: o VBlank da abertura seguinte (ou o total no fim) passa o do fechamento por pelo
    menos 2 s de campos;
  - a mudanca valeu (a linha do log do servico).

A interface comeca em ingles (sem legendas.toml e sem mods na copia: idioma padrao en-US), entao a navegacao e por
posicao nas paginas em ingles. O executavel e copiado para uma pasta temporaria (as preferencias ficam ao lado dele):
nada do usuario e tocado.

    python tools/dev/n7_menu_pausa.py --exe <bluestinger.exe> [--data <data>] [--casos nada,audio,...]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# paginas (ingles): principal Graphics 0, Audio 1, Subtitles 2, Controls 3, Restore 4, Return 5
ABRIR = ["back"]
FECHAR_B = ["b"]           # B na pagina principal
VOLTAR_AO_JOGO = ["dpup", "a"]  # da primeira linha, Up vai para a ultima: "Return to game"


def pagina(i):
    return ["dpdown"] * i + ["a"]


CASOS = {
    # caso: (botoes, linhas que o log tem de ter DEPOIS da primeira abertura do menu). Partida: volume geral 100 % (Esquerda
    # muda, Direita nao), Vulkan, camera Original, legendas desativadas (sem mods na copia): Direita no idioma liga en-US.
    "nada": (ABRIR + ["back"], []),
    "audio": (ABRIR + pagina(1) + ["dpleft"] + ["b"] + FECHAR_B, [r"\[menu\] preferencias gravadas"]),
    "video": (ABRIR + pagina(0) + ["a"] + ["b"] + FECHAR_B, [r"\[render\] renderizador: software"]),
    "controles": (ABRIR + pagina(3) + ["a", "dpright", "b", "b"] + FECHAR_B, [r"\[camera\] modo livre"]),
    "legendas": (ABRIR + pagina(2) + ["dpright"] + ["b"] + VOLTAR_AO_JOGO, [r"\[legenda\] idioma: en-US$"]),
    "sequencia": (ABRIR + pagina(1) + ["dpleft", "b"] + pagina(0) + ["a", "b"] + pagina(3) + ["a", "dpright", "b", "b"] +
                  pagina(2) + ["dpright", "b"] + ["back"],
                  [r"\[menu\] preferencias gravadas", r"\[render\] renderizador: software", r"\[camera\] modo livre",
                   r"\[legenda\] idioma: en-US$"]),
}


def roteiro(botoes):
    """Controle virtual: conecta, espera o titulo, aperta os botoes (100 ms cada, 400 ms entre eles), espera 4 s com o
    jogo andando, abre e fecha o menu de novo (o VBlank dessa abertura prova que o jogo andou) e sai."""
    linhas, t = ["0 conectar"], 6000
    for b in botoes:
        linhas += ["%d %s 1" % (t, b), "%d %s 0" % (t + 100, b)]
        t += 500
    t += 4000
    for b in ("back", "back"):
        linhas += ["%d %s 1" % (t, b), "%d %s 0" % (t + 100, b)]
        t += 1000
    linhas.append("%d sair" % (t + 1500))
    return "\n".join(linhas) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", default=os.path.join(REPO, "dist", "BlueStinger-Preview", "data"))
    ap.add_argument("--casos", default=",".join(CASOS))
    a = ap.parse_args()
    falhas = 0
    base = tempfile.mkdtemp(prefix="n7_menu_pausa_")
    for nome in a.casos.split(","):
        botoes, esperado = CASOS[nome]
        d = os.path.join(base, nome)
        os.makedirs(d)
        exe = os.path.join(d, "bluestinger.exe")
        shutil.copy2(a.exe, exe)
        with open(os.path.join(d, "pad.txt"), "w", encoding="utf-8") as f:
            f.write(roteiro(botoes))
        env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
        env.update({"BS_CONTROLE_VIRTUAL": os.path.join(d, "pad.txt"), "BS_RELOGIO_FIXO": "1700000000"})
        with open(os.path.join(d, "jogo.txt"), "w", encoding="utf-8") as f:
            rc = subprocess.call([exe, "--data", a.data, "--saves-pasta", os.path.join(d, "saves")], stdout=f,
                                 stderr=subprocess.STDOUT, env=env, cwd=d)
        log = open(os.path.join(d, "jogo.txt"), encoding="utf-8", errors="replace").read()
        eventos = [(m.group(1), int(m.group(2))) for m in re.finditer(r"\[menu\] (aberto|fechado)\D+(\d+)", log)]
        total = re.search(r"desempenho: (\d+) VBlanks", log)
        erros = []
        if rc != 0:
            erros.append("rc=%d" % rc)
        if [e for e, _ in eventos] != ["aberto", "fechado"] * 2:
            erros.append("aberturas e fechamentos: %s" % eventos)
        else:
            fechou, reabriu = eventos[1][1], eventos[2][1]
            if reabriu - fechou < 120:  # ~4 s de jogo entre fechar e reabrir: pelo menos 2 s andando
                erros.append("o jogo nao andou depois de fechar (VBlank %d -> %d)" % (fechou, reabriu))
            if not total or int(total.group(1)) - eventos[3][1] < 45:
                erros.append("o jogo nao andou depois do ultimo fechamento")
        # so depois da primeira abertura: as linhas da partida (idioma automatico, renderizador) nao contam
        aberto = log.find("[menu] aberto")
        for r in esperado:
            if aberto < 0 or not re.search(r, log[aberto:], re.M):
                erros.append("sem a linha %r depois de abrir o menu" % r)
        falhas += bool(erros)
        print("[%s] %s: %s%s" % ("ok" if not erros else "FALHA", nome, " ".join("%s %d" % e for e in eventos),
                                ("; " + "; ".join(erros)) if erros else ""))
    print("%d casos, %d falhas (%s)" % (len(a.casos.split(",")), falhas, base))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
