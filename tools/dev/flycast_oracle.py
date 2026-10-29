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
import re
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

    def write(self, addr, data: bytes):
        for i in range(0, len(data), CHUNK):
            k = data[i:i + CHUNK]
            r = self.cmd(f"M{addr + i:x},{len(k):x}:{k.hex()}")
            if r != "OK":
                raise SystemExit(f"GDB: escrita falhou em {addr + i:08X}: {r}")

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
# a imagem do usuario em ISO\ do repositorio; num worktree (sem ISO\), a do repositorio principal ao lado
GAME = next((g for g in (os.path.join(os.path.dirname(__file__), "..", "..", d, "ISO", "Blue Stinger.cdi")
                         for d in (".", os.path.join("..", "recomp-BStinger"))) if os.path.exists(g)),
            os.path.join(os.path.dirname(__file__), "..", "..", "ISO", "Blue Stinger.cdi"))


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
    # Logo depois de uma sessao anterior, a primeira conexao as vezes cai (ConnectionResetError
    # no Windows): tenta de novo algumas vezes antes de desistir.
    last = None
    # O primeiro "?" com prazo: as vezes o stub do Flycast fica em "Waiting for debugger" sem atender a conexao aceita
    # (2026-10-20/21: duas sessoes presas assim, o Python esperando a resposta para sempre). Sem resposta em 20 s, fecha
    # essa conexao e tenta de novo; depois do aperto de mao, volta ao modo bloqueante de sempre.
    for attempt in range(5):
        g = None
        try:
            g = Gdb(port=a.port)
            g.s.settimeout(20.0)
            g.cmd("?")
            g.s.settimeout(None)
            return g
        except (ConnectionResetError, ConnectionRefusedError, socket.timeout) as e:
            last = e
            if g is not None:
                try:
                    g.s.close()
                except OSError:
                    pass
            time.sleep(2.0)
        except BaseException:
            finish(a, None)
            raise
    finish(a, None)
    raise SystemExit(f"nao foi possivel conectar ao Flycast: {last}")


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


def cmd_mem(a):
    """Para na N-esima execucao de func e mostra len bytes de addr (palavras de 32 bits)."""
    if a.len % 4:
        raise SystemExit("len precisa ser multiplo de 4 (palavras de 32 bits)")
    g = connect(a)
    if a.depois:  # ancora: passa uma vez por este endereco antes de contar `func`
        run_to(g, a.depois, 1)
    r = run_to(g, a.func, a.hit)
    data = g.read(a.addr, a.len)
    print("pc=%08X pr=%08X" % (r[R_PC], r[R_PR]))
    for i in range(0, len(data), 16):
        words = [struct.unpack_from("<I", data, i + j)[0] for j in range(0, min(16, len(data) - i), 4)]
        print("%08X: %s" % (a.addr + i, " ".join("%08X" % w for w in words)))
    finish(a, g)


def cmd_regs(a):
    g = connect(a)
    r = g.regs()
    print(" ".join("r%d=%08X" % (i, r[i]) for i in range(16)))
    print("pc=%08X pr=%08X sr=%08X fpscr=%08X vbr=%08X gbr=%08X" % (r[R_PC], r[R_PR], r[R_SR], r[R_FPSCR], r[R_VBR], r[R_GBR]))
    finish(a, g)


# --- bu-trace: oraculo de SAVE/LOAD do N4 (docs/N4-PREPARACAO.md, "Investigacao do oraculo") ---
# Entradas da API publica `bu` (inventario do N4). 8C08C696 (estado) e 8C08C112 (ultimo erro) nao
# entram aqui: o servidor interno 8C08BF50 chama 8C08C696 8 vezes por passada; elas sao lidas nos
# retornos dos pontos de chamada do jogo (BU_RETORNOS).
BU_ENTRADAS = {
    0x8C08BDEC: "init", 0x8C08C074: "reg_conclusao", 0x8C08C07A: "reg_progresso", 0x8C08CA34: "mount",
    0x8C08C1FC: "disk_info", 0x8C08C636: "exists", 0x8C08C8D4: "load", 0x8C08C818: "save",
    0x8C08BEBC: "exit", 0x8C08CC58: "vms_blocos", 0x8C08CA5C: "vms_monta", 0x8C08C7BE: "c7be",
}
BU_RETORNOS = {  # endereco logo depois do jsr + slot no jogo
    0x8C01C826: "status", 0x8C01CC48: "status", 0x8C01CD90: "status", 0x8C01CE7E: "status",
    0x8C01C830: "ultimo_erro",
}
BU_CALLBACKS = {0x8C01C2D2: "cb_conclusao", 0x8C01C364: "cb_progresso"}
BU_PAINEL = 0x8C01D200            # controlador de save/load do jogo (r4 0 no jogo, 1 no titulo: H1)
BU_PAINEL_MODO = (0x8C01D240, 0x8C01D244)  # literais com os enderecos das palavras de modo
BU_ESTADO = (0x8C1F3710, 2 * 92)  # bloco de 92 bytes por unidade (0 e 1)
BU_PAYLOAD = (0x8C24F93C, 1200)
BU_OPS_DE_CARTAO = (1, 2, 0x12)   # ops do callback que nao sao fim de LOAD/SAVE
SAVE_POINT_RET = 0x8C0408EA     # volta de 8C04FB80(jogador, sp, 0) em 8C0407C0 (A no gameplay): r0 = tipo
PD_RETORNO = 0x8C1730BE           # volta do servidor pd 8C15C4E6 (uma passada por passo de jogo)
PDS_PORTA_A = 0x8C2D8970          # PDS_PERIPHERAL da porta A, unidade 0 (transitional/pad_contract.h)
# bits de `on` (ordem de tools/dev/pad_roteiros.py); LT/RT como o decodificador do SDK (>= 128)
PD_BITS = {"C": 0, "B": 1, "A": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7,
           "Z": 8, "Y": 9, "X": 10, "D": 11, "RT": 16, "LT": 17,
           # direcional analogico (os roteiros do recompilado): bits so do oraculo, viram x1/y1 do PDS (+0x1C/+0x1E)
           # no extremo, como o decodificador do SDK (eixo - 128): esquerda/cima -128, direita/baixo 127
           "ANALOG_UP": 24, "ANALOG_DOWN": 25, "ANALOG_LEFT": 26, "ANALOG_RIGHT": 27}
PD_ANALOG = 0x0F000000
FLYCAST_VMU = "T13001N_vmu_save_A1.bin"  # VMU por jogo do Flycast (build/data)


def roteiro_bu(path):
    """Roteiro "indice passo" (o formato de tools/dist/roteiros) com duas diretivas:
       @pulsos <estado> <intervalo>  ate a proxima ancora, <estado> por 3 passos a cada <intervalo>
       @ancora <endereco>            os indices seguintes contam do primeiro passo depois da
                                     primeira execucao de <endereco> (o tempo de boot do Flycast nao e
                                     o do recompilado; a ancora alinha o roteiro a um ponto do jogo)
       @ancora-ram <endereco> <valor> idem, no primeiro passo em que a palavra de 32 bits em <endereco> vale <valor>
                                     (hex): um ponto que o recompilado tambem ve (BS_STATE_PASSO), p.ex. a posicao
                                     do Eliot; so os oraculos que conferem a RAM por passada a usam (n6_oraculo)
    Devolve uma lista de segmentos {ancora, pulsos, eventos[(passo, mascara)]}."""
    segs = [{"ancora": None, "ram": None, "pulsos": None, "eventos": []}]
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split("#", 1)[0].strip()
        if not line or line == "indice passo":
            continue
        f = line.split()
        if f[0] == "@ancora":
            segs.append({"ancora": int(f[1], 16) & 0x1FFFFFFF, "ram": None, "pulsos": None, "eventos": []})
        elif f[0] == "@ancora-ram" and len(f) == 3:
            segs.append({"ancora": None, "ram": (int(f[1], 16), int(f[2], 16)), "pulsos": None, "eventos": []})
        elif f[0] == "@pulsos":
            segs[-1]["pulsos"] = (mascara_pd(f[1], path, n), int(f[2]))
        elif f[0].isdigit() and len(f) == 2:
            segs[-1]["eventos"].append((int(f[0]), mascara_pd(f[1], path, n)))
        else:
            raise SystemExit(f"{path}:{n}: esperado '<passo> <estado>', '@ancora <end>' ou '@pulsos <estado> <n>'")
    return segs


# Valores intermediarios do analogico e dos gatilhos (pad_state_text: AX=n, AY=n, LT=n, RT=n, A2X=n, A2Y=n; os roteiros
# gravados com um controle de verdade, tools/dev/gravacoes.py) vao acima dos 32 bits da mascara: bit PD_VALORES e os
# bytes x, y, R e L. A2X/A2Y sao aceitos e ignorados (o controle padrao nao tem o segundo analogico: x2/y2 ficam 0, como
# no decodificador, transitional/pad_contract.cpp). O gatilho conta como botao com o byte >= 128, como no SDK.
PD_VALORES = 1 << 40
PD_CAMPOS = {"AX": 48, "AY": 56, "RT": 64, "LT": 72}


def mascara_pd(estado, path, n):
    if estado == "-":
        return 0
    m = 0
    vals = {"AX": 128, "AY": 128, "RT": 0, "LT": 0}
    numerico = False
    for b in estado.split("+"):
        if "=" in b:
            k, _, v = b.partition("=")
            if k not in ("AX", "AY", "RT", "LT", "A2X", "A2Y") or not v.isdigit() or int(v) > 255:
                raise SystemExit(f"{path}:{n}: valor invalido '{b}' (AX, AY, LT, RT, A2X, A2Y = 0..255)")
            if k in vals:
                vals[k] = int(v)
                numerico = True
            continue
        if b not in PD_BITS:
            raise SystemExit(f"{path}:{n}: botao desconhecido '{b}' (validos: {', '.join(PD_BITS)})")
        m |= 1 << PD_BITS[b]
    if numerico:
        # os extremos digitais no mesmo estado viram bytes (ANALOG_LEFT = 0 ...), o gatilho inteiro vira 255
        for bit, k, v in ((26, "AX", 0), (27, "AX", 255), (24, "AY", 0), (25, "AY", 255), (16, "RT", 255), (17, "LT", 255)):
            if m & (1 << bit):
                vals[k] = v
        m &= ~(PD_ANALOG | (1 << 16) | (1 << 17))
        m |= (1 << 16) * (vals["RT"] >= 128) | (1 << 17) * (vals["LT"] >= 128) | PD_VALORES
        for k, sh in PD_CAMPOS.items():
            m |= vals[k] << sh
    return m


class EntradaBu:
    """Aplica o roteiro na volta do servidor pd: on, off, press e release da porta A, as bordas pela
    regra do modo 0 do SDK (pad_contract: press = (on^old) & on). O servidor reescreve o PDS a cada
    passo a partir do controle do Flycast (parado), entao um botao segurado e reescrito a cada passo."""

    def __init__(self, segs):
        # passos a partir de 1, como os roteiros do recompilado ("1 START" vale desde o 1o passo);
        # aqui um passo e uma passada do servidor pd
        self.segs, self.i, self.base, self.old, self.passo = segs, 0, 0, 0, 1
        self.ancoras = {s["ancora"] for s in segs if s["ancora"] is not None}

    def ancora(self, pc):
        nxt = self.i + 1
        if nxt < len(self.segs) and self.segs[nxt]["ancora"] == pc:
            self.i, self.base = nxt, self.passo - 1  # o 1o passo depois da ancora e o passo 1
            return True
        return False

    def ancora_ram(self, ler32):
        """Ancora por RAM (@ancora-ram): conferida a cada passada, antes de aplicar a entrada; o passo atual vira o 1."""
        nxt = self.i + 1
        if nxt < len(self.segs) and self.segs[nxt].get("ram") and ler32(self.segs[nxt]["ram"][0]) == self.segs[nxt]["ram"][1]:
            self.i, self.base = nxt, self.passo - 1
            return True
        return False

    def mascara(self):
        s, rel = self.segs[self.i], self.passo - self.base
        m = 0
        for p, mk in s["eventos"]:
            if p <= rel:
                m = mk
        if s["pulsos"] and not s["eventos"]:
            mk, every = s["pulsos"]
            m = mk if rel % every < 3 else 0
        return m

    def aplica(self, g):
        on = self.mascara()
        if on & PD_VALORES:  # valores intermediarios: os bytes do controle, como o decodificador (eixo - 128)
            b = {k: (on >> sh) & 0xFF for k, sh in PD_CAMPOS.items()}
            x1, y1 = b["AX"] - 128, b["AY"] - 128
            g.write(PDS_PORTA_A + 0x18, struct.pack("<HH", b["RT"], b["LT"]))
            on &= 0xFFFFFFFF
        else:
            an = on & PD_ANALOG
            x1 = -128 if an & (1 << 26) else 127 if an & (1 << 27) else 0
            y1 = -128 if an & (1 << 24) else 127 if an & (1 << 25) else 0
        on &= ~PD_ANALOG & 0xFFFFFFFF
        g.write(PDS_PORTA_A + 0x1C, struct.pack("<hh", x1, y1))
        # todo passo, inclusive o neutro: senao o controle do Flycast (teclado, foco) vazaria para o PDS
        press, release = (on ^ self.old) & on, (on ^ self.old) & ~on & 0xFFFFFFFF
        g.write(PDS_PORTA_A + 0x08, struct.pack("<IIII", on, ~on & 0xFFFFFFFF, press, release))
        self.old = on
        self.passo += 1
        return on


def fecha_flycast(a, g):
    """Encerra o Flycast com WM_CLOSE: a VMU so vai inteira para o disco no fclose (o Flycast grava
    os blocos com fwrite sem fflush; matar o processo pode perder o save). Kill so como ultimo recurso."""
    proc = getattr(a, "_proc", None)
    if g is not None:
        try:
            g.send("D")
        except BaseException:
            pass
        g.s.close()
    if proc is None:
        return True
    import ctypes
    from ctypes import wintypes
    u32 = ctypes.windll.user32
    found = []
    cb = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(h, _):
        pid = wintypes.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == proc.pid and u32.IsWindowVisible(h):
            found.append(h)
        return True
    u32.EnumWindows(cb(each), 0)
    for h in found:
        u32.PostMessageW(h, 0x0010, 0, 0)  # WM_CLOSE
    try:
        proc.wait(timeout=30)
        ok = True
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        ok = False
    a._proc = None
    return ok


def cmd_bu_trace(a):
    import shutil
    if a.attach and (a.vmu or a.vmu_saida or a.sem_vmu):
        # a VMU so vai inteira para o disco quando o Flycast fecha, e um Flycast anexado nao e nosso
        raise SystemExit("--attach nao combina com --vmu/--vmu-saida/--sem-vmu: o Flycast precisa ser aberto por este comando")
    base = os.path.dirname(a.flycast or FLYCAST)
    vmu = os.path.join(base, "data", FLYCAST_VMU)
    cfg = os.path.join(base, "emu.cfg")
    trocas = []  # (arquivo, guarda): restaurados no fim, inclusive se a preparacao falhar
    for f in ([vmu] if a.vmu else []) + ([cfg] if a.vmu or a.sem_vmu else []):
        if os.path.exists(f + ".bs-original"):
            raise SystemExit(f"{f}.bs-original existe (sessao anterior interrompida?): restaure-o antes")
    entrada = EntradaBu(roteiro_bu(a.roteiro)) if a.roteiro else None
    g = None
    fechou = True
    try:
        if a.vmu:  # a VMU do oraculo (13/10) nunca e alterada: fica guardada e volta no fim
            trocas.append((vmu, vmu + ".bs-original"))
            if os.path.exists(vmu):
                shutil.copyfile(vmu, vmu + ".bs-original")
            shutil.copyfile(a.vmu, vmu)
        if a.vmu or a.sem_vmu:
            # slot A1 (device1.1) so nesta sessao: MDT_SegaVMU = 1 com a VMU, MDT_None = 10 sem
            # cartao (core/hw/maple/maple_cfg.h); o emu.cfg de 14/10 tem 10, ou seja, sem VMU
            trocas.append((cfg, cfg + ".bs-original"))
            shutil.copyfile(cfg, cfg + ".bs-original")
            txt = open(cfg, encoding="utf-8").read()
            if not re.search(r"(?m)^device1\.1 = \d+$", txt):
                raise SystemExit(f"{cfg}: esperava uma linha 'device1.1 = <tipo>' (slot A1)")
            open(cfg, "w", encoding="utf-8").write(
                re.sub(r"(?m)^device1\.1 = \d+$", "device1.1 = " + ("1" if a.vmu else "10"), txt))
        if a.dump:
            os.makedirs(a.dump, exist_ok=True)
        g = connect(a)
        enter_game(g)
        fixos = set(BU_ENTRADAS) | set(BU_RETORNOS) | set(BU_CALLBACKS) | {BU_PAINEL}
        if entrada or a.passos:
            fixos.add(PD_RETORNO)
        if a.forcar_save_point:
            fixos.add(SAVE_POINT_RET)
        ponto_forcado = False
        eventos = set(fixos)  # enderecos que sao eventos por si (ficam ate o fim)
        if entrada:
            fixos |= {0x80000000 | x for x in entrada.ancoras}
        marcas = {x & 0x1FFFFFFF for x in a.marcas}
        fixos |= {0x80000000 | x for x in marcas}
        for x in fixos:
            g.bp(x)
        pend = {}   # retorno (sem espelho) -> [(rotulo, r15, seq)]
        modo_ant, seq, passo, fim, t0 = None, 0, 0, None, time.time()
        load_dst = None
        modo_end = [struct.unpack("<I", g.read(x, 4))[0] for x in BU_PAINEL_MODO]
        out = open(a.out, "w", encoding="utf-8")
        out.write("seq\tpasso\tms\ttipo\trotulo\tpc\tpr\tr4\tr5\tr6\tr7\ts0\ts1\tr0\textra\n")

        def linha(tipo, rot, r, r0="", extra=""):
            nonlocal seq
            s0, s1 = struct.unpack("<II", g.read(r[15], 8))
            out.write(f"{seq}\t{passo}\t{int((time.time() - t0) * 1000)}\t{tipo}\t{rot}\t{r[R_PC]:08X}\t{r[R_PR]:08X}\t"
                      + "\t".join(f"{r[i]:08X}" for i in (4, 5, 6, 7)) + f"\t{s0:08X}\t{s1:08X}\t{r0}\t{extra}\n")
            out.flush()
            seq += 1

        def espera_retorno(rot, r):
            ret = r[R_PR] & 0x1FFFFFFF
            pend.setdefault(ret, []).append((rot, r[15]))
            if ret not in g.bps:
                g.bp(0x80000000 | ret)

        def despejo(nome, addr, n):
            if not a.dump:
                return ""
            b = g.read(addr, n)
            path = os.path.join(a.dump, f"{seq:05d}_{nome}.bin")
            open(path, "wb").write(b)
            import hashlib
            return f"{nome}={os.path.basename(path)} sha256={hashlib.sha256(b).hexdigest()[:16]}"

        g.cont(limit=a.limite)
        while True:
            r = g.regs()
            pc = r[R_PC] & 0x1FFFFFFF
            if time.time() - t0 > a.limite:
                print(f"limite de {a.limite:.0f}s atingido")
                break
            if pc in pend:  # retorno de uma entrada (inclusive tail call: PR de quem chamou a entrada)
                ficam = []
                for rot, sp in pend[pc]:
                    if sp == r[15]:
                        extra = ""
                        if rot == "cb_conclusao":
                            extra = "estado=" + g.read(*BU_ESTADO).hex()
                        linha("retorno", rot, r, f"{r[0]:08X}", extra)
                    else:
                        ficam.append((rot, sp))
                if ficam:
                    pend[pc] = ficam
                else:
                    del pend[pc]
                    if (0x80000000 | pc) not in fixos:
                        g.bp(0x80000000 | pc, False)
            key = 0x80000000 | pc
            if key in BU_ENTRADAS:
                rot = BU_ENTRADAS[key]
                extra = ""
                if rot == "save":
                    extra = despejo("save_src", r[6], r[7] * 512) + " " + despejo("payload", *BU_PAYLOAD)
                elif rot == "load":
                    load_dst = r[6]
                linha("entrada", rot, r, "", extra.strip())
                espera_retorno(rot, r)
            elif key in BU_RETORNOS:
                linha("retorno", BU_RETORNOS[key], r, f"{r[0]:08X}")
            elif key in BU_CALLBACKS:
                rot = BU_CALLBACKS[key]
                extra = ""
                if rot == "cb_conclusao" and r[5] not in BU_OPS_DE_CARTAO:
                    if load_dst is not None:
                        extra = despejo("load_dst", load_dst, 3 * 512)
                        load_dst = None
                    if fim is None and a.fim_apos:
                        a.fim_apos -= 1
                        if a.fim_apos == 0:
                            fim = passo + a.passos_extra
                linha("callback", rot, r, "", extra)
                espera_retorno(rot, r)
            elif key == BU_PAINEL:
                modo = tuple(struct.unpack("<H", g.read(x, 2))[0] for x in modo_end)
                atual = (r[4], r[5], r[R_PR], modo)
                if a.forcar_modo_save and (r[R_PR] & 0x1FFFFFFF) == (a.forcar_modo_save + 4) & 0x1FFFFFFF and r[4] != 0:
                    g.set_reg(4, 0)
                    linha("forcado", "painel_r4_0", r, "", f"modo={modo[0]:04X},{modo[1]:04X}")
                elif atual != modo_ant:
                    linha("painel", "painel", r, "", f"modo={modo[0]:04X},{modo[1]:04X}")
                modo_ant = atual
            elif key == SAVE_POINT_RET and a.forcar_save_point and not ponto_forcado:
                # mesmo desvio do BS_SAVES_H2 do runtime: nada a frente (0) vira o tipo 4 (ator kind 98, save
                # point); o resto e o fluxo do jogo original. Uma vez so.
                if r[0] & 0xFFFF == 0:
                    g.set_reg(0, 4)
                    ponto_forcado = True
                    linha("forcado", "save_point_tipo4", r)
            elif key == PD_RETORNO:
                passo += 1
                if entrada:
                    entrada.aplica(g)
                if (a.passos and passo >= a.passos) or (fim is not None and passo >= fim):
                    break
            # ancoras e marcas podem cair num endereco que tambem e evento (o painel, por exemplo)
            if entrada and pc in entrada.ancoras and entrada.ancora(pc):
                linha("ancora", f"{key:08X}", r)
                if all(s["ancora"] != pc for s in entrada.segs[entrada.i + 1:]) and key not in eventos:
                    fixos.discard(key)  # consumida: nao parar mais a cada execucao
                    g.bp(key, False)
            if pc in marcas:
                linha("marca", f"{key:08X}", r)
                marcas.discard(pc)
                if key not in eventos and not (entrada and pc in entrada.ancoras):
                    fixos.discard(key)
                    g.bp(key, False)
            g.step()
            r2 = g.regs()
            if (r2[R_PC] & 0x1FFFFFFF) not in g.bps:
                g.cont(limit=max(1.0, a.limite - (time.time() - t0)))
        for addr, n in a.ler_fim:  # estado do jogo no fim (p.ex. posicao do Eliot depois do LOAD)
            linha("fim", f"{addr:08X}", g.regs(), "", "ram=" + g.read(addr, n).hex())
        out.close()
        print(f"{seq} eventos, {passo} passos -> {a.out}")
    finally:
        fechou = fecha_flycast(a, g)
        if a.vmu and a.vmu_saida and os.path.exists(vmu):
            shutil.copyfile(vmu, a.vmu_saida)
            print(f"VMU depois da sessao -> {a.vmu_saida}" + ("" if fechou else " (AVISO: Flycast morto, VMU pode estar incompleta)"))
        for f, guarda in trocas:
            if os.path.exists(guarda):
                os.replace(guarda, f)
            elif os.path.exists(f):
                os.remove(f)  # nao havia original: tira a copia da sessao
    return 0 if fechou else 3


def quadro_flycast(g):
    """N8.7: o quadro exibido pelo Flycast (rend.EmulateFramebuffer = yes: o render vai para a VRAM), com a mesma regra
    de composicao do recompilado (runtime/pvr/pvr_video.cpp): FB_R_CTRL, FB_R_SIZE, FB_R_SOF1/2, entrelacado pelo
    SPG_CONTROL. A VRAM e lida pelo caminho de 32 bits (0xA5000000), o mesmo layout dos enderecos de FB_R_SOF*."""
    reg = lambda off: struct.unpack("<I", g.read(0xA05F8000 + off, 4))[0]
    ctrl, size = reg(0x044), reg(0x05C)
    fmt = (ctrl >> 2) & 3
    bpp = 3 if fmt == 2 else (4 if fmt == 3 else 2)
    words, lines, mod = (size & 0x3FF) + 1, ((size >> 10) & 0x3FF) + 1, (size >> 20) & 0x3FF
    stride = (words + mod - (1 if mod else 0)) * 4
    interlace = (reg(0x0D0) >> 4) & 1
    dbl = (not interlace) and ((ctrl >> 1) & 1)
    w, h = words * 4 // bpp, lines * (2 if interlace or dbl else 1)
    sof1, sof2 = reg(0x050) & 0x00FFFFFC, reg(0x054) & 0x00FFFFFC
    exp5 = lambda v: (v << 3) | (v >> 2)
    exp6 = lambda v: (v << 2) | (v >> 4)
    img = bytearray(w * h * 3)  # BGR
    linhas = {}
    for y in range(h):
        base = ((sof2 if y & 1 else sof1) + (y >> 1) * stride) if interlace else (sof1 + ((y >> 1) if dbl else y) * stride)
        if base not in linhas:
            linhas[base] = g.read(0xA5000000 + base, w * bpp)
        row = linhas[base]
        for x in range(w):
            if fmt == 0:
                v = row[2 * x] | (row[2 * x + 1] << 8)
                r, gg, b = exp5((v >> 10) & 31), exp5((v >> 5) & 31), exp5(v & 31)
            elif fmt == 1:
                v = row[2 * x] | (row[2 * x + 1] << 8)
                r, gg, b = exp5(v >> 11), exp6((v >> 5) & 63), exp5(v & 31)
            else:
                b, gg, r = row[bpp * x], row[bpp * x + 1], row[bpp * x + 2]
            o = 3 * (y * w + x)
            img[o], img[o + 1], img[o + 2] = b, gg, r
    return w, h, bytes(img)


def grava_bmp(path, w, h, bgr):
    pad = (4 - (w * 3) % 4) % 4
    body = b"".join(bgr[3 * w * y:3 * w * (y + 1)] + b"\0" * pad for y in range(h - 1, -1, -1))
    head = struct.pack("<2sIHHIIiiHHIIiiII", b"BM", 54 + len(body), 0, 0, 54, 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    open(path, "wb").write(head + body)


def cmd_quadro(a):
    """N8.7: o oraculo grafico. Reproduz o roteiro por passo de jogo (como o bu-trace) e grava o quadro exibido pelo
    Flycast nos passos pedidos (<out>/pNNNNNN.bmp), para comparar com o do recompilado no mesmo passo."""
    import shutil
    alvos = sorted(set(a.passos))
    entrada = EntradaBu(roteiro_bu(a.roteiro))
    os.makedirs(a.out, exist_ok=True)
    g = None
    # --vmu: a VMU desta sessao no slot A1 (como o bu-trace); a do Flycast e o emu.cfg voltam no fim
    base = os.path.dirname(a.flycast or FLYCAST)
    vmu, cfg = os.path.join(base, "data", FLYCAST_VMU), os.path.join(base, "emu.cfg")
    trocas = []
    if a.vmu:
        for f in (vmu, cfg):
            if os.path.exists(f + ".bs-original"):
                raise SystemExit(f"{f}.bs-original existe (sessao anterior interrompida?): restaure-o antes")
    try:
        if a.vmu:
            trocas.append((vmu, vmu + ".bs-original"))
            if os.path.exists(vmu):
                shutil.copyfile(vmu, vmu + ".bs-original")
            shutil.copyfile(a.vmu, vmu)
            trocas.append((cfg, cfg + ".bs-original"))
            shutil.copyfile(cfg, cfg + ".bs-original")
            txt = open(cfg, encoding="utf-8").read()
            if not re.search(r"(?m)^device1\.1 = \d+$", txt):
                raise SystemExit(f"{cfg}: esperava uma linha 'device1.1 = <tipo>' (slot A1)")
            open(cfg, "w", encoding="utf-8").write(re.sub(r"(?m)^device1\.1 = \d+$", "device1.1 = 1", txt))
        # 2026-10-28: o stub as vezes aceita o "?" e nunca responde ao primeiro "s" do boot (4 h presa no recv). A partida
        # tem prazo; sem resposta, o Flycast e encerrado e a sessao recomeca do zero (no maximo 3 vezes)
        for tentativa in range(1, 4):
            g = connect(a)
            try:
                g.s.settimeout(120.0)
                enter_game(g)
                g.s.settimeout(None)
                break
            except (socket.timeout, ConnectionResetError) as e:
                print(f"partida do Flycast sem resposta (tentativa {tentativa}: {e!r}); recomecando", flush=True)
                fecha_flycast(a, g)
                g = None
                time.sleep(3.0)
        else:
            raise SystemExit("o Flycast nao respondeu a partida em 3 tentativas")
        fixos = {PD_RETORNO} | {0x80000000 | x for x in entrada.ancoras}
        for x in fixos:
            g.bp(x)
        passo, t0 = 0, time.time()
        g.cont(limit=a.limite)
        while alvos:
            r = g.regs()
            pc = r[R_PC] & 0x1FFFFFFF
            if time.time() - t0 > a.limite:
                print(f"limite de {a.limite:.0f}s atingido no passo {passo}")
                break
            if (0x80000000 | pc) == PD_RETORNO:
                passo += 1
                entrada.aplica(g)
                if passo == alvos[0]:
                    w, h, img = quadro_flycast(g)
                    grava_bmp(os.path.join(a.out, f"p{passo:06d}.bmp"), w, h, img)
                    print(f"passo {passo}: quadro {w}x{h} ({time.time() - t0:.0f} s)", flush=True)
                    alvos.pop(0)
            if pc in entrada.ancoras:
                entrada.ancora(pc)
            g.step()
            if (g.regs()[R_PC] & 0x1FFFFFFF) not in g.bps:
                g.cont(limit=max(1.0, a.limite - (time.time() - t0)))
    finally:
        fecha_flycast(a, g)
        for f, guarda in trocas:
            if os.path.exists(guarda):
                os.replace(guarda, f)
            elif os.path.exists(f):
                os.remove(f)
    return 0 if not alvos else 4


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
    m = sp.add_parser("mem", help="memoria na N-esima execucao de um endereco")
    m.add_argument("func", type=lambda s: int(s, 0))
    m.add_argument("addr", type=lambda s: int(s, 0))
    m.add_argument("len", type=lambda s: int(s, 0))
    m.add_argument("--hit", type=int, default=1)
    m.add_argument("--depois", type=lambda s: int(s, 0), help="para antes uma vez neste endereco (ancora)")
    b = sp.add_parser("bu-trace", help="oraculo de SAVE/LOAD (N4): API bu, callbacks, polling, VMU")
    b.add_argument("out", help="TSV de eventos")
    b.add_argument("--roteiro", help="roteiro 'indice passo' com @ancora e @pulsos (entrada na porta A)")
    b.add_argument("--vmu", help="imagem de VMU (128 KB) usada nesta sessao; a do Flycast volta no fim")
    b.add_argument("--vmu-saida", help="copia da VMU depois da sessao (com --vmu)")
    b.add_argument("--sem-vmu", action="store_true", help="slot A1 vazio nesta sessao (emu.cfg restaurado no fim)")
    b.add_argument("--dump", help="pasta para os bytes de SAVE (origem, payload) e de LOAD (destino)")
    b.add_argument("--forcar-modo-save", type=lambda s: int(s, 0),
                   help="ponto de chamada do painel (jsr) cujo r4 vira 0 (H1: 0x8C0792C8, LOAD GAME do titulo)")
    b.add_argument("--forcar-save-point", action="store_true",
                   help="o primeiro A no gameplay sem nada a frente ve um save point (tipo 4 em 8C0408EA; H2 do runtime)")
    b.add_argument("--ler-fim", type=lambda s: [(int(x.split(":")[0], 16), int(x.split(":")[1])) for x in s.split(",")],
                   default=[], help="end:n,... da RAM lidos no fim da sessao (evento fim)")
    b.add_argument("--passos", type=int, default=0, help="para depois de N passos de jogo (0 = sem limite)")
    b.add_argument("--fim-apos", type=int, default=0, help="para depois de N conclusoes de LOAD/SAVE...")
    b.add_argument("--passos-extra", type=int, default=120, help="...mais estes passos")
    b.add_argument("--limite", type=float, default=3600.0, help="tempo maximo do host (s)")
    b.add_argument("--marcas", type=lambda s: [int(x, 16) for x in s.split(",")], default=[],
                   help="enderecos (hex, virgula) cujo primeiro passo e registrado: calibra o roteiro")
    q = sp.add_parser("quadro", help="N8.7: oraculo grafico (quadro exibido nos passos pedidos)")
    q.add_argument("out", help="pasta dos BMP (pNNNNNN.bmp)")
    q.add_argument("--roteiro", required=True, help="roteiro 'indice passo' (tools/dist/roteiros)")
    q.add_argument("--passos", required=True, type=lambda s: [int(x) for x in s.split(",")], help="passos de jogo (virgula)")
    q.add_argument("--limite", type=float, default=7200.0, help="tempo maximo do host (s)")
    q.add_argument("--vmu", help="imagem de VMU (128 KB) no slot A1 nesta sessao (a do Flycast volta no fim)")
    a = p.parse_args()
    try:
        return {"call": cmd_call, "snap": cmd_snap, "trace": cmd_trace, "first-diff": cmd_first_diff, "regs": cmd_regs,
                "snap-free": cmd_snap_free, "mem": cmd_mem, "bu-trace": cmd_bu_trace, "quadro": cmd_quadro}[a.cmd](a) or 0
    finally:
        finish(a, None)  # erro no meio do comando: nao deixar Flycast orfao


if __name__ == "__main__":
    sys.exit(main())
