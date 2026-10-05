#include "idt.h"

#define IDT_SIZE 256

struct idt_entry idt[IDT_SIZE];
struct idt_ptr   idt_descriptor;

extern void isr0_stub(void);
extern void isr1_stub(void);
extern void isr6_stub(void);
extern void isr8_stub(void);
extern void isr13_stub(void);
extern void isr14_stub(void);

extern void irq0_stub(void);
extern void irq1_stub(void);

extern uint64_t isr_default_table[256];

extern void pic_remap(void);
extern void idt_load(void);

/*
 * Install one IDT gate.
 *
 *   vec      : interrupt vector number
 *   handler  : 64-bit address of the assembly stub
 *   ist      : IST index (0 = no IST, 1 = IST1, ..., 7 = IST7).
 *              The CPU switches to the corresponding TSS IST stack
 *              before pushing the exception frame. Used for #DF so a
 *              double fault can't fault on its own frame push.
 */
static void set_idt_entry(int vec, uint64_t handler, uint8_t ist) {
    idt[vec].offset_low  = handler & 0xFFFF;
    idt[vec].selector    = 0x18;
    idt[vec].ist         = ist & 0x7;   /* bits 0..2 only; 3..7 reserved */
    idt[vec].type_attr   = 0x8E;
    idt[vec].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[vec].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vec].zero        = 0;
}

void idt_init(void) {
    /*
     * Install all 256 gates.  Vectors 0, 1, 6, 8, 13, 14, 32, 33 have
     * dedicated stubs (see isr.asm) with special frame handling.  All
     * other vectors use isr_default_*, which push a dummy error code
     * (or not, depending on whether the CPU pushes one), push the
     * vector number, and jump to a common handler that prints the
     * vector and halts.
     *
     * Having every vector installed is what prevents a stray spurious
     * IRQ or an unexpected CPU exception from triple-faulting and
     * rebooting without diagnostics.
     */
    for (int vec = 0; vec < IDT_SIZE; vec++) {
        uint64_t handler;
        uint8_t  ist = 0;
        switch (vec) {
            case 0:  handler = (uint64_t)isr0_stub;  break;
            case 1:  handler = (uint64_t)isr1_stub;  break;
            case 6:  handler = (uint64_t)isr6_stub;  break;
            case 8:
                handler = (uint64_t)isr8_stub;
                /* IST1 is reserved for #DF. The CPU switches to
                 * tss->ist1 before pushing the double-fault frame,
                 * so a corrupted kernel stack cannot turn a
                 * diagnosable #DF into a silent triple fault. */
                ist = 1;
                break;
            case 13: handler = (uint64_t)isr13_stub; break;
            case 14: handler = (uint64_t)isr14_stub; break;
            case 32: handler = (uint64_t)irq0_stub;  break;
            case 33: handler = (uint64_t)irq1_stub;  break;
            default: handler = isr_default_table[vec]; break;
        }
        set_idt_entry(vec, handler, ist);
    }

    pic_remap();

    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base  = (uint64_t)&idt;

    idt_load();
}
