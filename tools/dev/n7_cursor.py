"""Cursor do mouse na janela (app/cursor_policy.h): escondido so no modo janela, com foco e sem o menu.

Roda o jogo na janela (copia do executavel numa pasta temporaria; preferencias ao lado dela) com o controle virtual
(BS_CONTROLE_VIRTUAL) e, em paralelo, tira e devolve o foco com uma janela auxiliar deste processo (Alt+Tab simulado,
sem mexer no mouse). Sequencia:
  gameplay com foco -> menu aberto -> tela cheia pelo menu (confirmada) -> volta a janela -> menu fechado ->
  perda de foco -> foco de volta -> sair.
Confere no log a sequencia de estados "[cursor] ...", que o estado efetivo do SDL (SDL_CursorVisible) bate com cada
um e que o encerramento deixa o cursor visivel.

    python tools/dev/n7_cursor.py --exe <bluestinger.exe> [--data <data>]
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
u32 = ctypes.windll.user32

# botoes (pagina principal em ingles: Graphics 0; Graficos: Display mode e a linha 4)
PAD = """0 conectar
6000 back 1
6100 back 0
7000 a 1
7100 a 0
7500 dpdown 1
7600 dpdown 0
7900 dpdown 1
8000 dpdown 0
8300 dpdown 1
8400 dpdown 0
8700 dpdown 1
8800 dpdown 0
9500 dpright 1
9600 dpright 0
11000 a 1
11100 a 0
13000 dpleft 1
13100 dpleft 0
14500 a 1
14600 a 0
15500 b 1
15600 b 0
16000 b 1
16100 b 0
26000 sair
"""
FOCO_FORA, FOCO_VOLTA = 18.0, 22.0  # segundos desde o inicio (o menu fecha ~16 s)

ESPERADO = ["oculto (janela com foco, gameplay)", "visivel (menu aberto)", "visivel (tela cheia)", "visivel (menu aberto)",
            "oculto (janela com foco, gameplay)", "visivel (sem foco)", "oculto (janela com foco, gameplay)",
            "visivel (encerramento)"]


def janela_do_processo(pid):
    achada = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, _):
        p = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u32.IsWindowVisible(h):
            achada.append(h)
        return True

    u32.EnumWindows(cb, 0)
    return achada[0] if achada else None


def trazer(h):
    # SetForegroundWindow so vale para quem "tem" a ultima entrada: um Alt solto antes libera (truque conhecido)
    u32.keybd_event(0x12, 0, 0, 0), u32.keybd_event(0x12, 0, 2, 0)
    u32.ShowWindow(h, 5)
    return bool(u32.SetForegroundWindow(h))


def auxiliar():
    """Uma janela minima deste processo, para tirar o foco do jogo (o que um Alt+Tab faz)."""
    import tkinter as tk
    r = tk.Tk()
    r.title("n7_cursor: foco")
    r.geometry("300x80+20+20")
    r.update()
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", default=os.path.join(REPO, "dist", "BlueStinger-Preview", "data"))
    a = ap.parse_args()
    d = tempfile.mkdtemp(prefix="n7_cursor_")
    exe = os.path.join(d, "bluestinger.exe")
    shutil.copy2(a.exe, exe)
    with open(os.path.join(d, "pad.txt"), "w", encoding="utf-8") as f:
        f.write(PAD)
    with open(os.path.join(d, "graficos.toml"), "w", encoding="utf-8") as f:
        f.write('[tela]\nmodo = "janela"\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
    env.update({"BS_CONTROLE_VIRTUAL": os.path.join(d, "pad.txt"), "BS_RELOGIO_FIXO": "1700000000"})
    log = open(os.path.join(d, "jogo.txt"), "w", encoding="utf-8")
    t0 = time.time()
    p = subprocess.Popen([exe, "--data", a.data, "--saves-pasta", os.path.join(d, "saves")], stdout=log, stderr=subprocess.STDOUT,
                         env=env, cwd=d)
    notas = []
    jogo = None
    while time.time() - t0 < 5 and not jogo:
        time.sleep(0.2)
        jogo = janela_do_processo(p.pid)
    if jogo:
        notas.append("foco inicial no jogo: %s" % trazer(jogo))
    aux = None
    while p.poll() is None:
        t = time.time() - t0
        if aux is None and t >= FOCO_FORA:
            aux = auxiliar()
            # a janela de topo do Tk e a mae do winfo_id
            notas.append("foco para a janela auxiliar: %s" % trazer(u32.GetParent(aux.winfo_id()) or aux.winfo_id()))
        if aux is not None and t >= FOCO_VOLTA and jogo and not any(n.startswith("foco de volta") for n in notas):
            notas.append("foco de volta ao jogo: %s" % trazer(jogo))
            aux.destroy()
        if aux is not None:
            try:
                aux.update()
            except Exception:
                pass
        time.sleep(0.05)
    log.close()
    texto = open(os.path.join(d, "jogo.txt"), encoding="utf-8", errors="replace").read()
    linhas = re.findall(r"^\[cursor\] (.*); SDL (visivel|oculto)$", texto, re.M)
    estados = [e for e, _ in linhas]
    # o estado efetivo do SDL (SDL_CursorVisible depois da chamada) tem de bater com a regra em cada troca
    reais = [e for e, sdl in linhas if (sdl == "oculto") != e.startswith("oculto")]
    ok = p.returncode == 0 and estados == ESPERADO and not reais
    if reais:
        print("  estado do SDL diferente da regra em: " + "; ".join(reais))
    for n in notas:
        print("  " + n)
    print("  estados: " + " -> ".join(estados))
    if estados != ESPERADO:
        print("  esperado: " + " -> ".join(ESPERADO))
    print("[%s] cursor (rc=%d; %s)" % ("ok" if ok else "FALHA", p.returncode, d))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
