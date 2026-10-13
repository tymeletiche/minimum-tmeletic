#include "minemu/boot.h"
#include "minemu/trap.h"
#include "minemu/trace.h"
#include "minemu/irq.h"
#include "minimum/kprintf.h"
#include "minimum/loader.h"
#include "minimum/uart.h"

void minemu_kernel_main(const struct minemu_boot_info *boot_info) {
    if ((uintptr_t)boot_info != MINEMU_BOOT_INFO_VADDR ||
        boot_info->magic != MINEMU_BOOT_INFO_MAGIC ||
        boot_info->version != MINEMU_ABI_VERSION ||
        boot_info->size != sizeof(*boot_info) ||
        boot_info->system_rom_base != UINT32_C(0x08000000) ||
        boot_info->direct_map_vaddr != UINT32_C(0xc0000000) ||
        boot_info->direct_map_paddr != UINT32_C(0x40000000) ||
        boot_info->direct_map_size != UINT32_C(0x04000000)) {
        minemu_trace_event(UINT32_C(0xb007bad0));
        minemu_fail_stop();
    }

    uart_init();

    /*
     * Interrupt bring-up, innermost first: the handler table and UART RX
     * enable (irq_register also unmasks UART0 in the interrupt controller),
     * and only then the CPU's IRQ mask. Unmasking the CPU first could take
     * an interrupt before its handler exists.
     */
    uart_enable_rx_interrupts();
    minemu_irq_enable();

    /*
     * HW2: msh now lives in user space. Map the image, load the
     * "minimum-user" module into its own address space, and drop to USR.
     * From here on the kernel only runs on SVC (syscall), IRQ, and abort
     * entries.
     */
    uint32_t entry;
    if (!loader_map_system_rom(boot_info) ||
        !loader_load_module(boot_info, "minimum-user", &entry)) {
        kprintf("failed to load minimum-user\n");
        minemu_trace_event(UINT32_C(0xb007bad1));
        minemu_fail_stop();
    }
    loader_enter_user(entry, USER_STACK_TOP);
}
