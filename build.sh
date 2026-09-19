#!/usr/bin/env bash
set -e

clang \
  -target x86_64-pc-windows-msvc \
  -ffreestanding \
  -fno-builtin \
  -nostdlib \
  -Wl,-entry:efi_main \
  -Wl,-subsystem:efi_application \
  -fuse-ld=lld \
  -o bootx64.efi \
  src/main.c \
  src/console_renderer.c \
  src/console.c \
  src/kernel.c \
  src/drivers/keyboard_ps2.c \
  src/input_subsystem.c \
  src/tty.c \
  src/memory.c \
  src/cpu.c \
  src/paging.c \
  src/util.c

cp bootx64.efi ./test_env/EFI/BOOT/