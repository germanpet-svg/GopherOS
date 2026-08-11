# Makefile - GopherOS Kernel Build
# Requiere: gcc (con soporte -m32 / gcc-multilib), nasm, ld, qemu-system-i386,
# y opcionalmente grub-mkrescue + xorriso para generar una ISO booteable.

CC  = gcc
LD  = ld
AS  = nasm

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Werror \
         -nostdlib -fno-builtin -fno-stack-protector \
         -fno-pie -fno-pic -mno-mmx -mno-sse -mno-sse2 \
         -std=c11 -Ikernel

LDFLAGS = -m elf_i386 -T kernel/linker.ld -nostdlib

BUILD = build

OBJS = \
    $(BUILD)/boot.o \
    $(BUILD)/isr.o \
    $(BUILD)/kernel.o \
    $(BUILD)/string.o \
    $(BUILD)/vga.o \
    $(BUILD)/statusbar.o \
    $(BUILD)/serial.o \
    $(BUILD)/gdt.o \
    $(BUILD)/paging.o \
    $(BUILD)/idt.o \
    $(BUILD)/interrupt.o \
    $(BUILD)/timer.o \
    $(BUILD)/keyboard.o \
    $(BUILD)/rtc.o \
    $(BUILD)/graphics.o \
    $(BUILD)/disk.o \
    $(BUILD)/pci.o \
    $(BUILD)/rtl8139.o \
    $(BUILD)/net.o \
    $(BUILD)/tcp.o \
    $(BUILD)/gopher_server.o \
    $(BUILD)/gopherpy_demo.o \
    $(BUILD)/ring3_demo.o \
    $(BUILD)/ring3_demo_mem.o \
    $(BUILD)/ring3_victim.o \
    $(BUILD)/ring3_attacker.o \
    $(BUILD)/spinlock.o \
    $(BUILD)/memory.o \
    $(BUILD)/process.o \
    $(BUILD)/syscall.o \
    $(BUILD)/filesystem.o \
    $(BUILD)/shell.o \
    $(USER_BLOB_OBJS)

# Lista de binarios de usuario reales (userland/<nombre>.c). Agregar uno
# nuevo es agregar el .c ahi y sumarlo aca — el resto (compilar, linkear
# con user.ld, embeber como blob, quedar disponible como /<nombre>.elf en
# el fs de arranque) sale solo via las reglas patron de abajo.
USER_PROGS = hello sysinfo counter calc stars
USER_BLOB_OBJS = $(patsubst %,$(BUILD)/%_blob.o,$(USER_PROGS))

.PHONY: all clean run run-nogui run-disk iso userland

all: $(BUILD)/gopheros.elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot/boot.s | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/isr.o: kernel/isr.s | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================
# userland - Binarios de usuario REALES: se compilan y enlazan APARTE del
# kernel (propio linker script, sin -Ikernel para nada que no sea
# gopheros_abi.h), y quedan embebidos como /<nombre>.elf en el fs de
# arranque (ver kernel.c). Esta es la prueba de que proc_load_elf() carga
# código externo de verdad, no código compilado junto al kernel.
# ============================================================
userland: $(patsubst %,$(BUILD)/userland/%.elf,$(USER_PROGS))

# Sin esto, make trata userland/%_blob.c como archivo intermedio de una
# cadena implicita y lo borra despues de compilarlo — preferimos dejarlo
# en el arbol para que se pueda inspectar el blob generado.
.PRECIOUS: userland/%_blob.c $(BUILD)/userland/%.o $(BUILD)/userland/%.elf

$(BUILD)/userland:
	mkdir -p $(BUILD)/userland

$(BUILD)/userland/%.o: userland/%.c | $(BUILD)/userland
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/userland/%.elf: $(BUILD)/userland/%.o userland/user.ld
	$(LD) -m elf_i386 -T userland/user.ld -nostdlib -static -o $@ $<

userland/%_blob.c: $(BUILD)/userland/%.elf userland/gen_blob.py
	python3 userland/gen_blob.py $< $*_elf_blob $@

$(BUILD)/%_blob.o: userland/%_blob.c | $(BUILD)
	$(CC) $(CFLAGS) -Iuserland -c $< -o $@

$(BUILD)/gopheros.elf: $(OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Verifica que el binario tenga cabecera Multiboot valida
check: $(BUILD)/gopheros.elf
	grub-file --is-x86-multiboot $(BUILD)/gopheros.elf && echo "Multiboot OK"

# Imagen ISO booteable via GRUB (opcional, para USB/CD real o VirtualBox/VMware)
iso: $(BUILD)/gopheros.elf
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(BUILD)/gopheros.elf $(BUILD)/isodir/boot/gopheros.elf
	cp boot/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD)/gopheros.iso $(BUILD)/isodir 2>/dev/null

run: $(BUILD)/gopheros.elf
	qemu-system-i386 -kernel $(BUILD)/gopheros.elf -m 32 -serial stdio

run-nogui: $(BUILD)/gopheros.elf
	qemu-system-i386 -kernel $(BUILD)/gopheros.elf -m 32 -display none -serial mon:stdio

run-disk: $(BUILD)/gopheros.elf $(BUILD)/disk.img
	qemu-system-i386 -kernel $(BUILD)/gopheros.elf -m 32 -serial stdio -hda $(BUILD)/disk.img

$(BUILD)/disk.img: | $(BUILD)
	qemu-img create -f raw $@ 10M

clean:
	rm -rf $(BUILD)
