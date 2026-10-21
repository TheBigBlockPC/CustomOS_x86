#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

flags=(
  -target x86_64-pc-windows-msvc
  -ffreestanding -fno-builtin -nostdlib
  -mno-red-zone -mgeneral-regs-only -fno-stack-protector
  -fuse-ld=lld
)

clang "${flags[@]}" \
  -Wl,-entry:efi_main -Wl,-subsystem:efi_application \
  -o bootx64.efi \
  src/main.c src/console_renderer.c src/console.c src/util.c

clang "${flags[@]}" \
  -Wl,-entry:kernel_entry -Wl,-subsystem:native \
  -Wl,-base:0xffffffff80000000 -Wl,-fixed -Wl,-dynamicbase:no \
  -o kernel.exe \
  src/boot.c \
  src/console_renderer.c \
  src/console.c \
  src/kernel.c \
  src/drivers/keyboard_ps2.c \
  src/input_subsystem.c \
  src/tty.c \
  src/memory.c \
  src/cpu.c \
  src/paging.c \
  src/util.c \
  src/gdt/gdt.c \
  src/gdt/gdt.S \
  src/interrupts/interrupts.c \
  src/interrupts/interrupts.S \
  src/panic.c \
  src/acpi.c \
  src/pcie.c \
  src/drivers/nvme.c \
  src/drivers/sata.c \
  src/filesystem.c

mkdir -p test_env/EFI/BOOT
cp bootx64.efi kernel.exe test_env/EFI/BOOT/
