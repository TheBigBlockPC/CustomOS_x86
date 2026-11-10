#ifndef GDT_H
#define GDT_H

#define GDT_TSS_INDEX 5
#define GDT_TSS_SELECTOR (GDT_TSS_INDEX * 8)

void gdt_init();
void gdt_set_tss(void);

#endif