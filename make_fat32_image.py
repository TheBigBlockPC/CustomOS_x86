#!/usr/bin/env python3
"""Build the FAT32 NVMe disk directly from test_env, without mounting it."""

import math
import os
import struct
import sys
import tempfile
from datetime import datetime
from pathlib import Path


SECTOR_SIZE = 512
DISK_SECTORS = 256 * 1024 * 1024 // SECTOR_SIZE
PARTITION_START = 2048
PARTITION_SECTORS = DISK_SECTORS - PARTITION_START
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1
CLUSTER_SIZE = SECTOR_SIZE * SECTORS_PER_CLUSTER
VOLUME_LABEL = b"TESTDISK   "
SHORT_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")


def fat_sectors():
    sectors = 1
    while True:
        clusters = (PARTITION_SECTORS - RESERVED_SECTORS - FAT_COUNT * sectors) // SECTORS_PER_CLUSTER
        needed = math.ceil((clusters + 2) * 4 / SECTOR_SIZE)
        if needed == sectors:
            return sectors, clusters
        sectors = needed


class Node:
    def __init__(self, path, directory, parent=None):
        self.path = path
        self.directory = directory
        self.parent = parent
        self.children = []
        self.short_name = None
        self.long_name = False
        self.clusters = []
        self.size = 0 if directory else path.stat().st_size
        if self.size > 0xFFFFFFFF:
            raise ValueError(f"FAT32 cannot store files larger than 4 GiB: {path}")


def read_tree(path, parent=None):
    if path.is_symlink():
        raise ValueError(f"Symbolic links cannot be stored in the FAT32 image: {path}")
    node = Node(path, path.is_dir(), parent)
    if node.directory:
        seen = set()
        for child_path in sorted(path.iterdir(), key=lambda p: (p.name.casefold(), p.name)):
            if child_path.name.casefold() in seen:
                raise ValueError(f"FAT names must be unique ignoring case: {child_path}")
            seen.add(child_path.name.casefold())
            node.children.append(read_tree(child_path, node))
        assign_short_names(node)
    elif not path.is_file():
        raise ValueError(f"Unsupported filesystem entry: {path}")
    return node


def split_name(name):
    if "." in name and not name.startswith("."):
        return name.rsplit(".", 1)
    return name, ""


def assign_short_names(directory):
    used = {VOLUME_LABEL} if directory.parent is None else set()
    for child in directory.children:
        name = child.path.name
        if not name or name in (".", "..") or name.endswith((" ", ".")):
            raise ValueError(f"Invalid FAT filename: {child.path}")
        if any(char in '<>:"/\\|?*' or ord(char) < 32 for char in name):
            raise ValueError(f"Invalid FAT filename: {child.path}")
        if len(name.encode("utf-16le")) // 2 > 255:
            raise ValueError(f"FAT filename is too long: {child.path}")

        base, extension = split_name(name)
        upper_base, upper_extension = base.upper(), extension.upper()
        valid_short = (
            1 <= len(upper_base) <= 8
            and len(upper_extension) <= 3
            and all(char in SHORT_CHARS for char in upper_base + upper_extension)
        )
        candidate = (upper_base.ljust(8) + upper_extension.ljust(3)).encode("ascii") if valid_short else None
        if candidate is not None and candidate not in used:
            child.short_name = candidate
            child.long_name = name != upper_base + ("." + upper_extension if extension else "")
        else:
            clean_base = "".join(char for char in upper_base if char in SHORT_CHARS) or "_"
            clean_extension = "".join(char for char in upper_extension if char in SHORT_CHARS)[:3]
            for number in range(1, 1000000):
                suffix = f"~{number}"
                alias = (clean_base[: 8 - len(suffix)] + suffix).ljust(8) + clean_extension.ljust(3)
                candidate = alias.encode("ascii")
                if candidate not in used:
                    child.short_name = candidate
                    child.long_name = True
                    break
            else:
                raise ValueError(f"Too many short-name collisions in {directory.path}")
        used.add(child.short_name)


def lfn_count(name):
    return math.ceil((len(name.encode("utf-16le")) // 2) / 13)


def directory_size(node):
    entries = 1 if node.parent is None else 2  # Volume label or . and ..
    entries += sum(1 + (lfn_count(child.path.name) if child.long_name else 0) for child in node.children)
    return (entries + 1) * 32  # Include an end marker.


def all_nodes(root):
    yield root
    for child in root.children:
        yield from all_nodes(child)


def allocate(root, cluster_count):
    fat = [0] * (cluster_count + 2)
    fat[0], fat[1] = 0x0FFFFFF8, 0xFFFFFFFF
    next_cluster = 2
    for node in all_nodes(root):
        length = directory_size(node) if node.directory else node.size
        count = max(1, math.ceil(length / CLUSTER_SIZE)) if node.directory else math.ceil(length / CLUSTER_SIZE)
        if next_cluster + count > len(fat):
            raise ValueError("test_env does not fit in the 256 MiB FAT32 image")
        node.clusters = list(range(next_cluster, next_cluster + count))
        for cluster in node.clusters:
            fat[cluster] = cluster + 1 if cluster != node.clusters[-1] else 0x0FFFFFFF
        next_cluster += count
    return fat, next_cluster


def fat_timestamp(path):
    stamp = datetime.fromtimestamp(path.stat().st_mtime)
    year = min(max(stamp.year, 1980), 2107)
    date = ((year - 1980) << 9) | (stamp.month << 5) | stamp.day
    time = (stamp.hour << 11) | (stamp.minute << 5) | (stamp.second // 2)
    return date, time


def short_entry(short_name, attributes, first_cluster=0, size=0, path=None):
    entry = bytearray(32)
    entry[:11] = short_name
    entry[11] = attributes
    if path is not None:
        date, time = fat_timestamp(path)
        struct.pack_into("<H", entry, 14, time)
        struct.pack_into("<H", entry, 16, date)
        struct.pack_into("<H", entry, 18, date)
        struct.pack_into("<H", entry, 22, time)
        struct.pack_into("<H", entry, 24, date)
    struct.pack_into("<H", entry, 20, first_cluster >> 16)
    struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return entry


def long_entries(name, short_name):
    checksum = 0
    for byte in short_name:
        checksum = (((checksum & 1) << 7) | (checksum >> 1)) + byte
        checksum &= 0xFF
    encoded = name.encode("utf-16le")
    units = list(struct.unpack("<" + "H" * (len(encoded) // 2), encoded))
    if len(units) % 13:
        units.append(0)
        units.extend([0xFFFF] * ((-len(units)) % 13))
    chunks = [units[index : index + 13] for index in range(0, len(units), 13)]
    positions = list(range(1, 11, 2)) + list(range(14, 26, 2)) + list(range(28, 32, 2))
    result = []
    for index in range(len(chunks) - 1, -1, -1):
        entry = bytearray(32)
        entry[0] = index + 1 | (0x40 if index == len(chunks) - 1 else 0)
        entry[11] = 0x0F
        entry[13] = checksum
        for position, unit in zip(positions, chunks[index]):
            struct.pack_into("<H", entry, position, unit)
        result.append(entry)
    return result


def directory_bytes(node):
    entries = []
    if node.parent is None:
        entries.append(short_entry(VOLUME_LABEL, 0x08, path=node.path))
    else:
        entries.append(short_entry(b".          ", 0x10, node.clusters[0], path=node.path))
        parent_cluster = 0 if node.parent.parent is None else node.parent.clusters[0]
        entries.append(short_entry(b"..         ", 0x10, parent_cluster, path=node.parent.path))
    for child in node.children:
        if child.long_name:
            entries.extend(long_entries(child.path.name, child.short_name))
        attributes = 0x10 if child.directory else 0x20
        if child.path.name.startswith("."):
            attributes |= 0x02
        entries.append(short_entry(child.short_name, attributes, child.clusters[0] if child.clusters else 0, child.size, child.path))
    data = b"".join(entries) + bytes(32)
    return data.ljust(len(node.clusters) * CLUSTER_SIZE, b"\x00")


def boot_sector(fat_length):
    boot = bytearray(SECTOR_SIZE)
    boot[:3] = b"\xEB\x58\x90"
    boot[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 255)
    struct.pack_into("<I", boot, 28, PARTITION_START)
    struct.pack_into("<I", boot, 32, PARTITION_SECTORS)
    struct.pack_into("<I", boot, 36, fat_length)
    struct.pack_into("<I", boot, 44, 2)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x43555354)
    boot[71:82] = VOLUME_LABEL
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"
    return boot


def filesystem_info(free_clusters, next_cluster):
    info = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", info, 0, 0x41615252)
    struct.pack_into("<I", info, 484, 0x61417272)
    struct.pack_into("<I", info, 488, free_clusters)
    struct.pack_into("<I", info, 492, next_cluster if free_clusters else 0xFFFFFFFF)
    info[510:512] = b"\x55\xAA"
    return info


def build_image(source, image):
    if not source.is_dir():
        raise ValueError(f"Source directory does not exist: {source}")
    root = read_tree(source)
    fat_length, cluster_count = fat_sectors()
    fat, next_cluster = allocate(root, cluster_count)
    fat_bytes = bytearray(fat_length * SECTOR_SIZE)
    for index, value in enumerate(fat):
        struct.pack_into("<I", fat_bytes, index * 4, value)

    mbr = bytearray(SECTOR_SIZE)
    mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\xFE\xFF\xFF", 0x0C, b"\xFE\xFF\xFF", PARTITION_START, PARTITION_SECTORS)
    mbr[510:512] = b"\x55\xAA"
    boot = boot_sector(fat_length)
    info = filesystem_info(cluster_count - (next_cluster - 2), next_cluster)
    data_start = PARTITION_START + RESERVED_SECTORS + FAT_COUNT * fat_length

    descriptor, temporary_name = tempfile.mkstemp(prefix="fat32-", suffix=".img", dir=image.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w+b") as output:
            output.truncate(DISK_SECTORS * SECTOR_SIZE)
            output.seek(0)
            output.write(mbr)
            for sector, contents in ((0, boot), (1, info), (6, boot), (7, info)):
                output.seek((PARTITION_START + sector) * SECTOR_SIZE)
                output.write(contents)
            for fat_index in range(FAT_COUNT):
                output.seek((PARTITION_START + RESERVED_SECTORS + fat_index * fat_length) * SECTOR_SIZE)
                output.write(fat_bytes)
            for node in all_nodes(root):
                if not node.clusters:
                    continue
                output.seek((data_start + (node.clusters[0] - 2) * SECTORS_PER_CLUSTER) * SECTOR_SIZE)
                if node.directory:
                    output.write(directory_bytes(node))
                else:
                    with node.path.open("rb") as source_file:
                        remaining = node.size
                        while remaining:
                            chunk = source_file.read(min(1024 * 1024, remaining))
                            if not chunk:
                                raise ValueError(f"File changed while building image: {node.path}")
                            output.write(chunk)
                            remaining -= len(chunk)
                        if source_file.read(1):
                            raise ValueError(f"File changed while building image: {node.path}")
        os.replace(temporary, image)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    directory = Path(__file__).resolve().parent
    build_image(directory / "test_env", directory / "fat32.img")
    print("FAT32 image ready: fat32.img (256 MiB, partition starts at LBA 2048)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
