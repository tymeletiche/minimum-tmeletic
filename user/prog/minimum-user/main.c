/* HW2 Task 1: user-mode hello world through the ioctl syscall. */
#include <stdint.h>

#include "minemu/user_abi.h"

void minemu_user_main(void) {
    static const char message[] = "hello world\n";
    (void)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)message,
                (uint32_t)(sizeof(message) - 1U));
    for (;;) {
    }
}
