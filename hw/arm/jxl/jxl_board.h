/*
 * JXL minimal ARM64 board definitions.
 *
 * Board-level memory resources:
 *   0x04000000 +---------------+  NOR flash (16 MiB, 64 KiB sectors)
 *              |     FLASH     |  writable pflash image for U-Boot/env
 *   0x05000000 +---------------+
 *              |               |
 *   0x40000000 +---------------+  DRAM (configurable via -m, default 2 GiB)
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
#include "hw/boards.h"
#include "hw/sd/sd.h"
#include "qom/object.h"
#include "system/memory.h"

typedef struct JXLSocState JXLSocState;

/*
 * JXL machine subclass: carries a `secure` machine option that controls
 * whether the CPU is built with EL3. The default (off) keeps the EL2-only
 * boot chains (jxl, jxl-linux, jxl-linux-spl, jxl-xen) working with QEMU's
 * built-in PSCI emulation. The BL31-using chains (jxl-xen-atf, jxl-optee,
 * jxl-xen-optee) flip secure=on so EL3 exists for TF-A to live in.
 */
#define TYPE_JXL_MACHINE MACHINE_TYPE_NAME("jxl")
OBJECT_DECLARE_SIMPLE_TYPE(JXLMachineState, JXL_MACHINE)

struct JXLMachineState {
    MachineState parent_obj;
    bool secure;
};

#define JXL_MAX_CPUS     4
#define JXL_DEFAULT_CPUS 4

#define JXL_FLASH_BASE   0x04000000
#define JXL_FLASH_SIZE   (16 * 1024 * 1024)
#define JXL_FLASH_SECTOR_SIZE 0x10000

#define JXL_DRAM_BASE    0x40000000
#define JXL_DRAM_DEFAULT (2ULL * 1024 * 1024 * 1024)

typedef struct JXLState {
    JXLSocState *soc;
    PFlashCFI01 *flash;
    SDState *sd_card;
} JXLState;

#endif /* HW_ARM_JXL_BOARD_H */
