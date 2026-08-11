#!/usr/bin/env python3
# gen_blob.py - Convierte un ELF de userland en un array de bytes C para
# embeberlo en el kernel (fs de arranque). Uso: gen_blob.py in.elf sym_name out.c
import sys

def main():
    if len(sys.argv) != 4:
        print("uso: gen_blob.py <in.elf> <nombre_simbolo> <out.c>", file=sys.stderr)
        sys.exit(1)
    in_path, sym, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(in_path, "rb").read()
    with open(out_path, "w") as f:
        f.write(f'// {out_path} - generado automaticamente por userland/gen_blob.py\n')
        f.write(f'// a partir de {in_path}. NO editar a mano.\n')
        f.write('#include "types.h"\n\n')
        f.write(f'const uint8_t {sym}[] = {{\n')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write('    ' + ','.join(str(b) for b in chunk) + ',\n')
        f.write('};\n')
        f.write(f'const unsigned int {sym}_len = {len(data)};\n')

if __name__ == "__main__":
    main()
