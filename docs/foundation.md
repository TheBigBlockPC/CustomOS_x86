# Memory and exception foundation

The kernel is a separate fixed-address PE32+ executable mapped at
`0xffffffff80000000`. `bootx64.efi` reads `EFI/BOOT/kernel.exe` from its own boot
volume, loads its sections into reserved physical pages below 4 GiB, and maps
them with their section permissions. Temporary tables preserve firmware's
lower-half mappings during the handoff; the kernel replaces those tables after
importing the console, final memory map, framebuffer configuration, and ACPI
RSDP through `boot_info_t` in `src/boot.h`. `boot_import_console()` restores the
terminal cells and cursor, and `boot_import_memory()` initializes kernel-owned
allocator state and mappings. The kernel switches to its own high-address stack
before installing its final tables, which omit the loader and low image aliases.

PMM-managed RAM and the allocator bitmap
also have an HHDM at `HHDM_BASE + physical_address`. Loader/firmware memory is
not implicitly part of that direct map.

## PMM

The allocator still uses the existing range list and a physical-page bitmap.
Ranges are sorted on insertion, overlaps and invalid ranges fail explicitly,
page zero is excluded, and bitmap size follows the highest physical page rather
than the sum of usable pages. Allocation scans the managed ranges with a hint,
so address-space holes neither hide RAM nor cause long scans. Freeing a reserved,
unmanaged, unaligned, or already-free page does not change allocator state.
The bitmap itself is removed from the allocatable ranges before initialization.

`mem_alloc_page()` returns a zeroed **physical address** and panics on exhaustion.
Use `mem_phys_to_virt()` to access it from the CPU. `mem_free_page()` takes the
physical address, never an HHDM pointer. `mem_get_free_page_count()` reports the
current number of free pages; `mem_get_memory_size()` reports the usable RAM
registered at boot, including the pages later reserved for the bitmap.

`mem_kernel_to_phys()` converts a pointer within the contiguous high kernel
image into its physical address, including the page-table root and static DMA
buffers. `mem_phys_to_virt()` also resolves physical addresses within that image
to their high kernel mapping once the final tables are active.

Only UEFI conventional-memory descriptors from the successfully exited memory
map are registered. The kernel, firmware tables, memory-map pool, and boot-services
allocations are conservatively excluded. Reclaiming loader/boot-services memory
requires an explicit reservation/reclamation design first.

## Paging and permissions

`mmu_map_4k(virtual, physical, flags)` and
`mmu_map_2m(virtual, physical, flags)` take explicit leaf permissions.
`PAGE_PRESENT` and `PAGE_PS` are internal, not caller flags.

| Flags | Meaning |
| --- | --- |
| `0` | Supervisor, read-only, executable |
| `PAGE_RW` | Writable |
| `PAGE_USER` | Accessible from ring 3; ancestors are promoted as necessary |
| `PAGE_NX` | Instruction fetch prohibited |
| `PAGE_PWT`, `PAGE_PCD` | Cache selection; both together select uncached MMIO |

For example, allocate and access one page without creating another mapping:

```c
uint64_t physical = mem_alloc_page();
uint8_t* bytes = mem_phys_to_virt(physical);
bytes[0] = 42;
mem_free_page(physical);
```

A separate writable, non-executable mapping uses:

```c
mmu_map_4k(virtual_address, physical, PAGE_RW | PAGE_NX);
```

Do not free a page while another mapping or a DMA command still owns it.
There is no unmap API yet. Mapping over an existing 4K leaf is explicit replacement;
mapping a 4K leaf within a 2M leaf splits the latter and preserves its neighbors.
A 2M mapping over a 4K page table is rejected, preserving that table and its pages.
All changes invalidate the affected address on the current CPU.

Aligned, complete 2M chunks of managed RAM remain large HHDM pages. Range edges
and the bitmap use 4K pages as needed; holes are not rounded into the direct map.
HHDM pages are supervisor RW/NX. Kernel PE sections use 4K mappings: code is RX,
read-only data is R/NX, and writable data, stacks, and static DMA buffers are RW/NX.
ACPI tables are R/NX. Framebuffer, PCI configuration space, and device registers
are RW/NX and uncached. Drivers map their own register extents, including the
controller-reported NVMe doorbell spacing.

The switch enables EFER.NXE and CR0.WP, drops firmware global/PCID translations,
and establishes the standard PAT layout (slot 0 WB, slot 3 UC) with caches flushed.
NX and PAT support are required. The kernel uses four-level paging and rejects
five-level firmware paging. CPU address width, alignment, canonical addresses,
flags, and range overflow are checked.

## Exception registration

`interrupts_init()` installs all 256 IDT entries before switching page tables.
The existing `interrupts_set_isr(vector, void_callback)` remains for vectors
32–255. Register or unregister vectors 0–31 separately:

```c
static void my_exception(exception_frame_t* frame)
{
    /* Inspect the context; recover only after fixing the fault or deliberately
     * adjusting frame->rip. Simply returning from an unresolved fault repeats it. */
}

exceptions_set_handler(14, my_exception);
exceptions_set_handler(14, 0); /* unregister */
```

The frame contains every general-purpose register, RIP, RSP, RFLAGS, CS/SS,
vector and normalized error code, CR0/CR2/CR3/CR4, and DS/ES/FS/GS selectors.
GPRs and the CPU return frame are restored from the structure. Control registers
and segment-selector fields are snapshots, not write-back requests. CR2 is the
fault address for a page fault.

`fx_state` exposes the saved FXSAVE64 x87/MMX and XMM0–15 state. It can be edited
in place and is restored on return. Do not replace the pointer. It is NULL when
CR0.EM/TS or CR4.OSFXSR makes saving unsafe, notably on a device-not-available
exception. Debug registers, FS/GS base MSRs, and extended XSAVE/AVX state are not
included. The kernel builds with `-mgeneral-regs-only` and `-mno-red-zone`, so C
handlers do not clobber floating-point/SIMD state when that snapshot is unavailable.

Stubs normalize the exceptions that push CPU error codes and use the Microsoft
x64 ABI with aligned stack and shadow space. Do not test error-code exceptions
with `int n`: software INT does not push their hardware error code. Tests generate
real faults instead. Unregistered exceptions report vector/RIP/error and CR2 for
page faults, then halt. No per-exception recovery policies are installed.

## Validation

Run `bash build.sh`, then `python3 tests/run_foundation.py` on Linux with clang,
lld, QEMU, OVMF, and clang's sanitizer runtime installed. The runner builds all
instrumented kernels and disk images in a temporary directory; production source
and boot artifacts do not acquire test hooks.

Host tests exercise sparse/unsorted physical maps, the highest range, reserved
and invalid frees, OOM, reuse/zeroing, exact HHDM boundaries, mixed page tables,
ancestor user flags, invalid mappings, and overflow. Sanitized core tests exercise
string termination, prefix matching, decimal formatting, and keyboard releases.

QEMU boots use 128 MiB/NVMe, 1 GiB/SATA, and 8 GiB/NVMe. Tests exercise all saved
GPRs, RSP/segments/flags, an editable XMM snapshot, the existing ISR registry,
real #UD/#GP/#PF frames and error codes, a live huge-page split, permission/TLB
changes, high-address execution, preserved bootloader console text, reserved
kernel memory, removal of low image aliases, kernel text write protection,
kernel data NX, and FAT initialization.
These are emulator checks, not physical-hardware or SMP validation.

## Review: major remaining issues

1. **Filesystem I/O and FAT validation can still fault or loop.**
   `list_files()`, `change_directory()`, and `fat_next_cluster()` dereference read
   buffers without checking failure. The chain walker accepts invalid/reserved/bad
   cluster values and cycles, and does not know the volume's maximum cluster.
   Directory scans use a fixed 64 entries (2048 bytes) regardless of cluster size,
   skip directory-chain traversal, and do not consistently filter deleted/LFN
   entries or enforce directory attributes. `FS_fd_t` discards file size and the
   public API has no file read operation. Correct these before supporting arbitrary
   disks or implementing file reads. The unconditional boot-time `TEST` chain walk
   has been removed, so a missing test file no longer enters that debug path.
   Sources: `src/filesystem.c`, particularly directory scans and FAT traversal.

2. **Storage timeout recovery does not establish that DMA has stopped.**
   `sata_command()` can return with a command still active; subsequent calls reuse
   its command table and data page. An NVMe timeout leaves the controller marked
   initialized with a potentially outstanding submission and shared read buffer.
   A late completion can invalidate data/queue assumptions. Before retrying after
   timeout, stop/reset the controller or quarantine its buffers and mark it unusable.
   Shared buffers are also overwritten by every successful read, including FAT
   lookups. This must be accounted for in file reading and any future concurrency.
   Sources: `src/drivers/sata.c:sata_command`,
   `src/drivers/nvme.c:nvme_wait_completion/nvme_submit`.

3. **Exception delivery still relies on a usable current kernel stack.**
   There is no TSS/IST emergency stack or stack guard page. A stack overflow can
   prevent double-fault delivery and still lead to a triple fault. The registry
   does not solve this. Add a TSS and at least a dedicated double-fault stack before
   relying on diagnostics for stack failures. Userspace also needs privilege-stack
   setup and a defined FS/GS/SWAPGS convention. Sources: `src/gdt/gdt.c`,
   `src/cpu.c`, and `src/interrupts/interrupts.c`.

4. **The framebuffer renderer assumes at least 1024 × 640 pixels.**
   The bootloader now rejects smaller modes, unsupported pixel formats, and
   invalid framebuffer dimensions before initializing the console. It does not
   select a compatible mode automatically; supporting more displays still needs
   mode selection or dynamic terminal dimensions.
   Sources: `src/main.c`, `src/console_renderer.c:draw_chars`.

5. **The memory and interrupt APIs are single-CPU boot infrastructure.**
   There are no PMM/page-table locks, remote TLB shootdowns, address-space lifecycle,
   or unmap/reclamation operations. External interrupts remain masked; there is
   no PIC/APIC routing/acknowledgement initialization. Do not introduce SMP,
   allocation from interrupt handlers, userspace, or `sti` without those pieces.
   This is an explicit scope limit, not a claim that those systems are complete.

Small review fixes included in this change: bounded short-name preparation to
prevent directory-name stack overwrite, unsigned FAT cluster-high shifting,
normalized keyboard release indices, right-shift handling, correct string
termination and prefix matching, bounded terminal deletion/backspace, correct
NUL-terminated decimal formatting, and matching Windows build flags/SATA linkage.
The broader filesystem, timeout recovery, emergency-stack, and graphics-mode work
above is intentionally left visible for the next implementation steps.

Architecture reference: [Intel Software Developer's Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html),
especially interrupt stack frames, paging/NX/WP, PAT updates, and FXSAVE64.
