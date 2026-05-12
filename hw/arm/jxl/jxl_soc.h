/*
 * JXL SoC model definitions.
 *
 * SoC internal IP layout:
 *   0x00000000 +---------------+  SRAM (64 KiB) — SPL runs here (-bios)
 *              |     SRAM      |
 *   0x00010000 +---------------+
 *              |               |
 *   0x08000000 +---------------+  GICv3 distributor (64 KiB)
 *              |     GICD      |
 *   0x08010000 +---------------+
 *              |               |
 *   0x080a0000 +---------------+  GICv3 redistributor window (512 KiB)
 *              |     GICR      |
 *   0x08120000 +---------------+
 *              |               |
 *   0x09000000 +---------------+  PL011 UART0 (4 KiB)
 *              |    UART0      |
 *   0x09001000 +---------------+
 *              |               |
 *   0x0a000000 +---------------+  PL181 MMCI (4 KiB)
 *              |     MMCI      |
 *   0x0a001000 +---------------+
 *              |               |
 *   0x0a010000 +---------------+  CPU power controller (4 KiB)
 *              |   CPU_PWR     |  EL3 firmware wakes powered-off CPUs here
 *   0x0a011000 +---------------+
 *              |               |
 *   0x0a020000 +---------------+  virtio-mmio transport (4 KiB)
 *              |  VIRTIO_MMIO  |  attach with -device virtio-net-device,...
 *   0x0a021000 +---------------+
 *              |               |
 *   0x0a021000 +---------------+  virtio-mmio transport (4 KiB)
 *              | VIRTIO_GPU_MMIO| attach with -device virtio-gpu-device,...
 *   0x0a022000 +---------------+
 *              |               |
 *   0x0a022000 +---------------+  virtio-mmio transport (4 KiB)
 *              | VIRTIO_KBD_MMIO| attach with -device virtio-keyboard-device
 *   0x0a023000 +---------------+
 *              |               |
 *   0x0a023000 +---------------+  virtio-mmio transport (4 KiB)
 *              |VIRTIO_TABLET   | attach with -device virtio-tablet-device
 *   0x0a024000 +---------------+
 *              |               |
 *   0x0a030000 +---------------+  PL111 LCD controller (4 KiB)
 *              |     CLCD      |  framebuffer DMA → host display window
 *   0x0a031000 +---------------+
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_JXL_SOC_H
#define HW_ARM_JXL_SOC_H

#include "exec/hwaddr.h"
#include "qom/object.h"
#include "hw/char/pl011.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/sd/pl181.h"
#include "system/memory.h"
#include "target/arm/cpu.h"

#include "jxl_board.h"

#define TYPE_JXL_SOC "jxl-soc"
OBJECT_DECLARE_SIMPLE_TYPE(JXLSocState, JXL_SOC)

typedef enum JXLSocIrq {
    JXL_SOC_IRQ_UART0 = 32,
    JXL_SOC_IRQ_MMCI_CMD,
    JXL_SOC_IRQ_MMCI_DATA,
    JXL_SOC_IRQ_VIRTIO_MMIO,
    JXL_SOC_IRQ_CLCD,
    JXL_SOC_IRQ_VIRTIO_GPU_MMIO,
    JXL_SOC_IRQ_VIRTIO_KBD_MMIO,
    JXL_SOC_IRQ_VIRTIO_TABLET_MMIO,
    JXL_SOC_NUM_IRQS = 64,
} JXLSocIrq;

typedef enum JXLSocIpIndex {
    JXL_SOC_IP_SRAM,
    JXL_SOC_IP_GIC_DIST,
    JXL_SOC_IP_GIC_REDIST,
    JXL_SOC_IP_UART0,
    JXL_SOC_IP_MMCI,
    JXL_SOC_IP_CPU_PWRCTL,
    JXL_SOC_IP_VIRTIO_MMIO,
    JXL_SOC_IP_VIRTIO_GPU_MMIO,
    JXL_SOC_IP_VIRTIO_KBD_MMIO,
    JXL_SOC_IP_VIRTIO_TABLET_MMIO,
    JXL_SOC_IP_CLCD,
    JXL_SOC_IP_COUNT,
} JXLSocIpIndex;

typedef struct JXLSocIpInfo {
    hwaddr base_addr;
    hwaddr size;
    const char *name;
} JXLSocIpInfo;

extern const JXLSocIpInfo jxl_soc_ip_info[JXL_SOC_IP_COUNT];

struct JXLSocState {
    DeviceState parent_obj;

    bool has_el2;
    bool has_el3;
    ARMCPU cpu[JXL_MAX_CPUS];
    MemoryRegion sram;
    MemoryRegion cpu_pwrctl;
    GICv3State gic;
    PL181State mmci;
    PL011State uart0;
};

#define JXL_SRAM_BASE       (jxl_soc_ip_info[JXL_SOC_IP_SRAM].base_addr)
#define JXL_SRAM_SIZE       (jxl_soc_ip_info[JXL_SOC_IP_SRAM].size)
#define JXL_GIC_DIST_BASE   (jxl_soc_ip_info[JXL_SOC_IP_GIC_DIST].base_addr)
#define JXL_GIC_DIST_SIZE   (jxl_soc_ip_info[JXL_SOC_IP_GIC_DIST].size)
#define JXL_GIC_REDIST_BASE (jxl_soc_ip_info[JXL_SOC_IP_GIC_REDIST].base_addr)
#define JXL_GIC_REDIST_SIZE (jxl_soc_ip_info[JXL_SOC_IP_GIC_REDIST].size)
#define JXL_UART0_BASE      (jxl_soc_ip_info[JXL_SOC_IP_UART0].base_addr)
#define JXL_UART0_SIZE      (jxl_soc_ip_info[JXL_SOC_IP_UART0].size)
#define JXL_MMCI_BASE       (jxl_soc_ip_info[JXL_SOC_IP_MMCI].base_addr)
#define JXL_MMCI_SIZE       (jxl_soc_ip_info[JXL_SOC_IP_MMCI].size)
#define JXL_CPU_PWRCTL_BASE (jxl_soc_ip_info[JXL_SOC_IP_CPU_PWRCTL].base_addr)
#define JXL_CPU_PWRCTL_SIZE (jxl_soc_ip_info[JXL_SOC_IP_CPU_PWRCTL].size)
#define JXL_VIRTIO_MMIO_BASE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_MMIO].base_addr)
#define JXL_VIRTIO_MMIO_SIZE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_MMIO].size)
#define JXL_VIRTIO_GPU_MMIO_BASE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_GPU_MMIO].base_addr)
#define JXL_VIRTIO_GPU_MMIO_SIZE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_GPU_MMIO].size)
#define JXL_VIRTIO_KBD_MMIO_BASE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_KBD_MMIO].base_addr)
#define JXL_VIRTIO_KBD_MMIO_SIZE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_KBD_MMIO].size)
#define JXL_VIRTIO_TABLET_MMIO_BASE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_TABLET_MMIO].base_addr)
#define JXL_VIRTIO_TABLET_MMIO_SIZE (jxl_soc_ip_info[JXL_SOC_IP_VIRTIO_TABLET_MMIO].size)
#define JXL_CLCD_BASE        (jxl_soc_ip_info[JXL_SOC_IP_CLCD].base_addr)
#define JXL_CLCD_SIZE        (jxl_soc_ip_info[JXL_SOC_IP_CLCD].size)

#endif /* HW_ARM_JXL_SOC_H */
