target platform: UEFI (x64)

Run `bash build.sh` on Linux or `build.bat` on Windows to build `bootx64.efi`
and `kernel.exe`. Both files are copied to `test_env/EFI/BOOT`. The EFI loader
reads the kernel from that same boot volume and maps it at `0xffffffff80000000`.
The handoff preserves the framebuffer console, final UEFI memory map, and ACPI
RSDP; the kernel creates its own allocator, stack, and page tables.

On Windows, run `run.bat` with Python 3 and QEMU installed. The script uses
Python's standard library to rebuild `fat32.img` from `test_env` before launch;
it needs neither administrator rights nor WSL. QEMU is expected at
`C:\Program Files\qemu`.

Memory/exception APIs, validation instructions, and the foundation review are in
[docs/foundation.md](docs/foundation.md). Run `python3 tests/run_foundation.py` for
host regression checks and QEMU NVMe/SATA boot tests.
