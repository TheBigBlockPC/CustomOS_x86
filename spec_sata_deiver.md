create a small SATA driver for this OS, keep it very lean,
in /src/drivers you creata the sata.c and sata.h files and you create a block storage proxy that is API identical to the nvme driver that filesystem.c just needs it's nvme spesiffic API call replaced with the SATA equialent.

do not change other source files, do not make the code overly long.

the proxy MUST provide 4096 bytes of read data to keep identical behaviour across nvme and sata.

drive 0 is either the first nvme found or the first sata if no nvme is existent

the public function naming syntac is system_function therefore nvme_init is valid for nvme related.

copy run.sh but make it a SATA instad of a NVME to varify
