@echo off
setlocal
pushd "%~dp0" || exit /b 1
set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
set "FLAGS=-target x86_64-pc-windows-msvc -ffreestanding -fno-builtin -nostdlib -mno-red-zone -mgeneral-regs-only -fno-stack-protector -fuse-ld=lld"

"%CLANG%" %FLAGS% -Wl,-entry:efi_main -Wl,-subsystem:efi_application -o bootx64.efi src/main.c src/console_renderer.c src/console.c src/util.c
if errorlevel 1 goto fail

"%CLANG%" %FLAGS% -Wl,-entry:kernel_entry -Wl,-subsystem:native -Wl,-base:0xffffffff80000000 -Wl,-fixed -Wl,-dynamicbase:no -o kernel.exe src/boot.c src/console_renderer.c src/console.c src/kernel.c src/drivers/keyboard_ps2.c src/input_subsystem.c src/tty.c src/memory.c src/cpu.c src/paging.c src/util.c src/gdt/gdt.c src/gdt/gdt.S src/interrupts/interrupts.c src/interrupts/interrupts.S src/panic.c src/acpi.c src/pcie.c src/drivers/nvme.c src/drivers/sata.c src/filesystem.c
if errorlevel 1 goto fail

if not exist "test_env\EFI\BOOT" mkdir "test_env\EFI\BOOT"
copy /y bootx64.efi test_env\EFI\BOOT\ >nul
if errorlevel 1 goto fail
copy /y kernel.exe test_env\EFI\BOOT\ >nul
if errorlevel 1 goto fail
popd
exit /b 0

:fail
popd
exit /b 1
