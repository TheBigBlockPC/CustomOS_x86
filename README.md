target platform: UEFI (x64)

On Windows, run `run.bat` with Python 3 and QEMU installed. The script uses
Python's standard library to rebuild `fat32.img` from `test_env` before launch;
it needs neither administrator rights nor WSL. QEMU is expected at
`C:\Program Files\qemu`.
