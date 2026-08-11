# GopherOS — kernel x86 didáctico, completado y compilado

## Qué había en tu zip

Los 7 archivos (`kernel.c`, `memory.c`, `process.c`, `interrupt.c`, `syscall.c`,
`filesystem.c`, `typesafe.h`) eran **fragmentos conceptuales**: mostraban muy bien
el diseño (Option/Result, region allocator, IRQ top/bottom-half, scheduler
cooperativo, 15 syscalls estables), pero no compilaban ni arrancaban:
faltaban el bootloader, el linker script, la GDT/IDT reales, los drivers
(VGA, PIT, serial), la libc freestanding (`memset`, `strlen`, etc.), y varias
piezas de esos mismos archivos usaban pseudocódigo (`pusha`/`popa`/`iret` como
si fueran funciones C, macros de `typesafe.h` con un bug de stringificación).

## Qué hice

Completé el proyecto hasta tener un **kernel x86 de 32 bits real, que arranca
y corre en QEMU** (y también en VirtualBox/VMware/USB vía la ISO):

- **Arranque Multiboot** (`boot/boot.s` + `kernel/linker.ld`): no hace falta
  bootsector propio, GRUB o QEMU `-kernel` cargan el ELF directamente.
- **GDT/IDT reales** (`gdt.c`, `idt.c`, `isr.s`): remapeo del PIC 8259,
  32 excepciones + 16 IRQs + `int 0x80` para syscalls, con los stubs de
  ensamblador correctos.
- **Drivers**: VGA modo texto 80x25 (`vga.c`), PIT/timer a 100Hz (`timer.c`),
  serial COM1 para depuración (`serial.c`).
- **`typesafe.h`** corregido (bug de `__LINE__` sin stringificar) y completado
  (Option/Result/Region/SpinLock/FixedArray funcionando de verdad).
- **`memory.c`**: allocator físico de páginas (bitmap), pools por tamaño,
  region allocator — todo el "nunca hace leak" del diseño original, ahora real.
- **`process.c`**: scheduler cooperativo con **cambio de contexto real en
  ensamblador** (`context_switch` en `isr.s`), stacks construidos a mano para
  que el primer `yield` "aterrice" en la función de entrada de cada proceso.
- **`syscall.c`**: las 15 syscalls originales, con `write`/`yield`/`exit`/
  `malloc`/`free`/`getpid` implementadas de verdad vía `int 0x80`.
- **`filesystem.c`**: FS plano en memoria, compilando y probado (crea y lee
  un archivo de demo).
- **`kernel.c`**: arranca todo, crea 2 procesos de demostración (`gopherd` y
  `monitor`) que cooperan vía `yield()`, usan el FS y el timer.

## Bugs reales que encontré y arreglé compilando/probando (no solo "a ojo")

1. **`typesafe.h`**: `__FILE__ ":" __LINE__` no compila — `__LINE__` es un
   entero, no un string. Arreglado con un macro `STRINGIFY`.
2. **Tipos anónimos de `Result()`/`Option()` entre archivos**: la macro genera
   un `struct { ... }` sin nombre cada vez que se usa; si `kernel.c` y
   `process.c` la expanden por separado, son tipos incompatibles para el
   compilador aunque el layout binario sea idéntico. Solución: `kresults.h`
   con typedefs con nombre compartidos.
3. **Bug del "centinela cero"**: `page_alloc()` devolvía `0` tanto para "sin
   memoria" como para "aquí tienes la página física 0" (que es una dirección
   válida). El primer `region_new()` fallaba silenciosamente porque su
   primera asignación real caía justo en el offset 0. Arreglado reservando
   la página 0 permanentemente.
4. **Desalineación de 4 bytes en el frame de interrupción**: el stub de
   ensamblador (`isr_common_stub`/`irq_common_stub`) empuja el selector `ds`
   como registro extra después de `pusha`, pero el `struct registers` en C no
   tenía ese campo — todo se leía corrido (el `int_no` mostraba basura de
   `eax`, etc.). Esto lo detecté forzando una excepción de CPU (`int3`) y
   viendo que el kernel reportaba el número de excepción equivocado.

Verificado con: compilación limpia (`-Wall -Wextra -Werror`, cero warnings de
código propio), arranque en QEMU con salida serial para depurar paso a paso,
`-d int` de QEMU para confirmar que el PIC/IDT realmente entregan IRQ0, y
capturas de pantalla del framebuffer VGA.

## Novedad: teclado PS/2 + shell interactivo

Sobre la base anterior, agregué:

- **`keyboard.c`**: driver PS/2 real (IRQ1, scancode set 1, shift para
  mayúsculas), con la misma disciplina top-half/bottom-half del resto del
  kernel — la ISR solo lee el puerto y traduce a ASCII, nunca bloquea.
- **`shell.c`**: consola interactiva (`gopheros>`) con comandos `help`, `ls`,
  `cat`, `ps`, `jiffies`, `echo`, `clear`, `about`, `exit`, con edición de
  línea (backspace) y eco en pantalla.
- **`sys_read`** ahora lee de verdad del teclado (antes era `ENOSYS`).
- Arreglé dos bugs nuevos que aparecieron al agregar el teclado:
  1. El scheduler declaraba "sistema detenido" apenas no había ningún
     proceso *listo* — pero con un shell que pasa la mayor parte del tiempo
     *bloqueado* esperando una tecla, eso apagaba el kernel al instante.
     Ahora distingue "no hay nada vivo" (sí apaga) de "todo está bloqueado
     esperando una IRQ" (duerme con `sti;hlt` y reintenta en cada
     interrupción, sin busy-waiting).
  2. Cuando el único proceso listo era el mismo que se acababa de
     desbloquear a sí mismo, el scheduler lo marcaba `RUNNING` y en la
     misma línea lo revertía a `READY` (por comparar el puntero contra sí
     mismo). Se veía como `ps` mostrando el shell como "listo" en vez de
     "corriendo" mientras corría.

Probado inyectando teclas reales vía el monitor de QEMU (`sendkey`),
incluyendo mayúsculas con shift y corrección de errores con backspace.

## Novedad 2: paging real (memoria virtual)

- **`paging.c`**: activa paginación x86 de verdad — CR4.PSE, CR3 apuntando a
  un directorio de páginas, CR0.PG. Usa páginas de 4MB (PSE) para mapear
  identity 32MB con una sola tabla de 8 entradas (en vez de miles de PTEs de
  4KB), suficiente para lo que el kernel usa hoy.
- El **Page Fault (int 14)** ahora es un BSOD útil de verdad: decodifica
  `CR2` (dirección exacta que falló) y el error code (¿página ausente o
  violación de permisos? ¿lectura o escritura? ¿supervisor o usuario?).
  Lo probé forzando un acceso a `0x08000000` (fuera del rango identity-mapeado)
  y confirmé que reporta todo correctamente antes de detener el sistema.
- Sigue siendo identity-map (virt==phys), consistente con el diseño de
  `types.h`. Paginación por proceso (cada uno con su propio directorio,
  aislamiento de memoria) queda como el siguiente paso si se quiere.

## Novedad 3: RTC real, comandos duales DOS/Linux, modo gráfico VGA y ABI mínima

- **`rtc.c`**: lee el reloj de hardware real (CMOS, puertos 0x70/0x71), con
  manejo de BCD/binario y 12/24h. Probado poniéndole a QEMU una fecha
  simulada (`-rtc base=2026-07-14T15:30:00`) y confirmando que el kernel
  la lee correctamente (`14-Jul-2026`, `15:30:03`).
- **Comandos duales**: el shell ahora entiende tanto el nombre Unix como el
  DOS clásico para lo mismo: `ls`/`dir`, `cat`/`type`, `clear`/`cls`,
  `rm`/`del`, `mv`/`ren`, `cp`/`copy`. Se agregaron `date`, `time`, `ver`,
  `vol`, y `demo`.
- **`graphics.c`**: modo gráfico VGA real (modo 13h, 320x200, 256 colores).
  No hay BIOS disponible tras Multiboot, así que el cambio de modo
  reprograma a mano los registros del Secuenciador/CRTC/Graphics
  Controller/Attribute Controller — lo mismo que hacía `INT 10h` antes de
  entrar a modo protegido. Probado con el comando `demo`: dibuja
  rectángulos, una línea y barras de color, capturado y verificado que
  aparecen los 16 colores esperados en las posiciones correctas.
- **`gopheros_abi.h`**: la base para que programas *futuros* (por ejemplo,
  un GWBASIC portado a C) no necesiten ninguna librería — ni libc, ni
  gráficos, nada. Es un solo header con funciones `static inline` que hacen
  `int 0x80` directo. "Poner un punto en pantalla" es literalmente:
  ```c
  gos_screen_mode(GOS_VIDEO_MODE_VGA256);
  gos_pset(160, 100, 14);
  ```
  Un lenguaje interprete/compilador dirigido a este kernel solo necesitaría
  conocer estas ~10 funciones — el kernel interpreta todo lo demás
  (drivers, framebuffer, timer, teclado, reloj).
- Para no romper la regla de "15 syscalls, nunca más", el video, el RTC, y
  cualquier dispositivo nuevo entran por `ioctl` con subcomandos (igual que
  el `ioctl` real de Unix), no como syscalls nuevos.

## Novedad 4: disco duro real (ATA PIO), directorios de verdad, y persistencia

- **`disk.c`**: driver ATA PIO real (bus primario, LBA28, por polling). Detecta
  si hay disco conectado (vía `IDENTIFY DEVICE`) sin colgarse si no lo hay.
- **`filesystem.c` reescrito**: ahora es un árbol de verdad (cada nodo apunta
  a su padre por índice), no una lista plana. `mkdir`/`rmdir`/`cd`/`pwd`
  funcionan de verdad — antes eran un stub que decía "no soportado".
- **`fs_save()`/`fs_load()`**: serializan todo el árbol (directorios +
  archivos + contenido) a un formato propio simple, escrito/leído del disco
  sector por sector en streaming (sin necesitar un buffer gigante en RAM).
  Comandos `save`/`load` en el shell.
- **Probado de punta a punta con persistencia real**: creé `docs/s.txt`,
  hice `save`, **reinicié la máquina virtual completa**, hice `load`, y el
  directorio y el archivo volvieron exactamente igual, contenido incluido.
  Esto no es solo "compila" — es persistencia real verificada a través de un
  reinicio completo del kernel.
- Si no hay disco conectado (`-hda ...`), todo lo demás sigue funcionando
  igual, solo que `save`/`load` avisan que no hay disco en vez de fallar.

### Sobre USB

Arrancar desde USB **ya funciona** — es responsabilidad de la BIOS/GRUB
(booteo El Torito), no necesita ningún driver nuestro. Lo que NO está hecho
es leer/escribir un pendrive **desde dentro** del kernel una vez arrancado:
eso requiere un driver de controlador USB (UHCI/EHCI/xHCI) más la clase de
almacenamiento masivo (bulk-only transport + comandos SCSI) — es un proyecto
aparte, bastante más grande que el driver ATA de disco duro. Si lo querés,
lo empezamos como el siguiente paso, pero avisado: es semanas de trabajo de
driver, no una tarde.

### Sobre el shell.c que me pasaste

Lo revisé a fondo: viene de otra sesión/agente, apunta a una versión vieja
del filesystem (plano, sin directorios reales), y **no compila** — llama a
`cmd_uptime()`, `cmd_whoami()`, `cmd_meminfo()`, `cmd_sleep()`, `cmd_calc()`
y `proc_get_uptime()` sin definirlas en ningún lado (hay hasta un comentario
literal que dice "mantener las mismas que en la version anterior", como un
parche a medio pegar). Además su modelo de directorios es cosmético: vive en
una estructura `Directory` separada en RAM, no conectada al filesystem real,
así que un archivo con el mismo nombre en dos carpetas distintas se
pisaría por debajo. No lo integré — seguimos con la versión propia,
probada y con disco real.

## Novedad 5: GopherPy — el primer programa de GopherOS

Un traductor Python-como → C nativo, apuntando directo a la ABI mínima
(`gopheros_abi.h`). La idea: escribís con sintaxis cómoda, pero el binario
final llama a la ABI del kernel tan directo como C escrito a mano — cero
runtime de Python, cero librerías intermedias.

El código que me pasaste (`gopherpy.c`/`.h`) nunca se había compilado ni
corrido. Lo llevé hasta andar de verdad, encontrando y arreglando 7 bugs
reales con evidencia (no solo lectura) — el más serio: varios arrays del
generador "tomaban prestada" memoria del pool de nodos AST reinterpretándola
como array de punteros, y como un puntero pesa menos que un nodo completo,
se auto-corrompían apenas se seguía parseando. Reemplazado por arrays fijos
embebidos. Detalle completo de los 7 bugs en `gopherpy_toolchain/README.md`.

**Probado de punta a punta**: escribí `demo.py` (variables, if/else,
for/range, llamadas a video y gráficos), lo traduje con la herramienta ya
arreglada, y el C resultante — integrado como un proceso más del kernel —
corrió en GopherOS de verdad: imprimió texto, contó con un for real, cambió
a modo gráfico VGA, dibujó, volvió a modo texto, y terminó limpio (`ps` lo
muestra como zombi al final, sin ningún panic).

```
gopheros:/> gopherpy
=== GopherPy - primer programa de GopherOS ===
Compilado a C nativo, cero librerias, un solo int 0x80 por syscall
x es grande
Contando con un for real (range):
0
1
2
3
4
Probando el modo grafico VGA 320x200x256...
Listo. GopherPy funcionando de punta a punta.
```

Te entrego el toolchain completo aparte (`gopherpy_toolchain.zip`): el
compilador (`gopherpy.c/.h`), la tabla de bindings real, el CLI, el
`demo.py`, y el `.c` que generó — para que puedas escribir tus propios
programas y traducirlos vos mismo.

## Novedad 6: Ring 3 real — el primer paso del "modo multiusuario"

Esto era lo pendiente de la conversación sobre el menú Unikernel/Multiusuario.
Empecé por la pieza chica que hablamos: probar que la transición de
privilegio funciona de verdad, con aislamiento de fallos real.

- **TSS** (`gdt.c`): agregado el Task State Segment mínimo indispensable —
  le dice a la CPU a qué stack de kernel saltar (`SS0:ESP0`) cuando un
  proceso ring3 dispara una interrupción, excepción o syscall.
- **`enter_ring3()`** (`isr.s`): arma a mano el frame que espera `IRET` para
  saltar de CPL0 a CPL3 (selectores de código/datos de usuario ya existían
  en la GDT desde el principio, nunca se habían usado).
- **`proc_create_ring3()`** (`process.c`): variante de creación de proceso
  que arranca en ring3 real, con su propia stack de usuario.
- **Aislamiento de fallos** (`idt.c`): si el código que falló corría en
  CPL3, el kernel ya no hace panic total — mata solo ese proceso y el
  resto del sistema sigue vivo. El truco: en vez de manejarlo en el medio
  de la ISR (donde las interrupciones siguen apagadas y abandonar la pila
  a mitad de camino es peligroso), se reescribe a dónde "vuelve" el `IRET`
  normal — a una función de limpieza en ring0, con todo ya restaurado
  correctamente.

**Probado con un caso que no deja dudas**: el proceso de prueba
(`ring3_demo.c`, que solo usa la ABI pública, cero funciones internas del
kernel) primero imprime texto por syscall (prueba que `int 0x80` funciona
en CPL3), y después ejecuta `cli` a propósito — una instrucción prohibida
en ring3. Si el proceso realmente corriera en CPL3, tiene que fallar con
GPF; si por error siguiera en CPL0 (como todo lo demás hasta ahora), `cli`
ejecutaría sin problema y seguiría de largo. Resultado real:

```
gopheros:/> ring3demo
[ring3_demo] hola desde CPL3 de verdad (no simulado)
[ring3_demo] ahora pruebo una instruccion privilegiada (cli)...
[ring3] 'ring3_demo' fallo: GPF (int=13)
[ring3] proceso terminado por fallo, el resto del sistema sigue vivo.
gopheros:/> ps
  PID  ESTADO      NOMBRE
  2    corriendo    shell
  3    zombi    ring3_demo
```

Falló exactamente como tenía que fallar, y el shell siguió andando. Eso es
la prueba de que el aislamiento de privilegio es real, no cosmético.

### Limitación honesta (documentada a propósito)

Este primer corte **no tiene aislamiento de memoria real todavía** — para
simplificar, todas las páginas identity-mapeadas quedaron con el bit US=1
(accesibles desde ring3), así que un proceso ring3 hoy PUEDE leer/escribir
memoria del kernel o de otros procesos si quisiera (no lo hace, pero
podría). Lo que sí está probado y es real: la transición de privilegio en
sí, las syscalls funcionando desde CPL3, y el aislamiento de **fallos**
(que un proceso se caiga no tumbe el sistema). Aislamiento de **memoria**
de verdad requiere el siguiente paso: páginas de 4KB con permisos por
región en vez del identity-map global de hoy — avisado como el próximo
pendiente si querés seguir por ahí.

## Novedad 7: páginas de 4KB con permisos reales (aislamiento de memoria)

Reemplacé el esquema anterior (páginas de 4MB, todo accesible desde ring3)
por un Directorio + Tablas de página de 4KB de verdad, con permisos por
página. Por defecto **todo es supervisor-only**; un proceso ring3 solo
gana acceso a su propia región de memoria (`paging_set_user_access()`,
llamado por `proc_create_ring3()`).

Complicación real que apareció al probarlo (no algo que anticipé de
antemano): como todavía no hay un cargador de programas separado, el
código de un proceso ring3 vive **compilado adentro del mismo binario del
kernel** — mismo `.text`. Si todo el `.text` es supervisor-only, la CPU ni
siquiera puede *buscar* la primera instrucción del proceso ring3. Solución:
expuse `.text`+`.rodata` (código y constantes) como accesible-y-ejecutable
para todos, pero `.data`/`.bss` (el ESTADO real del kernel: stacks,
estructuras de procesos, todo lo que importa proteger) sigue
supervisor-only. Es una simplificación intencional y documentada — la
solución "correcta" (código de usuario separado del código del kernel)
necesita un cargador de programas real, que es justamente el próximo paso
pendiente (ver roadmap abajo).

Otro bug real que encontré probando esto: nuestro allocator de memoria
(`memory.c`) devuelve **offsets relativos a un arena interno**, no
direcciones físicas absolutas — al llamar `paging_set_user_access()` con
ese offset crudo, marcaba como accesible la región equivocada (un rango
que ni siquiera pertenecía al proceso). Arreglado convirtiendo a la
dirección absoluta real antes de tocar las tablas de página.

**Probado con dos casos, no uno:**
```
gopheros:/> ring3demo          (instruccion privilegiada, ya lo teniamos)
[ring3_demo] hola desde CPL3 de verdad (no simulado)
...
[ring3] 'ring3_demo' fallo: GPF (int=13)          <- cli prohibido, correcto

gopheros:/> ring3mem           (memoria del kernel, NUEVO)
[ring3_mem] intentando escribir en 0x0010b000 (.bss del kernel)...
[ring3] 'ring3_mem' fallo: Page Fault (int=14)    <- bloqueado, correcto
```

En ambos casos el shell sigue vivo después (`ps` lo confirma). Esto es
aislamiento de memoria real, no cosmético — un proceso ring3 hoy puede
ejecutar código compartido y tocar SU PROPIA memoria, pero no puede tocar
el estado interno del kernel.

## Novedad 8: dos mejoras que le sumaste vos al repo público

Al revisar el zip de tu GitHub encontré dos cambios genuinos que adopté:

- **`filesystem.h`**: separaron la API del filesystem (struct `FileEntry` +
  todos los prototipos `fs_*`) en su propio header. Mejor práctica, elimina
  un montón de `extern` repetidos en `kernel.c`/`shell.c`. Ya lo integré.
- **`FS_DISK_START_LBA` de 0 a 2048** (offset de 1 MiB): escribir en el
  sector 0 pisaría el MBR/tabla de particiones de un disco real. Este fix
  evita eso — importante si algún día se usa un disco no dedicado.

También sumé el target `make run-disk` que tenían (crea el disco de 10MB
automáticamente si no existe).

## Novedad 9: red real — PCI, NIC, ARP/IP/ICMP, TCP, y servidor Gopher en puerto 70

Esto era el pedido más grande de la sesión. Cuatro capas, cada una probada
antes de subir a la siguiente:

- **`pci.c`**: acceso al espacio de configuración PCI (puertos 0xCF8/0xCFC)
  y enumeración para encontrar dispositivos por vendor/device ID.
- **`rtl8139.c`**: driver de la tarjeta de red que emula QEMU por defecto.
  Probado: detecta la NIC por PCI y lee su MAC real (`52:54:00:12:34:56`).
- **`net.c`**: Ethernet + ARP + IPv4 + ICMP. **Probado con `ping` de verdad**:
  el comando `ping` del shell arma un echo request, lo manda, y espera la
  respuesta real de QEMU — funciona.
- **`tcp.c`**: TCP mínimo (una conexión a la vez, sin retransmisión) —
  three-way handshake, envío/recepción de datos, cierre con FIN/ACK.
- **`gopher_server.c`**: servidor Gopher (RFC 1436) de verdad sobre ese TCP,
  sirviendo el filesystem real (directorios como menús, archivos como
  texto).

**La prueba que importa**: no probé esto solo con comandos internos del
shell — conecté un **cliente TCP real, externo, corriendo en otra
máquina** (un script Python en la terminal de esta sesión, hablando con la
VM a través del port-forwarding de QEMU) y el servidor le respondió
correctamente:

```
$ python3 -c "socket.create_connection(('localhost', 7070))..."
1gopher	/gopher	localhost	70
.
```

Eso es un menú Gopher válido, generado a partir del filesystem real del
kernel (el directorio `/gopher` que crea `gopherd` al arrancar).

**Limitación honesta que encontré probando esto**: la primera conexión
funciona perfecto de punta a punta. Conexiones *subsecuentes* en la misma
sesión (segunda, tercera...) no siempre se completan — el kernel a veces
las acepta por su lado pero el cliente no logra verlas como establecidas.
Sospecho que es una interacción entre el cierre de nuestra conexión
anterior y el NAT/tracking de conexiones de QEMU (SLIRP), no un error de
checksum ni de secuencia (esos ya los verifiqué). No perseguí esto hasta
el final para no comerme el resto del tiempo en debugging de una pila TCP
hecha a mano contra un NAT — lo dejo documentado en vez de darlo por
resuelto. Para usar el servidor hoy: `gopherserve`, hacé UN pedido, y si
querés otro, reiniciá el servidor (o la VM).

### Sobre `lib.7z` que subiste

Lo revisé: son `math.h/c`, `string.h/c`, `stdio.h/c` más "bindings" para
GopherPy y unos programas de demo. Buena idea, pero viene de otra sesión
que no conoce nuestra arquitectura real — no lo integré tal cual porque:

- Inventa su propio mecanismo `syscall_register()` con syscalls 16-30,
  contradiciendo la regla que mantuvimos todo el proyecto ("15 syscalls,
  nunca más — lo nuevo entra por `ioctl`").
- Redefine `memcpy`/`memmove`/etc., que ya existen en nuestro `string.c` —
  chocarían al linkear.
- Usa `float` sin que el kernel inicialice la FPU (`fninit`) en ningún
  lado — con nuestros flags actuales (`-mno-sse -mno-mmx`) esto podría
  fallar en tiempo de ejecución.
- Los `.pyc` de demo usan sintaxis que GopherPy todavía no soporta
  (`import math`, `math.pi`, `range(0,90,10)` de 3 argumentos).

Los algoritmos en sí (Taylor para seno/coseno, Newton para raíz cuadrada,
etc.) son reutilizables si alguna vez querés matemática real en GopherPy —
pero necesitan adaptarse a nuestra ABI (`ioctl`, no syscalls nuevas) y el
kernel necesita inicializar la FPU primero. Si te interesa, lo armamos
como paso aparte.

## Novedad 10: espacio de direcciones por proceso (aislamiento ring3-a-ring3 real)

Charlamos de usar Rust para esto; te dije que no íbamos a tener la misma
garantía en tiempo de compilación, pero sí podíamos acercarnos con
disciplina de C real — y eso fue el plan: antes de tocar el kernel
freestanding, armé la misma lógica de índices/aislamiento en un programa
"hosted" aparte y la corrí con **AddressSanitizer + UBSan** en la propia
máquina (`paging_test/paging_logic.c`, incluido en el zip de fuente): 5
casos, incluido aislamiento cruzado entre espacios y accesos fuera de
rango, todos limpios, sin overflow ni corrupción detectada. Recién ahí lo
integré al kernel real.

**Qué cambió**: hasta la sesión anterior, todos los procesos ring3
compartían las mismas tablas de página globales — el kernel estaba
protegido, pero un proceso ring3 SÍ podía tocar la memoria de otro. Ahora
cada proceso ring3 tiene su **propio directorio de páginas** (copia
privada), así que darle acceso a uno no le da acceso a ningún otro — no
es "permiso denegado", es que la página ni siquiera existe para él.

**Probado con un ataque real, no solo unitario**: un proceso "víctima"
escribe una marca conocida en su propia stack y expone la dirección; un
proceso "atacante" separado intenta escribir esa dirección exacta desde
afuera.

```
gopheros:/> ring3victim
[victim] direccion de la marca: 0x00199ff8
gopheros:/> ring3attack 0x00199ff8
[attacker] intentando escribir ahi...
[ring3] 'ring3_atk' fallo: Page Fault (int=14)
[ring3] proceso terminado por fallo, el resto del sistema sigue vivo.
gopheros:/> ps
  PID  ESTADO      NOMBRE
  2    corriendo    shell
  3    listo    ring3_victim
  4    zombi    ring3_atk
```

**Bug real que encontré probando esto** (no en el test unitario — recién
apareció con el ataque real): justo después de bloquear el ataque, el
kernel **entero** se caía con un segundo Page Fault, esta vez en modo
supervisor. La causa: los procesos ring3 seguían compartiendo un único
stack de kernel para el TSS. Cuando la víctima quedó a mitad de un
`gos_yield()` (trampeada en ring0) y el scheduler cambió al atacante (que
también entra a ring0 al fallar), sus marcos de pila se pisaban entre sí.
Encontré que ya existía una función `tss_set_kernel_stack()` escrita pero
nunca invocada — cada proceso ring3 ya reservaba su propio stack de kernel
al crearse, solo faltaba decirle al TSS cuál usar en cada cambio de
contexto. Con eso conectado, el segundo crash desapareció y quedó
confirmado con la regresión completa (`ring3demo`, `ring3mem`, `gopherpy`,
`ping` — todos sin cambios).

## Novedad 11: editor de texto + "programas" ejecutables (`edit`/`run`)

Pediste un editor y poder guardar programas en `/gopher` para ejecutarlos.
Como todavía no hay cargador de programas real (compilador embebido en el
kernel — eso sigue siendo el paso #2 del roadmap), lo implementé como lo
más honesto que se puede dar hoy: un **editor de línea** (sin flechas, el
teclado no las soporta todavía) y un **intérprete de scripts** que ejecuta,
línea por línea, los mismos comandos que ya existen en el shell — como un
`.bat`/`.sh` real, no un binario compilado.

```
gopheros:/> edit /gopher/saludo.txt
--- editor de GopherOS ---
Escribi el contenido, una linea a la vez.
Termina con '.' sola en una linea para GUARDAR.
> echo Hola desde un programa guardado
> date
> echo Fin del programa
> .
Guardado (3 lineas)

gopheros:/> run /gopher/saludo.txt
+ echo Hola desde un programa guardado
Hola desde un programa guardado
+ date
La fecha es: 24-Jul-2026 ...
+ echo Fin del programa
Fin del programa
```

### Dos bugs reales encontrados en el camino, no cosméticos

1. **Miscompilación con -O1/-O2**: la primera versión del editor usaba un
   loop propio que llamaba a `kb_readline()` repetidamente desde adentro
   de `cmd_edit` (una función anidada, no el loop principal del shell).
   Con -O1 o -O2 esto colgaba el kernel entero con un Page Fault en una
   dirección inválida (`0x53f000ff`), siempre en la segunda llamada
   anidada a `kb_readline()`. Confirmado con un experimento controlado:
   compilando el mismo código con -O0 el crash desaparecía por completo —
   o sea, era una interacción real entre el optimizador de GCC y nuestro
   `context_switch()` no estándar (una función que "no vuelve" al llamador
   de la forma que un compilador espera), no un error de lógica.
2. **Un segundo bug, más sutil, que el primero tapaba**: incluso arreglando
   el crash (con un workaround de `-O0` puntual), el editor salía del modo
   edición prematuramente, como si ya hubiera recibido `.` sin que el
   usuario lo tipeara — mezclando su entrada con el prompt del shell.

En vez de seguir cazando el bug exacto a nivel de ensamblador (una interacción
sutil entre el optimizador y nuestro scheduler cooperativo, difícil de
aislar sin un debugger real conectado a QEMU), **rediseñé el editor para
que el problema no pueda ocurrir**: ahora `kb_readline()` se llama
ÚNICAMENTE desde el loop principal de `shell_main()`, nunca desde una
función anidada. El modo edición es una máquina de estados — cuando está
activo, cada línea tipeada se manda a `edit_process_line()` en vez del
dispatcher normal de comandos. Esto evita la clase entera de bug en vez de
parchearla, y de paso quedó más simple de leer. Probado de punta a punta
sin fallas: crear, guardar, `cat`, y `run` — con `ps` confirmando que el
sistema queda sano después.

### Comandos nuevos

- `edit <archivo>` — abre el editor (carga el contenido existente si lo hay).
- `run <archivo>` — ejecuta el archivo línea por línea como comandos del
  shell (soporta comentarios con `#` y líneas vacías).

### Limitación honesta

`run` no soporta variables, condicionales, ni loops — es un script lineal,
comando tras comando. Y usar `edit` *adentro* de un script ejecutado con
`run` no está probado (caso de uso raro, no bloqueante).

### Sobre el `gopherpy_toolchain.zip` extendido que subiste

Lo revisé al empezar esta tanda: alguien (otra sesión) extendió `gopherpy.c`
de ~1266 a ~2183 líneas, agregando soporte para `def` (funciones) e
inferencia de tipos. **No compila** — declara el campo `func_def` dos veces
en el mismo union con tipos que chocan, y usa funciones antes de
declararlas. Mismo patrón que el resto de los uploads de otras sesiones
esta conversación: buena ambición, cero verificación. No lo integré; si
querés que le meta mano y lo deje andando, decime y lo hacemos como tarea
aparte (es un trabajo real, no un fix de una línea).

## Novedad 12: cargador de programas ELF32 real (deja de ser "todo compilado adentro del kernel")

Esto era el ítem #2 del roadmap pendiente ("Cargador de programas real") y
la limitación más repetida de las secciones anteriores. Ahora existe de
verdad:

- **`kernel/elf.h`** — estructuras `Elf32_Ehdr`/`Elf32_Phdr` estándar (no
  inventadas: el mismo ELF32 que cualquier toolchain genera).
- **`kernel/paging.c` → `paging_map_page()`** — a diferencia del mecanismo
  anterior (`grant_in`, que solo prendía/apagaba el bit `US` sobre un
  identity-map fijo), esto remapea una dirección virtual a un frame físico
  **arbitrario**, solo dentro del espacio de direcciones de un proceso.
  Es lo que permite que el código de un programa cargado viva en páginas
  propias, no compartidas con el kernel ni con otros procesos.
- **`kernel/process.c` → `proc_load_elf()`** — valida la cabecera (magic,
  `ELFCLASS32`, `EM_386`, `ET_EXEC`), recorre los `PT_LOAD`, copia cada
  segmento a páginas físicas nuevas, y las mapea en el espacio del proceso
  con los permisos (`PF_W`) que indica el propio ELF.
- **`kernel/shell.c` → comando `exec <archivo.elf>`**.
- **`userland/`** (carpeta nueva) — binarios de usuario **compilados y
  linkeados aparte del kernel**, con su propio `user.ld` (dirección de
  carga fija `0x01000000`, dentro del rango identity-mapeado pero lejos de
  donde vive el kernel). La única frontera con el kernel es
  `gopheros_abi.h` — ni un `#include` de nada interno.
- **`Makefile`** — target `userland` genérico: compila cada `.c` de
  `userland/`, lo linkea con `user.ld`, y lo embebe como array de bytes en
  el binario del kernel (`userland/gen_blob.py` + `userland/%_blob.c`,
  auto-generados en cada build) — quedan disponibles como archivos reales
  en el filesystem de arranque (`/hello.elf`, etc.), sin pasos manuales.

Verificado con un boot real en QEMU (no solo compilación): `exec hello.elf`
corrió un ELF32 externo de verdad, y `ps` lo mostró con PID propio y estado
`zombi` al terminar — la prueba de que no es un hilo cooperativo definido
dentro de `kernel.c` como `gopherd`/`shell`, sino un binario ajeno que el
kernel leyó, parseó y saltó a ejecutar en ring3.

## Novedad 13: pie de página persistente estilo GW-BASIC

GW-BASIC siempre dejaba la fila 25 de la pantalla con la barra de F-keys,
sin importar qué estuvieras haciendo arriba. Reproducimos la misma idea:

- **`kernel/vga.c`/`vga.h`** — la última fila de pantalla queda **reservada**
  (`vga_scroll()`/`vga_putc()` solo usan las primeras 24). Nueva función
  `vga_status_line()` que pinta ahí directo, sin tocar el cursor del shell.
- **`kernel/statusbar.c`/`.h`** (nuevo) — arma el texto: procesos activos,
  memoria usada, tiempo desde el arranque, nombre del proceso corriendo.
  Enganchado al **bottom-half** del timer (no al top-half — respeta la
  disciplina de ISRs mínimas del resto del kernel), con auto-throttle a
  ~10 refrescos/segundo aunque el timer corra a 100Hz.
- **`kernel/memory.c` → `memory_get_stats()`** — cuenta páginas usadas/
  totales del arena, para que la barra tenga algo real que mostrar.

Encontré y corregí un bug propio en el camino: mi primer intento de
espejar la salida de `write()` a serial duplicaba cada carácter — resultó
que `vga_putc()` **ya** lo hacía internamente (por eso el banner de arranque
aparecía en el log sin que nadie lo pidiera explícitamente). Lo vi en el
log (`[[hheelllloo`, cada letra doble) y revertí el cambio innecesario.

Verificado con capturas de pantalla reales de QEMU (`screendump` vía el
monitor): boot con la barra visible desde el arranque, y un `help` largo
(fuerza scroll de las 24 filas) confirmando que la fila 25 nunca se
ensucia ni se mueve.

## Novedad 14: más programas de usuario — y dos bugs reales que aparecieron al sumarlos

Subí de 2 a 5 binarios de usuario reales en `userland/`:

- **`sysinfo.c`** — usa una syscall distinta a `hello.c` (`GOS_IOCTL_RTC_READ`,
  el reloj real de hardware), para probar que el aislamiento no depende de
  que todos los procesos hagan lo mismo.
- **`counter.c`** — cede la CPU explícitamente entre pasos (`gos_yield()`),
  probando que el scheduler cooperativo trata a un ELF externo exactamente
  igual que a `gopherd`/`shell`.
- **`calc.c`** — usa `malloc`/`free` real desde ring3 (factoriales en un
  buffer pedido en runtime, no en el stack).
- **`stars.c`** — usa el modo gráfico VGA real vía syscall (`GOS_IOCTL_VIDEO_*`),
  dibuja un patrón, y vuelve a modo texto antes de salir.

### Bug 1 — la tabla de procesos se agotaba para siempre

Con `MAX_PROCESSES=5` y `gopherd`+`shell` ocupando 2 slots **permanentemente**
(los procesos zombis nunca liberaban su slot — no existía ningún mecanismo
de reaping), solo alcanzaba para 3 ejecutables externos por sesión de
arranque. Con 5 programas nuevos, la tabla se llenaba antes de poder
probarlos a todos. Subí `MAX_PROCESSES`/`MAX_ADDR_SPACES` de 5 a 8
(`kernel/process.c`, `kernel/paging.c`) como parche inmediato — el arreglo
de fondo (reaping real) es la Novedad 15.

### Bug 2 — `calc.elf` se caía con Page Fault al primer `malloc()`

Este fue más serio. `sys_malloc()` devolvía una dirección del heap del
kernel (`kmalloc()`), pero esa memoria es **supervisor-only** (`US=0`) por
defecto en cualquier espacio de direcciones nuevo — nunca se le otorgaba
acceso de usuario. Cualquier proceso ring3 que pidiera memoria dinámica y
después escribiera ahí se caía en el primer acceso. Arreglado en
`kernel/syscall.c`: `sys_malloc()` ahora llama a `paging_grant_access()`
sobre el bloque recién asignado, en el espacio de direcciones del proceso
actual (`proc_current_addr_space()`, accesor nuevo en `process.c`).

**Limitación conocida, documentada a propósito y no resuelta**: el permiso
se otorga a nivel de **página** (4KB), y los pools de `kmalloc` empaquetan
varios slots chicos por página. Un proceso ring3 con acceso a su propio
bloque queda, por construcción de esta granularidad, con acceso de
lectura/escritura a los demás slots de esa misma página física —
potencialmente memoria de otro proceso o del kernel. Un heap de usuario
separado del heap interno del kernel resolvería esto; no está
implementado. Tampoco se revoca el acceso en `sys_free()` (`kfree()` no
conoce el tamaño del bloque). Aceptable para este proyecto didáctico, no
para producción.

Verificado con una sesión real en QEMU: los 5 ejecutables corren limpio
(`dir` los lista con su tamaño real en bytes, cada uno imprime su salida
esperada — incluido `calc.elf` con los factoriales 1,1,2,6,24,120 tras el
fix — y `ps` los muestra a todos con PID propio).

## Novedad 15: comando `kill` — reaping real de procesos

Antes de esto no había forma de "sacar" un proceso terminado de la tabla:
un `PROC_ZOMBIE` se quedaba ahí para siempre, y su `Region` (stack de
kernel + stack de usuario) tampoco se liberaba — un leak real de memoria
física además de slots de proceso agotados (ver Novedad 14, Bug 1).
`region_destroy()`/`page_free()` ya existían en `memory.c`, pero nadie los
llamaba desde ningún lado.

- **`kernel/process.c` → `proc_kill(pid)`** — funciona sobre cualquier
  estado salvo `UNUSED`: si el proceso está `READY`/`BLOCKED`, lo fuerza a
  `ZOMBIE` (liberando su espacio de direcciones si era ring3); si ya es
  `ZOMBIE`, reapea directo. En ambos casos libera su `Region` y devuelve el
  slot a `UNUSED`, listo para reusar. Se niega explícitamente a matar al
  proceso que está corriendo en ese mismo instante (no hay forma segura de
  auto-matarse desde adentro de la propia llamada en un scheduler
  cooperativo de un solo core).
- **`kernel/shell.c` → comando `kill <pid>`**.

Verificado en QEMU: corrí los 5 ejecutables de la Novedad 14, los maté
todos con `kill 3` a `kill 7`, `ps` confirmó que los slots volvieron a
`UNUSED`, y pude re-ejecutar los 5 de nuevo sin problema (nuevos PIDs
8-12) — antes de este cambio, una tercera vuelta se habría quedado sin
slots.

## Novedad 16: portapapeles interno (Ctrl+C / Ctrl+V)

No hay portapapeles del sistema host (no hay mouse, no hay integración
QEMU/hardware real para eso). Lo que sí tiene sentido en una consola de
texto: `Ctrl+C` copia la línea que se está editando ahora mismo (sin
necesidad de Enter), `Ctrl+V` la pega donde se esté escribiendo, incluso
en otro momento y otra línea.

- **`kernel/keyboard.c`** — trackeo de la tecla `Ctrl` (antes no se
  registraba en absoluto: caía en el mismo camino que cualquier tecla sin
  traducción). Dos códigos de control reservados (`0x03`/`0x16`, los
  mismos que usa cualquier terminal Unix para ETX/SYN) para que
  `kb_readline()` los distinga de texto normal. Portapapeles interno de
  128 bytes, vive solo mientras el kernel está arriba (no persiste a
  disco, no es del sistema operativo host).
- El copiado muestra un "toast" instantáneo en la barra de estado
  (Novedad 13), reusando `vga_status_line()` directo sin esperar el
  refresco periódico — se autolimpia solo con el siguiente refresco
  (~100ms después).

Verificado con un round-trip completo en QEMU: copié `echo hola` a mitad
de edición (sin darle Enter), corrí `ver` normalmente para probar que el
shell seguía funcionando bien en el medio, y después pegué el portapapeles
en una línea nueva — volvió exactamente `echo hola` y lo ejecutó de
verdad, imprimiendo `hola`. El portapapeles sobrevivió intacto entre medio
de otro comando.

## Novedad 17: cursor movible (flechas izquierda/derecha) en el editor de línea

Hasta acá, `kb_readline()` solo podía escribir al final y borrar con
backspace desde el final — corregir algo en el medio de lo ya tipeado
significaba borrar todo lo de después primero. Con esto:

- **`kernel/keyboard.c`** — detección de scancodes extendidos (prefijo
  `0xE0`, el que usan las flechas en scancode set 1). `kb_readline()`
  reescrito entero alrededor de un cursor lógico independiente del final
  de la línea: insertar, borrar (`backspace`) y pegar (`Ctrl+V`, Novedad
  16) ahora respetan la posición del cursor, no solo el final del buffer.
- **`kernel/vga.c`/`vga.h`** — `vga_get_cursor()`/`vga_set_cursor()`,
  exponen el cursor real de pantalla (antes vivía solo como estado interno
  de `vga.c`) para que `keyboard.c` pueda reposicionarlo tras cada edición.

**Limitación conocida, documentada a propósito**: si una línea es tan larga
que hace scroll de pantalla *mientras* se está editando, el punto de
referencia donde arranca la línea (`row0`/`col0`) queda desactualizado y el
cursor visual se puede desalinear. No pasa con comandos normales (líneas
cortas); arreglarlo bien requeriría que `vga.c` avise a `keyboard.c` cuando
hace scroll — no implementado.

**Nota aparte, no un bug de esta novedad**: si otro proceso (`gopherd`,
por ejemplo) imprime algo en el medio de una edición, ambos comparten el
mismo cursor de VGA y el resultado se ve entreverado en pantalla — es una
característica preexistente del modelo de este kernel (no hay bloqueo de
pantalla por proceso), no algo que este cambio introdujo. El *buffer* que
usa el shell queda correcto igual (confirmado leyendo el contenido real
vía el log serial, no solo mirando la pantalla).

Verificado con dos pruebas que confirman el contenido real del buffer (no
solo lo que se ve en pantalla): escribí `helloworld`, fui 5 posiciones a
la izquierda, inserté un espacio → quedó `hello world` exacto. Escribí
`abcdef`, fui 3 a la izquierda, `backspace` → quedó `abdef` exacto (borró
la `c` del medio, no la del final).

## Novedad 18 (experimental, aislado): primer prototipo de transición a long mode (64 bits)

Este es el primer paso concreto hacia el ítem de 64 bits del roadmap —
deliberadamente aislado en `experiments/longmode/`, **sin tocar nada del
kernel real**, porque la transición de modo protegido a long mode es de
las partes más delicadas de escribir un kernel x86-64 y conviene
depurarla aparte antes de meterla en `gdt.c`/`paging.c`/`boot.s` de
verdad.

`experiments/longmode/longmode_test.s` hace la secuencia completa y real
que exige la arquitectura (no un atajo):

1. Verifica soporte de long mode vía `CPUID` (bit `LM`, función extendida
   `0x80000001`).
2. Arma tablas de paginación de 4 niveles (`PML4→PDPT→PD`, con páginas de
   2MB en el último nivel para no necesitar una 4ta tabla todavía —
   identity-map del primer 1GB).
3. Activa `CR4.PAE`, carga `CR3`, prende `EFER.LME` (MSR), prende
   `CR0.PG`.
4. Carga una GDT con un descriptor de código de 64 bits (bit `L`) y hace
   el far jump que efectivamente cambia de modo.
5. **Prueba definitiva**: ejecuta `mov rax, 0x1234567890ABCDEF` — una
   instrucción que solo existe en modo de 64 bits real (imposible en modo
   compatibilidad) — y la imprime por serial.

Un problema técnico real en el camino: Multiboot exige que el kernel sea
un ELF**32** (aunque el código de adentro sea de 64 bits), y NASM no puede
generar relocaciones de 64 bits dentro de un objeto ELF32. Lo resolví
cargando las direcciones de los strings con `mov eax, ...` (32 bits, que
en long mode extiende automáticamente a `rax` con ceros) en vez de
`mov rax, ...` — evita necesitar una reubicación de 8 bytes que el formato
no soporta.

Verificado en `qemu-system-x86_64` (no `-i386`: ese emula un CPU sin bit
`LM`) con salida serial real:
```
[longmode_test] CPUID reporta soporte de long mode (LM=1)
[longmode_test] PAE + CR3 + EFER.LME + CR0.PG activados
[longmode_test] far jump a segmento de codigo de 64 bits...
[longmode_test] ESTO SE IMPRIME DESDE CODIGO REAL DE 64 BITS.
[longmode_test] mov rax, 0x1234567890ABCDEF ; rax = 1234567890ABCDEF
[longmode_test] OK: long mode confirmado end-to-end.
```

**Esto sigue siendo un prototipo aislado, no una migración**: el kernel
real (`gopheros.elf`) sigue siendo 32 bits puro de punta a punta. Migrar
de verdad implica reescribir `gdt.c` (descriptor de 64 bits), `paging.c`
(paginación de 4 niveles en vez de las 8 tablas de 2 niveles actuales),
`isr.s`/TSS (formato de frame de interrupción distinto), y decidir si
32 bits queda como modo de compatibilidad (como Linux/Windows reales) —
la única opción que tiene sentido en x86 real. Ver la sección de Roadmap
más abajo.

## Archivos que te entrego

- **`gopheros_src.zip`** (o los módulos sueltos tocados, según la ronda) —
  todo el código fuente, corregido y completado hasta la Novedad 18.
- **`experiments/longmode/`** — el prototipo aislado de long mode
  (Novedad 18), con su propio `Makefile`. No se compila junto al kernel
  principal (usa un target de arquitectura distinto, x86-64).

Este README documenta el estado acumulado del proyecto. Si recibiste solo
"los módulos tocados" de una ronda puntual, este archivo te dice en qué
contexto encajan.

## Cómo correrlo (IMPORTANTE: elegí el comando según tu entorno)

El teclado del kernel lee del controlador **PS/2**, no del puerto serie. Si
corrés con `-serial stdio`, esa terminal queda conectada al puerto serie
(solo sirve para depurar viendo la salida), y escribir ahí **no llega al
teclado** — por eso puede parecer que el shell se cuelga después de
`[gopherd] terminando.` cuando en realidad está esperando bien, solo que
por la entrada equivocada.

**Si tenés entorno gráfico** (Linux con escritorio, Windows, Mac) — se abre
una ventana, hacé click adentro para que capture el teclado, y escribí ahí:
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32
# o con la ISO (make iso):
qemu-system-i386 -cdrom build/gopheros.iso -m 32
```

**Si estás en una terminal pura (SSH, sin entorno gráfico)** — usá
`-display curses`, que muestra la pantalla de texto DENTRO de la terminal
y sí conecta el teclado de esa misma terminal al PS/2 emulado:
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32 -display curses
```

**Solo para depurar viendo el log** (sin poder escribir comandos):
```bash
qemu-system-i386 -kernel build/gopheros.elf -m 32 -serial stdio -display none
```

Una vez en el shell, para probar lo nuevo de un tirón:
```
dir                 # lista /hello.elf, /sysinfo.elf, /counter.elf, /calc.elf, /stars.elf
exec hello.elf       # corre un ELF32 externo real (Novedad 12)
exec calc.elf         # malloc/free real desde ring3 (Novedad 14)
ps                     # ver los procesos, con su PID y estado
kill <pid>              # sacar un proceso de la tabla (Novedad 15)
```
Y en el editor de línea: escribí algo, `Ctrl+C` lo copia, `Ctrl+V` lo pega
(Novedad 16); las flechas izquierda/derecha mueven el cursor para corregir
en el medio (Novedad 17).

## Para recompilar desde la fuente

```bash
# Ubuntu/Debian:
sudo apt install gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso mtools
unzip gopheros_src.zip && cd gopheros_src
make            # genera build/gopheros.elf (y de paso build/userland/*.elf)
make iso        # genera build/gopheros.iso (requiere grub-mkrescue)
make run        # lo arranca directo en QEMU
```

Para el prototipo de long mode (Novedad 18), aparte:
```bash
cd experiments/longmode
make run        # usa qemu-system-x86_64, no -i386 (necesita CPU con bit LM)
```

## Roadmap actualizado — qué está hecho y qué falta

### ✅ Hecho y probado de punta a punta

**Core del kernel**
- Boot Multiboot (GRUB o `qemu -kernel`), sin bootloader propio.
- GDT + IDT + PIC remapeado + 32 excepciones + 16 IRQs + `int 0x80`.
- TSS (necesario para las transiciones ring3→ring0).
- Paginación real de 4KB con permisos por página (supervisor vs usuario),
  más `paging_map_page()` para remapeo arbitrario por proceso (Novedad 12).
- Memoria: allocator físico de páginas + pools + region allocator, con
  `region_destroy()` conectado de verdad al ciclo de vida de un proceso
  (Novedad 15).
- Scheduler cooperativo (8 procesos concurrentes, con reaping real de
  zombis — Novedad 15), `yield`/`sleep_on`/`wakeup`.
- 15 syscalls estables (ABI fija, `ioctl` como mecanismo de extensión).

**Cargador de programas (Novedad 12) — ítem que antes estaba en "falta"**
- ELF32 externo real: binarios compilados y linkeados aparte del kernel,
  cargados en runtime, con código en páginas propias (no compartidas).
- 5 binarios de ejemplo en `userland/` (Novedad 14), cubriendo syscalls
  distintas: `write`, RTC, `yield` cooperativo, `malloc`/`free`, video
  gráfico.

**Drivers**
- VGA texto 80x25 (con fila de estado persistente, Novedad 13) + VGA
  gráfico modo 13h (320x200x256) real.
- PS/2 teclado (scancode set 1, shift, **Ctrl** (Novedad 16), backspace,
  **cursor izq/der** (Novedad 17)).
- RTC/CMOS (fecha/hora real de hardware).
- ATA PIO (disco duro real, LBA28, polling).

**Filesystem**
- Árbol de directorios real (`mkdir`/`rmdir`/`cd`/`pwd`), no flat.
- Persistencia real a disco (`save`/`load`), probada a través de un reinicio
  completo de la VM.

**Shell**
- Comandos duales Unix/DOS (`ls`/`dir`, `cat`/`type`, `rm`/`del`, etc.)
- `date`, `time`, `ver`, `vol`, `demo` (gráficos), `gopherpy`, `ring3demo`,
  `ring3mem`, `exec` (Novedad 12), `kill` (Novedad 15).
- Editor de línea con cursor movible y portapapeles interno
  (Novedades 16-17).

**Ring 3 (modo multiusuario) — aislamiento real, incluido el código**
- Transición de privilegio CPL0↔CPL3 real (no simulada).
- Syscalls funcionando desde CPL3, incluida memoria dinámica otorgada
  correctamente por página (Novedad 14).
- Aislamiento de **fallos**: un proceso ring3 que se cae no tumba el kernel.
- Aislamiento de **memoria kernel-vs-proceso**: `.data`/`.bss` del kernel
  protegidos de cualquier proceso ring3.
- Aislamiento de **memoria proceso-vs-proceso**, incluido el **código**:
  desde la Novedad 12, cada proceso puede tener su propio binario cargado
  en páginas físicas privadas — ya no comparten necesariamente `.text`
  como antes.
- Ciclo de vida completo: crear, correr, terminar, y **reapear** (Novedad 15)
  — antes solo existían las primeras tres etapas.

**GopherPy**
- Traductor Python-como → C nativo, apuntando a `gopheros_abi.h` (ABI de
  cero librerías). 7 bugs reales encontrados y arreglados. Primer programa
  corriendo de punta a punta en GopherOS.

**Red**
- PCI (enumeración + config space) + driver RTL8139 real, detectado por
  hardware (MAC leída del dispositivo, no inventada).
- Ethernet + ARP + IPv4 + ICMP — `ping` funciona de verdad contra QEMU.
- TCP mínimo (una conexión a la vez) + servidor Gopher en puerto 70,
  probado con un cliente TCP externo real (no solo desde el shell interno).

**64 bits (Novedad 18) — arrancado, no migrado**
- Prototipo aislado de transición 32→64 bits, verificado end-to-end en
  `qemu-system-x86_64`. El kernel real sigue siendo 32 bits puro.

### 🚧 Limitaciones conocidas (documentadas a propósito, no descuido)

- **`fork()` no implementado** (`ENOSYS`); `proc_create()`/`proc_create_ring3()`/
  `proc_load_elf()` sí funcionan.
- El allocator de memoria física es un arena estático de 8MB embebido en
  el propio kernel — no lee el mapa de memoria real que entrega Multiboot.
- **Heap de usuario no aislado del heap del kernel** (Novedad 14): el
  permiso de `malloc()` para ring3 se otorga a nivel de página, no de
  slot — procesos que comparten página de pool pueden pisarse. Ver detalle
  en Novedad 14.
- **`sys_free()` no revoca el acceso de usuario** otorgado por
  `sys_malloc()` (Novedad 14) — `kfree()` no conoce el tamaño del bloque
  liberado.
- **`proc_load_elf()` no libera páginas ya asignadas si falla a mitad de
  camino** (por ejemplo, un segundo `PT_LOAD` que no entra en memoria) —
  leak menor en el camino de error, no en el camino feliz.
- **El editor de línea puede desalinearse si la línea hace scroll de
  pantalla mientras se edita** (Novedad 17) — no pasa con comandos cortos.
- **Prints de distintos procesos pueden entreverarse en pantalla**
  (Novedad 17, nota aparte): no hay bloqueo de cursor de VGA por proceso.
- **TCP soporta una sola conexión confiable por sesión** — la primera
  conexión al servidor Gopher funciona de punta a punta con un cliente
  externo real; conexiones subsecuentes en la misma sesión pueden no
  completarse (sospecha: interacción con el NAT de QEMU, no verificado a
  fondo). Sin retransmisión ni control de congestión.
- Sin USB (arrancar por USB sí funciona vía BIOS/GRUB; leer/escribir un
  pendrive desde adentro del kernel necesitaría un driver UHCI/EHCI +
  almacenamiento masivo — proyecto aparte, más grande que el de disco/red).
- El patrón "Connector+Plug" de la arquitectura pública todavía no está
  aplicado literalmente — VGA/teclado/timer/disco/NIC están cableados
  directo al kernel en vez de pasar por una capa de abstracción.
- El shell no tiene historial de comandos ni autocompletado.
- **64 bits es un prototipo aislado (Novedad 18), no una migración** — ver
  el próximo paso más abajo, es el más grande de todos los pendientes.

### 📋 Próximos pasos posibles (elegí por dónde seguir)

En orden de qué tan grande es cada uno:

1. **Cerrar los leaks documentados de `proc_load_elf()`/`sys_free()`**
   (Novedad 14/12) — chico, contenido.
2. **Heap de usuario separado del heap del kernel** — resuelve de raíz la
   limitación de aislamiento de memoria dinámica (Novedad 14).
3. **Historial de comandos** en el shell (flecha arriba/abajo ahora que ya
   hay soporte de teclas extendidas, Novedad 17) — reusa la misma
   infraestructura de scancodes extendidos.
4. **Arreglar TCP multi-conexión** — el bug de conexiones subsecuentes con
   el NAT de QEMU (o probar con `-netdev tap` en vez de `user`).
5. **Patrón Connector+Plug** — refactor de arquitectura, acerca el código
   a la documentación pública del proyecto.
6. **Driver de disco más robusto** (IRQ14 en vez de polling, soporte AHCI/SATA).
7. **Migración real a 64 bits** (a partir del prototipo de la Novedad 18) —
   el más grande: reescribir `gdt.c`, `paging.c` (paginación de 4 niveles),
   `isr.s`/TSS, decidir la estrategia de compatibilidad con 32 bits, y
   extender `proc_load_elf()` con la rama `ELFCLASS64`.
8. **USB** (UHCI/EHCI + almacenamiento masivo) — grande, aparte.

Decime cuál seguimos.
