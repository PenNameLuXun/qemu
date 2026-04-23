/*
 * JXL minimal ARM64 board for U-Boot SPL learning.
 *
 * Memory map:
 *   0x00000000 +---------------+  SRAM (64 KiB) — SPL runs here (-bios)
 *              |     SRAM      |
 *   0x00010000 +---------------+
 *              |               |
 *   0x04000000 +---------------+  NOR flash (16 MiB, 64 KiB sectors)
 *              |     FLASH     |  writable pflash image for U-Boot/env
 *   0x05000000 +---------------+
 *              |               |
 *   0x09000000 +---------------+  PL011 UART0 (4 KiB)
 *              |    UART0      |
 *   0x09001000 +---------------+
 *              |               |
 *   0x40000000 +---------------+  DRAM (configurable via -m, default 128 MiB)
 *              |     DRAM      |
 *              +---------------+
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_JXL_H
#define HW_ARM_JXL_H

#define JXL_SRAM_BASE    0x00000000
#define JXL_SRAM_SIZE    (64 * 1024)

#define JXL_FLASH_BASE   0x04000000
#define JXL_FLASH_SIZE   (16 * 1024 * 1024)
#define JXL_FLASH_SECTOR_SIZE 0x10000

#define JXL_UART0_BASE   0x09000000
#define JXL_UART0_SIZE   0x1000

#define JXL_DRAM_BASE    0x40000000
#define JXL_DRAM_DEFAULT (128 * 1024 * 1024)

#endif /* HW_ARM_JXL_H */
