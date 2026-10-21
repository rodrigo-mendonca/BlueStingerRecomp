"""N6 fase F (docs/N6-PLANO.md secao 18): as sequencias de musica do Manatee (SMSB/SMSD), lidas como o driver as le.

O formato do evento vem do proprio driver do jogo (BSDATA\\MANATEE.DRV, ARM7, carregado sem o cabecalho de 0x20 bytes
no endereco 0 da RAM de som; o laco das trilhas em 0x4310 e o despacho em 0x4390):
  s < 0x80          nota: canal = s & 0x0F; tecla, velocidade; duracao ("gate") em (s >> 5) + 1 bytes (big-endian);
                    delta em 1 byte, 2 se s & 0x10
  0x90..0x9F, 0xF0+ ignorados (o driver segue para o proximo evento sem delta)
  0xA0..0xBF        controle: c (bit 7 = delta de 2 bytes), valor, delta
  0xC0..0xDF        programa/pressao: v (bit 7 = delta de 2 bytes), delta
  0xE0..0xEF        pitch bend: valor, delta (1 byte)
  0x81              referencia: deslocamento (2), contagem (1): toca 'contagem' eventos a partir do deslocamento e volta
  0x82              laco: contagem (0 = sem fim; bit 7 = delta de 2 bytes) e delta; o proximo 0x82 volta ao inicio
  0x83              fim da trilha
  0x84              tempo (2 bytes) e delta (1 byte)
  0x80, 0x85..0x8F  ignorados
Este script so le e confere a estrutura (nenhum byte vai para a saida): cada sequencia tem de ser consumida ate o fim
sem sair dos limites. A interpretacao no tempo (sequenciador) e a comparacao com o Flycast vem depois.

    python tools/dev/n6_smsd.py <BSDATA> [--eventos M2A]
"""
import argparse
import os
import struct
import sys
from collections import Counter


class Erro(Exception):
    pass


def u16be(b, i):
    return (b[i] << 8) | b[i + 1]


def eventos(data, ini, fim, limite=1 << 20):
    """Le os eventos de [ini, fim) como o driver (sem seguir as referencias 0x81, que sao listadas)."""
    i, n, out = ini, 0, []

    def byte():
        nonlocal i
        if i >= fim:
            raise Erro(f"evento cortado em {i:#x}")
        v = data[i]
        i += 1
        return v

    while i < fim and n < limite:
        pos, s = i, byte()
        n += 1
        if s < 0x80:
            tecla, vel = byte(), byte()
            gate = 0
            for _ in range((s >> 5) + 1):
                gate = (gate << 8) | byte()
            delta = byte()
            if s & 0x10:
                delta = (delta << 8) | byte()
            out.append(("nota", pos, s & 0x0F, tecla, vel, gate, delta))
        elif s == 0x83:
            out.append(("fim", pos))
            return out, i
        elif s == 0x84:
            tempo = (byte() << 8) | byte()
            out.append(("tempo", pos, tempo, byte()))
        elif s == 0x81:
            off = (byte() << 8) | byte()
            out.append(("ref", pos, off, byte()))
        elif s == 0x82:
            c = byte()
            delta = byte()
            if c & 0x80:
                delta = (delta << 8) | byte()
            out.append(("laco", pos, c & 0x7F, delta))
        elif 0xA0 <= s < 0xC0:
            c, v = byte(), byte()
            delta = byte()
            if c & 0x80:
                delta = (delta << 8) | byte()
            out.append(("controle", pos, s & 0x0F, c & 0x7F, v, delta))
        elif 0xC0 <= s < 0xE0:
            v = byte()
            delta = byte()
            if v & 0x80:
                delta = (delta << 8) | byte()
            out.append(("programa" if s < 0xD0 else "pressao", pos, s & 0x0F, v & 0x7F, delta))
        elif 0xE0 <= s < 0xF0:
            v = byte()
            out.append(("pitch", pos, s & 0x0F, v, byte()))
        else:  # 0x80, 0x85..0x9F, 0xF0..0xFF: o driver ignora
            out.append(("ignorado", pos, s))
    raise Erro(f"sem fim da trilha (0x83) ate {fim:#x}")


def smsb(path):
    b = open(path, "rb").read()
    if b[:4] != b"SMSB":
        raise Erro("nao e SMSB")
    n = struct.unpack_from("<I", b, 12)[0]
    offs = struct.unpack_from(f"<{n}I", b, 16)
    seqs = []
    for k, o in enumerate(offs):
        if b[o:o + 4] != b"SMSD":
            raise Erro(f"sequencia {k}: sem SMSD em {o:#x}")
        fim_seq = offs[k + 1] if k + 1 < len(offs) else len(b)
        h1, h2 = struct.unpack_from("<II", b, o + 4)
        ev, fim = eventos(b, o + 12, fim_seq)
        seqs.append(dict(indice=k, inicio=o, cab=(h1, h2), eventos=ev, consumido=fim - o, tamanho=fim_seq - o))
    return seqs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bsdata")
    ap.add_argument("--eventos", default="", help="lista os eventos desta musica (p.ex. M2A)")
    a = ap.parse_args()
    falhas = 0
    for f in sorted(os.listdir(a.bsdata)):
        if not f.upper().endswith(".MSB"):
            continue
        nome = f[:-4].upper()
        try:
            seqs = smsb(os.path.join(a.bsdata, f))
        except Erro as e:
            print(f"{nome}: ERRO {e}")
            falhas += 1
            continue
        for s in seqs:
            tipos = Counter(e[0] for e in s["eventos"])
            canais = sorted({e[2] for e in s["eventos"] if e[0] == "nota"})
            print(f"{nome} seq {s['indice']}: cabecalho {s['cab'][0]:#x} {s['cab'][1]:#x}; {len(s['eventos'])} eventos "
                  f"({dict(tipos)}); canais {canais}; consumidos {s['consumido']} de {s['tamanho']} bytes")
            if nome == a.eventos.upper():
                for e in s["eventos"][:80]:
                    print("   ", e)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
