"""Oraculo Flycast para testes diferenciais (ferramenta de desenvolvimento).

Fala o protocolo GDB remoto com um Flycast compilado com ENABLE_GDB_SERVER
(processo externo; nenhum codigo do Flycast entra no projeto). Ver CLAUDE.md.

    python tools/dev/flycast_oracle.py call  <func> <dir>   [--hit N] [--no-mask-irq]
        para na N-esima entrada de <func>, grava <dir>/entry.bss, roda ate o
        retorno (PR, com R15 restaurado) e grava <dir>/exit.bss
    python tools/dev/flycast_oracle.py trace <func> <entries.txt> <saida.tsv> [--hit N] [--max-calls N]
        executa <func> (breakpoints nas entradas) e grava cada entrada de funcao conhecida
        (mesmo formato de `bs-diff trace`)
    python tools/dev/flycast_oracle.py first-diff <flycast.tsv> <recompilado.tsv> [--accept lista.tsv]
        mostra a primeira divergencia entre dois traces
    python tools/dev/flycast_oracle.py snap  <func> <arquivo.bss> [--hit N] [--no-mask-irq] [--vram]
        para na N-esima entrada de <func> e grava o estado (checkpoint para
        `bs-diff until`); com --vram, tambem <arquivo>.vram; com --entry, grava
        antes a entrada do jogo na mesma sessao (pareada com o checkpoint)
    python tools/dev/flycast_oracle.py regs
        registradores atuais (diagnostico)

Snapshots contem memoria do jogo: grave-os em snapshots/ (fora do git).
"""
import argparse
import os
import socket
import subprocess
import struct
import sys
import time

RAM_BASE = 0x8C000000
RAM_SIZE = 16 << 20
VRAM32_BASE = 0xA5000000
VRAM_SIZE = 8 << 20
PVR_BASE = 0xA05F8000
PVR_SIZE = 0x2000
CHUNK = 0x400
SR_IMASK = 0xF << 4

# Ordem dos registradores do stub GDB do Flycast (core/debug/debug_agent.h, Sh4RegList).
R_PC, R_PR, R_GBR, R_VBR, R_MACH, R_MACL, R_SR, R_FPUL, R_FPSCR = 16, 17, 18, 19, 20, 21, 22, 23, 24
R_FR0, R_SSR, R_SPC, R_BANK0 = 25, 41, 42, 51


class Gdb:
    def __init__(self, host="127.0.0.1", port=3263, timeout=60.0):
        deadline = time.time() + timeout
        while True:
            try:
                self.s = socket.create_connection((host, port), timeout=5)
                break
            except OSError:
                if time.time() > deadline:
                    raise SystemExit(f"Flycast nao responde em {host}:{port}")
                time.sleep(0.5)
        self.s.settimeout(None)
        self.buf = b""
        self.bps = {}  # endereco sem espelho -> endereco gravado

    def _read(self, n=4096):
        d = self.s.recv(n)
        if not d:
            raise SystemExit("conexao com o Flycast encerrada")
        self.buf += d

    def send(self, data: str):
        pkt = f"${data}#{sum(data.encode()) & 0xFF:02x}".encode()
        self.s.sendall(pkt)
        while not self.buf:
            self._read()
        ack, self.buf = self.buf[:1], self.buf[1:]
        if ack != b"+":
            raise SystemExit(f"GDB: sem ack ({ack!r}) para {data[:40]}")

    def recv(self) -> str:
        while True:
            start = self.buf.find(b"$")
            if start >= 0:
                end = self.buf.find(b"#", start)
                if end >= 0 and len(self.buf) >= end + 3:
                    payload = self.buf[start + 1:end].decode()
                    self.buf = self.buf[end + 3:]
                    self.s.sendall(b"+")
                    return payload
            self._read()

    def cmd(self, data: str) -> str:
        self.send(data)
        return self.recv()

    def regs(self):
        h = self.cmd("g")
        return [struct.unpack("<I", bytes.fromhex(h[i:i + 8]))[0] for i in range(0, len(h), 8)]

    def set_reg(self, n, v):
        r = self.cmd(f"P{n:x}={struct.pack('<I', v & 0xFFFFFFFF).hex()}")
        if r != "OK":
            raise SystemExit(f"GDB: falha ao escrever registrador {n}: {r}")

    def read(self, addr, n) -> bytes:
        out = bytearray()
        while n:
            k = min(n, CHUNK)
            h = self.cmd(f"m{addr:x},{k:x}")
            if h.startswith("E"):
                raise SystemExit(f"GDB: leitura falhou em {addr:08X}: {h}")
            out += bytes.fromhex(h)
            addr += k
            n -= k
        return bytes(out)

    def bp(self, addr, on=True):
        r = self.cmd(f"{'Z' if on else 'z'}0,{addr:x},2")
        if r != "OK":
            raise SystemExit(f"GDB: breakpoint em {addr:08X} recusado: {r}")
        if on:
            self.bps[addr & 0x1FFFFFFF] = addr
        else:
            self.bps.pop(addr & 0x1FFFFFFF, None)

    def cont(self, poll=0.05, limit=600.0) -> str:
        """Continua ate um breakpoint.

        O stub do Flycast descarta o aviso de parada se ainda estiver enviando
        outro pacote (Connection::send, "Message dropped"): o trap para a CPU,
        mas a resposta nunca chega. Por isso, sem resposta em `poll` segundos,
        interrompe (^C) e confere se o PC parou num breakpoint nosso. A espera dobra
        a cada tentativa (ate 2 s): cada ^C para e recria a thread de emulacao, e
        interromper a cada 50 ms deixaria execucoes longas mais paradas que rodando.
        """
        deadline = time.time() + limit
        self.send("c")
        while True:
            if time.time() > deadline:
                raise SystemExit(f"nenhum breakpoint atingido em {limit:.0f}s")
            self.s.settimeout(poll)
            poll = min(poll * 2, 2.0)
            try:
                return self.recv()
            except socket.timeout:
                pass
            finally:
                self.s.settimeout(None)
            self.s.sendall(b"\x03")
            reply = self.recv()
            self.s.settimeout(0.05)  # se a parada real chegou junto com o ^C, sobra um pacote
            try:
                while True:
                    self.recv()
            except socket.timeout:
                pass
            finally:
                self.s.settimeout(None)
            if (self.regs()[R_PC] & 0x1FFFFFFF) in self.bps:
                return reply
            self.send("c")

    def step(self) -> str:
        # O agente so tira o breakpoint do PC se o endereco for identico; o mesmo
        # codigo roda em espelhos diferentes (AC010000 x 8C010000), entao tiramos
        # e repomos nos mesmos pelo endereco com que foi gravado.
        addr = self.bps.get(self.regs()[R_PC] & 0x1FFFFFFF)
        if addr is not None:
            self.bp(addr, False)
        try:
            return self.cmd("s")
        finally:
            if addr is not None:
                self.bp(addr)


FLYCAST = "C:/Source/flycast-oracle/build/flycast.exe"
GAME = os.path.join(os.path.dirname(__file__), "..", "..", "ISO", "Blue Stinger.cdi")


def connect(a) -> Gdb:
    """Abre um Flycast novo (boot do zero, reproduzivel) e conecta."""
    if not a.attach:
        # Um Flycast ja escutando na porta seria o processo conectado, e o novo ficaria
        # orfao: nunca abrir um segundo.
        try:
            socket.create_connection(("127.0.0.1", a.port), timeout=1).close()
            raise SystemExit(f"ja existe um servidor GDB na porta {a.port}: feche-o ou use --attach")
        except OSError:
            pass
        exe = a.flycast or FLYCAST
        a._proc = subprocess.Popen([exe, os.path.abspath(a.game or GAME)], cwd=os.path.dirname(exe))
    try:
        g = Gdb(port=a.port)
        g.cmd("?")
        return g
    except BaseException:
        finish(a, None)
        raise


def finish(a, g):
    """Fecha a conexao e encerra o Flycast que este script abriu (nunca um ja aberto)."""
    proc = getattr(a, "_proc", None)
    if g is not None:
        if proc is None:
            g.send("D")  # Flycast ja aberto: desanexa e deixa rodando
        g.s.close()
    if proc is not None:
        proc.kill()
        proc.wait()
        a._proc = None


def snapshot(g: Gdb, path):
    r = g.regs()
    ram = g.read(RAM_BASE, RAM_SIZE)
    regs = r[0:16] + [r[R_PC], r[R_PR], r[R_GBR], r[R_VBR], r[R_MACH], r[R_MACL], r[R_SR], r[R_FPUL], r[R_FPSCR]]
    regs += r[R_FR0:R_FR0 + 16] + [r[R_SSR], r[R_SPC]] + r[R_BANK0:R_BANK0 + 8]
    regs += [0] * 16 + [0]  # XF nao exposto pelo stub; flags = 0
    with open(path, "wb") as f:
        f.write(b"BSS1" + struct.pack("<II", 1, RAM_SIZE) + struct.pack("<%dI" % len(regs), *regs) + ram)
    return r


# Breakpoints do Flycast sao `trapa` gravados na RAM, e o IP.BIN so desembaralha o
# 1ST_READ.BIN em 0xAC010000 depois do boot (rotina em 0x8C00E35E): um breakpoint
# posto antes disso e sobrescrito. Este e o `jmp @r0` do IP.BIN para a entrada,
# executado logo apos o desembaralhamento (IP.BIN 8C00E1EA..8C00E204).
BOOT_JUMP = 0x8C00E202
ENTRY = 0x8C010000


def enter_game(g: Gdb):
    """A partir do reset, para na primeira instrucao do 1ST_READ.BIN (ja carregado)."""
    if g.regs()[R_PC] & 0x1FFFFFFF == 0:
        g.step()  # boot HLE (reios): carrega o IP.BIN e salta para 0xAC008300
    g.bp(BOOT_JUMP)
    g.cont()
    g.bp(BOOT_JUMP, False)
    for _ in range(2):  # o step do Flycast executa jmp + slot de atraso juntos
        g.step()
        pc = g.regs()[R_PC]
        if pc & 0x1FFFFFFF == ENTRY & 0x1FFFFFFF:
            break
    if pc & 0x1FFFFFFF != ENTRY & 0x1FFFFFFF:
        raise SystemExit(f"esperava a entrada do jogo apos o salto do IP.BIN; PC={pc:08X}")


def run_to(g: Gdb, func, hit):
    pc = g.regs()[R_PC] & 0x1FFFFFFF
    if pc == 0 or pc & 0x1FFF0000 == 0x0C000000:
        enter_game(g)  # ainda no reset (A0000000) ou no IP.BIN (8C008000..8C00FFFF)
    if func & 0x1FFFFFFF == ENTRY & 0x1FFFFFFF:
        hit -= 1  # ja estamos na entrada (primeira passagem)
        if hit == 0:
            return g.regs()
    g.bp(func)
    for _ in range(hit):
        g.cont()
    g.bp(func, False)
    return g.regs()


def mask_irq(g: Gdb, r):
    if (r[R_SR] & SR_IMASK) != SR_IMASK:
        g.set_reg(R_SR, r[R_SR] | SR_IMASK)


def cmd_snap(a):
    g = connect(a)
    if a.entry:
        # Entrada e checkpoint da mesma sessao: o reios grava a hora do host em
        # 0x8C000078 no boot, entao snapshots de sessoes diferentes nunca batem.
        enter_game(g)
        snapshot(g, a.entry)
        print(f"entrada do jogo -> {a.entry}")
    r = run_to(g, a.func, a.hit)
    if not a.no_mask_irq:
        mask_irq(g, r)
    r = snapshot(g, a.file)
    print(f"entrada {a.func:08X}: PC={r[R_PC]:08X} PR={r[R_PR]:08X} R15={r[15]:08X} -> {a.file}")
    if a.vram:
        # caminho de 32 bits (linear), o mesmo layout de runtime/pvr/vram.cpp
        path = os.path.splitext(a.file)[0] + ".vram"
        with open(path, "wb") as f:
            f.write(g.read(VRAM32_BASE, VRAM_SIZE))
        print(f"vram -> {path}")
    if a.pvr:
        # registradores do PVR, paleta e neblina (0x005F8000-0x005F9FFF), lidos pelo GDB
        path = os.path.splitext(a.file)[0] + ".pvr"
        with open(path, "wb") as f:
            f.write(g.read(PVR_BASE, PVR_SIZE))
        print(f"pvr -> {path}")
    finish(a, g)


def interrupt(g: Gdb):
    """^C e descarta pacotes que sobrarem (parada real chegando junto)."""
    g.s.sendall(b"\x03")
    g.recv()
    g.s.settimeout(0.2)
    try:
        while True:
            g.recv()
    except socket.timeout:
        pass
    finally:
        g.s.settimeout(None)


def cmd_snap_free(a):
    """Captura sem breakpoint durante o render.

    Breakpoints em STARTRENDER (snap --hit) interferem no PVR do Flycast: com paradas
    repetidas na submissao, o buffer do jogo fica no estado 4, o video nunca liga e a
    VRAM do framebuffer fica zerada; rodando livre, o mesmo jogo gira os buffers e liga
    o video (STATUS.md, oraculo grafico). Aqui o jogo roda sem breakpoints por
    `seconds` (tempo do host) e para por ^C num ponto qualquer.
    """
    g = connect(a)
    if a.entry:
        enter_game(g)
        snapshot(g, a.entry)
        print(f"entrada do jogo -> {a.entry}")
    else:
        enter_game(g)
    g.send("c")
    time.sleep(a.seconds)
    interrupt(g)
    r = snapshot(g, a.file)
    print(f"parada livre apos {a.seconds:.0f}s: PC={r[R_PC]:08X} -> {a.file}")
    base = os.path.splitext(a.file)[0]
    with open(base + ".vram", "wb") as f:
        f.write(g.read(VRAM32_BASE, VRAM_SIZE))
    with open(base + ".pvr", "wb") as f:
        f.write(g.read(PVR_BASE, PVR_SIZE))
    print(f"vram, pvr -> {base}.vram/.pvr")
    finish(a, g)


def cmd_call(a):
    import os
    os.makedirs(a.dir, exist_ok=True)
    g = connect(a)
    r = run_to(g, a.func, a.hit)
    if not a.no_mask_irq:
        mask_irq(g, r)
    r = snapshot(g, os.path.join(a.dir, "entry.bss"))
    ret, sp = r[R_PR], r[15]
    print(f"entrada {a.func:08X}: PR={ret:08X} R15={sp:08X}")
    g.bp(ret)
    for _ in range(1000):  # chamadas recursivas podem passar pelo mesmo retorno
        g.cont()
        r = g.regs()
        if r[R_PC] == ret and r[15] == sp:
            break
    else:
        raise SystemExit(f"retorno de {a.func:08X} nao encontrado em 1000 paradas; exit.bss nao gravado")
    g.bp(ret, False)
    snapshot(g, os.path.join(a.dir, "exit.bss"))
    print(f"saida em {r[R_PC]:08X}; snapshots em {a.dir}")
    finish(a, g)


def trace_line(entry, r):
    return "%08X\t" % entry + "\t".join("%08X" % x for x in r[0:16]) + "\t%08X\t%08X\t%08X\t%08X\n" % (
        r[R_PR], r[R_SR], r[R_FPSCR], r[R_FPUL])


def cmd_trace(a):
    entries = set()
    for line in open(a.entries):
        line = line.split("\t")[0].strip()
        if line.startswith("0x"):
            entries.add(int(line, 16) & 0x1FFFFFFF)
    g = connect(a)
    r = run_to(g, a.func, a.hit)
    if not a.no_mask_irq:
        mask_irq(g, r)
        r = g.regs()
    ret, sp = r[R_PR], r[15]
    # Um step do Flycast custa ~40 ms (cria e encerra a thread de emulacao), entao
    # nada de passo a passo: breakpoint em cada entrada conhecida e no retorno, e
    # `cont` entre elas. O custo passa a ser por chamada, nao por instrucao.
    for e in entries:
        g.bp(0x80000000 | e)
    g.bp(ret)
    n = 0
    with open(a.out, "w") as out:
        while True:
            pc = r[R_PC] & 0x1FFFFFFF
            if pc in entries:
                # endereco canonico (8C...), como o codigo recompilado registra
                out.write(trace_line(0x80000000 | pc, r))
                n += 1
            if r[R_PC] & 0x1FFFFFFF == ret & 0x1FFFFFFF and r[15] == sp and n:
                break
            if n >= a.max_calls:
                print("limite de chamadas atingido")
                break
            g.step()  # o agente tira e repoe o breakpoint do PC atual
            r = g.regs()
            if (r[R_PC] & 0x1FFFFFFF) not in g.bps:
                g.cont()
                r = g.regs()
    print(f"{n} entradas de funcao -> {a.out}")
    finish(a, g)


NAMES = ["func"] + ["r%d" % i for i in range(16)] + ["pr", "sr", "fpscr", "fpul"]


def load_accepted(path):
    """Diferencas aceitas, uma por linha (sem mascara global):
    <chamada> <funcao> <coluna> <valor flycast> <valor recompilado> <motivo...>
    So sao aceitas se chamada, funcao, coluna e os dois valores casarem exatamente."""
    acc = {}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = line.split(None, 5)
        if len(f) < 6 or f[2] not in NAMES:
            raise SystemExit(f"{path}:{n}: esperado <chamada> <funcao> <coluna> <flycast> <recompilado> <motivo>")
        acc[(int(f[0]), f[1].upper(), NAMES.index(f[2]))] = (f[3].upper(), f[4].upper(), f[5])
    return acc


def cmd_first_diff(a):
    A = [l.rstrip("\n").split("\t") for l in open(a.a)]
    B = [l.rstrip("\n").split("\t") for l in open(a.b)]
    acc = load_accepted(a.accept) if a.accept else {}
    used = set()
    for i, (x, y) in enumerate(zip(A, B)):
        bad = []
        for k, (u, v) in enumerate(zip(x, y)):
            if u == v:
                continue
            key = (i, x[0].upper(), k)
            if acc.get(key, (None, None))[:2] == (u.upper(), v.upper()):
                used.add(key)
                print(f"  aceito #{i} {x[0]} {NAMES[k]}: flycast={u} recompilado={v} ({acc[key][2]})")
            else:
                bad.append((k, u, v))
        if bad:
            prev = A[i - 1][0] if i else "(inicio)"
            print(f"primeira divergencia na chamada #{i}: flycast entra em {x[0]}, recompilado em {y[0]}")
            print(f"  ultima chamada igual: #{i - 1} {prev} -> o desvio aconteceu entre ela e esta")
            for k, u, v in bad:
                print(f"  {NAMES[k]:6s} flycast={u} recompilado={v}")
            return 1
    n = min(len(A), len(B))
    stale = [k for k in acc if k[0] < n and k not in used]
    for k in stale:  # aceita algo que nao aconteceu: a lista esta desatualizada
        print(f"diferenca aceita nao ocorreu: #{k[0]} {k[1]} {NAMES[k[2]]}")
    if len(A) != len(B):
        print(f"traces iguais nas primeiras {n} chamadas; tamanhos diferentes ({len(A)} x {len(B)})")
        return 1
    print(f"traces identicos ({len(A)} chamadas, {len(used)} diferencas aceitas)")
    return 1 if stale else 0


def cmd_regs(a):
    g = connect(a)
    r = g.regs()
    print(" ".join("r%d=%08X" % (i, r[i]) for i in range(16)))
    print("pc=%08X pr=%08X sr=%08X fpscr=%08X vbr=%08X gbr=%08X" % (r[R_PC], r[R_PR], r[R_SR], r[R_FPSCR], r[R_VBR], r[R_GBR]))
    finish(a, g)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=3263)
    p.add_argument("--attach", action="store_true", help="usa um Flycast ja aberto em vez de iniciar um novo")
    p.add_argument("--flycast", help="caminho do flycast.exe com ENABLE_GDB_SERVER")
    p.add_argument("--game", help="imagem do jogo (padrao: ISO/Blue Stinger.cdi)")
    sp = p.add_subparsers(dest="cmd", required=True)
    c = sp.add_parser("call")
    c.add_argument("func", type=lambda s: int(s, 0))
    c.add_argument("dir")
    c.add_argument("--hit", type=int, default=1)
    c.add_argument("--no-mask-irq", action="store_true")
    t = sp.add_parser("trace")
    t.add_argument("func", type=lambda s: int(s, 0))
    t.add_argument("entries")
    t.add_argument("out")
    t.add_argument("--hit", type=int, default=1)
    t.add_argument("--max-calls", type=int, default=100_000)
    t.add_argument("--no-mask-irq", action="store_true")
    d = sp.add_parser("first-diff")
    d.add_argument("a")
    d.add_argument("b")
    d.add_argument("--accept", help="diferencas aceitas, uma por linha (ver load_accepted)")
    n = sp.add_parser("snap")
    n.add_argument("func", type=lambda s: int(s, 0))
    n.add_argument("file")
    n.add_argument("--hit", type=int, default=1)
    n.add_argument("--no-mask-irq", action="store_true")
    n.add_argument("--pvr", action="store_true", help="grava tambem <arquivo>.pvr (registradores do PVR)")
    n.add_argument("--entry", help="grava antes o estado na entrada do jogo (mesma sessao)")
    n.add_argument("--vram", action="store_true", help="grava tambem <arquivo>.vram (8 MB, caminho de 32 bits)")
    sf = sp.add_parser("snap-free")
    sf.add_argument("file")
    sf.add_argument("--seconds", type=float, default=180.0, help="tempo do host rodando livre antes da parada")
    sf.add_argument("--entry", help="grava antes o estado na entrada do jogo (mesma sessao)")
    sp.add_parser("regs")
    a = p.parse_args()
    try:
        return {"call": cmd_call, "snap": cmd_snap, "trace": cmd_trace, "first-diff": cmd_first_diff, "regs": cmd_regs, "snap-free": cmd_snap_free}[a.cmd](a) or 0
    finally:
        finish(a, None)  # erro no meio do comando: nao deixar Flycast orfao


if __name__ == "__main__":
    sys.exit(main())
