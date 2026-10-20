#include "nvme.h"
#include "../pcie.h"
#include "../console.h"
#include "../util.h"
#include "../paging.h"

#define NVME_PAGE_SIZE 4096
#define NVME_ADMIN_QUEUE_DEPTH 32
#define NVME_IO_QUEUE_DEPTH 16
#define NVME_IO_QUEUE_ID 1
#define NVME_TIMEOUT_ITERATIONS 100000000ULL

#define NVME_ADMIN_CREATE_IO_SQ 0x01
#define NVME_ADMIN_CREATE_IO_CQ 0x05
#define NVME_ADMIN_IDENTIFY 0x06
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_CMD_READ 0x02
#define NVME_FEATURE_NUM_QUEUES 0x07
#define NVME_IDENTIFY_NAMESPACE 0x00
#define NVME_IDENTIFY_ACTIVE_NS_LIST 0x02

#define NVME_CC_EN (1u << 0)
#define NVME_CC_IOSQES (6u << 16)
#define NVME_CC_IOCQES (4u << 20)
#define NVME_CSTS_RDY (1u << 0)
#define NVME_CSTS_CFS (1u << 1)

typedef struct __attribute__((packed)) {
    uint64_t cap;
    uint32_t vs, intms, intmc, cc, reserved, csts, nssr, aqa;
    uint64_t asq, acq;
    uint32_t cmbloc, cmbsz;
} nvme_regs_t;

typedef struct __attribute__((packed)) {
    uint8_t opcode, flags;
    uint16_t cid;
    uint32_t nsid, cdw2, cdw3;
    uint64_t mptr, prp1, prp2;
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} nvme_sqe_t;

typedef struct __attribute__((packed)) {
    uint32_t result, reserved;
    uint16_t sq_head, sq_id, cid, status;
} nvme_cqe_t;

typedef struct {
    volatile nvme_sqe_t* sq;
    volatile nvme_cqe_t* cq;
    volatile uint32_t* sq_doorbell;
    volatile uint32_t* cq_doorbell;
    uint16_t depth, tail, head;
    uint8_t phase;
} nvme_queue_t;

typedef struct {
    volatile nvme_regs_t* regs;
    uint64_t cap;
    uint64_t namespace_blocks;
    uint32_t namespace_id, block_size;
    uint16_t next_cid;
    uint8_t initialized;
    nvme_queue_t admin, io;

    /* DMA addresses equal virtual addresses while the kernel is identity mapped. */
    uint8_t admin_sq_page[NVME_PAGE_SIZE] __attribute__((aligned(NVME_PAGE_SIZE)));
    uint8_t admin_cq_page[NVME_PAGE_SIZE] __attribute__((aligned(NVME_PAGE_SIZE)));
    uint8_t io_sq_page[NVME_PAGE_SIZE] __attribute__((aligned(NVME_PAGE_SIZE)));
    uint8_t io_cq_page[NVME_PAGE_SIZE] __attribute__((aligned(NVME_PAGE_SIZE)));
    uint8_t identify_page[NVME_PAGE_SIZE] __attribute__((aligned(NVME_PAGE_SIZE)));
    uint8_t read_page[NVME_PAGE_SIZE] __attribute__((aligned(NVME_PAGE_SIZE)));
} nvme_controller_t;

static nvme_controller_t controller __attribute__((aligned(NVME_PAGE_SIZE)));

static void nvme_barrier(void)
{
    __sync_synchronize();
}

static uint64_t nvme_phys(const void* ptr)
{
    return (uint64_t)ptr;
}

static int nvme_wait_ready(nvme_controller_t* c, uint8_t wanted)
{
    for (uint64_t i = 0; i < NVME_TIMEOUT_ITERATIONS; i++) {
        uint32_t status = c->regs->csts;
        if (status & NVME_CSTS_CFS) {
            con_write("NVMe: controller fatal status\n", 0x04);
            return 0;
        }
        if (!!(status & NVME_CSTS_RDY) == wanted) return 1;
        __asm__ volatile("pause");
    }
    con_write("NVMe: RDY timeout\n", 0x04);
    return 0;
}

static int nvme_wait_completion(nvme_controller_t* c, nvme_queue_t* q,
                                uint16_t expected_cid, uint32_t* result)
{
    for (uint64_t i = 0; i < NVME_TIMEOUT_ITERATIONS; i++) {
        volatile nvme_cqe_t* entry = &q->cq[q->head];
        uint16_t status = entry->status;
        if ((status & 1u) == q->phase) {
            nvme_barrier();
            uint16_t cid = entry->cid;
            if (result) *result = entry->result;

            q->head++;
            if (q->head == q->depth) {
                q->head = 0;
                q->phase ^= 1;
            }
            nvme_barrier();
            *q->cq_doorbell = q->head;
            nvme_barrier();

            if (cid != expected_cid) {
                con_write("NVMe: unexpected command ID\n", 0x04);
                return 0;
            }
            /* Bit 0 is phase; all other completion status bits must be clear. */
            if (status & ~1u) {
                con_write("NVMe command failed. SCT=", 0x04);
                print_uint64_dec((status >> 9) & 7u);
                con_write(" SC=", 0x04);
                print_uint64_dec((status >> 1) & 0xffu);
                con_write("\n", 0x04);
                return 0;
            }
            return 1;
        }
        if (c->regs->csts & NVME_CSTS_CFS) {
            con_write("NVMe: fatal status while waiting for command\n", 0x04);
            return 0;
        }
        __asm__ volatile("pause");
    }
    con_write("NVMe: command completion timeout\n", 0x04);
    return 0;
}

/* Commands are synchronous; one submission is outstanding at a time. */
static int nvme_submit(nvme_controller_t* c, nvme_queue_t* q,
                       nvme_sqe_t* command, uint32_t* result)
{
    uint16_t cid = c->next_cid++;
    command->cid = cid;
    memory_copy((void*)&q->sq[q->tail], command, sizeof(*command));
    nvme_barrier();

    q->tail++;
    if (q->tail == q->depth) q->tail = 0;
    *q->sq_doorbell = q->tail;
    nvme_barrier();
    return nvme_wait_completion(c, q, cid, result);
}

static int nvme_set_num_queues(nvme_controller_t* c)
{
    nvme_sqe_t command;
    memory_zero(&command, sizeof(command));
    uint32_t result;
    command.opcode = NVME_ADMIN_SET_FEATURES;
    command.cdw10 = NVME_FEATURE_NUM_QUEUES;
    /* Zero based SQ/CQ counts: request one of each. */
    if (!nvme_submit(c, &c->admin, &command, &result)) return 0;

    uint32_t sq_count = (result & 0xffffu) + 1u;
    uint32_t cq_count = (result >> 16) + 1u;
    con_write("NVMe allocated IO SQs: ", 0x07);
    print_uint64_dec(sq_count);
    con_write(" CQs: ", 0x07);
    print_uint64_dec(cq_count);
    con_write("\n", 0x07);
    return 1;
}

static int nvme_identify(nvme_controller_t* c, uint32_t nsid, uint32_t kind)
{
    nvme_sqe_t command;
    memory_zero(&command, sizeof(command));
    memory_zero(c->identify_page, NVME_PAGE_SIZE);
    command.opcode = NVME_ADMIN_IDENTIFY;
    command.nsid = nsid;
    command.prp1 = nvme_phys(c->identify_page);
    command.cdw10 = kind;
    return nvme_submit(c, &c->admin, &command, 0);
}

static int nvme_find_namespace(nvme_controller_t* c)
{
    if (!nvme_identify(c, 0, NVME_IDENTIFY_ACTIVE_NS_LIST)) {
        con_write("NVMe: failed getting namespace list\n", 0x04);
        return 0;
    }
    c->namespace_id = *(uint32_t*)c->identify_page;
    if (!c->namespace_id) {
        con_write("NVMe: no active namespaces\n", 0x04);
        return 0;
    }
    con_write("NVMe namespace: ", 0x07);
    print_uint64_dec(c->namespace_id);
    con_write("\n", 0x07);
    return 1;
}

static int nvme_identify_namespace(nvme_controller_t* c)
{
    if (!nvme_identify(c, c->namespace_id, NVME_IDENTIFY_NAMESPACE)) {
        con_write("NVMe: Identify Namespace failed\n", 0x04);
        return 0;
    }

    /* NSZE starts at byte 0. FLBAS byte 26 selects an LBAF at byte 128. */
    c->namespace_blocks = *(uint64_t*)c->identify_page;
    uint8_t format = c->identify_page[26] & 0x0fu;
    uint32_t lbaf = *(uint32_t*)&c->identify_page[128 + 4u * format];
    uint8_t lbads = (lbaf >> 16) & 0xffu;
    if (lbaf & 0xffffu) {
        con_write("NVMe: namespaces with metadata are unsupported\n", 0x04);
        return 0;
    }
    if (lbads >= 32) {
        con_write("NVMe: invalid LBADS\n", 0x04);
        return 0;
    }
    c->block_size = 1u << lbads;
    if (c->block_size > NVME_PAGE_SIZE || NVME_PAGE_SIZE % c->block_size) {
        con_write("NVMe: block size incompatible with 4096-byte reads\n", 0x04);
        return 0;
    }
    con_write("NVMe block size: ", 0x07);
    print_uint64_dec(c->block_size);
    con_write("\nNVMe namespace blocks: ", 0x07);
    print_uint64_dec(c->namespace_blocks);
    con_write("\n", 0x07);
    return 1;
}

static int nvme_create_io_cq(nvme_controller_t* c)
{
    nvme_sqe_t command;
    memory_zero(&command, sizeof(command));
    command.opcode = NVME_ADMIN_CREATE_IO_CQ;
    command.prp1 = nvme_phys(c->io_cq_page);
    command.cdw10 = ((uint32_t)(c->io.depth - 1) << 16) | NVME_IO_QUEUE_ID;
    command.cdw11 = 1u; /* Physically contiguous; poll completions. */
    if (!nvme_submit(c, &c->admin, &command, 0)) {
        con_write("NVMe: Create IO CQ failed\n", 0x04);
        return 0;
    }
    return 1;
}

static int nvme_create_io_sq(nvme_controller_t* c)
{
    nvme_sqe_t command;
    memory_zero(&command, sizeof(command));
    command.opcode = NVME_ADMIN_CREATE_IO_SQ;
    command.prp1 = nvme_phys(c->io_sq_page);
    command.cdw10 = ((uint32_t)(c->io.depth - 1) << 16) | NVME_IO_QUEUE_ID;
    command.cdw11 = ((uint32_t)NVME_IO_QUEUE_ID << 16) | 1u;
    if (!nvme_submit(c, &c->admin, &command, 0)) {
        con_write("NVMe: Create IO SQ failed\n", 0x04);
        return 0;
    }
    return 1;
}

static void nvme_setup_queue(nvme_queue_t* q, uint8_t* sq_page, uint8_t* cq_page,
                             uint16_t depth, uint64_t doorbell_base, uint32_t stride,
                             uint32_t queue_id)
{
    q->sq = (volatile nvme_sqe_t*)sq_page;
    q->cq = (volatile nvme_cqe_t*)cq_page;
    q->depth = depth;
    q->phase = 1;
    q->sq_doorbell = (volatile uint32_t*)(doorbell_base + 2u * queue_id * stride);
    q->cq_doorbell = (volatile uint32_t*)(doorbell_base + (2u * queue_id + 1u) * stride);
}

static int nvme_init(nvme_controller_t* c, uint64_t bar)
{
    memory_zero(c, sizeof(*c));
    c->regs = (volatile nvme_regs_t*)bar;
    c->cap = c->regs->cap;

    uint32_t version = c->regs->vs;
    con_write("NVMe version: ", 0x07);
    print_uint64_dec(version >> 16);
    con_write(".", 0x07);
    print_uint64_dec((version >> 8) & 0xffu);
    con_write(".", 0x07);
    print_uint64_dec(version & 0xffu);
    con_write("\n", 0x07);

    if ((c->cap >> 48) & 0xfu) {
        con_write("NVMe: 4K pages unsupported\n", 0x04);
        return 0;
    }
    uint32_t max_entries = (uint32_t)(c->cap & 0xffffu) + 1u;
    if (max_entries < 2) {
        con_write("NVMe: queue limit too small\n", 0x04);
        return 0;
    }
    uint16_t admin_depth = max_entries < NVME_ADMIN_QUEUE_DEPTH
                         ? max_entries : NVME_ADMIN_QUEUE_DEPTH;
    uint16_t io_depth = max_entries < NVME_IO_QUEUE_DEPTH
                      ? max_entries : NVME_IO_QUEUE_DEPTH;

    c->regs->cc &= ~NVME_CC_EN;
    nvme_barrier();
    if (!nvme_wait_ready(c, 0)) return 0;

    uint64_t admin_sq_phys = nvme_phys(c->admin_sq_page);
    uint64_t admin_cq_phys = nvme_phys(c->admin_cq_page);
    if ((admin_sq_phys | admin_cq_phys | nvme_phys(c->io_sq_page) |
         nvme_phys(c->io_cq_page) | nvme_phys(c->identify_page) |
         nvme_phys(c->read_page)) & (NVME_PAGE_SIZE - 1)) {
        con_write("NVMe: DMA page not aligned\n", 0x04);
        return 0;
    }

    uint32_t admin_size = admin_depth - 1;
    c->regs->aqa = (admin_size << 16) | admin_size;
    c->regs->asq = admin_sq_phys;
    c->regs->acq = admin_cq_phys;
    uint32_t cc = NVME_CC_IOSQES | NVME_CC_IOCQES;
    c->regs->cc = cc; /* 4K pages and NVM command set use zero-valued fields. */
    nvme_barrier();

    uint32_t stride = 4u << ((c->cap >> 32) & 0xfu);
    for (uint32_t index = 0; index <= 2 * NVME_IO_QUEUE_ID + 1; index++)
        mmu_identity_map_range(bar + 0x1000 + (uint64_t)index * stride, 4,
                               PAGE_RW | PAGE_NX | PAGE_PCD | PAGE_PWT);
    nvme_setup_queue(&c->admin, c->admin_sq_page, c->admin_cq_page,
                     admin_depth, bar + 0x1000, stride, 0);
    nvme_setup_queue(&c->io, c->io_sq_page, c->io_cq_page,
                     io_depth, bar + 0x1000, stride, NVME_IO_QUEUE_ID);
    c->next_cid = 1;

    c->regs->cc = cc | NVME_CC_EN;
    nvme_barrier();
    if (!nvme_wait_ready(c, 1)) return 0;
    con_write("NVMe controller ready\n", 0x02);

    if (!nvme_set_num_queues(c) || !nvme_find_namespace(c) ||
        !nvme_identify_namespace(c)) return 0;
    /* The completion queue must exist before its submission queue. */
    if (!nvme_create_io_cq(c) || !nvme_create_io_sq(c)) return 0;

    c->initialized = 1;
    con_write("NVMe initialization complete\n", 0x02);
    return 1;
}

uint8_t* nvme_read_block(uint8_t nvme_index, uint64_t lba)
{
    if (nvme_index != 0 || !controller.initialized) return 0;
    nvme_controller_t* c = &controller;
    uint32_t block_count = NVME_PAGE_SIZE / c->block_size;
    if (lba >= c->namespace_blocks ||
        block_count > c->namespace_blocks - lba) {
        con_write("NVMe: LBA out of range for 4096-byte read\n", 0x04);
        return 0;
    }

    nvme_sqe_t command;
    memory_zero(&command, sizeof(command));
    command.opcode = NVME_CMD_READ;
    command.nsid = c->namespace_id;
    /* The aligned buffer and exact 4K transfer fit in PRP1. */
    command.prp1 = nvme_phys(c->read_page);
    command.cdw10 = (uint32_t)lba;
    command.cdw11 = (uint32_t)(lba >> 32);
    command.cdw12 = block_count - 1; /* NLB is zero based. */
    if (!nvme_submit(c, &c->io, &command, 0)) {
        con_write("NVMe: READ failed\n", 0x04);
        return 0;
    }
    return c->read_page;
}

void nvme_driver_init(void)
{
    uint32_t id = pcie_find_device(1, 8, 2, PCIE_NO_DEVICE);
    if (id == PCIE_NO_DEVICE) return;
    con_write("NVMe PCI ID: ", 0x03);
    print_uint64(id);
    uint64_t bar = pcie_get_device_bar(id, 0);
    if (!bar) {
        con_write("NVMe BAR unavailable\n", 0x04);
        return;
    }
    con_write("\nNVMe BAR: ", 0x03);
    print_uint64(bar);
    con_write("\n", 0x07);

    /* PCI memory space and bus mastering must already be enabled for DMA. */
    if (!nvme_init(&controller, bar)) {
        con_write("NVMe initialization FAILED\n", 0x04);
        return;
    }

    con_write("Reading NVMe block 0...\n", 0x07);
    uint8_t* page = nvme_read_block(0, 0);
    if (!page) {
        con_write("NVMe test read FAILED\n", 0x04);
        return;
    }
    con_write("NVMe test read SUCCESS\nFirst 32 bytes:\n", 0x02);
    for (uint32_t i = 0; i < 32; i++) {
        print_uint64(page[i]);
        con_write(" ", 0x07);
    }
    con_write("\n", 0x07);
    if (page[510] == 0x55 && page[511] == 0xaa)
        con_write("MBR signature success\n", 0x02);
    else
        con_write("MBR signature failed\n", 0x01);
}
