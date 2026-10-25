"""Confere que o icone de um executavel Windows e o de um .ico (lido do recurso do binario, nao do build).

Uso: python tools/dev/icone_exe.py <exe> <arquivo.ico>
Le o grupo de icones 1 (RT_GROUP_ICON, o "1 ICON" de app/bluestinger.rc.in) e cada imagem (RT_ICON) pela API de
recursos do Windows, e compara com as imagens do .ico, na ordem. Saida 0 se iguais; 1 se diferentes ou ausentes.
"""
import ctypes
import struct
import sys
from ctypes import wintypes

RT_ICON, RT_GROUP_ICON = 3, 14
LOAD_LIBRARY_AS_DATAFILE, LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x2, 0x20

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
k32.LoadLibraryExW.restype = ctypes.c_void_p
k32.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
k32.FindResourceW.restype = ctypes.c_void_p
k32.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
k32.LoadResource.restype = ctypes.c_void_p
k32.LockResource.argtypes = [ctypes.c_void_p]
k32.LockResource.restype = ctypes.c_void_p
k32.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
k32.SizeofResource.restype = wintypes.DWORD
k32.FreeLibrary.argtypes = [ctypes.c_void_p]


def recurso(h, nome, tipo):
    r = k32.FindResourceW(h, ctypes.c_void_p(nome), ctypes.c_void_p(tipo))
    if not r:
        return None
    return ctypes.string_at(k32.LockResource(k32.LoadResource(h, r)), k32.SizeofResource(h, r))


def imagens_exe(exe):
    h = k32.LoadLibraryExW(exe, None, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)
    if not h:
        raise SystemExit(f"FALHA nao abre {exe} (erro {ctypes.get_last_error()})")
    try:
        g = recurso(h, 1, RT_GROUP_ICON)
        if g is None or len(g) < 6:
            return None
        n = struct.unpack_from("<H", g, 4)[0]
        ids = [struct.unpack_from("<H", g, 6 + 14 * i + 12)[0] for i in range(n)]
        return [recurso(h, i, RT_ICON) for i in ids]
    finally:
        k32.FreeLibrary(h)


def imagens_ico(ico):
    b = open(ico, "rb").read()
    res, tipo, n = struct.unpack_from("<HHH", b, 0)
    if res != 0 or tipo != 1 or n == 0:
        raise SystemExit(f"FALHA {ico} nao e um .ico")
    out = []
    for i in range(n):
        tam, off = struct.unpack_from("<II", b, 6 + 16 * i + 8)
        out.append(b[off:off + tam])
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    exe, ico = sys.argv[1], sys.argv[2]
    a, b = imagens_exe(exe), imagens_ico(ico)
    if a is None:
        print("FALHA sem o grupo de icones 1 no executavel")
        return 1
    if len(a) != len(b) or any(x != y for x, y in zip(a, b)):
        print(f"FALHA icone diferente: exe {len(a)} imagens, ico {len(b)} imagens")
        return 1
    print(f"ok icone igual ao .ico ({len(a)} imagens, {sum(map(len, a))} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
