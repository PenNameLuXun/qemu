/*
 * JXL minimal ARM64 board for U-Boot SPL learning.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "hw/boards.h"
#include "hw/arm/bsa.h"
#include "hw/block/flash.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/char/pl011.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "qobject/qlist.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/cpu.h"
#include "target/arm/gtimer.h"

#include "jxl.h"

static struct arm_boot_info jxl_binfo;

static void jxl_flash_create(hwaddr base, DriveInfo *dinfo)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    if (dinfo) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo));
    }

    qdev_prop_set_uint32(dev, "num-blocks",
                         JXL_FLASH_SIZE / JXL_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint64(dev, "sector-length", JXL_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", "jxl.flash");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
}

static DeviceState *jxl_gic_create(MemoryRegion *sysmem, int smp_cpus)
{
    DeviceState *gic;
    SysBusDevice *gicbusdev;
    QList *redist_region_count;
    uint32_t redist_capacity;
    int i;

    gic = qdev_new(gicv3_class_name());
    qdev_prop_set_uint32(gic, "revision", 3);
    qdev_prop_set_uint32(gic, "num-cpu", smp_cpus);
    qdev_prop_set_uint32(gic, "num-irq", JXL_NUM_IRQS + 32);
    qdev_prop_set_bit(gic, "has-security-extensions", false);

    redist_capacity = JXL_GIC_REDIST_SIZE / GICV3_REDIST_SIZE;
    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, MIN(smp_cpus, redist_capacity));
    qdev_prop_set_array(gic, "redist-region-count", redist_region_count);

    object_property_set_link(OBJECT(gic), "sysmem", OBJECT(sysmem),
                             &error_fatal);

    gicbusdev = SYS_BUS_DEVICE(gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0, JXL_GIC_DIST_BASE);
    sysbus_mmio_map(gicbusdev, 1, JXL_GIC_REDIST_BASE);

    for (i = 0; i < smp_cpus; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int intidbase = JXL_NUM_IRQS + i * GIC_INTERNAL;
        int irq;
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP] = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC] = ARCH_TIMER_S_EL1_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(gic, intidbase + timer_irq[irq]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(gic, intidbase + ARCH_GIC_MAINT_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    return gic;
}

static void jxl_init(MachineState *machine)
{
    ARMCPU *cpu;
    DeviceState *gic;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    DriveInfo *dinfo;

    /* CPU: single Cortex-A53 booting directly at EL1 (no EL2/EL3, no PSCI). */
    cpu = ARM_CPU(object_new(machine->cpu_type));
    object_property_set_bool(OBJECT(cpu), "has_el3", false, &error_abort);
    object_property_set_bool(OBJECT(cpu), "has_el2", false, &error_abort);
    qdev_realize(DEVICE(cpu), NULL, &error_abort);

    /* SRAM — SPL target */
    memory_region_init_ram(sram, NULL, "jxl.sram",
                           JXL_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, JXL_SRAM_BASE, sram);

    /*
     * NOR flash backed by an optional pflash image. This allows U-Boot to use
     * the region both as SPL boot media and as writable persistent storage
     * for 'saveenv'.
     */
    dinfo = drive_get(IF_PFLASH, 0, 0);
    jxl_flash_create(JXL_FLASH_BASE, dinfo);

    /* DRAM */
    memory_region_add_subregion(sysmem, JXL_DRAM_BASE, machine->ram);

    gic = jxl_gic_create(sysmem, machine->smp.cpus);

    /* PL011 UART0 on SPI 32 */
    pl011_create(JXL_UART0_BASE, qdev_get_gpio_in(gic, JXL_IRQ_UART0),
                 serial_hd(0));

    /*
     * Two boot modes:
     *
     *   SPL mode (-bios given):
     *     -bios <u-boot-spl.bin>   loaded into SRAM at 0x0. CPU resets at 0x0
     *                              and runs SPL. U-Boot proper can live in the
     *                              pflash image mapped at 0x04000000.
     *
     *   Direct mode (no -bios):
     *     -kernel <u-boot.bin>     loaded into DRAM at 0x40080000. A tiny
     *                              AArch64 trampoline is installed in SRAM
     *                              at 0x0 which jumps to 0x40080000.
     *                              Useful for bootstrapping U-Boot proper
     *                              before SPL is ready.
     */
    if (machine->firmware) {
        load_image_targphys(machine->firmware, JXL_SRAM_BASE,
                            JXL_SRAM_SIZE, &error_fatal);
        if (machine->kernel_filename) {
            load_image_targphys(machine->kernel_filename, JXL_FLASH_BASE,
                                JXL_FLASH_SIZE, &error_fatal);
        }
    } else if (machine->kernel_filename) {
        hwaddr entry = JXL_DRAM_BASE + 0x80000;
        uint32_t code[4];

        load_image_targphys(machine->kernel_filename, entry,
                            machine->ram_size - 0x80000, &error_fatal);

        /* AArch64:   ldr x0, [pc, #8] ; br x0 ; .quad entry  */
        code[0] = cpu_to_le32(0x58000040);
        code[1] = cpu_to_le32(0xd61f0000);
        code[2] = cpu_to_le32((uint32_t)entry);
        code[3] = cpu_to_le32((uint32_t)(entry >> 32));
        rom_add_blob_fixed("jxl-trampoline", code, sizeof(code),
                           JXL_SRAM_BASE);
    }

    /*
     * We've already handled firmware/kernel placement manually. Hide these
     * from arm_load_kernel so it takes the pure-firmware-boot path without
     * trying to stage the kernel via fw_cfg (which this machine does not
     * have).
     */
    machine->kernel_filename = NULL;
    machine->firmware = NULL;

    jxl_binfo.ram_size = machine->ram_size;
    jxl_binfo.loader_start = JXL_SRAM_BASE;
    jxl_binfo.firmware_loaded = true;
    jxl_binfo.board_id = -1;
    jxl_binfo.psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;
    arm_load_kernel(cpu, machine, &jxl_binfo);
}

static void jxl_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a53"),
        ARM_CPU_TYPE_NAME("cortex-a57"),
        NULL
    };

    mc->desc = "JXL minimal ARM64 SPL-learning board";
    mc->init = jxl_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = 1;
    mc->min_cpus = 1;
    mc->default_cpus = 1;
    mc->default_ram_size = JXL_DRAM_DEFAULT;
    mc->default_ram_id = "jxl.dram";
}

DEFINE_MACHINE_AARCH64("jxl", jxl_machine_init)
