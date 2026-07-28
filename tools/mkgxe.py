#!/usr/bin/env python3
# mkgxe.py - Crea un ejecutable .gxe a partir de un binario plano.

import argparse
import struct
import sys

GXE_MAGIC = 0x45584700
GXE_VERSION = 1

def main():
    p = argparse.ArgumentParser(description="Empaqueta un binario plano como .gxe")
    p.add_argument("input", help="Binario plano (text+data)")
    p.add_argument("output", help="Archivo .gxe de salida")
    p.add_argument("--entry", type=lambda x: int(x, 0), default=0,
                 help="Offset de entrada desde USER_BASE (default: 0)")
    p.add_argument("--bss", type=lambda x: int(x, 0), default=0,
                 help="Tamaño del BSS a inicializar en cero (default: 0)")
    p.add_argument("--stack", type=lambda x: int(x, 0), default=16384,
                 help="Tamaño minimo de stack de usuario (default: 16K)")
    args = p.parse_args()

    with open(args.input, "rb") as f:
        image = f.read()

    header = struct.pack(
        "<IIIIII",
        GXE_MAGIC,
        GXE_VERSION,
        args.entry,
        len(image),
        args.bss,
        args.stack,
    )

    with open(args.output, "wb") as f:
        f.write(header)
        f.write(image)

if __name__ == "__main__":
    main()
