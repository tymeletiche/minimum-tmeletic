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
 *
 * Receive is interrupt-driven: the RX interrupt handler moves bytes from the
 * hardware FIFO into a software ring buffer; uart_getc() takes bytes out of
 * that buffer, sleeping (WFI) while it is empty.
 */

/* Put the UART in a known state with RX interrupts off. Call once at boot. */
void uart_init(void);

/*
 * Register the RX interrupt handler and enable RX interrupts at the UART.
 * Call after uart_init() and before unmasking IRQs on the CPU.
 */
void uart_enable_rx_interrupts(void);

/* Blocking: waits until the transmitter can accept a byte, then sends it. */
void uart_putc(char c);

/* Send a NUL-terminated string. */
void uart_puts(const char *s);

/* Send exactly len bytes (may contain NULs). */
void uart_write(const char *buf, size_t len);

/*
 * Blocking: returns the next received byte, sleeping until one arrives.
 * Must be called with IRQs enabled (otherwise nothing can ever arrive).
 */
char uart_getc(void);

#endif
