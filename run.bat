@echo off
:: 1. Copy the variables template to your local workspace
copy "C:\Users\j.naumann\AppData\Local\Programs\qemu\share\edk2-i386-vars.fd" .\BIOS\vars.fd
copy "C:\Users\j.naumann\AppData\Local\Programs\qemu\share\edk2-x86_64-code.fd" .\BIOS\edk2-x86_64-code.fd

:: 2. Launch QEMU loading both Unit 0 (Code) and Unit 1 (Vars)
C:\Users\j.naumann\appdata\Local\Programs\qemu\qemu-system-x86_64 -drive if=pflash,format=raw,unit=0,readonly=on,file=".\BIOS\edk2-x86_64-code.fd" -drive if=pflash,format=raw,unit=1,file=.\BIOS\vars.fd -drive format=raw,file=fat:rw:test_env -vga std -display sdl -m 4G
