"""N6 (docs/N6-PLANO.md secao 15): efeitos nativos contra o jogo original no Flycast, no mesmo roteiro por passo.

Entradas:
  oraculo  o TSV do tools/dev/n6_oraculo.py (passo, ms, tipo, detalhe): tocar/parar/estado do SDK e os canais da
           AICA que mudaram por passada; o key-on de um canal na passada de um "tocar" e desse som, o key-off
           seguinte do mesmo canal e o fim dele (o driver desliga o canal ao fim da amostra sem laco)
  nativo   o BS_EFEITOS_LOG do provedor nativo (passo, vblank, tipo, detalhe): tocar/parar/estado e "fim"
           (a porta ficou livre no tempo logico)
O oraculo conta as passadas a partir de 0 e o jogo a partir de 1: --deslocamento (padrao 1) e somado ao oraculo.

Compara, por ordem:
  - a sequencia de (passo, porta, banco, pedido) dos "tocar": tem de ser IGUAL;
  - o passo do fim de cada som (fim nativo x key-off do original) e a duracao em passos: diferenca por som;
    sem laco, |diferenca| <= --tolerancia passos (padrao 2: o passo de jogo nao tem duracao fixa em VBlanks);
  - os "estado" (+24 por porta, so quando muda): a sequencia de valores por porta tem de ser igual.
Sai com 1 se alguma comparacao falhar. Uso:
    python tools/dev/n6_compara_o6.py <oraculo.tsv> <nativo.tsv> [--ate PASSO] [--tolerancia N]
"""
import argparse
import re
import sys


def ler(path, com_vb=False):
    with open(path, encoding="utf-8") as f:
        next(f)
        for ln in f:
            c = ln.rstrip("\n").split("\t")
            if len(c) >= 4:
                yield (int(c[0]), int(c[1]), c[2], c[3]) if com_vb else (int(c[0]), c[2], c[3])


def vb_de(det):
    """Tempo da linha em VBlanks (float): o TMU0 do SH-4 que o oraculo registra (tmu=TCNT/prescaler; conta para baixo a
    partir de 0xFFFFFFFF, P0 = 50 MHz). Calibrado em 2026-10-20: o efeito 117 (2,898 s de amostra) mede 2,903 s do key-on
    ao key-off. Sem tmu=, None (a comparacao dos fins fica em passos)."""
    m = re.search(r"tmu=([0-9A-F]{8})/(\d)", det)
    if not m:
        return None
    div = {0: 4, 1: 16, 2: 64, 3: 256, 4: 1024}.get(int(m.group(2)))
    if not div:
        return None
    return (0xFFFFFFFF - int(m.group(1), 16)) * div / 50e6 * 60000 / 1001


def kv(det):
    return dict(re.findall(r"(\w+)=(-?\w+)", det))


def oraculo(path, desl, ate):
    tocar, fim, estado = [], [], []
    canal = {}           # canal da AICA -> (porta, id, passo do tocar)
    pendentes = []       # tocar desta passada ainda sem key-on
    passo_ant = None
    for passo, tipo, det in ler(path):
        p = passo + desl
        if ate and p > ate:
            break
        if passo != passo_ant:
            pendentes, passo_ant = [], passo
        if tipo == "tocar":
            d = kv(det)
            t = (p, int(d["porta"]), int(d["banco"]), int(d["pedido"]))
            tocar.append(t)
            pendentes.append(t)
        elif tipo == "estado":
            d = kv(det)
            if "+24" in det:
                estado.append((int(d["porta"]), det.split("+24=")[1][:8]))
        elif tipo == "aica":
            for ch, k in re.findall(r"c(\d+):k(\d)", det):
                ch, k = int(ch), int(k)
                if k == 1 and det.startswith("cmd") and pendentes:
                    t = pendentes.pop(0)
                    canal[ch] = (t[1], t[2] * 128 + t[3], t[0], vb_de(det))
                elif k == 0 and ch in canal:
                    porta, sid, ini, vb0 = canal.pop(ch)
                    vb1 = vb_de(det)
                    fim.append((p, porta, sid, ini, None if vb0 is None or vb1 is None else vb1 - vb0))
    return tocar, fim, estado


def nativo(path, ate):
    tocar, fim, estado, inicio, vbi = [], [], [], {}, {}
    for p, vb, tipo, det in ler(path, True):
        if ate and p > ate:
            break
        d = kv(det)
        if tipo == "tocar":
            tocar.append((p, int(d["porta"]), int(d["banco"]), int(d["pedido"])))
            inicio[int(d["porta"])] = p
            vbi[int(d["porta"])] = vb
        elif tipo == "fim":
            pt = int(d["porta"])
            fim.append((p, pt, int(d["id"]), inicio.get(pt, p), vb - vbi.get(pt, vb)))
        elif tipo == "estado" and "+24" in det:
            estado.append((int(d["porta"]), det.split("+24=")[1][:8]))
    return tocar, fim, estado


def main():
    a = argparse.ArgumentParser()
    a.add_argument("oraculo")
    a.add_argument("nativo")
    a.add_argument("--deslocamento", type=int, default=1)
    a.add_argument("--ate", type=int, default=0, help="ultimo passo comparado (o oraculo pode ter parado antes)")
    a.add_argument("--tolerancia", type=int, default=2)
    a.add_argument("--desde", type=int, default=0, help="primeiro passo (do jogo) comparado: depois da ancora do roteiro")
    o = a.parse_args()
    ot, of, oe = oraculo(o.oraculo, o.deslocamento, o.ate)
    nt, nf, ne = nativo(o.nativo, o.ate)
    if o.desde:  # estado: a sequencia por porta depois do corte nao tem passo; so tocar e fim sao cortados
        ot = [t for t in ot if t[0] >= o.desde]; nt = [t for t in nt if t[0] >= o.desde]
        of = [f for f in of if f[3] >= o.desde]; nf = [f for f in nf if f[3] >= o.desde]
    falhas = 0

    def check(nome, ok, det=""):
        nonlocal falhas
        print(f"[{'ok' if ok else 'FALHA'}] {nome} {det}")
        falhas += 0 if ok else 1

    dif = next((i for i, (x, y) in enumerate(zip(ot, nt)) if x != y), None)
    check("tocar: mesma sequencia (passo, porta, banco, pedido)", len(ot) == len(nt) and dif is None,
          f"{len(ot)} no original, {len(nt)} no nativo" + (f"; 1a diferenca #{dif}: {ot[dif]} x {nt[dif]}" if dif is not None else ""))
    # fins pelo (porta, id, passo do tocar)
    # duracao em VBlanks quando o oraculo a tem (vb=): a AICA acaba o som em tempo real, e um passo de jogo no
    # Flycast nao dura sempre 2 VBlanks; sem vb, em passos
    porvb = bool(of) and all(f[4] is not None for f in of)
    nfi = {(x[1], x[2], x[3]): (x[4] if porvb else x[0] - x[3]) for x in nf}
    difs = []
    for p, porta, sid, ini, dvb in of:
        q = nfi.get((porta, sid, ini))
        difs.append((sid, porta, ini, dvb if porvb else p - ini, q))
    unidade = "VBlanks" if porvb else "passos"
    sem = [d for d in difs if d[4] is None]
    fora = [d for d in difs if d[4] is not None and abs(d[4] - d[3]) > o.tolerancia]
    exatos = sum(1 for d in difs if d[4] is not None and abs(d[4] - d[3]) < 1)  # em VBlanks: menos de 1
    check(f"fim de cada som (|diferenca| <= {o.tolerancia} {unidade})", not sem and not fora,
          f"{len(difs)} fins no original: {exatos} com diferenca < 1, {len(fora)} fora, {len(sem)} sem fim nativo")
    for d in (fora + sem)[:10]:
        print(f"    id {d[0]} porta {d[1]} tocado no passo {d[2]}: original {d[3]:.1f} {unidade}, nativo {d[4]}")
    sobra = len(nf) - (len(difs) - len(sem))
    check("nenhum fim nativo a mais (efeito com laco que acaba sozinho)", sobra <= 0, f"{max(sobra, 0)} a mais")
    porta_o, porta_n = {}, {}
    for porta, v in oe:
        porta_o.setdefault(porta, []).append(v)
    for porta, v in ne:
        porta_n.setdefault(porta, []).append(v)
    check("estado (+24 por porta, mudancas): igual", porta_o == porta_n,
          "" if porta_o == porta_n else f"original {dict(list(porta_o.items())[:3])} x nativo {dict(list(porta_n.items())[:3])}")
    print(f"{falhas} falhas")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
