/*
 * JXL minimal ARM64 board definitions.
 *
 * Board-level memory resources:
 *   0x04000000 +---------------+  NOR flash (16 MiB, 64 KiB sectors)
 *              |     FLASH     |  writable pflash image for U-Boot/env
 *   0x05000000 +---------------+
 *              |               |
 *   0x40000000 +---------------+  DRAM (configurable via -m, default 128 MiB)
 *              |     DRAM      |
 *              +---------------+
 *
 * SoC-internal IP layout is documented in jxl_soc.h.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_JXL_BOARD_H
#define HW_ARM_JXL_BOARD_H

#include "hw/block/flash.h"
#include "hw/sd/sd.h"
#include "system/memory.h"

typedef struct JXLSocState JXLSocState;

#define JXL_MAX_CPUS     4
#define JXL_DEFAULT_CPUS 4

#define JXL_FLASH_BASE   0x04000000
#define JXL_FLASH_SIZE   (16 * 1024 * 1024)
#define JXL_FLASH_SECTOR_SIZE 0x10000

#define JXL_DRAM_BASE    0x40000000
#define JXL_DRAM_DEFAULT (128 * 1024 * 1024)

typedef struct JXLState {
    JXLSocState *soc;
    PFlashCFI01 *flash;
    SDState *sd_card;
} JXLState;

#endif /* HW_ARM_JXL_BOARD_H */
