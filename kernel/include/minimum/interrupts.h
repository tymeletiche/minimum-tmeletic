#ifndef MINIMUM_INTERRUPTS_H
#define MINIMUM_INTERRUPTS_H

#include <stdint.h>

#include "minemu/irq.h"

/*
 * Kernel interrupt layer on top of the fixed minemu/irq.h interface.
 *
 * minemu_irq_dispatch() (interrupts.c) looks up the handler for the claimed
 * source in a table, runs it, then writes EOI. Handlers must clear their
 * device's reason for interrupting (e.g. drain UART RX) before returning.
 */

typedef void (*irq_handler_t)(void);

/* Install a handler and unmask the source in the interrupt controller. */
void irq_register(uint32_t source, irq_handler_t handler);

/*
 * Critical sections that nest correctly.
 *
 * irq_save() masks IRQs and returns the previous CPSR; irq_restore() only
 * re-enables IRQs if they were enabled at the matching irq_save(). A plain
 * disable/enable pair would wrongly turn interrupts on inside an outer
 * critical section (or inside the IRQ handler itself, where they are masked
 * by hardware).
 */
#define CPSR_IRQ_MASKED UINT32_C(0x80) /* CPSR.I bit */

static inline uint32_t irq_save(void) {
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr\n\tcpsid i" : "=r"(cpsr) : : "memory");
    return cpsr;
}

static inline void irq_restore(uint32_t saved_cpsr) {
    if ((saved_cpsr & CPSR_IRQ_MASKED) == 0) {
        minemu_irq_enable();
    }
}

/*
 * Sleep until an interrupt is pending. Safe to call with IRQs masked: WFI
 * wakes on a pending interrupt regardless of CPSR.I, and the interrupt is
 * then taken as soon as the caller restores IRQs.
 */
static inline void cpu_wait_for_interrupt(void) {
    __asm__ volatile("wfi" : : : "memory");
}

#endif
