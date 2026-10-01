#include "minimum/uart.h"

#include <stdint.h>

#include "minemu/platform.h"
#include "minimum/interrupts.h"

/* The console is wired to UART0; every access goes through the struct. */
#define CONSOLE MINEMU_UART0

/* ------------------------------------------------------------------------ */
/* Transmit (polled)                                                         */
/* ------------------------------------------------------------------------ */

void uart_putc(char c)
{
    /* Spin until the transmitter has room. Reading STATUS is side-effect free. */
    while ((CONSOLE->status & MINEMU_UART_STATUS_TX_READY) == 0)
    {
    }
    /* One 32-bit write; only the low 8 bits are transmitted. */
    CONSOLE->tx_data = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0')
    {
        uart_putc(*s++);
    }
}

void uart_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        uart_putc(buf[i]);
    }
}

/* ------------------------------------------------------------------------ */
/* Receive (interrupt-driven)                                                */
/* ------------------------------------------------------------------------ */

/*
 * Single-producer (IRQ handler) / single-consumer (uart_getc) ring buffer.
 *
 * head and tail are free-running counters; the slot is (counter & MASK), so
 * the size must be a power of two. This needs no division (% would call the
 * libgcc divide helper, which hangs under minemu), and full vs. empty are
 * unambiguous: empty when head == tail, full when head - tail == SIZE.
 *
 * Shared between two execution contexts, so:
 *   - volatile: the compiler must re-read them, not cache them in registers;
 *   - the consumer touches them only inside irq_save()/irq_restore(), so the
 *     handler can never run in the middle of a read-modify-write.
 *     (The handler itself runs with IRQs masked by hardware.)
 */
#define RX_BUFFER_SIZE 256u
#define RX_BUFFER_MASK (RX_BUFFER_SIZE - 1u)
_Static_assert((RX_BUFFER_SIZE & RX_BUFFER_MASK) == 0, "RX buffer size must be a power of two");

static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint32_t rx_head; /* written only by the IRQ handler */
static volatile uint32_t rx_tail; /* written only by uart_getc */

static uint32_t rx_count(void)
{
    return rx_head - rx_tail; /* unsigned wrap-around makes this correct */
}

/*
 * Flow control instead of dropping bytes.
 *
 * The handler must leave RX_READY clear before EOI, or the interrupt fires
 * again immediately. If the software buffer fills, the handler can't drain
 * the hardware FIFO, so it turns the UART's RX interrupt off; bytes wait
 * losslessly in the 4 KiB hardware FIFO. uart_getc() turns it back on once
 * it has made room. (Only if more than FIFO + buffer bytes arrive while the
 * shell isn't reading does the hardware drop its oldest byte.)
 */
static void uart_rx_interrupt(void)
{
    while ((CONSOLE->status & MINEMU_UART_STATUS_RX_READY) != 0)
    {
        if (rx_count() == RX_BUFFER_SIZE)
        {
            CONSOLE->control = 0; /* pause RX interrupts until there is room */
            return;
        }
        /* Reading RX_DATA pops one byte from the hardware FIFO. */
        rx_buffer[rx_head & RX_BUFFER_MASK] = (uint8_t)CONSOLE->rx_data;
        rx_head = rx_head + 1u;
    }
}

void uart_init(void)
{
    CONSOLE->control = 0;
    rx_head = 0;
    rx_tail = 0;
}

void uart_enable_rx_interrupts(void)
{
    irq_register(MINEMU_IRQ_UART0, uart_rx_interrupt);
    CONSOLE->control = MINEMU_UART_CONTROL_RX_IRQ_ENABLE;
}

char uart_getc(void)
{
    for (;;)
    {
        uint32_t saved = irq_save();

        if (rx_count() != 0)
        {
            char c = (char)rx_buffer[rx_tail & RX_BUFFER_MASK];
            rx_tail = rx_tail + 1u;
            /* Room again: resume RX interrupts if the handler paused them. */
            CONSOLE->control = MINEMU_UART_CONTROL_RX_IRQ_ENABLE;
            irq_restore(saved);
            return c;
        }

        /*
         * Empty. Sleep with IRQs still masked: checking and sleeping as one
         * step closes the race where a byte arrives between "buffer is
         * empty" and "wfi" (which would sleep with data waiting). WFI still
         * wakes on the pending interrupt; restoring IRQs lets it be taken,
         * then we loop and re-check.
         */
        cpu_wait_for_interrupt();
        irq_restore(saved);
    }
}
