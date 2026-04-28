/*
 * JXL minimal ARM64 board for U-Boot SPL learning.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/block/flash.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "system/address-spaces.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/cpu.h"

#include "jxl_board.h"
#include "jxl_soc.h"

static struct arm_boot_info jxl_binfo;

static PFlashCFI01 *jxl_flash_create(hwaddr base, DriveInfo *dinfo)
{
    PFlashCFI01 *flash = PFLASH_CFI01(object_new(TYPE_PFLASH_CFI01));
    DeviceState *dev = DEVICE(flash);

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

    return flash;
}

static void jxl_attach_flash(JXLState *jxl)
{
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);

    jxl->flash = jxl_flash_create(JXL_FLASH_BASE, dinfo);
}

static void jxl_create_memory(JXLState *jxl, MachineState *machine,
                              MemoryRegion *sysmem)
{
    jxl_attach_flash(jxl);

    memory_region_add_subregion(sysmem, JXL_DRAM_BASE, machine->ram);
}

static void jxl_attach_sd_card(JXLState *jxl, Error **errp)
{
    DriveInfo *dinfo = drive_get(IF_SD, 0, 0);

    if (!dinfo) {
        return;
    }

    jxl->sd_card = SD_CARD(object_new(TYPE_SD_CARD));
    qdev_prop_set_drive_err(DEVICE(jxl->sd_card), "drive",
                            blk_by_legacy_dinfo(dinfo), errp);
    if (*errp) {
        object_unref(OBJECT(jxl->sd_card));
        jxl->sd_card = NULL;
        return;
    }

    if (!qdev_realize(DEVICE(jxl->sd_card),
                      qdev_get_child_bus(DEVICE(&jxl->soc->mmci), "sd-bus"),
                      errp)) {
        object_unref(OBJECT(jxl->sd_card));
        jxl->sd_card = NULL;
    }
}

static void jxl_init(MachineState *machine)
{
    JXLState jxl = { 0 };
    ARMCPU *boot_cpu;
    MemoryRegion *sysmem = get_system_memory();
    JXLMachineState *jms = JXL_MACHINE(machine);

    jxl.soc = JXL_SOC(object_new(TYPE_JXL_SOC));
    jxl.soc->has_el2 = true;
    /*
     * EL3 follows the explicit `secure` machine option, not whether `-bios`
     * was passed. SPL-only chains (e.g. jxl-linux-spl) use `-bios` to load
     * U-Boot SPL but never install an EL3 SMC handler, so leaving has_el3
     * tied to firmware-presence breaks PSCI for them. Only the BL31-bearing
     * chains opt in via `-machine jxl,secure=on`.
     */
    jxl.soc->has_el3 = jms->secure;

    jxl_create_memory(&jxl, machine, sysmem);
    qdev_realize(DEVICE(jxl.soc), NULL, &error_abort);
    jxl_attach_sd_card(&jxl, &error_abort);
    boot_cpu = &jxl.soc->cpu[0];

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
    jxl_binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    arm_load_kernel(boot_cpu, machine, &jxl_binfo);

}

static bool jxl_get_secure(Object *obj, Error **errp)
{
    return JXL_MACHINE(obj)->secure;
}

static void jxl_set_secure(Object *obj, bool value, Error **errp)
{
    JXL_MACHINE(obj)->secure = value;
}

static void jxl_machine_class_init(ObjectClass *oc, const void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a53"),
        ARM_CPU_TYPE_NAME("cortex-a57"),
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "JXL minimal ARM64 SPL-learning board";
    mc->init = jxl_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = JXL_MAX_CPUS;
    mc->min_cpus = 1;
    mc->default_cpus = JXL_DEFAULT_CPUS;
    mc->default_ram_size = JXL_DRAM_DEFAULT;
    mc->default_ram_id = "jxl.dram";

    object_class_property_add_bool(oc, "secure",
                                   jxl_get_secure, jxl_set_secure);
    object_class_property_set_description(oc, "secure",
        "Set on to build the CPU with EL3. Required for SPL+BL31 chains "
        "(jxl-xen-atf, jxl-optee, jxl-xen-optee). Leave off for plain SPL "
        "or kernel-direct chains so QEMU's PSCI emulation handles SMC.");
}

static const TypeInfo jxl_machine_typeinfo = {
    .name          = TYPE_JXL_MACHINE,
    .parent        = TYPE_MACHINE,
    .class_init    = jxl_machine_class_init,
    .instance_size = sizeof(JXLMachineState),
    .interfaces    = aarch64_machine_interfaces,
};

static void jxl_machine_register_types(void)
{
    type_register_static(&jxl_machine_typeinfo);
}

type_init(jxl_machine_register_types)
