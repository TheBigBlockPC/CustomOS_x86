#!/bin/bash
mkdir BIOS
if [ ! -e "./BIOS/OVMF_VARS.fd" ]; then
    cp /usr/share/OVMF/OVMF_VARS_4M.fd ./BIOS/OVMF_VARS.fd
fi
qemu-system-x86_64 \
  -machine q35,accel=kvm \
  -cpu host \
  -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=./BIOS/OVMF_VARS.fd \
  -drive format=raw,file=fat:rw:test_env \
  -drive if=none,id=nvme0,format=raw,file=fat:rw:test_env \
  -device nvme,drive=nvme0,serial=NVME0001 \
  -device qemu-xhci,id=xhci \
  -device usb-kbd,bus=xhci.0 \
  -vga std \
  -no-reboot \
  -no-shutdown \
  -d int,cpu_reset,guest_errors \
  -D qemu.log