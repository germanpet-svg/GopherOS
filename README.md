# GopherOS — Un kernel x86 didáctico, completado y compilado

<div align="center">

![GopherOS Banner](https://via.placeholder.com/800x200/2d2d2d/00ff88?text=🐹+GopherOS)

**Un kernel x86 de 32 bits didáctico, completo y funcional**

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()
[![C](https://img.shields.io/badge/C-99-blue)]()
[![Assembly](https://img.shields.io/badge/ASM-NASM-yellow)]()
[![QEMU](https://img.shields.io/badge/QEMU-8.0+-orange)]()
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen)]()

</div>

---

## 📖 Tabla de Contenidos

- [¿Qué es GopherOS?](#qué-es-gopheros)
- [Estado del Proyecto](#estado-del-proyecto)
- [Características](#características)
- [Arquitectura](#arquitectura)
- [Requisitos](#requisitos)
- [Compilación](#compilación)
- [Ejecución](#ejecución)
- [Comandos del Shell](#comandos-del-shell)
- [GopherPy](#gopherpy)
- [Roadmap](#roadmap)
- [Limitaciones Conocidas](#limitaciones-conocidas)
- [Contribuciones](#contribuciones)
- [Licencia](#licencia)

---

## 🎯 ¿Qué es GopherOS?

GopherOS es un **kernel x86 de 32 bits didáctico** y funcional, escrito en C y ensamblador, que demuestra conceptos fundamentales de sistemas operativos como **paginación**, **protección de memoria (ring 3)**, **sistema de archivos**, **drivers** (VGA, teclado, ATA, red) y **syscalls**. Es un proyecto ideal para aprender OSDev con código real que compila y corre en QEMU.

**Filosofía del proyecto:**
- 🔬 **Didáctico**: Cada componente está documentado y probado
- 🛠️ **Funcional**: Corre de verdad, no es un ejercicio teórico
- 🧩 **Modular**: Arquitectura clara y extensible
- 📚 **Demostrativo**: Muestra conceptos como paginación, protección de memoria, syscalls

---

## ✨ Características

### Core del Kernel

| Componente | Estado | Descripción |
|------------|--------|-------------|
| **Boot** | ✅ | Multiboot con GRUB o `qemu -kernel` |
| **GDT/IDT/PIC** | ✅ | Remapeo del PIC 8259, 32 excepciones + 16 IRQs |
| **TSS** | ✅ | Task State Segment para transiciones ring3→ring0 |
| **Paginación** | ✅ | 4KB con permisos por página, aislamiento real |
| **Memoria** | ✅ | Bitmap físico + pools + region allocator |
| **Scheduler** | ✅ | Cooperativo con `yield`/`sleep_on`/`wakeup` |
| **Syscalls** | ✅ | 15 estables, ABI fija, `ioctl` como extensión |

### Drivers

| Componente | Estado | Descripción |
|------------|--------|-------------|
| **VGA Texto** | ✅ | 80x25 modo texto |
| **VGA Gráfico** | ✅ | Modo 13h (320x200x256), reprogramación manual |
| **Teclado PS/2** | ✅ | IRQ1, scancode set 1, shift, backspace |
| **RTC/CMOS** | ✅ | Fecha/hora real del hardware |
| **ATA PIO** | ✅ | Disco duro LBA28, polling, detección de presencia |
| **PCI** | ✅ | Enumeración y espacio de configuración |
| **RTL8139** | ✅ | NIC detectada por hardware |
| **Red** | ✅ | ARP, IPv4, ICMP — `ping` funciona |
| **TCP** | ✅ | Mínimo (una conexión a la vez) |
| **Servidor Gopher** | ✅ | RFC 1436, probado con cliente externo |

### Filesystem

| Componente | Estado | Descripción |
|------------|--------|-------------|
| **Árbol de directorios** | ✅ | `mkdir`/`rmdir`/`cd`/`pwd` reales |
| **Persistencia** | ✅ | `save`/`load` a disco, probado con reinicio |
| **Sector seguro** | ✅ | LBA 2048 (offset 1 MiB) |

### Ring 3 (Multiusuario)

| Capacidad | Estado | Descripción |
|-----------|--------|-------------|
| **Transición de privilegio** | ✅ | CPL0↔CPL3 real |
| **Syscalls desde ring3** | ✅ | `int 0x80` funciona desde CPL3 |
| **Aislamiento de fallos** | ✅ | Un proceso no tumba el kernel |
| **Aislamiento memoria** | ✅ | Kernel vs procesos, proceso vs proceso |
| **Stack kernel por proceso** | ✅ | TSS actualizado en cada cambio de contexto |

### Shell

25+ comandos incluyendo:
- 📁 `ls/dir`, `cd`, `pwd`, `mkdir`, `rmdir`
- 📄 `cat/type`, `rm/del`, `mv/ren`, `cp/copy`
- 🕐 `date`, `time`, `ver`, `vol`
- 🎨 `demo` (gráficos VGA)
- 🌐 `ping`, `gopherserve`
- 📝 `edit`, `run` (scripts)
- 🐍 `gopherpy`
- 🔒 `ring3demo`, `ring3mem`, `ring3victim`, `ring3attack`

### GopherPy

- ✅ Traductor Python-como → C nativo
- ✅ ABI mínima (`gopheros_abi.h`)
- ✅ 7 bugs reales arreglados
- ✅ Primer programa corriendo de punta a punta

---

## 🏗️ Arquitectura

```
┌─────────────────────────────────────────────────────────────┐
│                         GopherOS                            │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │   Shell      │  │  GopherPy   │  │   Servidor Gopher   │ │
│  │   (ring3)    │  │  (ring3)    │  │     (ring3)         │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    Syscall Layer (int 0x80)             │ │
│  └─────────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    Kernel Core                          │ │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐  │ │
│  │  │Memory   │ │Process  │ │Scheduler│ │Filesystem   │  │ │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    Drivers Layer                        │ │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌────────────┐  │ │
│  │  │VGA   │ │PS/2  │ │ATA   │ │PCI   │ │RTL8139/TCP │  │ │
│  │  └──────┘ └──────┘ └──────┘ └──────┘ └────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    Hardware Abstraction                  │ │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌────────────┐  │ │
│  │  │GDT   │ │IDT   │ │PIC   │ │PIT   │ │Paging      │  │ │
│  │  └──────┘ └──────┘ └──────┘ └──────┘ └────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## 📦 Requisitos

### Ubuntu/Debian
```bash
sudo apt-get install gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso mtools make
```

### Arch Linux
```bash
sudo pacman -S gcc-multilib nasm qemu-system-x86 grub xorriso mtools make
```

### macOS (con Homebrew)
```bash
brew install i686-elf-gcc nasm qemu xorriso mtools make
```

---

## 🔧 Compilación

```bash
# Clonar el repositorio
git clone https://github.com/germanpet-svg/GopherOS.git
cd GopherOS

# Compilar el kernel
make

# Generar ISO booteable
make iso

# Limpiar archivos de compilación
make clean
```

---

## 🏃 Ejecución

### Entorno Gráfico (Linux, Windows, Mac)
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32
# o con ISO:
qemu-system-i386 -cdrom build/gopheros.iso -m 32
```
⚠️ **Importante**: Haz click en la ventana para capturar el teclado.

### Terminal Pura (SSH sin entorno gráfico)
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32 -display curses
```

### Con Disco (Persistencia)
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32 -hda disk.img
# o usando make:
make run-disk
```

### Con Red (Servidor Gopher)
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32 \
  -netdev user,id=net0,hostfwd=tcp::7070-:70 \
  -device rtl8139,netdev=net0
```

### Solo Depuración (sin interacción)
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32 -serial stdio -display none
```

### Debug con GDB
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32 -s -S &
gdb build/gopheros.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

---

## 🖥️ Comandos del Shell

### Navegación y Archivos

| Comando | Descripción | Alias DOS |
|---------|-------------|-----------|
| `ls` | Lista archivos | `dir` |
| `cd` | Cambia directorio | - |
| `pwd` | Muestra directorio actual | - |
| `mkdir` | Crea directorio | - |
| `rmdir` | Elimina directorio vacío | - |
| `cat` | Muestra contenido | `type` |
| `rm` | Elimina archivo | `del` |
| `mv` | Mueve/renombra | `ren` |
| `cp` | Copia archivo | `copy` |

### Sistema

| Comando | Descripción |
|---------|-------------|
| `date` | Muestra fecha |
| `time` | Muestra hora |
| `ver` | Versión del kernel |
| `vol` | Volumen del disco |
| `clear` | Limpia pantalla | `cls` |
| `ps` | Lista procesos |
| `jiffies` | Ticks del timer |

### Red

| Comando | Descripción |
|---------|-------------|
| `ping` | Test ICMP |
| `gopherserve` | Inicia servidor Gopher en puerto 70 |

### Desarrollo

| Comando | Descripción |
|---------|-------------|
| `demo` | Demostración gráfica VGA |
| `gopherpy` | Ejecuta programa GopherPy |
| `edit` | Editor de línea |
| `run` | Ejecuta script de comandos |

### Ring 3 (Demostración)

| Comando | Descripción |
|---------|-------------|
| `ring3demo` | Demuestra CPL3 real (falla con `cli`) |
| `ring3mem` | Demuestra aislamiento de memoria |
| `ring3victim` | Proceso víctima para ataque |
| `ring3attack` | Proceso atacante (falla) |

### Miscelánea

| Comando | Descripción |
|---------|-------------|
| `help` | Muestra ayuda |
| `about` | Información del sistema |
| `exit` | Sale del shell |

---

## 🐍 GopherPy

GopherPy es un **traductor de Python-como a C nativo** que genera código que corre directamente sobre la ABI de GopherOS, sin librerías intermedias.

### Sintaxis Soportada

```python
# Variables
x = 42
nombre = "GopherOS"

# If/else
if x > 10:
    print("x es grande")
else:
    print("x es pequeño")

# For/range
for i in range(5):
    print(i)

# Llamadas a la ABI
gos_screen_mode(GOS_VIDEO_MODE_VGA256)
gos_pset(160, 100, 14)
gos_print("Hola desde GopherPy")
```

### Ejemplo Completo: `demo.py`

```python
# demo.py - Demostración de GopherPy
x = 42

if x > 10:
    gos_print("x es grande\n")
else:
    gos_print("x es pequeño\n")

gos_print("Contando con un for real (range):\n")
for i in range(5):
    gos_print(str(i) + "\n")

gos_print("Probando el modo gráfico VGA 320x200x256...\n")
gos_screen_mode(GOS_VIDEO_MODE_VGA256)

# Dibujar rectángulos
for y in range(0, 200, 20):
    for x in range(0, 320, 20):
        color = (x + y) % 256
        gos_rect(x, y, 20, 20, color)

gos_print("Listo.\n")
gos_screen_mode(GOS_VIDEO_MODE_TEXT)
```

### Uso

```bash
# Traducir Python a C
./gopherpy demo.py -o demo.c

# El archivo demo.c generado se compila e integra al kernel
make
# ... y luego desde el shell de GopherOS:
gopheros:/> gopherpy
```

---

## 🗺️ Roadmap

### ✅ Hecho y Probado

- [x] Boot Multiboot
- [x] GDT/IDT/PIC con remapeo
- [x] Paginación 4KB con permisos
- [x] Scheduler cooperativo
- [x] 15 syscalls estables
- [x] VGA texto y gráfico (modo 13h)
- [x] Teclado PS/2
- [x] RTC/CMOS
- [x] ATA PIO (disco duro)
- [x] Filesystem jerárquico con persistencia
- [x] Shell con 25+ comandos
- [x] Ring 3 (CPL3) con aislamiento real
- [x] GopherPy (traductor Python→C)
- [x] PCI + RTL8139
- [x] ARP/IP/ICMP (ping)
- [x] TCP mínimo
- [x] Servidor Gopher RFC 1436
- [x] Editor de línea (`edit`)
- [x] Scripts de comandos (`run`)

### 🚧 En Progreso / Próximos Pasos

| Prioridad | Tarea | Tamaño | Dependencias |
|-----------|-------|--------|--------------|
| 1 | Arreglar TCP multi-conexión | Chico | - |
| 2 | Cargador de programas real (ELF) | Mediano | - |
| 3 | `fork()`/`exec()` | Mediano | #2 |
| 4 | Patrón Connector+Plug | Mediano | - |
| 5 | Driver ATA con IRQ (polling→IRQ14) | Mediano | - |
| 6 | Inicializar FPU + soporte float | Chico | - |
| 7 | USB (UHCI/EHCI + almacenamiento) | Grande | - |
| 8 | Preemptive scheduling | Grande | - |

### 💡 Ideas Futuras

- [ ] Soporte para múltiples terminales (TTY)
- [ ] Driver de red completo (socket/send/recv)
- [ ] VFS con inodos
- [ ] `mmap`/`munmap`
- [ ] Sistema de archivos FAT32 real
- [ ] Compilador C embebido
- [ ] Port de GWBASIC

---

## ⚠️ Limitaciones Conocidas

| Limitación | Detalle | Plan |
|------------|---------|------|
| **Sin cargador de programas** | Todos los procesos compilados dentro del kernel | Roadmap #2 |
| **TCP limitado** | Una conexión confiable por sesión | Roadmap #1 |
| **Memoria física fija** | Arena estática de 8MB | Leer mapa de Multiboot |
| **Sin FPU** | `float` no soportado | Roadmap #6 |
| **Sin USB** | Leer pendrive requiere controlador | Roadmap #7 |
| **Sin `fork()`** | `ENOSYS` en syscall | Roadmap #3 |
| **Sin preemption** | Scheduler cooperativo | Roadmap #8 |

---

## 🤝 Contribuciones

¡Las contribuciones son bienvenidas! Por favor:

1. **Fork** el repositorio
2. Crea una **rama** para tu feature (`git checkout -b feature/nueva-funcionalidad`)
3. **Commit** tus cambios (`git commit -m 'Añadir: nueva funcionalidad'`)
4. **Push** a la rama (`git push origin feature/nueva-funcionalidad`)
5. Abre un **Pull Request**

### Estilo de Código

- C99 con `-Wall -Wextra -Werror`
- Ensamblador NASM
- Comentarios en español o inglés
- Documentación para nuevas funcionalidades

---

## 📄 Licencia

Este proyecto está licenciado bajo la **MIT License** - ver el archivo [LICENSE](LICENSE) para más detalles.

---

## 🙏 Agradecimientos

- **OSDev Wiki** - Documentación invaluable
- **QEMU** - Emulación y depuración
- **GCC** y **NASM** - Herramientas de compilación
- **GRUB** - Bootloader

---

<div align="center">

**🐹 GopherOS — Un kernel x86 didáctico, completo y funcional**

[⬆ Volver arriba](#gopheros)

</div>
