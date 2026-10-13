/*
 * User module loader and address-space setup.
 *
 * Adapted from examples/user-mode-hello/kernel/main.c (minimum-template,
 * rchtsang, authored with gpt5.6-sol), cited per the course policy. Changes:
 * split into a reusable API (minimum/loader.h), page/flag bookkeeping kept
 * queryable so the syscall layer can validate user pointers, divisions
 * written as shifts (the libgcc divide helper hangs under minemu 0.2.3).
 */
#include "minimum/loader.h"

#include <stddef.h>

#include "minemu/mmu.h"
#include "minemu/platform.h"

#define BOOTSTRAP_PAGE_DIRECTORY_PADDR UINT32_C(0x40010000)
#define MAX_USER_IMAGE_PAGES 16U
#define SYSTEM_ROM_TABLE_COUNT 1U /* 4 MiB of ROM; the image is far smaller */
#define PAGE_SHIFT 12U
#define TABLE_SHIFT 22U
#define TABLE_ENTRIES 1024U

/*
 * Page tables, user pages and the user stack come from a bump allocator over
 * the RAM past the end of the kernel image, not from static arrays: ~90 KiB
 * of page-aligned .bss made the boot-time BSS clear alone cost ~500k ticks,
 * past the msh-prompt deadline. Only pages actually used get zeroed.
 */
extern char __minemu_und_stack_top[]; /* last thing the kernel linker script places */
static uint32_t next_free_page;

static uint32_t *user_page_table;
static uint8_t *user_image_pages[MAX_USER_IMAGE_PAGES];
static uint8_t *user_stack;
static uint32_t user_page_vaddrs[MAX_USER_IMAGE_PAGES];
static uint32_t user_page_flags[MAX_USER_IMAGE_PAGES];
static size_t user_page_count;

/*
 * Page-sized clears go a word at a time (the kernel builds at -O0, so a
 * byte loop costs ~4x the emulator ticks).
 */
static void zero_words(void *destination, size_t bytes) {
    uint32_t *words = destination;
    for (size_t index = 0; index < (bytes >> 2); ++index) {
        words[index] = 0;
    }
}

/* One zeroed 4 KiB page from the kernel direct map, or NULL when RAM runs out. */
static void *alloc_page(void) {
    if (next_free_page == 0) {
        next_free_page = ((uint32_t)(uintptr_t)__minemu_und_stack_top + MINEMU_PAGE_SIZE - 1U) &
                         MINEMU_PTE_PAGE_MASK;
    }
    if (next_free_page >= MINEMU_KERNEL_DIRECT_BASE + MINEMU_KERNEL_DIRECT_SIZE) {
        return NULL;
    }
    void *page = (void *)(uintptr_t)next_free_page;
    next_free_page += MINEMU_PAGE_SIZE;
    zero_words(page, MINEMU_PAGE_SIZE);
    return page;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t index = 0; index < length; ++index) {
        out[index] = in[index];
    }
}

static bool bytes_equal(const uint8_t *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != (uint8_t)right[index]) {
            return false;
        }
    }
    return true;
}

static size_t string_length(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

/* All image offsets/lengths come from the image itself, so bounds-check them. */
static bool image_range_valid(uint32_t offset, uint32_t length, uint32_t image_size) {
    return offset <= image_size && length <= image_size - offset;
}

/* Module and segment records are both 32 bytes, hence >> 5. */
static bool image_records_valid(uint32_t offset, uint32_t count, uint32_t image_size) {
    return offset <= image_size && count <= ((image_size - offset) >> 5);
}

static uint32_t kernel_vaddr_to_paddr(const void *address) {
    return (uint32_t)(uintptr_t)address - MINEMU_KERNEL_DIRECT_BASE + MINEMU_RAM_BASE;
}

static uint32_t paddr_to_kernel_vaddr(uint32_t paddr) {
    return paddr - MINEMU_RAM_BASE + MINEMU_KERNEL_DIRECT_BASE;
}

static volatile uint32_t *active_page_directory(void) {
    return (volatile uint32_t *)(uintptr_t)paddr_to_kernel_vaddr(BOOTSTRAP_PAGE_DIRECTORY_PADDR);
}

bool loader_map_system_rom(const struct minemu_boot_info *boot_info) {
    if (boot_info->system_rom_base != MINEMU_SYSTEM_ROM_BASE || boot_info->image_size == 0 ||
        boot_info->image_size > MINEMU_SYSTEM_ROM_SIZE) {
        return false;
    }

    uint32_t page_count = (boot_info->image_size + MINEMU_PAGE_SIZE - 1U) >> PAGE_SHIFT;
    uint32_t table_count = (page_count + TABLE_ENTRIES - 1U) >> 10;
    if (table_count > SYSTEM_ROM_TABLE_COUNT) {
        return false;
    }

    volatile uint32_t *directory = active_page_directory();
    for (uint32_t table = 0; table < table_count; ++table) {
        uint32_t *rom_table = alloc_page();
        if (rom_table == NULL) {
            return false;
        }
        uint32_t remaining = page_count - table * TABLE_ENTRIES;
        uint32_t entries = remaining < TABLE_ENTRIES ? remaining : TABLE_ENTRIES;
        for (uint32_t index = 0; index < entries; ++index) {
            uint32_t page = table * TABLE_ENTRIES + index;
            /* Supervisor-only, read-only: no USER bit. */
            if (index < entries) {
                rom_table[index] = (boot_info->system_rom_base + (page << PAGE_SHIFT)) |
                                   MINEMU_PTE_VALID | MINEMU_PTE_READABLE;
            }
        }
        uint32_t directory_index = (boot_info->system_rom_base >> TABLE_SHIFT) + table;
        directory[directory_index] = kernel_vaddr_to_paddr(rom_table) | MINEMU_PDE_VALID;
    }
    minemu_mmu_tlbiall();
    return true;
}

static const struct minemu_module_record *find_module(const struct minemu_boot_info *boot_info,
                                                       const char *name, size_t name_length) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    if (!image_records_valid(boot_info->module_table_offset, boot_info->module_count,
                             boot_info->image_size)) {
        return NULL;
    }

    const struct minemu_module_record *modules =
        (const struct minemu_module_record *)(image + boot_info->module_table_offset);
    for (uint32_t index = 0; index < boot_info->module_count; ++index) {
        const struct minemu_module_record *module = &modules[index];
        if (module->name_length == name_length &&
            image_range_valid(module->name_offset, module->name_length, boot_info->image_size) &&
            bytes_equal(image + module->name_offset, name, name_length)) {
            return module;
        }
    }
    return NULL;
}

static uint32_t pte_flags_from_segment(uint32_t segment_flags) {
    uint32_t pte_flags = 0;
    if ((segment_flags & MINEMU_SEGMENT_READABLE) != 0) {
        pte_flags |= MINEMU_PTE_READABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_WRITABLE) != 0) {
        pte_flags |= MINEMU_PTE_WRITABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_EXECUTABLE) != 0) {
        pte_flags |= MINEMU_PTE_EXECUTABLE;
    }
    return pte_flags;
}

/* Find (or, with create, allocate) the RAM page backing user page `page_vaddr`. */
static int user_page_index(uint32_t page_vaddr, uint32_t flags, bool create) {
    for (size_t index = 0; index < user_page_count; ++index) {
        if (user_page_vaddrs[index] == page_vaddr) {
            /* Two segments sharing a page with different permissions: reject. */
            if (create && user_page_flags[index] != flags) {
                return -1;
            }
            return (int)index;
        }
    }
    if (!create || user_page_count == MAX_USER_IMAGE_PAGES) {
        return -1;
    }
    uint8_t *page = alloc_page(); /* zeroed, so BSS and gaps need no extra work */
    if (page == NULL) {
        return -1;
    }
    size_t index = user_page_count++;
    user_page_vaddrs[index] = page_vaddr;
    user_page_flags[index] = flags;
    user_image_pages[index] = page;
    return (int)index;
}

static bool prepare_user_range(uint32_t vaddr, uint32_t length, uint32_t flags) {
    uint32_t end = vaddr + length;
    uint32_t page = vaddr & MINEMU_PTE_PAGE_MASK;
    while (page < end) {
        if (user_page_index(page, flags, true) < 0) {
            return false;
        }
        page += MINEMU_PAGE_SIZE;
    }
    return true;
}

/* Copy initialized bytes; pages were zeroed when allocated, so BSS needs nothing. */
static bool write_user_range(uint32_t vaddr, const uint8_t *source, uint32_t length) {
    while (length != 0) {
        uint32_t page_vaddr = vaddr & MINEMU_PTE_PAGE_MASK;
        int index = user_page_index(page_vaddr, 0, false);
        if (index < 0) {
            return false;
        }
        uint32_t offset = vaddr - page_vaddr;
        uint32_t chunk = MINEMU_PAGE_SIZE - offset;
        if (chunk > length) {
            chunk = length;
        }
        copy_bytes(&user_image_pages[index][offset], source, chunk);
        source += chunk;
        vaddr += chunk;
        length -= chunk;
    }
    return true;
}

bool loader_load_module(const struct minemu_boot_info *boot_info, const char *name,
                        uint32_t *entry) {
    const struct minemu_module_record *module = find_module(boot_info, name, string_length(name));
    if (module == NULL) {
        return false;
    }

    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    if (module->segment_count == 0 ||
        !image_records_valid(module->segment_table_offset, module->segment_count,
                             boot_info->image_size) ||
        (module->entry_vaddr & 3U) != 0) {
        return false;
    }

    const struct minemu_module_segment *segments =
        (const struct minemu_module_segment *)(image + module->segment_table_offset);
    bool entry_is_executable = false;
    user_page_table = alloc_page();
    user_stack = alloc_page();
    if (user_page_table == NULL || user_stack == NULL) {
        return false;
    }
    user_page_count = 0;

    for (uint32_t index = 0; index < module->segment_count; ++index) {
        const struct minemu_module_segment *segment = &segments[index];
        if (segment->memory_size == 0 || segment->file_size > segment->memory_size ||
            (segment->flags & ~(MINEMU_SEGMENT_READABLE | MINEMU_SEGMENT_WRITABLE |
                                MINEMU_SEGMENT_EXECUTABLE)) != 0 ||
            ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 &&
             (segment->flags & MINEMU_SEGMENT_READABLE) == 0) ||
            segment->virtual_address < USER_IMAGE_BASE ||
            segment->virtual_address >= USER_STACK_BOTTOM ||
            segment->memory_size > USER_STACK_BOTTOM - segment->virtual_address ||
            !image_range_valid(segment->data_offset, segment->file_size, boot_info->image_size)) {
            return false;
        }

        uint32_t pte_flags = pte_flags_from_segment(segment->flags);
        if (!prepare_user_range(segment->virtual_address, segment->memory_size, pte_flags) ||
            !write_user_range(segment->virtual_address, image + segment->data_offset,
                              segment->file_size)) {
            return false;
        }

        if ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 && segment->file_size >= 4U &&
            module->entry_vaddr >= segment->virtual_address &&
            module->entry_vaddr - segment->virtual_address <= segment->file_size - 4U) {
            entry_is_executable = true;
        }
    }
    if (!entry_is_executable) {
        return false;
    }

    for (size_t index = 0; index < user_page_count; ++index) {
        uint32_t table_index = (user_page_vaddrs[index] >> PAGE_SHIFT) & (TABLE_ENTRIES - 1U);
        user_page_table[table_index] = kernel_vaddr_to_paddr(user_image_pages[index]) |
                                       MINEMU_PTE_VALID | MINEMU_PTE_USER | user_page_flags[index];
    }
    /* Stack: RW, never executable. */
    user_page_table[(USER_STACK_BOTTOM >> PAGE_SHIFT) & (TABLE_ENTRIES - 1U)] =
        kernel_vaddr_to_paddr(user_stack) | MINEMU_PTE_VALID | MINEMU_PTE_USER |
        MINEMU_PTE_READABLE | MINEMU_PTE_WRITABLE;

    volatile uint32_t *directory = active_page_directory();
    directory[USER_IMAGE_BASE >> TABLE_SHIFT] = kernel_vaddr_to_paddr(user_page_table) | MINEMU_PDE_VALID;
    minemu_mmu_tlbiall();

    *entry = module->entry_vaddr;
    return true;
}

uint32_t loader_user_pte(uint32_t vaddr) {
    /* The whole user address space lives in one page-directory slot. */
    if (user_page_table == NULL || (vaddr >> TABLE_SHIFT) != (USER_IMAGE_BASE >> TABLE_SHIFT)) {
        return 0;
    }
    return user_page_table[(vaddr >> PAGE_SHIFT) & (TABLE_ENTRIES - 1U)];
}

uint8_t *loader_user_kaddr(uint32_t vaddr) {
    uint32_t pte = loader_user_pte(vaddr);
    uint32_t paddr = (pte & MINEMU_PTE_PAGE_MASK) | (vaddr & (MINEMU_PAGE_SIZE - 1U));
    return (uint8_t *)(uintptr_t)paddr_to_kernel_vaddr(paddr);
}
