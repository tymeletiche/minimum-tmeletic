#ifndef MINIMUM_UART_H
#define MINIMUM_UART_H

#include <stddef.h>

/*
 * Console UART driver (UART0).
 *
 * Transmit is polled: each byte waits for STATUS.TX_READY, then is written
 * to TX_DATA with a single aligned 32-bit store (the only access the MMIO
 * block allows). Output is raw bytes: no "\n" -> "\r\n" translation, because
 * the TUI and the graders compare the exact byte stream.
 */

/* Put the UART in a known state: RX interrupts off. Call once at boot. */
void uart_init(void);

/* Blocking: waits until the transmitter can accept a byte, then sends it. */
void uart_putc(char c);

/* Send a NUL-terminated string. */
void uart_puts(const char *s);

/* Send exactly len bytes (may contain NULs). */
void uart_write(const char *buf, size_t len);

#endif
