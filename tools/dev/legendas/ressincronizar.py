"""Ressincroniza pacotes de legendas JA GERADOS (en e pt-BR) com a voz audivel, sem refazer o texto (2026-10-23).

O texto, os estados, as revisoes (inclusive a reconferencia no audio, aplicada depois do gerar_mod.py) e as chaves
(recurso, linha) do pacote existente ficam como estao; so mudam os tempos e, quando ha um corte seguro, a divisao de uma
linha em partes (sincronia.py). O pt-BR segue as partes do ingles quando o numero de frases bate; senao a linha fica
inteira e vai para fontes/partes-sem-divisao.pt-BR.tsv.

Entradas: os pacotes (pasta com mod.toml, legendas/, fontes/) e a pasta de trabalho local (pcm/ das falas,
filmes-audio/ dos filmes, transcricoes/ com as palavras do ASR, catalogo.tsv). Saida: copias novas dos pacotes.

    python tools/dev/legendas/ressincronizar.py --en <legendas-en> --pt <legendas-pt-br> --saida <pasta>
        [--trabalho <pasta>] [--versao-en 0.3.0] [--versao-pt 0.2.0] [--so-filmes]

Filmes (2026-10-23): a trilha tem musica e efeitos o tempo todo, entao a energia nao separa a voz (antes toda linha de
filme ia ate a seguinte e falas seguidas ficavam juntas). Os trechos de voz dos filmes vem das palavras do ASR, com os
defeitos conhecidos corrigidos (sincronia.palavras_corrigidas). --so-filmes refaz so os filmes: falas.*.toml ficam
identicos e as linhas das falas nas tabelas de partes vem do pacote de entrada.
"""
import argparse
import json
import math
import os
import shutil
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import gerar_mod as gm  # noqa: E402
import sincronia  # noqa: E402

NOMES = {"legendas-en": "Legendas em inglês (falas e filmes)", "legendas-pt-br": "Legendas em português do Brasil (falas e filmes)"}


def palavras_asr(trabalho, recurso, vid, arquivo, _cache={}):
    chave = "voz-%s" % vid if recurso.startswith("voz/") else "filme-%s" % arquivo
    if chave not in _cache:
        p = os.path.join(trabalho, "transcricoes", chave + ".json")
        ws = []
        if os.path.exists(p):
            for s in json.load(open(p, encoding="utf-8"))["segmentos"]:
                seg = [dict(w) for w in s["palavras"] if w["p"].strip()]
                if seg:
                    seg[0]["inicio_segmento"] = True  # o defeito conhecido do ASR e na primeira palavra do segmento
                ws += seg
        _cache[chave] = ws
    return _cache[chave]


def mod_toml(destino, ident, versao, idioma):
    with open(os.path.join(destino, "mod.toml"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f'formato    = 1\nid         = "{ident}"\nnome       = "{NOMES[ident]}"\nversao     = "{versao}"\nprioridade = 10\n'
                f'idiomas    = ["{idioma}"]\njogo       = "T13001N V1.000"\nprovisorio = true\n')


def tsv_escreve(path, cab, rows):
    with open(path, "w", encoding="utf-8", newline="\n") as o:
        o.write("\t".join(cab) + "\n")
        for r in rows:
            o.write("\t".join(str(r[c]).replace("\n", "\\n") for c in cab) + "\n")


def cabecalho_toml(path):
    out = []
    for l in open(path, encoding="utf-8"):
        if not l.startswith("#"):
            break
        out.append(l.rstrip("\n"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--en", required=True)
    ap.add_argument("--pt", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--trabalho", default=gm.TRABALHO)
    ap.add_argument("--versao-en", default="0.3.0")
    ap.add_argument("--versao-pt", default="0.2.0")
    ap.add_argument("--so-filmes", action="store_true", help="so os tempos e as partes dos filmes; as falas ficam como estao")
    a = ap.parse_args()
    gl = gm.glossario()
    cat = {r["id"]: r for r in gm.ler_tsv(os.path.join(a.trabalho, "catalogo.tsv"))}
    en_dir, pt_dir = os.path.join(a.saida, "legendas-en"), os.path.join(a.saida, "legendas-pt-br")
    for src, dst in ((a.en, en_dir), (a.pt, pt_dir)):
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
    tsv_en = os.path.join(en_dir, "fontes", "transcricoes.en-US.tsv")
    rows = gm.ler_tsv(tsv_en)
    cab = open(tsv_en, encoding="utf-8").readline().rstrip("\n").split("\t")
    # por recurso, na ordem do arquivo
    por = {}
    for r in rows:
        por.setdefault(r["recurso"], []).append(r)
    partes = {}  # (recurso, linha) -> [(ini, fim, texto, corte)]
    stats = dict(linhas=0, partes=0, sem_corte=0, sem_audio=0, adiantadas_antes=[], movidas=[])
    for rec, rs in por.items():
        vivas = [r for r in rs if r["estado"] != "excluida"]
        if not vivas or (a.so_filmes and rec.startswith("voz/")):
            continue
        vid, arquivo = rs[0]["id"], rs[0]["arquivo"]
        if rec.startswith("voz/"):
            c = cat.get(vid, {})
            wav = os.path.join(a.trabalho, "pcm", c["pcm"]) if c.get("pcm") else None
            dur = float(c.get("segundos") or 0) or max(float(r["fim"]) for r in rs)
        else:
            wav = os.path.join(a.trabalho, "filmes-audio", arquivo + ".wav")
            dur = 10 ** 6  # o filme: o fim de cada linha so e limitado pela seguinte
        ws = palavras_asr(a.trabalho, rec, vid, arquivo)
        if rec.startswith("voz/"):
            segs = sincronia.trechos_audiveis(wav) if wav else None
        else:  # filme: a voz pelas palavras (a energia da trilha nao separa a voz da musica)
            ws = sincronia.palavras_corrigidas(ws)
            segs = sincronia.trechos_das_palavras(ws) if os.path.exists(wav) and ws else None
        for k, r in enumerate(vivas):
            ini, fim = float(r["inicio"]), float(r["fim"])
            prox = float(vivas[k + 1]["inicio"]) if k + 1 < len(vivas) else dur
            l = dict(inicio=ini, fim=fim, texto=r["texto"].replace("\\n", "\n"),
                     palavras=[w for w in ws if ini - 0.05 <= (w["i"] + w["f"]) / 2 <= fim + 0.05])
            asr_gl, _ = gm.aplicar_glossario(r["texto_asr"].replace("\\n", " "), gl)
            ps, modo = sincronia.partes_da_linha(l, segs, prox, dur, asr_gl,
                                                 None if rec.startswith("voz/") else sincronia.PAUSA_PALAVRA)
            ps = [(pa, pb, gm.quebrar(" ".join(x.split())) or x, cc) for pa, pb, x, cc in ps]
            partes[(rec, int(r["linha"]))] = ps
            stats["linhas"] += 1
            stats["partes"] += len(ps) > 1
            stats["sem_corte"] += modo == "pausa-sem-corte"
            stats["sem_audio"] += modo in ("sem-audio", "sem-trecho")
            stats["movidas"].append(ps[0][0] - ini)
            r["inicio"], r["fim"] = f"{ps[0][0]:.2f}", f"{ps[-1][1]:.2f}"
    tsv_escreve(tsv_en, cab, rows)
    # ---- en: falas e filmes ----
    falas = os.path.join(en_dir, "legendas", "falas.en-US.toml")
    head = [h.replace("0.2.0", a.versao_en) for h in cabecalho_toml(falas)]
    head.append("# Tempos pela voz audivel (inicio no primeiro trecho de voz, fim no ultimo); falas separadas por silencio")
    head.append("# viram partes da mesma linha (fontes/partes.en-US.tsv).")
    partes_tsv = os.path.join(en_dir, "fontes", "partes.en-US.tsv")
    # --so-filmes: as partes das falas vem do pacote de entrada (as falas nao sao refeitas)
    partes_rows = [r for r in gm.ler_tsv(partes_tsv) if r["recurso"].startswith("voz/")] if a.so_filmes else []
    with open(os.devnull if a.so_filmes else falas, "w", encoding="utf-8", newline="\n") as o:
        o.write("\n".join(head) + "\n")
        for r in rows:
            if r["estado"] == "excluida" or not r["recurso"].startswith("voz/") or a.so_filmes:
                continue
            ps = partes[(r["recurso"], int(r["linha"]))]
            for n, (pa, pb, txt, cc) in enumerate(ps):
                parte = f", parte {n + 1} de {len(ps)}" if len(ps) > 1 else ""
                o.write(f"\n[[fala]]  # id {r['id']}, linha {r['linha']}{parte}, {r['estado']}\nvoz = {gm.toml_str(r['recurso'])}\n"
                        f"inicio = {gm.t_inicio(pa):.2f}\nfim = {gm.t_fim(pb):.2f}\ntexto = {gm.toml_str(txt)}\n")
    filmes_en = {}
    for r in rows:
        if r["estado"] == "excluida" or (r["recurso"], int(r["linha"])) not in partes:
            continue
        for n, (pa, pb, txt, cc) in enumerate(partes[(r["recurso"], int(r["linha"]))]):
            partes_rows.append(dict(recurso=r["recurso"], linha=r["linha"], parte=n, inicio=f"{pa:.2f}", fim=f"{pb:.2f}", texto=txt, corte=cc))
            if r["recurso"].startswith("filme/"):
                filmes_en.setdefault(r["arquivo"], []).append((pa, pb, txt))
    for nome, cues in filmes_en.items():
        srt = "".join(f"{k}\n{gm.srt_tempo(pa)} --> {gm.srt_tempo(math.floor(pb * 1000) / 1000)}\n{txt}\n\n" for k, (pa, pb, txt) in enumerate(cues, 1))
        for d in (os.path.join(en_dir, "legendas", f"{nome}.en-US.srt"), os.path.join(en_dir, "video", f"{nome}.en.srt")):
            with open(d, "w", encoding="utf-8", newline="\n") as o:
                o.write(srt)
    tsv_escreve(partes_tsv, ["recurso", "linha", "parte", "inicio", "fim", "texto", "corte"], partes_rows)
    mod_toml(en_dir, "legendas-en", a.versao_en, "en-US")
    # ---- pt-BR: o texto da traducao nas partes do ingles ----
    trad = {(r["recurso"], int(r["linha"])): r for r in gm.ler_tsv(os.path.join(pt_dir, "fontes", "traducao.pt-BR.tsv"))}
    sem_div_tsv = os.path.join(pt_dir, "fontes", "partes-sem-divisao.pt-BR.tsv")
    sem_div = [r for r in gm.ler_tsv(sem_div_tsv) if r["recurso"].startswith("voz/")] \
        if a.so_filmes and os.path.exists(sem_div_tsv) else []
    falas_pt, filmes_pt = [], {}
    for r in rows:
        if r["estado"] == "excluida" or (a.so_filmes and r["recurso"].startswith("voz/")):
            continue
        chave = (r["recurso"], int(r["linha"]))
        if chave not in trad:
            raise SystemExit(f"sem traducao: {chave}")
        ps = partes[chave]
        txt_pt = trad[chave]["pt"].replace("\\n", "\n")
        if len(ps) > 1:
            div = sincronia.dividir_traducao(txt_pt, ps[0][3], len(ps))
            if div:
                saidas = [(pa, pb, gm.quebrar(x) or x) for (pa, pb, _, _), x in zip(ps, div)]
            else:
                sem_div.append(dict(recurso=r["recurso"], linha=r["linha"], partes_en=len(ps), corte=ps[0][3], en=r["texto"], pt=trad[chave]["pt"]))
                saidas = [(ps[0][0], ps[-1][1], txt_pt)]
        else:
            saidas = [(ps[0][0], ps[0][1], txt_pt)]
        for n, s in enumerate(saidas):
            if r["recurso"].startswith("voz/"):
                falas_pt.append((r, n, len(saidas), s))
            else:
                filmes_pt.setdefault(r["arquivo"], []).append(s)
    falas = os.path.join(pt_dir, "legendas", "falas.pt-BR.toml")
    head = [h.replace("0.1.0", a.versao_pt) for h in cabecalho_toml(falas)]
    head.append("# Tempos pela voz audivel (os do ingles 0.3.0); a linha so e dividida em partes quando as frases batem com as")
    head.append("# do ingles (as outras: fontes/partes-sem-divisao.pt-BR.tsv).")
    with open(os.devnull if a.so_filmes else falas, "w", encoding="utf-8", newline="\n") as o:
        o.write("\n".join(head) + "\n")
        for r, n, tot, (pa, pb, txt) in falas_pt:
            parte = f", parte {n + 1} de {tot}" if tot > 1 else ""
            o.write(f"\n[[fala]]  # id {r['id']}, linha {r['linha']}{parte}\nvoz = {gm.toml_str(r['recurso'])}\n"
                    f"inicio = {gm.t_inicio(pa):.2f}\nfim = {gm.t_fim(pb):.2f}\ntexto = {gm.toml_str(txt)}\n")
    for nome, cues in filmes_pt.items():
        srt = "".join(f"{k}\n{gm.srt_tempo(pa)} --> {gm.srt_tempo(math.floor(pb * 1000) / 1000)}\n{txt}\n\n" for k, (pa, pb, txt) in enumerate(cues, 1))
        for d in (os.path.join(pt_dir, "legendas", f"{nome}.pt-BR.srt"), os.path.join(pt_dir, "video", f"{nome}.pt-br.srt")):
            with open(d, "w", encoding="utf-8", newline="\n") as o:
                o.write(srt)
    tsv_escreve(sem_div_tsv, ["recurso", "linha", "partes_en", "corte", "en", "pt"], sem_div)
    mod_toml(pt_dir, "legendas-pt-br", a.versao_pt, "pt-BR")
    mv = sorted(stats["movidas"])
    print("linhas: %d; inicio movido para a voz: mediana %.2f s, p90 %.2f s, max %.2f s (%d > 0,5 s); divididas em partes: %d; "
          "pausa sem corte seguro: %d; sem audio de referencia: %d; pt-BR sem divisao: %d" % (
              stats["linhas"], mv[len(mv) // 2], mv[int(len(mv) * 0.9)], mv[-1], sum(1 for x in mv if x > 0.5), stats["partes"],
              stats["sem_corte"], stats["sem_audio"], len(sem_div)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
