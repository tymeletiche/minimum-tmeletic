#ifndef MINIMUM_LOADER_H
#define MINIMUM_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "minemu/boot.h"

/*
 * User address-space layout (one 4 MiB page-directory slot, one page table):
 *   0x00400000 .. 0x007ff000  program image (code, rodata, data, bss)
 *   0x007ff000 .. 0x00800000  user stack (one page, RW, never executable)
 */
#define USER_IMAGE_BASE UINT32_C(0x00400000)
#define USER_STACK_BOTTOM UINT32_C(0x007ff000)
#define USER_STACK_TOP UINT32_C(0x00800000)

/* Map the System ROM image read-only into the kernel's address space. */
bool loader_map_system_rom(const struct minemu_boot_info *boot_info);

/*
 * Find module `name` in the image, copy its segments into fresh RAM pages,
 * zero BSS, build the user page table and stack, and install it.
 * On success stores the module's entry address in *entry.
 */
bool loader_load_module(const struct minemu_boot_info *boot_info, const char *name,
                        uint32_t *entry);

/*
 * Page-table lookup for the syscall layer: the PTE that maps user virtual
 * address `vaddr`, or 0 if `vaddr` is outside the user window / unmapped.
 */
uint32_t loader_user_pte(uint32_t vaddr);

/*
 * Kernel pointer to the byte backing user address `vaddr`. Only valid after
 * the page has been checked with loader_user_pte (it does no checking).
 */
uint8_t *loader_user_kaddr(uint32_t vaddr);

/* Drop to USR mode at `entry` with sp = stack_top. IRQs enabled in user. */
void loader_enter_user(uint32_t entry, uint32_t stack_top) __attribute__((noreturn));

#endif
