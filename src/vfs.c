#include "types.h"
#include "dynamic_list.h"
#include "vmm.h"
#include "util.h"

#define VFS_TYPE_MASK    0x0F

#define VFS_FILE         0x01
#define VFS_DIRECTORY    0x02
#define VFS_CHAR_DEVICE  0x03
#define VFS_BLOCK_DEVICE 0x04
#define VFS_PIPE         0x05
#define VFS_SYMLINK      0x06

#define VFS_READONLY     (1U << 4)
#define VFS_HIDDEN       (1U << 5)
#define VFS_SYSTEM       (1U << 6)

#define VFS_NAME_MAX 255

#ifndef NULL
#define NULL ((void*)0)
#endif

// Forward declarations
typedef struct vfs_node vfs_node_t;
typedef struct vfs_ops vfs_ops_t;

// Operations
struct vfs_ops {
    int64_t (*read)(
        vfs_node_t* node,
        uint64_t offset,
        void* buffer,
        uint64_t size
    );

    int64_t (*write)(
        vfs_node_t* node,
        uint64_t offset,
        const void* buffer,
        uint64_t size
    );

    vfs_node_t* (*lookup)(
        vfs_node_t* directory,
        const char* name
    );

    vfs_node_t* (*create)(
        vfs_node_t* directory,
        const char* name,
        uint32_t flags
    );

    int (*remove)(
        vfs_node_t* directory,
        const char* name
    );

    void (*release)(
        vfs_node_t* node
    );
};

// Base VFS node
struct vfs_node {
    uint32_t flags;
    uint32_t ref_count;

    char name[VFS_NAME_MAX + 1];
    const vfs_ops_t* ops;
};

// Extended RAMFS directory node
typedef struct {
    vfs_node_t node;
    dynamic_list_t chieldren;
} vfs_ramfs_dir;
/*
static const vfs_ops_t ramfs_dir_ops = {
    .read    = NULL,
    .write   = NULL,
    .lookup  = NULL,
    .create  = NULL,
    .remove  = NULL,
    .release = NULL
};*/
static const vfs_ops_t ramfs_dir_ops;

vfs_node_t* create_ramfs_dir(
    const char* name,
    uint32_t flags
){
    vfs_ramfs_dir* new_dir = vm_alloc(sizeof(vfs_ramfs_dir),0,VM_FLAGS_DEFAULT);
    str_copy((char*)&new_dir->node.name,(char*)name,true);
    new_dir->node.ref_count++;
    new_dir->node.flags = flags;
    new_dir->node.ops = &ramfs_dir_ops;

    new_dir->chieldren = create_dynamic_list(sizeof(vfs_node_t*));
    return (vfs_node_t*)new_dir;
}

vfs_node_t* ramfs_dir_create(
    vfs_node_t* directory,
    const char* name,
    uint32_t flags
){
    vfs_ramfs_dir* dir = (vfs_ramfs_dir*)directory;
    uint32_t type = flags & VFS_TYPE_MASK;
    switch (type)
    {
        case VFS_DIRECTORY:
            vfs_node_t* new_dir = create_ramfs_dir(
                name,
                flags
            );
            list_push_element(&dir->node,new_dir);
            break;
        default:
            break;
    }
    return NULL;
}

static const vfs_ops_t ramfs_dir_ops = {
    .read    = NULL,
    .write   = NULL,
    .lookup  = NULL,
    .create  = ramfs_dir_create,
    .remove  = NULL,
    .release = NULL
};