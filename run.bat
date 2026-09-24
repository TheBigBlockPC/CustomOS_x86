@echo off
setlocal
pushd "%~dp0" || exit /b 1

::set "QEMU_DIR=C:\Program Files\qemu"
set "QEMU_DIR=C:\Users\j.naumann\appdata\Local\Programs\qemu"
set "QEMU_SYSTEM=%QEMU_DIR%\qemu-system-x86_64.exe"

if not exist "%QEMU_SYSTEM%" (
    echo Error: QEMU was not found at "%QEMU_SYSTEM%"
    exit /b 1
)
if not exist "BIOS" mkdir "BIOS" || exit /b 1
if not exist "BIOS\vars.fd" copy "%QEMU_DIR%\share\edk2-i386-vars.fd" "BIOS\vars.fd" >nul || exit /b 1
if not exist "BIOS\edk2-x86_64-code.fd" copy "%QEMU_DIR%\share\edk2-x86_64-code.fd" "BIOS\edk2-x86_64-code.fd" >nul || exit /b 1

where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0make_fat32_image.py"
) else (
    python "%~dp0make_fat32_image.py"
)
if errorlevel 1 exit /b 1

"%QEMU_SYSTEM%" -machine q35 -m 8G ^
  -drive if=pflash,format=raw,unit=0,readonly=on,file=BIOS\edk2-x86_64-code.fd ^
  -drive if=pflash,format=raw,unit=1,file=BIOS\vars.fd ^
  -drive format=raw,file=fat:rw:test_env ^
  -drive if=none,id=nvme0,format=raw,file=fat32.img ^
  -device nvme,drive=nvme0,serial=NVME0001 ^
  -device qemu-xhci ^
  -vga std -no-reboot -no-shutdown

set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
