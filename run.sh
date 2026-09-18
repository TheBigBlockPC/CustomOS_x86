#!/bin/bash
mkdir BIOS
if [ ! -e "./BIOS/OVMF_VARS.fd" ]; then
    cp /usr/share/OVMF/OVMF_VARS_4M.fd ./BIOS/OVMF_VARS.fd
fi
qemu-system-x86_64 \
  -machine q35 \
  -enable-kvm \
  -m 8G \
  -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=./BIOS/OVMF_VARS.fd \
  -drive format=raw,file=fat:rw:test_env -vga std \
  -no-reboot -d int,cpu_reset,guest_errors  \
  -D qemu.log