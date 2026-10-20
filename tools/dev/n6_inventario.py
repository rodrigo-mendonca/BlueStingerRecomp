"""Inventario do audio de gameplay do Blue Stinger (N6, docs/N6-PLANO.md). Reproduzivel e sem conteudo
do jogo: so formatos, contagens e duracoes. O audio dos filmes (Sofdec) e do N5 e fica de fora.

    python -I tools/dev/n6_inventario.py <BSDATA> --saida <inventario.tsv> [--resumo <resumo.md>]

Recursos:
  *.WAV                 falas e sons dos eventos (RIFF; tag 0 = ADPCM de 4 bits da AICA, Yamaha)
  BS_SND.MLT            conteiner Manatee (SMLT): unidades SMPB/SMSB/SMDB/... (efeitos e bancos)
  M*.MSB / M*.MPB       musicas: sequencias (SMSB/SMSD) e bancos de programas e amostras (SMPB)
"""
import argparse
import os
import struct

WAV_TAGS = {0x0000: "aica-adpcm4", 0x0001: "pcm", 0x0002: "ms-adpcm", 0x0011: "ima-adpcm", 0x0020: "yamaha-adpcm"}


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def wav(path):
    b = open(path, "rb").read()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        return {"erro": "nao e RIFF/WAVE"}
    o, info, chunks = 12, {}, []
    while o + 8 <= len(b):
        cid, size = b[o:o + 4].decode("latin1"), u32(b, o + 4)
        chunks.append(cid.strip())
        body = b[o + 8:o + 8 + size]
        if cid == "fmt ":
            tag, ch, rate, brate, align, bits = struct.unpack_from("<HHIIHH", body, 0)
            info.update(tag=tag, canais=ch, taxa=rate, bits=bits, align=align)
        elif cid == "data":
            info["dados"] = size
        elif cid == "smpl" and len(body) >= 36:
            n = u32(body, 28)
            if n and len(body) >= 36 + 24:
                info["laco"] = (u32(body, 36 + 8), u32(body, 36 + 12))  # inicio, fim (amostras)
        o += 8 + size + (size & 1)
    ch, bits = info.get("canais", 1) or 1, info.get("bits", 16) or 16
    amostras = info.get("dados", 0) * 8 // (bits * ch) if bits else 0
    info["amostras"] = amostras
    info["duracao_s"] = amostras / info["taxa"] if info.get("taxa") else 0
    info["formato"] = WAV_TAGS.get(info.get("tag"), f"tag-{info.get('tag')}")
    info["chunks"] = ",".join(chunks)
    return info


def smlt(path):
    """Tabela de unidades do conteiner Manatee: tipo, banco, endereco na RAM de som, tamanho."""
    b = open(path, "rb").read()
    if b[:4] != b"SMLT":
        return []
    n = u32(b, 8)
    out = []
    for i in range(n):
        e = 0x20 + i * 0x20
        out.append(dict(tipo=b[e:e + 4].decode("latin1"), banco=u32(b, e + 4), aica=u32(b, e + 8),
                        tamanho=u32(b, e + 12), arquivo=u32(b, e + 16), tamanho_arquivo=u32(b, e + 20)))
    return out


def smsb(path):
    b = open(path, "rb").read()
    if b[:4] != b"SMSB":
        return {"erro": "nao e SMSB"}
    seqs = b.count(b"SMSD")
    return {"versao": u32(b, 4), "tamanho": u32(b, 8), "sequencias_decl": u32(b, 12), "blocos_SMSD": seqs}


def smpb(path):
    b = open(path, "rb").read()
    if b[:4] != b"SMPB":
        return {"erro": "nao e SMPB"}
    return {"versao": u32(b, 4), "tamanho": u32(b, 8), "campo_0x14": u32(b, 0x14)}


def main():
    a = argparse.ArgumentParser()
    a.add_argument("bsdata")
    a.add_argument("--saida", required=True)
    a.add_argument("--resumo", default="")
    o = a.parse_args()
    linhas = ["tipo\tnome\tformato\tcanais\ttaxa\tbits\tamostras\tduracao_s\tlaco\tdetalhe"]
    nomes = sorted(os.listdir(o.bsdata))
    wavs = [n for n in nomes if n.upper().endswith(".WAV")]
    tot_wav, formatos, taxas, lacos = 0.0, {}, {}, 0
    for n in wavs:
        i = wav(os.path.join(o.bsdata, n))
        if "erro" in i:
            linhas.append(f"wav\t{n}\t-\t-\t-\t-\t-\t-\t-\t{i['erro']}")
            continue
        tot_wav += i["duracao_s"]
        formatos[i["formato"]] = formatos.get(i["formato"], 0) + 1
        taxas[i["taxa"]] = taxas.get(i["taxa"], 0) + 1
        lacos += "laco" in i
        laco = f"{i['laco'][0]}-{i['laco'][1]}" if "laco" in i else "-"
        linhas.append(f"wav\t{n}\t{i['formato']}\t{i['canais']}\t{i['taxa']}\t{i['bits']}\t{i['amostras']}\t{i['duracao_s']:.3f}\t{laco}\tchunks={i['chunks']}")
    mlt = [n for n in nomes if n.upper().endswith(".MLT")]
    unidades = []
    for n in mlt:
        for u in smlt(os.path.join(o.bsdata, n)):
            unidades.append(u)
            linhas.append(f"mlt\t{n}\t{u['tipo']}\t-\t-\t-\t-\t-\t-\tbanco={u['banco']} aica=0x{u['aica']:X} tamanho={u['tamanho']} arquivo=0x{u['arquivo']:X}")
    musicas = sorted({n[:-4] for n in nomes if n.upper().endswith((".MSB", ".MPB"))})
    for m in musicas:
        s = smsb(os.path.join(o.bsdata, m + ".MSB")) if os.path.exists(os.path.join(o.bsdata, m + ".MSB")) else {"erro": "sem MSB"}
        p = smpb(os.path.join(o.bsdata, m + ".MPB")) if os.path.exists(os.path.join(o.bsdata, m + ".MPB")) else {"erro": "sem MPB"}
        linhas.append(f"musica\t{m}\tSMSB+SMPB\t-\t-\t-\t-\t-\t-\tMSB={s} MPB={p}")
    open(o.saida, "w", encoding="utf-8").write("\n".join(linhas) + "\n")
    res = [f"# Inventario do audio de gameplay (gerado por tools/dev/n6_inventario.py)", "",
           f"- WAV: {len(wavs)} arquivos, {tot_wav / 60:.1f} min no total; formatos {formatos}; taxas {taxas}; com laco (smpl): {lacos}",
           f"- MLT: {len(mlt)} conteiner(es), {len(unidades)} unidades: " + ", ".join(f"{u['tipo']}#{u['banco']}" for u in unidades),
           f"- Musicas: {len(musicas)} pares MSB/MPB: {', '.join(musicas)}"]
    if o.resumo:
        open(o.resumo, "w", encoding="utf-8").write("\n".join(res) + "\n")
    print("\n".join(res))


if __name__ == "__main__":
    main()
