#!/bin/bash
set -euo pipefail

# ------------------------------------------------------------
# Configuration
# ------------------------------------------------------------

BIOS_DIR="./BIOS"
TEST_DIR="./test_env"

FAT_IMG="./fat32.img"
FAT_IMG_SIZE="256M"

# Standard modern MBR alignment:
# 2048 sectors * 512 bytes = 1 MiB
PARTITION_START_LBA=2048
SECTOR_SIZE=512
PARTITION_OFFSET=$((PARTITION_START_LBA * SECTOR_SIZE))


# ------------------------------------------------------------
# Check required tools
# ------------------------------------------------------------

for cmd in qemu-system-x86_64 truncate parted mkfs.fat mcopy; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Error: required command '$cmd' was not found"
        exit 1
    fi
done


# ------------------------------------------------------------
# Prepare OVMF
# ------------------------------------------------------------

mkdir -p "$BIOS_DIR"

if [ ! -e "$BIOS_DIR/OVMF_VARS.fd" ]; then
    cp /usr/share/OVMF/OVMF_VARS_4M.fd \
       "$BIOS_DIR/OVMF_VARS.fd"
fi


# ------------------------------------------------------------
# Build FAT32 disk image
# ------------------------------------------------------------

echo "Building FAT32 image..."

# Always rebuild it so it reflects the current test_env contents.
rm -f "$FAT_IMG"

# Create empty raw disk image.
truncate -s "$FAT_IMG_SIZE" "$FAT_IMG"

# Create MBR partition table.
parted -s "$FAT_IMG" mklabel msdos

# Create partition starting at LBA 2048.
parted -s "$FAT_IMG" unit s \
    mkpart primary fat32 "${PARTITION_START_LBA}s" 100%

# Format the partition as FAT32.
#
# --offset is measured in sectors, so this places the filesystem
# boot sector at disk LBA 2048.
#
# -h stores the number of hidden sectors in the FAT32 BPB.
mkfs.fat \
    -F 32 \
    --offset "$PARTITION_START_LBA" \
    -h "$PARTITION_START_LBA" \
    -n TESTDISK \
    "$FAT_IMG"


# ------------------------------------------------------------
# Copy test_env into the FAT32 filesystem
# ------------------------------------------------------------

if [ -d "$TEST_DIR" ]; then
    echo "Copying $TEST_DIR into FAT32 image..."

    # Include hidden files such as .config, while avoiding literal '*'
    # when the directory is empty.
    shopt -s dotglob nullglob

    files=("$TEST_DIR"/*)

    if [ ${#files[@]} -gt 0 ]; then
        mcopy \
            -s \
            -i "${FAT_IMG}@@${PARTITION_OFFSET}" \
            "${files[@]}" \
            ::/
    fi

    shopt -u dotglob nullglob
else
    echo "Warning: $TEST_DIR does not exist"
fi

echo "FAT32 image ready: $FAT_IMG"
echo "Partition starts at LBA: $PARTITION_START_LBA"
echo "Partition byte offset:     $PARTITION_OFFSET"


# ------------------------------------------------------------
# Start QEMU
# ------------------------------------------------------------

qemu-system-x86_64 \
  -machine q35,accel=kvm \
  -cpu host \
  -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file="$BIOS_DIR/OVMF_VARS.fd" \
  -drive format=raw,file=fat:rw:test_env \
  -drive if=none,id=nvme0,format=raw,file="$FAT_IMG" \
  -device nvme,drive=nvme0,serial=NVME0001 \
  -device qemu-xhci,id=xhci \
  -device usb-kbd,bus=xhci.0 \
  -vga std \
  -no-reboot \
  -no-shutdown \
  -d int,cpu_reset,guest_errors \
  -D qemu.log