/*
 * JXL SoC model.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "hw/boards.h"
#include "hw/arm/bsa.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/char/pl011.h"
#include "hw/intc/arm_gicv3_common.h"
#include "qobject/qlist.h"
#include "qemu/log.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "target/arm/arm-powerctl.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

#include "jxl_soc.h"

#define JXL_ATF_BL31_WARM_ENTRY 0xbff90184ULL

static uint64_t jxl_cpu_pwrctl_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    return 0;
}

static void jxl_cpu_pwrctl_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    int ret;

    if (offset != 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "jxl-cpu-pwrctl: invalid write offset 0x%" HWADDR_PRIx
                      "\n", offset);
        return;
    }

    ret = arm_set_cpu_on(value, JXL_ATF_BL31_WARM_ENTRY, 0, 3, true);
    if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "jxl-cpu-pwrctl: failed to power on CPU 0x%" PRIx64
                      " (ret=%d)\n", value, ret);
    }
}

static const MemoryRegionOps jxl_cpu_pwrctl_ops = {
    .read = jxl_cpu_pwrctl_read,
    .write = jxl_cpu_pwrctl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

const JXLSocIpInfo jxl_soc_ip_info[JXL_SOC_IP_COUNT] = {
    [JXL_SOC_IP_SRAM] = {
        .base_addr = 0x00000000,
        .size = 64 * KiB,
        .name = "sram",
    },
    [JXL_SOC_IP_GIC_DIST] = {
        .base_addr = 0x08000000,
        .size = 0x10000,
        .name = "gic-dist",
    },
    [JXL_SOC_IP_GIC_REDIST] = {
        .base_addr = 0x080a0000,
        .size = 0x80000,
        .name = "gic-redist",
    },
    [JXL_SOC_IP_UART0] = {
        .base_addr = 0x09000000,
        .size = 0x1000,
        .name = "uart0",
    },
    [JXL_SOC_IP_MMCI] = {
        .base_addr = 0x0a000000,
        .size = 0x1000,
        .name = "mmci",
    },
    [JXL_SOC_IP_CPU_PWRCTL] = {
        .base_addr = 0x0a010000,
        .size = 0x1000,
        .name = "cpu-pwrctl",
    },
    [JXL_SOC_IP_VIRTIO_MMIO] = {
        .base_addr = 0x0a020000,
        .size = 0x1000,
        .name = "virtio-mmio",
    },
    [JXL_SOC_IP_CLCD] = {
        .base_addr = 0x0a030000,
        .size = 0x1000,
        .name = "clcd",
    },
};

static void jxl_soc_init(Object *obj)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    JXLSocState *soc = JXL_SOC(obj);
    char name[16];
    int i;

    for (i = 0; i < MIN(machine->smp.cpus, JXL_MAX_CPUS); i++) {
        snprintf(name, sizeof(name), "cpu%d", i);
        object_initialize_child(obj, name, &soc->cpu[i], machine->cpu_type);
    }

    object_initialize_child(obj, "gic", &soc->gic, TYPE_ARM_GICV3);
    object_initialize_child(obj, "mmci", &soc->mmci, TYPE_PL181);
    object_initialize_child(obj, "uart0", &soc->uart0, TYPE_PL011);

    /*
     * Default to a non-secure EL2-capable machine so Linux/Xen can use
     * virtualization. Board code may enable EL3 before realize when a secure
     * firmware chain (e.g. SPL -> BL31) is requested.
     */
    soc->has_el2 = true;
    soc->has_el3 = false;
}

static void jxl_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    JXLSocState *soc = JXL_SOC(dev);
    MemoryRegion *sysmem = get_system_memory();
    SysBusDevice *gicbusdev;
    QList *redist_region_count;
    uint32_t redist_capacity;
    int i;

    if (machine->smp.cpus > JXL_MAX_CPUS) {
        error_setg(errp, "%s supports at most %d CPUs (%d requested)",
                   TYPE_JXL_SOC, JXL_MAX_CPUS, machine->smp.cpus);
        return;
    }

    for (i = 0; i < machine->smp.cpus; i++) {
        object_property_set_bool(OBJECT(&soc->cpu[i]), "has_el3",
                                 soc->has_el3, &error_abort);
        object_property_set_bool(OBJECT(&soc->cpu[i]), "has_el2",
                                 soc->has_el2, &error_abort);
        if (i > 0) {
            object_property_set_bool(OBJECT(&soc->cpu[i]),
                                     "start-powered-off", true,
                                     &error_abort);
        }
        if (!qdev_realize(DEVICE(&soc->cpu[i]), NULL, errp)) {
            return;
        }
    }

    memory_region_init_ram(&soc->sram, OBJECT(dev), "jxl.sram",
                           jxl_soc_ip_info[JXL_SOC_IP_SRAM].size, errp);
    if (*errp) {
        return;
    }
    memory_region_add_subregion(sysmem, jxl_soc_ip_info[JXL_SOC_IP_SRAM].base_addr,
                                &soc->sram);

    memory_region_init_io(&soc->cpu_pwrctl, OBJECT(dev), &jxl_cpu_pwrctl_ops,
                          soc, "jxl.cpu-pwrctl",
                          jxl_soc_ip_info[JXL_SOC_IP_CPU_PWRCTL].size);
    memory_region_add_subregion(sysmem,
                                jxl_soc_ip_info[JXL_SOC_IP_CPU_PWRCTL].base_addr,
                                &soc->cpu_pwrctl);

    qdev_prop_set_uint32(DEVICE(&soc->gic), "revision", 3);
    qdev_prop_set_uint32(DEVICE(&soc->gic), "num-cpu", machine->smp.cpus);
    qdev_prop_set_uint32(DEVICE(&soc->gic), "num-irq", JXL_SOC_NUM_IRQS + 32);
    qdev_prop_set_bit(DEVICE(&soc->gic), "has-security-extensions",
                      soc->has_el3);

    redist_capacity = jxl_soc_ip_info[JXL_SOC_IP_GIC_REDIST].size /
                      GICV3_REDIST_SIZE;
    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count,
                     MIN(machine->smp.cpus, redist_capacity));
    qdev_prop_set_array(DEVICE(&soc->gic), "redist-region-count",
                        redist_region_count);
    object_property_set_link(OBJECT(&soc->gic), "sysmem", OBJECT(sysmem),
                             &error_abort);

    if (!sysbus_realize(SYS_BUS_DEVICE(&soc->gic), errp)) {
        return;
    }

    gicbusdev = SYS_BUS_DEVICE(&soc->gic);
    sysbus_mmio_map(gicbusdev, 0,
                    jxl_soc_ip_info[JXL_SOC_IP_GIC_DIST].base_addr);
    sysbus_mmio_map(gicbusdev, 1,
                    jxl_soc_ip_info[JXL_SOC_IP_GIC_REDIST].base_addr);

    for (i = 0; i < machine->smp.cpus; i++) {
        DeviceState *cpudev = DEVICE(&soc->cpu[i]);
        int intidbase = JXL_SOC_NUM_IRQS + i * GIC_INTERNAL;
        int irq;
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP] = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC] = ARCH_TIMER_S_EL1_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(DEVICE(&soc->gic),
                                                   intidbase + timer_irq[irq]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(DEVICE(&soc->gic),
                                                     intidbase + ARCH_GIC_MAINT_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + machine->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * machine->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * machine->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&soc->mmci), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->mmci), 0,
                    jxl_soc_ip_info[JXL_SOC_IP_MMCI].base_addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->mmci), 0,
                       qdev_get_gpio_in(DEVICE(&soc->gic), JXL_SOC_IRQ_MMCI_CMD));
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->mmci), 1,
                       qdev_get_gpio_in(DEVICE(&soc->gic), JXL_SOC_IRQ_MMCI_DATA));

    qdev_prop_set_chr(DEVICE(&soc->uart0), "chardev", serial_hd(0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&soc->uart0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->uart0), 0,
                    jxl_soc_ip_info[JXL_SOC_IP_UART0].base_addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->uart0), 0,
                       qdev_get_gpio_in(DEVICE(&soc->gic), JXL_SOC_IRQ_UART0));

    /*
     * One virtio-mmio transport. The guest binds whatever -device
     * virtio-<foo>-device,... is attached on the command line (typically
     * virtio-net-device for SLIRP networking). With no attached device the
     * region simply reports an empty virtio header and is harmless.
     */
    sysbus_create_simple("virtio-mmio",
                         jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_MMIO].base_addr,
                         qdev_get_gpio_in(DEVICE(&soc->gic),
                                          JXL_SOC_IRQ_VIRTIO_MMIO));

    /*
     * PL111 LCD controller. The guest's DRM driver (drivers/gpu/drm/pl111)
     * tells QEMU the framebuffer base/format/dimensions; QEMU's pl111
     * model then renders that buffer into whatever -display backend was
     * selected (sdl, vnc, ...). With -display none the device exists but
     * draws nowhere, which is fine for headless boots. The model wants an
     * explicit `framebuffer-memory` link before realize so it knows which
     * AddressSpace the framebuffer DMA addresses live in.
     */
    {
        DeviceState *clcd = qdev_new("pl111");
        SysBusDevice *clcd_sbd = SYS_BUS_DEVICE(clcd);
        object_property_set_link(OBJECT(clcd), "framebuffer-memory",
                                 OBJECT(sysmem), &error_fatal);
        sysbus_realize_and_unref(clcd_sbd, &error_fatal);
        sysbus_mmio_map(clcd_sbd, 0,
                        jxl_soc_ip_info[JXL_SOC_IP_CLCD].base_addr);
        sysbus_connect_irq(clcd_sbd, 0,
                           qdev_get_gpio_in(DEVICE(&soc->gic),
                                            JXL_SOC_IRQ_CLCD));
    }
}

static void jxl_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = jxl_soc_realize;
    dc->desc = "JXL SoC";
}

static const TypeInfo jxl_soc_types[] = {
    {
        .name          = TYPE_JXL_SOC,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(JXLSocState),
        .instance_init = jxl_soc_init,
        .class_init    = jxl_soc_class_init,
    },
};

DEFINE_TYPES(jxl_soc_types)
