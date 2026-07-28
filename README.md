# GopherOS — README actualizado

## ¿Qué es GopherOS?

GopherOS es un **kernel x86 de 32 bits didáctico** que arranca con GRUB, corre en modo protegido, y demuestra conceptos fundamentales de sistemas operativos con **código real que compila y corre en QEMU**.

No es un "diseño en papel" ni fragmentos sueltos — es un kernel funcional de punta a punta.

---

## 🚀 Estado actual: COMPLETADO Y COMPILADO

### ✅ Core del kernel (hecho y probado)

| Componente | Estado | Detalle |
|------------|--------|---------|
| **Boot** | ✅ | Multiboot con GRUB o `qemu -kernel` |
| **GDT/IDT/PIC** | ✅ | Remapeo del PIC 8259, 32 excepciones + 16 IRQs + `int 0x80` |
| **TSS** | ✅ | Necesario para transiciones ring3→ring0 |
| **Paginación** | ✅ | 4KB con permisos por página, aislamiento real entre procesos |
| **Memoria** | ✅ | Bitmap físico + pools + region allocator (sin leaks) |
| **Scheduler** | ✅ | Cooperativo con `yield`/`sleep_on`/`wakeup`, máx 5 procesos |
| **Syscalls** | ✅ | 15 estables, ABI fija, `ioctl` como extensión |

### ✅ Drivers (hechos y probados)

| Componente | Estado | Detalle |
|------------|--------|---------|
| **VGA texto** | ✅ | 80x25, modo texto |
| **VGA gráfico** | ✅ | Modo 13h (320x200x256), reprogramación manual de registros CRTC |
| **Teclado PS/2** | ✅ | IRQ1, scancode set 1, shift, backspace |
| **RTC/CMOS** | ✅ | Fecha/hora real del hardware, formato BCD/binario y 12/24h |
| **ATA PIO** | ✅ | Disco duro real, LBA28, polling, detección de presencia |
| **PCI** | ✅ | Enumeración y espacio de configuración (puertos 0xCF8/0xCFC) |
| **RTL8139** | ✅ | NIC detectada por hardware, MAC real leída |
| **Red (Ethernet/IP/ICMP)** | ✅ | ARP, IPv4, ICMP — `ping` funciona contra QEMU |
| **TCP** | ✅ | Mínimo (una conexión a la vez), tres vías, cierre FIN/ACK |
| **Servidor Gopher** | ✅ | RFC 1436, probado con cliente TCP externo real |

### ✅ Filesystem (hecho y probado)

| Componente | Estado | Detalle |
|------------|--------|---------|
| **Árbol de directorios** | ✅ | `mkdir`/`rmdir`/`cd`/`pwd` reales, no flat |
| **Persistencia a disco** | ✅ | `save`/`load` serializan a disco, probado con reinicio completo |
| **Sector seguro** | ✅ | LBA 2048 (offset 1 MiB), no pisa MBR/particiones |

### ✅ Ring 3 — Multiusuario (hecho y probado)

| Capacidad | Estado | Detalle |
|-----------|--------|---------|
| **Transición de privilegio** | ✅ | CPL0↔CPL3 real, no simulada |
| **Syscalls desde ring3** | ✅ | `int 0x80` funciona desde CPL3 |
| **Aislamiento de fallos** | ✅ | Un proceso que se cae no tumba el kernel |
| **Aislamiento kernel vs proceso** | ✅ | `.data`/`.bss` del kernel protegidos |
| **Aislamiento proceso vs proceso** | ✅ | Cada proceso tiene su propio directorio de páginas |
| **Stack de kernel por proceso** | ✅ | TSS actualizado en cada cambio de contexto |

### ✅ Shell (hecho y probado)

| Comando | Estado | Detalle |
|---------|--------|---------|
| `ls`/`dir`, `cd`, `pwd` | ✅ | Navegación por directorios reales |
| `cat`/`type`, `rm`/`del`, `mkdir` | ✅ | Operaciones sobre FS real |
| `mv`/`ren`, `cp`/`copy` | ✅ | Movimiento y copia de archivos |
| `date`, `time`, `ver`, `vol` | ✅ | RTC y metadata del sistema |
| `clear`/`cls` | ✅ | Limpia la pantalla VGA |
| `demo` | ✅ | Modo gráfico VGA 13h con dibujo |
| `ping` | ✅ | Test de red ICMP real |
| `gopherserve` | ✅ | Servidor Gopher en puerto 70 |
| `edit` | ✅ | Editor de línea (crear/editar archivos) |
| `run` | ✅ | Ejecuta scripts de comandos (.bat/.sh estilo) |
| `gopherpy` | ✅ | Ejecuta programa traducido de Python-como a C nativo |
| `ring3demo` | ✅ | Demuestra que CPL3 es real (falla con `cli`) |
| `ring3mem` | ✅ | Demuestra aislamiento de memoria (falla tocando `.bss` del kernel) |
| `ring3victim`/`ring3attack` | ✅ | Demuestra aislamiento proceso↔proceso |

### ✅ GopherPy (hecho y probado)

| Componente | Estado | Detalle |
|------------|--------|---------|
| **Traductor Python-como → C** | ✅ | Sintaxis con variables, if/else, for/range, llamadas a la ABI |
| **ABI mínima** | ✅ | `gopheros_abi.h`: cero librerías, solo `int 0x80` |
| **7 bugs reales arreglados** | ✅ | Incluyendo bug de stringificación, arrays prestados, etc. |
| **Primer programa corriendo** | ✅ | `demo.py` → C → ejecución real en GopherOS |

---

## ❌ Limitaciones conocidas (documentadas, no son bugs ocultos)

| Limitación | Detalle |
|------------|---------|
| **Sin cargador de programas** | Todos los procesos están compilados dentro del kernel. Por eso `.text`/`.rodata` son accesibles desde ring3 — NO hay separación de código entre procesos. |
| **Conexiones TCP subsecuentes** | La primera conexión al servidor Gopher funciona punta a punta; las siguientes pueden fallar (sospecha: interacción con el NAT de QEMU). |
| **Allocator de memoria física** | Arena estática de 8MB embebida en el kernel — NO lee el mapa de memoria de Multiboot. |
| **Sin FPU inicializada** | `float` no está soportado (falta `fninit`). |
| **Sin USB** | Arrancar por USB sí funciona (BIOS/GRUB), pero leer/escribir un pendrive desde el kernel necesitaría driver UHCI/EHCI + clase de almacenamiento masivo (proyecto aparte). |
| **Sin `fork()`/`exec()`** | `proc_create()` existe, pero `fork()` devuelve `ENOSYS`. |
| **Patrón Connector+Plug** | No aplicado literalmente — VGA/teclado/timer/disco/NIC están cableados directo al kernel. |

---

## 📥 Archivos que se entregan

| Archivo | Contenido |
|---------|-----------|
| `gopheros.elf` | Kernel compilado, listo para `qemu-system-i386 -kernel` |
| `gopheros.iso` | Imagen booteable con GRUB (para VM/USB) |
| `gopheros_src.zip` | Código fuente completo |
| `gopherpy_toolchain.zip` | Compilador host + demo + .c generado |

---

## 🏃 Cómo correrlo

### Entorno gráfico (Linux, Windows, Mac)
```bash
qemu-system-i386 -kernel gopheros.elf -m 32
# o con ISO:
qemu-system-i386 -cdrom gopheros.iso -m 32
```
**Importante:** hacé click en la ventana para capturar el teclado.

### Terminal pura (SSH, sin entorno gráfico)
```bash
qemu-system-i386 -kernel gopheros.elf -m 32 -display curses
```
Aquí la terminal misma funciona como teclado.

### Solo depuración (sin poder escribir comandos)
```bash
qemu-system-i386 -kernel gopheros.elf -m 32 -serial stdio -display none
```

### Con disco (para probar persistencia)
```bash
qemu-system-i386 -kernel gopheros.elf -m 32 -hda disk.img
# o con make:
make run-disk
```

### Con red (para probar servidor Gopher)
```bash
qemu-system-i386 -kernel gopheros.elf -m 32 -netdev user,id=net0,hostfwd=tcp::7070-:70 -device rtl8139,netdev=net0
```
Luego desde el host:
```bash
python3 -c "socket.create_connection(('localhost', 7070))..."
```

---

## 🛠️ Recompilar desde fuente

```bash
# Ubuntu/Debian:
sudo apt install gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso mtools

unzip gopheros_src.zip && cd gopheros_src
make            # genera build/gopheros.elf
make iso        # genera build/gopheros.iso
make run        # arranca en QEMU
make run-disk   # con disco de 10MB
```

---

## 📋 Próximos pasos posibles (elegí por dónde seguir)

| Prioridad | Tarea | Tamaño |
|-----------|-------|--------|
| 1 | **Arreglar TCP multi-conexión** (o probar con `-netdev tap` para esquivar NAT) | Chico |
| 2 | **Cargador de programas real** (ELF o formato propio) — separa código de cada proceso | Mediano |
| 3 | **Patrón Connector+Plug** — refactor de arquitectura | Mediano |
| 4 | **Driver de disco con IRQ** (polling→IRQ14) + AHCI/SATA | Grande |
| 5 | **USB** (UHCI/EHCI + almacenamiento masivo) | Muy grande |
| 6 | **Fork/exec reales** | Grande |
| 7 | **Inicializar FPU** y soporte para `float` en GopherPy | Chico |

---

## 📄 Nota final

Este README refleja el **estado real y probado** del kernel, no un diseño ideal. Todo lo listado como "✅" ha sido verificado con evidencia de ejecución real — no solo compilación. Las limitaciones están documentadas a propósito, para que sepas exactamente qué está hecho y qué queda.

GopherOS es hoy un kernel funcional con:
- **Boot**, **paginación**, **scheduler**, **15 syscalls estables**
- **Drivers**: VGA texto+gráfico, teclado PS/2, RTC, ATA PIO, PCI, RTL8139, TCP
- **Filesystem** jerárquico con persistencia real a disco
- **Ring 3** con aislamiento de fallos, memoria kernel, y memoria entre procesos
- **Shell** con 25+ comandos, editor de línea, y scripts
- **GopherPy** (traductor Python-como → C nativo)
- **Servidor Gopher** RFC 1436 probado con cliente externo

Todo compila con `-Wall -Wextra -Werror`, cero warnings de código propio.
