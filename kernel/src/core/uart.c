#include "minimum/uart.h"

#include <stdint.h>

#include "minemu/platform.h"

/* The console is wired to UART0; every access goes through the struct. */
#define CONSOLE MINEMU_UART0

void uart_init(void) {
    /* Task 2 turns on MINEMU_UART_CONTROL_RX_IRQ_ENABLE here. */
    CONSOLE->control = 0;
}

void uart_putc(char c) {
    /* Spin until the transmitter has room. Reading STATUS is side-effect free. */
    while ((CONSOLE->status & MINEMU_UART_STATUS_TX_READY) == 0) {
    }
    /* One 32-bit write; only the low 8 bits are transmitted. */
    CONSOLE->tx_data = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

void uart_write(const char *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        uart_putc(buf[i]);
    }
}
