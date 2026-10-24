"""Gravacao e reproducao de sessoes de gameplay (so desenvolvimento).

O jogador joga na janela com teclado ou controle; o jogo (BS_GRAVAR, app/main_sdl.cpp) grava as entradas por
passo de jogo como um roteiro "indice passo", o mesmo formato do BS_INPUT_SCRIPT dos testes. A reproducao roda
o roteiro headless e confere, passo a passo, o estado logico gravado junto (BS_STATE_PASSO: hash da regiao de
variaveis do jogo, vida e posicao do Eliot) e os saves que a sessao produziu. Nada muda a logica do jogo.

  python tools/dev/gravacoes.py                       interface (Tk)
  python tools/dev/gravacoes.py listar
  python tools/dev/gravacoes.py gravar <nome> [--save <BLUESTIN.000>] [--saves-de <sessao>] [--load]
                                       [--de <sessao|roteiro.txt> --ate <passo|cpN>] [--extra "<args do jogo>"]
  python tools/dev/gravacoes.py reproduzir <nome> [--janela]
  python tools/dev/gravacoes.py exportar <nome> <arquivo.vms> [--slot BLUESTIN.000]

Origem de uma sessao: NEW GAME (cartao vazio), um save (arquivo de --saves-importar), os saves que outra sessao
produziu (--saves-de) ou outra sessao reproduzida ate um passo ou checkpoint (--de/--ate: o jogo reproduz o
roteiro dela ate ali e o jogador assume; a gravacao nova contem o trecho reproduzido; --de tambem aceita um roteiro
"indice passo", e --load = --de passo_load_slot1.txt --ate 2131: com --save, o LOAD do slot 1 e automatico e o
jogador assume no ponto do save). F5 na janela marca um
checkpoint. Sessoes ficam em %LOCALAPPDATA%\\BlueStingerRecomp\\gravacoes\\<nome>:
  sessao.json       origem, executavel, dados, argumentos, relogio
  saves_inicio\\    o cartao no inicio (copiado, nunca alterado)
  saves\\           o cartao da gravacao (os saves que o jogador fez)
  entrada.txt       o roteiro gravado;  estado.tsv: o estado logico por passo;  checkpoint_<n>.bmp
  reproducao\\      a ultima reproducao (estado.tsv, saves\\, saida)
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RAIZ = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "BlueStingerRecomp", "gravacoes")
RELOGIO = "1700000000"  # BS_RELOGIO_FIXO de todos os roteiros
ARGS_PADRAO = "--audio nativo --tempo experimental"  # os tres provedores nativos (efeitos tambem: os sons do menu); o
# tempo explicito: a janela usa o experimental por padrao (2026-10-24) e a reproducao headless usaria o estavel


def com_tempo(args):
    """Os argumentos de uma sessao com o modo de tempo explicito. Sessao sem --tempo e anterior a 2026-10-24, gravada
    no tempo estavel (o padrao de entao): a reproducao usa --tempo nativo, como na gravacao."""
    return args if "--tempo" in args.split() else (args + " --tempo nativo").strip()
# LOAD do slot 1 pelo titulo (N4): o jogo volta ao ponto do save no passo ~2131 e o jogador assume ali
LOAD_SLOT1 = os.path.join(REPO, "tools", "dist", "roteiros", "passo_load_slot1.txt")
LOAD_SLOT1_ATE = 2131


def primeiro(*caminhos):
    for c in caminhos:
        if c and os.path.exists(c):
            return os.path.abspath(c)
    return os.path.abspath(caminhos[-1])


def config():
    p = os.path.join(RAIZ, "config.json")
    c = {}
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            c = json.load(f)
    c.setdefault("exe", primeiro(os.environ.get("BS_EXE"), os.path.join(REPO, "build", "msvc-release", "app", "bluestinger.exe"),
                                 os.path.join(REPO, "build", "n6-game", "app", "bluestinger.exe")))
    c.setdefault("data", primeiro(os.environ.get("BS_DATA"), os.path.join(REPO, "dist", "BlueStinger-Preview", "data"),
                                  os.path.join(REPO, "..", "recomp-BStinger", "dist", "BlueStinger-Preview", "data")))
    return c


def salvar_config(c):
    os.makedirs(RAIZ, exist_ok=True)
    with open(os.path.join(RAIZ, "config.json"), "w", encoding="utf-8") as f:
        json.dump(c, f, indent=2)


def pasta(nome):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", nome or ""):
        raise SystemExit("nome de sessao invalido (letras, digitos, _ . -): %r" % nome)
    return os.path.join(RAIZ, nome)


def ler_sessao(nome):
    with open(os.path.join(pasta(nome), "sessao.json"), encoding="utf-8") as f:
        return json.load(f)


def conteudo_save(b):
    """Bytes que identificam um save: no envelope .bsv v1 (native/platform/saves.h: "BSSV", versao, tamanho do
    cabecalho, tamanho e CRC-32 da imagem, data i64 nos bytes 16-23, reservados, imagem), tudo menos a DATA, que e a
    hora de gravacao pelo relogio do host (gravacao e reproducao acontecem em horas diferentes). Outros arquivos: inteiros."""
    # so um envelope v1 estruturalmente valido (versao 1, cabecalho de 32, tamanho e CRC-32 da imagem conferem); qualquer
    # outro arquivo (corrompido, versao futura) e comparado inteiro (revisao Codex 2026-10-21)
    if len(b) >= 32 and b[:4] == b"BSSV":
        ver, cab, tam, crc = struct.unpack_from("<HHII", b, 4)
        # e os limites do decodificador do jogo (saves_format.cpp): imagem nao vazia, multipla de 512, ate 200 blocos
        if ver == 1 and cab == 32 and len(b) == cab + tam and 0 < tam <= 200 * 512 and tam % 512 == 0 and zlib.crc32(b[cab:]) == crc:
            return b[:16] + bytes(8) + b[24:]
    return b


def hashes(d):
    """SHA-256 do conteudo de cada arquivo da pasta (caminho relativo -> hash), sem a data dos envelopes .bsv."""
    out = {}
    for raiz, _, arqs in os.walk(d):
        for a in arqs:
            p = os.path.join(raiz, a)
            with open(p, "rb") as f:
                out[os.path.relpath(p, d).replace("\\", "/")] = hashlib.sha256(conteudo_save(f.read())).hexdigest()
    return out


def roteiro_info(path):
    """(ultimo passo, [(n, passo) dos checkpoints]) de um entrada.txt."""
    fim, cps, ultimo = None, [], 0
    for l in open(path, encoding="utf-8"):
        m = re.match(r"# checkpoint (\d+) passo (\d+)", l)
        if m:
            cps.append((int(m.group(1)), int(m.group(2))))
        m = re.match(r"# fim passo (\d+)", l)
        if m:
            fim = int(m.group(1))
        m = re.match(r"(\d+) ", l)
        if m:
            ultimo = int(m.group(1))
    return (fim if fim is not None else ultimo), cps


def rodape(path):
    """(passo, VBlank) do rodape "# fim passo P vblank V" que o jogo escreve ao fechar normalmente; None sem ele (sessao
    interrompida: queda, processo morto)."""
    out = None
    for l in open(path, encoding="utf-8"):
        m = re.match(r"# fim passo (\d+) vblank (\d+)", l)
        if m:
            out = (int(m.group(1)), int(m.group(2)))
    return out


def jogo(c, args, env_extra, saida, espera=True):
    env = dict(os.environ)
    for k in ("BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_GRAVAR_BASE", "BS_GRAVAR_ATE", "BS_ASSISTIDO"):
        env.pop(k, None)
    env["BS_RELOGIO_FIXO"] = RELOGIO
    env.update(env_extra)
    cmd = [c["exe"], "--data", c["data"]] + args
    with open(saida + ".txt", "w", encoding="utf-8") as out:
        out.write("# " + subprocess.list2cmdline(cmd) + "\n# env " + json.dumps(env_extra) + "\n")
        out.flush()
        p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=os.path.dirname(c["exe"]))
        return p.wait() if espera else p


def importar_save(c, vms, saves, slot="BLUESTIN.000"):
    os.makedirs(saves, exist_ok=True)
    rc = jogo(c, ["--saves-pasta", saves, "--saves-importar", vms, "--saves-nome", slot], {}, os.path.join(saves, "..", "importar"))
    if rc != 0:
        raise SystemExit("importar o save falhou (rc=%d); ver importar.txt" % rc)


def gravar(nome, save=None, saves_de=None, de=None, ate=None, extra=None, log=print):
    c = config()
    d = pasta(nome)
    if os.path.exists(d):
        raise SystemExit("a sessao %s ja existe" % nome)
    os.makedirs(d)
    ini = os.path.join(d, "saves_inicio")
    env = {"BS_GRAVAR": os.path.join(d, "entrada.txt"), "BS_STATE_PASSO": os.path.join(d, "estado.tsv"), "BS_STATE_PASSO_CADA": "1"}
    origem = {"tipo": "newgame"}
    args_jogo = extra if extra is not None else ARGS_PADRAO
    if de and os.path.isfile(de):  # um roteiro "indice passo" qualquer (p.ex. o LOAD do slot 1), com o save de --save
        fim, _ = roteiro_info(de)
        passo = int(ate) if ate is not None else fim
        if save:
            importar_save(c, os.path.abspath(save), ini)
        else:
            os.makedirs(ini)
        shutil.copy(de, os.path.join(d, "base.txt"))
        env.update({"BS_GRAVAR_BASE": os.path.join(d, "base.txt"), "BS_GRAVAR_ATE": str(passo)})
        origem = {"tipo": "roteiro", "roteiro": os.path.abspath(de), "ate_passo": passo}
        if save:
            with open(save, "rb") as f:
                origem.update({"save": os.path.abspath(save), "sha256": hashlib.sha256(f.read()).hexdigest()})
    elif de:
        base = ler_sessao(de)
        fim, cps = roteiro_info(os.path.join(pasta(de), "entrada.txt"))
        m = re.fullmatch(r"cp(\d+)", str(ate or ""))
        passo = dict(cps)[int(m.group(1))] if m else int(ate) if ate is not None else fim
        shutil.copytree(os.path.join(pasta(de), "saves_inicio"), ini)
        shutil.copy(os.path.join(pasta(de), "entrada.txt"), os.path.join(d, "base.txt"))
        env.update({"BS_GRAVAR_BASE": os.path.join(d, "base.txt"), "BS_GRAVAR_ATE": str(passo)})
        origem = {"tipo": "sessao", "sessao": de, "ate_passo": passo, "origem_dela": base["origem"]}
        if extra is None:
            args_jogo = base["args"]
    elif saves_de:
        shutil.copytree(os.path.join(pasta(saves_de), "saves"), ini)
        origem = {"tipo": "saves_de", "sessao": saves_de}
    elif save:
        importar_save(c, os.path.abspath(save), ini)
        with open(save, "rb") as f:
            origem = {"tipo": "save", "arquivo": os.path.abspath(save), "sha256": hashlib.sha256(f.read()).hexdigest()}
    else:
        os.makedirs(ini)
    args_jogo = com_tempo(args_jogo)
    origem["cartao"] = hashes(ini)
    shutil.copytree(ini, os.path.join(d, "saves"))
    s = {"criada": time.strftime("%Y-%m-%d %H:%M:%S"), "exe": c["exe"], "data": c["data"], "args": args_jogo,
         "relogio": RELOGIO, "origem": origem}
    with open(os.path.join(d, "sessao.json"), "w", encoding="utf-8") as f:
        json.dump(s, f, indent=2)
    log("gravando %s (feche a janela para terminar; F5 = checkpoint)" % nome)
    rc = jogo(c, ["--saves-pasta", os.path.join(d, "saves")] + args_jogo.split(), env, os.path.join(d, "jogo"))
    fim, cps = roteiro_info(os.path.join(d, "entrada.txt"))
    s.update({"rc": rc, "fim_passo": fim, "checkpoints": cps, "saves_fim": hashes(os.path.join(d, "saves"))})
    with open(os.path.join(d, "sessao.json"), "w", encoding="utf-8") as f:
        json.dump(s, f, indent=2)
    log("gravada: rc=%d, ate o passo %d, %d checkpoint(s)" % (rc, fim, len(cps)))
    return rc


def ler_estado(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        next(f, None)
        for l in f:
            p = l.rstrip("\n").split("\t")
            if len(p) >= 7:
                out[int(p[0])] = p
    return out


def reproduzir(nome, janela=False, log=print):
    """Reproduz a sessao e confere o estado de todos os passos e os saves no mesmo VBlank em que a gravacao parou.
    Vereditos: DETERMINISTICA (gravacao completa e tudo igual), DIVERGENTE, INCOMPLETA (a gravacao nao terminou
    normalmente: sem rodape, rc != 0 ou passos faltando no estado; o trecho existente ainda e conferido)."""
    s = ler_sessao(nome)
    c = config()
    d = pasta(nome)
    # na janela (assistir) a pasta e outra: ela nao substitui o veredito da reproducao headless, que vale para as analises
    r = os.path.join(d, "reproducao_janela" if janela else "reproducao")
    shutil.rmtree(r, ignore_errors=True)
    os.makedirs(r)
    shutil.copytree(os.path.join(d, "saves_inicio"), os.path.join(r, "saves"))
    gravado = ler_estado(os.path.join(d, "estado.tsv")) if os.path.exists(os.path.join(d, "estado.tsv")) else {}
    fimrod = rodape(os.path.join(d, "entrada.txt"))
    problemas = []
    if fimrod is None:
        problemas.append("sem rodape (a gravacao nao fechou normalmente)")
    if s.get("rc", 0) != 0:
        problemas.append("gravacao terminou com rc=%s" % s.get("rc"))
    fim = fimrod[0] if fimrod else (max(gravado) if gravado else 0)
    passos = sorted(k for k in gravado if k <= fim)
    if not passos or passos[-1] != fim or passos != list(range(passos[0], fim + 1)):
        problemas.append("estado gravado sem continuidade ate o passo %d" % fim)
    env = {"BS_INPUT_SCRIPT": os.path.join(d, "entrada.txt"), "BS_STATE_PASSO": os.path.join(r, "estado.tsv"),
           "BS_STATE_PASSO_CADA": "1"}
    args = ["--saves-pasta", os.path.join(r, "saves")] + com_tempo(s["args"]).split()
    if not janela:
        # o mesmo VBlank em que a gravacao parou (o jogo para depois do gancho desse VBlank nos dois modos); sem rodape, o
        # VBlank do ultimo passo gravado
        vb = fimrod[1] if fimrod else (int(gravado[fim][1]) + 1 if fim in gravado else 0)  # o log do passo sai depois do VBlank dele
        args += ["--headless", str(vb)]
    log("reproduzindo %s ate o passo %d%s" % (nome, fim, " na janela" if janela else " (headless)"))
    t0 = time.time()
    rc = jogo(c, args, env, os.path.join(r, "jogo"))
    rep = ler_estado(os.path.join(r, "estado.tsv")) if os.path.exists(os.path.join(r, "estado.tsv")) else {}
    if janela:
        # assistir: a janela para quando o usuario a fecha; confere so os passos ate onde chegou, sem os saves
        ate = max(rep) if rep else 0
        passos = [k for k in passos if k <= ate]
        problemas.append("na janela: parou no passo %d de %d (janela fechada); saves conferidos so no headless" % (ate, fim))
        if not passos:
            problemas.append("na janela: nenhum passo para comparar")
    diverg = next((k for k in passos if k not in rep or rep[k][2:] != gravado[k][2:]), None)
    vbdif = sum(1 for k in passos if k in rep and rep[k][1] != gravado[k][1])
    alem = sorted(k for k in rep if k > fim) if fimrod else []  # sem rodape o ponto de parada e aproximado
    saves_ok = True if janela else hashes(os.path.join(r, "saves")) == hashes(os.path.join(d, "saves"))
    veredito = "DIVERGENTE" if diverg is not None or not saves_ok or rc != 0 or vbdif or alem else \
        ("JANELA_IGUAL" if passos else "JANELA_SEM_COBERTURA") if janela else "INCOMPLETA" if problemas else "DETERMINISTICA"
    res = {"quando": time.strftime("%Y-%m-%d %H:%M:%S"), "veredito": veredito, "rc": rc, "segundos": round(time.time() - t0),
           "passos": len(passos), "primeira_divergencia": diverg, "vblank_diferente": vbdif, "passos_alem_do_fim": len(alem),
           "saves_iguais": saves_ok, "problemas": problemas}
    if diverg is not None:
        res["gravado"], res["reproduzido"] = gravado[diverg], rep.get(diverg)
    with open(os.path.join(r, "resultado.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, indent=2)
    log("%s: %d passos, estado %s, saves %s, VBlanks diferentes em %d passos, passos alem do fim %d, rc=%d, %d s" % (
        veredito, len(passos), "igual" if diverg is None else "diverge no passo %d" % diverg,
        "iguais" if saves_ok else "DIFERENTES", vbdif, len(alem), rc, res["segundos"]))
    for pr in problemas:
        log("  " + pr)
    if diverg is not None:
        log("  gravado     %s\n  reproduzido %s" % ("\t".join(res["gravado"]), "\t".join(res["reproduzido"]) if res["reproduzido"] else "(ausente)"))
    return 0 if veredito == "DETERMINISTICA" else 1


def exportar(nome, vms, slot="BLUESTIN.000", log=print):
    c = config()
    saves = os.path.join(pasta(nome), "saves")
    rc = jogo(c, ["--saves-pasta", saves, "--saves-exportar", os.path.abspath(vms), "--saves-nome", slot], {},
              os.path.join(pasta(nome), "exportar"))
    log("exportar %s %s -> %s: rc=%d" % (nome, slot, vms, rc))
    return rc


def sessoes():
    if not os.path.isdir(RAIZ):
        return []
    out = []
    for n in sorted(os.listdir(RAIZ)):
        if os.path.exists(os.path.join(RAIZ, n, "sessao.json")):
            s = ler_sessao(n)
            r = os.path.join(RAIZ, n, "reproducao", "resultado.json")
            if os.path.exists(r):
                with open(r, encoding="utf-8") as f:
                    v = json.load(f)
                s["verificacao"] = {"DETERMINISTICA": "ok"}.get(v.get("veredito"), v.get("veredito", "FALHOU"))
            out.append((n, s))
    return out


def descricao(n, s):
    o = s["origem"]
    org = {"newgame": "NEW GAME", "save": "save " + os.path.basename(o.get("arquivo", "")),
           "saves_de": "saves de " + o.get("sessao", ""), "sessao": "%s ate %s" % (o.get("sessao"), o.get("ate_passo")),
           "roteiro": "%s ate %s" % (os.path.splitext(os.path.basename(o.get("roteiro", "")))[0], o.get("ate_passo"))}[o["tipo"]]
    cps = s.get("checkpoints", [])
    return "%-22s %-28s passos %-7s cps %-3d %s" % (n, org, s.get("fim_passo", "?"), len(cps), s.get("verificacao", "-"))


def gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox

    c = config()
    w = tk.Tk()
    w.title("Blue Stinger - gravacoes de gameplay")
    lista = tk.Listbox(w, width=100, height=12, font=("Consolas", 10), exportselection=False)
    lista.grid(row=0, column=0, columnspan=4, padx=8, pady=8, sticky="nsew")
    nomes = []

    def atualizar():
        lista.delete(0, "end")
        nomes.clear()
        for n, s in sessoes():
            nomes.append(n)
            lista.insert("end", descricao(n, s))

    tk.Label(w, text="Nome da nova sessao").grid(row=1, column=0, sticky="e")
    nome = tk.Entry(w, width=30)
    nome.grid(row=1, column=1, sticky="w")
    origem = tk.StringVar(value="newgame")
    fr = tk.Frame(w)
    fr.grid(row=2, column=0, columnspan=4, sticky="w", padx=8)
    for v, t in (("newgame", "NEW GAME"), ("save", "save (LOAD automatico)..."), ("saves_de", "saves da sessao selecionada"),
                 ("sessao", "continuar a sessao selecionada no checkpoint/passo:")):
        tk.Radiobutton(fr, text=t, variable=origem, value=v).pack(side="left")
    ate = tk.Entry(fr, width=10)
    ate.pack(side="left")
    tk.Label(w, text="Argumentos do jogo").grid(row=3, column=0, sticky="e")
    extra = tk.Entry(w, width=60)
    extra.insert(0, ARGS_PADRAO)
    extra.grid(row=3, column=1, columnspan=3, sticky="w")
    saida = tk.Text(w, width=100, height=10, font=("Consolas", 9))
    saida.grid(row=5, column=0, columnspan=4, padx=8, pady=8)

    def log(t):
        w.after(0, lambda: (saida.insert("end", t + "\n"), saida.see("end")))

    def em_fundo(fn):
        def run():
            try:
                fn()
            except SystemExit as e:
                log("erro: %s" % e)
            except Exception as e:  # noqa: BLE001 (mostra qualquer falha na janela)
                log("erro: %r" % e)
            w.after(0, atualizar)
        threading.Thread(target=run, daemon=True).start()

    def sel():
        i = lista.curselection()
        if not i:
            messagebox.showinfo("Sessao", "Selecione uma sessao na lista.")
            return None
        return nomes[i[0]]

    def b_gravar():
        n = nome.get().strip() or time.strftime("sessao-%Y%m%d-%H%M%S")  # vazio: nome automatico
        nome.delete(0, "end"), nome.insert(0, n)
        o = origem.get()
        kw = {"extra": extra.get().strip()}
        if o == "save":
            f = filedialog.askopenfilename(title="Save (arquivo de --saves-importar)")
            if not f:
                return
            kw["save"], kw["de"], kw["ate"] = f, LOAD_SLOT1, LOAD_SLOT1_ATE
        elif o in ("saves_de", "sessao"):
            s = sel()
            if not s:
                return
            if o == "saves_de":
                kw["saves_de"] = s
            else:
                kw["de"], kw["ate"] = s, ate.get().strip() or None
                kw["extra"] = None  # os argumentos da sessao de origem
        em_fundo(lambda: gravar(n, log=log, **kw))

    def b_reproduzir(janela):
        s = sel()
        if s:
            em_fundo(lambda: reproduzir(s, janela=janela, log=log))

    def b_exportar():
        s = sel()
        if not s:
            return
        f = filedialog.asksaveasfilename(title="Exportar o slot 1 como VMS", defaultextension=".vms")
        if f:
            em_fundo(lambda: exportar(s, f, log=log))

    def b_pasta():
        s = sel()
        os.startfile(pasta(s) if s else RAIZ)

    bt = tk.Frame(w)
    bt.grid(row=4, column=0, columnspan=4, pady=4)
    for t, fn in (("Gravar", b_gravar), ("Reproduzir e verificar", lambda: b_reproduzir(False)),
                  ("Assistir na janela", lambda: b_reproduzir(True)), ("Exportar save", b_exportar), ("Abrir pasta", b_pasta),
                  ("Atualizar", atualizar)):
        tk.Button(bt, text=t, command=fn, width=20).pack(side="left", padx=3)
    log("executavel: %s\ndados: %s\nsessoes: %s" % (c["exe"], c["data"], RAIZ))
    log("Na janela do jogo: teclado ou controle; F5 = checkpoint; feche a janela para terminar a gravacao.")
    atualizar()
    w.mainloop()


def main():
    if len(sys.argv) == 1:
        return gui()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("listar")
    sp.add_parser("gui")
    g = sp.add_parser("gravar")
    g.add_argument("nome")
    g.add_argument("--save")
    g.add_argument("--saves-de")
    g.add_argument("--de")
    g.add_argument("--ate")
    g.add_argument("--extra")
    g.add_argument("--load", action="store_true")
    r = sp.add_parser("reproduzir")
    r.add_argument("nome")
    r.add_argument("--janela", action="store_true")
    e = sp.add_parser("exportar")
    e.add_argument("nome")
    e.add_argument("vms")
    e.add_argument("--slot", default="BLUESTIN.000")
    a = ap.parse_args()
    if a.cmd == "listar":
        for n, s in sessoes():
            print(descricao(n, s))
        return 0
    if a.cmd == "gui":
        return gui()
    if a.cmd == "gravar":
        if a.load:
            a.de, a.ate = LOAD_SLOT1, a.ate or LOAD_SLOT1_ATE
        return gravar(a.nome, a.save, a.saves_de, a.de, a.ate, a.extra)
    if a.cmd == "reproduzir":
        return reproduzir(a.nome, a.janela)
    return exportar(a.nome, a.vms, a.slot)


if __name__ == "__main__":
    sys.exit(main())
