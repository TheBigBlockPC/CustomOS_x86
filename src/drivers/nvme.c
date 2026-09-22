#include "../types.h"
#include "../pcie.h"
#include "../console.h"
#include "../util.h"
#include "../memory.h"

// NVMe MMIO Controller Registers (BAR0)
typedef struct __attribute__((packed)) {
    uint64_t cap;       // 0x00: Controller Capabilities
    uint32_t vs;        // 0x08: Version
    uint32_t intms;     // 0x0C: Interrupt Mask Set
    uint32_t intmc;     // 0x10: Interrupt Mask Clear
    uint32_t cc;        // 0x14: Controller Configuration
    uint32_t reserved;  // 0x18
    uint32_t csts;      // 0x1C: Controller Status
    uint32_t nssr;      // 0x20: NVM Subsystem Reset
    uint32_t aqa;       // 0x24: Admin Queue Attributes
    uint64_t asq;       // 0x28: Admin Submission Queue Base Address
    uint64_t acq;       // 0x30: Admin Completion Queue Base Address
    uint32_t cmbloc;    // 0x38: Controller Memory Buffer Location
    uint32_t cmbsz;     // 0x3C: Controller Memory Buffer Size
    // Doorbells start at offset 0x1000
} nvme_regs_t;

// 64-Byte Submission Queue Entry (SQE)
typedef struct __attribute__((packed)) {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;       // Command Identifier
    uint32_t nsid;      // Namespace Identifier
    uint32_t reserved0;
    uint64_t mptr;      // Metadata Pointer
    uint64_t prp1;      // Physical Region Page 1 (DMA Target)
    uint64_t prp2;      // Physical Region Page 2
    uint32_t cdw10[6];  // Command-specific Dwords (10 to 15)
} nvme_sqe_t;

// 16-Byte Completion Queue Entry (CQE)
typedef struct __attribute__((packed)) {
    uint32_t cdw0;      // Command-specific result
    uint32_t reserved;
    uint16_t sq_head;   // Current Submission Queue Head pointer
    uint16_t sq_id;     // ID of submission queue
    uint16_t cid;       // Command ID being completed
    uint16_t status;    // Status Field (Bit 0 is Phase Tag)
} nvme_cqe_t;

typedef struct {
    volatile nvme_regs_t* regs;
    uint32_t db_stride;   // Doorbell spacing factor
    
    // Admin Queue (Slots capacity, e.g., 64)
    nvme_sqe_t* admin_sq;
    nvme_cqe_t* admin_cq;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    uint16_t admin_cq_phase;

    // I/O Queue 1 (For reads/writes)
    nvme_sqe_t* io_sq;
    nvme_cqe_t* io_cq;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    uint16_t io_cq_phase;
} nvme_ctrl_t;

// Helper function to ring doorbells
void nvme_ring_db(nvme_ctrl_t* ctrl, uint32_t qid, int is_cq, uint16_t val) {
    // Formula: 0x1000 + ((QID * 2) + (Is_CQ ? 1 : 0)) * DB_STRIDE
    uintptr_t offset = 0x1000 + ((qid * 2) + (is_cq ? 1 : 0)) * ctrl->db_stride;
    volatile uint32_t* db = (volatile uint32_t*)((uintptr_t)ctrl->regs + offset);
    *db = val;
}

int nvme_init(nvme_ctrl_t* ctrl, nvme_regs_t* bar0_vaddr) {
    ctrl->regs = bar0_vaddr;

    // 1. Read Capabilities to determine Doorbell Stride
    ctrl->db_stride = 1 << ((ctrl->regs->cap >> 32) & 0xF);

    // 2. Disable controller to perform configuration reset
    if (ctrl->regs->cc & 1) {
        ctrl->regs->cc &= ~1;
    }
    while (ctrl->regs->csts & 1); // Wait for CSTS.RDY == 0

    // 3. Setup Admin Queue (Allocate 1 page per queue, space for 64 entries)
    ctrl->admin_sq = (nvme_sqe_t*)mem_alloc_page(); // Replace with your allocator
    ctrl->admin_cq = (nvme_cqe_t*)mem_alloc_page();
    ctrl->admin_sq_tail = 0;
    ctrl->admin_cq_head = 0;
    ctrl->admin_cq_phase = 1; // Phase tag starts at 1

    // Write size (Slots - 1). Standardizing on 63 for 64 slots.
    ctrl->regs->aqa = (63 << 16) | 63; 
    ctrl->regs->asq = (uint64_t)ctrl->admin_sq;
    ctrl->regs->acq = (uint64_t)ctrl->admin_cq;

    // 4. Configure Controller (4KB Page, NVMe Command set, Standard Entry sizes)
    // 0x00460000 -> CSS=0 (NVM), MPS=0 (4KB), IOCQES=4 (16B), IOSQES=6 (64B)
    ctrl->regs->cc = (0 << 20) | (0 << 7) | (4 << 20) | (6 << 16); 
    ctrl->regs->cc |= 1; // Enable

    while (!(ctrl->regs->csts & 1)); // Wait for CSTS.RDY == 1

    // 5. Create I/O Completion Queue via Admin Command
    // (Omitted detailed admin wrappers for brevity; involves sending 
    // Create I/O CQ [opcode 0x05] and Create I/O SQ [opcode 0x01])
    
    return 0; // Success
}

void nvme_driver_init(){
    uint8_t id = pcie_get_device_index(1,8,2);
    con_write("NVME with ID at: ",0x03);
    print_uint64(id);
    con_write("\nNVME BAR: ",0x03);
    uint64_t bar = pcie_get_device_bar(id,0);
    print_uint64(bar);

    nvme_regs_t* nvme_ptr = (nvme_regs_t*)bar;
    uint32_t ver = nvme_ptr->vs;
    uint8_t tenery = ver & 0xFF;
    uint8_t minor = (ver>>8) & 0xFF;
    uint8_t major = (ver>>16) & 0xFF;
    con_write("\nversion: ",0x03);
    print_uint64_dec(major);
    con_write(".",0x07);
    print_uint64_dec(minor);
    con_write(".",0x07);
    print_uint64_dec(tenery);
    con_write("\n",0x03);

    
    con_write("initializing NVME\n",0x07);
    nvme_ctrl_t nvme;
    nvme_init(&nvme,nvme_ptr);
    con_write("NVME initialized\n",0x07);
}