#include "minimum/interrupts.h"

#include <stddef.h>

#include "minemu/platform.h"

/*
 * One slot per interrupt-controller source. The controller has 4 sources
 * (SYSTICK, UART0, UART1, BLOCK), matching MINEMU_IRQ_ENABLE_MASK = 0x0f.
 */
#define IRQ_SOURCE_COUNT 4u

static irq_handler_t handlers[IRQ_SOURCE_COUNT];

void irq_register(uint32_t source, irq_handler_t handler) {
    if (source >= IRQ_SOURCE_COUNT) {
        return;
    }
    uint32_t saved = irq_save();
    handlers[source] = handler;
    MINEMU_INTERRUPT->enable = MINEMU_INTERRUPT->enable | (UINT32_C(1) << source);
    irq_restore(saved);
}

/*
 * Called by minemu_irq_trampoline in IRQ mode, with IRQs masked, after it has
 * saved the interrupted context in *frame and stored the CLAIM register value
 * in frame->exception_id.
 */
struct minemu_trap_frame *minemu_irq_dispatch(struct minemu_trap_frame *frame) {
    uint32_t source = (uint32_t)frame->exception_id;

    /* CLAIM reads MINEMU_IRQ_NONE if nothing is active (spurious entry). */
    if (source >= IRQ_SOURCE_COUNT) {
        return frame;
    }

    if (handlers[source] != NULL) {
        handlers[source]();
    } else {
        /*
         * No handler: the device will keep asserting its line, so EOI alone
         * would re-enter here forever. Mask the source instead.
         */
        MINEMU_INTERRUPT->enable = MINEMU_INTERRUPT->enable & ~(UINT32_C(1) << source);
    }

    /* Tell the controller this source is serviced. */
    MINEMU_INTERRUPT->eoi = source;

    /* Same frame for now; a scheduler would return a different task's frame. */
    return frame;
}
