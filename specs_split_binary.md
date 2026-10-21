migrate the code from the current implementation where the kernel is in bootx64.efi into a system where the bootloader and kernel are seperate binaries.

the kernel should be mapped to the top of the virtual adress spece.
add functions to migrate over some state from the bootloader to the kernel like the console and memory map

only touch what's required. update both the linux and windows version of the compilation scripts.