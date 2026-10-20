#ifndef FS_H
#define FS_H

void fs_init();
uint64_t fs_cd(uint64_t cluster, char *name);
void fs_ls(uint64_t cluster);
uint64_t fs_root();

#endif