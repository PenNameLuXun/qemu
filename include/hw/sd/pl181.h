/*
 * Arm PrimeCell PL181 MultiMedia Card Interface
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SD_PL181_H
#define HW_SD_PL181_H

#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "qom/object.h"

#define PL181_FIFO_LEN 16

#define TYPE_PL181 "pl181"
OBJECT_DECLARE_SIMPLE_TYPE(PL181State, PL181)

struct PL181State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SDBus sdbus;
    uint32_t clock;
    uint32_t power;
    uint32_t cmdarg;
    uint32_t cmd;
    uint32_t datatimer;
    uint32_t datalength;
    uint32_t respcmd;
    uint32_t response[4];
    uint32_t datactrl;
    uint32_t datacnt;
    uint32_t status;
    uint32_t mask[2];
    int32_t fifo_pos;
    int32_t fifo_len;
    int32_t linux_hack;
    uint32_t fifo[PL181_FIFO_LEN];
    qemu_irq irq[2];
    qemu_irq card_readonly;
    qemu_irq card_inserted;
};

#endif /* HW_SD_PL181_H */
