# GopherOS

GopherOS es un kernel didáctico x86 de 32 bits (modo protegido, Multiboot).

## Características

- Kernel Multiboot que arranca con GRUB o `qemu-system-i386 -kernel`.
- GDT/IDT/PIC/PIT propios, paging real con páginas de 4MB (PSE).
- Scheduler cooperativo con un máximo de 5 procesos.
- Procesos en ring 3 (CPL3) con transición `iret` real y TSS por proceso.
- 15 syscalls estables vía `int 0x80`.
- Driver PS/2 básico, VGA texto y modo gráfico 320x200x256 (modo 13h).
- Driver ATA PIO (LBA28) para persistencia de filesystem.
- Filesystem jerárquico en RAM con directorios, archivos y persistencia opcional.
- Shell interactivo con comandos estilo Unix/DOS.

## Compilar

Requisitos en Ubuntu/Debian:

```bash
sudo apt-get install gcc-multilib nasm qemu-system-x86 grub-common xorriso
```

Compilar:

```bash
make
```

Ejecutar en QEMU:

```bash
make run          # con GUI
make run-nogui    # solo consola serial
make run-disk     # con disco ATA para probar persistencia
```

Generar ISO:

```bash
make iso
```

## Mejoras implementadas recientemente

- Tabla de descriptores de archivos por proceso: `open`/`close`/`read`/`write` funcionan
  para archivos, no solo stdin/stdout.
- `getpid` expuesto a través de `ioctl` (sin romper la regla de 15 syscalls).
- TSS.ESP0 actualizado por proceso para que syscalls y excepciones en ring 3 usen
  el stack de kernel correcto.
- Persistencia del filesystem desplazada a un sector seguro del disco (LBA 2048)
  para no pisar el MBR/boot sector.
- Carga automática del filesystem desde disco al arrancar, si existe una imagen guardada.

## Mejoras futuras sugeridas

- Paging con tablas de 4KB y permisos por proceso para aislar memoria de ring 3.
- Preemptive scheduling mediante timer interrupt con quantum.
- Implementar `fork`, `exec`, `mmap`/`munmap` y un VFS con inodos.
- Driver de red (NIC) para `socket`/`send`/`recv`.
- Soporte de múltiples terminales y un TTY layer.
