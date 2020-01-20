/*
 * iPhone 6s plus - n66 - S8000
 *
 * Copyright (c) 2019 Jonathan Afek <jonyafek@me.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/boot.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "qemu/error-report.h"
#include "hw/platform-bus.h"

#include "hw/arm/n66_iphone6splus.h"

#include "hw/arm/exynos4210.h"
#include "hw/arm/guest-services/general.h"

#define N66_SECURE_RAM_SIZE (0x100000)
#define N66_PHYS_BASE (0x40000000)
#define STATIC_TRUST_CACHE_OFFSET (0x2000000)

//compiled nop instruction: mov x0, x0
#define NOP_INST (0xaa0003e0)

#define SMC_INST_VADDR_16B92 (0xfffffff0070a7d3c)

//hook the kernel to execute our "driver" code in this function
//after things are already running in the kernel but the root mount is not
//yet mounted.
//We chose this place in the beginning of ubc_init() inlined in bsd_init()
//because enough things are up and running for our driver to properly setup,
//This means that global IOKIT locks and dictionaries are already initialized
//and in general, the IOKIT system is already initialized.
//We are now able to initialize our driver and attach it to an existing
//IOReg object.
//On the other hand, no mounting of any FS happened yet so we have a chance
//for our block device driver to present a new block device that will be
//mounted on the root mount.
//We need to choose the hook location carefully.
//We need 3 instructions in a row that we overwrite that are not location
//dependant (such as adr, adrp and branching) as we are going to execute
//them elsewhere.
//We also need a register to use as a scratch register that its value is
//disregarded right after the hook and does not affect anything.
#define UBC_INIT_VADDR_16B92 (0xfffffff0073dec10)

#define N66_CPREG_FUNCS(name) \
static uint64_t n66_cpreg_read_##name(CPUARMState *env, \
                                      const ARMCPRegInfo *ri) \
{ \
    N66MachineState *nms = (N66MachineState *)ri->opaque; \
    return nms->N66_CPREG_VAR_NAME(name); \
} \
static void n66_cpreg_write_##name(CPUARMState *env, const ARMCPRegInfo *ri, \
                                   uint64_t value) \
{ \
    N66MachineState *nms = (N66MachineState *)ri->opaque; \
    nms->N66_CPREG_VAR_NAME(name) = value; \
}

#define N66_CPREG_DEF(p_name, p_op0, p_op1, p_crn, p_crm, p_op2, p_access) \
    { .cp = CP_REG_ARM64_SYSREG_CP, \
      .name = #p_name, .opc0 = p_op0, .crn = p_crn, .crm = p_crm, \
      .opc1 = p_op1, .opc2 = p_op2, .access = p_access, .type = ARM_CP_IO, \
      .state = ARM_CP_STATE_AA64, .readfn = n66_cpreg_read_##p_name, \
      .writefn = n66_cpreg_write_##p_name }

N66_CPREG_FUNCS(ARM64_REG_HID11)
N66_CPREG_FUNCS(ARM64_REG_HID3)
N66_CPREG_FUNCS(ARM64_REG_HID5)
N66_CPREG_FUNCS(ARM64_REG_HID4)
N66_CPREG_FUNCS(ARM64_REG_HID8)
N66_CPREG_FUNCS(ARM64_REG_HID7)
N66_CPREG_FUNCS(ARM64_REG_LSU_ERR_STS)
N66_CPREG_FUNCS(PMC0)
N66_CPREG_FUNCS(PMC1)
N66_CPREG_FUNCS(PMCR1)
N66_CPREG_FUNCS(PMSR)

static const ARMCPRegInfo n66_cp_reginfo[] = {
    // Apple-specific registers
    N66_CPREG_DEF(ARM64_REG_HID11, 3, 0, 15, 13, 0, PL1_RW),
    N66_CPREG_DEF(ARM64_REG_HID3, 3, 0, 15, 3, 0, PL1_RW),
    N66_CPREG_DEF(ARM64_REG_HID5, 3, 0, 15, 5, 0, PL1_RW),
    N66_CPREG_DEF(ARM64_REG_HID4, 3, 0, 15, 4, 0, PL1_RW),
    N66_CPREG_DEF(ARM64_REG_HID8, 3, 0, 15, 8, 0, PL1_RW),
    N66_CPREG_DEF(ARM64_REG_HID7, 3, 0, 15, 7, 0, PL1_RW),
    N66_CPREG_DEF(ARM64_REG_LSU_ERR_STS, 3, 3, 15, 0, 0, PL1_RW),
    N66_CPREG_DEF(PMC0, 3, 2, 15, 0, 0, PL1_RW),
    N66_CPREG_DEF(PMC1, 3, 2, 15, 1, 0, PL1_RW),
    N66_CPREG_DEF(PMCR1, 3, 1, 15, 1, 0, PL1_RW),
    N66_CPREG_DEF(PMSR, 3, 1, 15, 13, 0, PL1_RW),

    // Aleph-specific registers for communicating with QEMU

    // REG_QEMU_CALL:
    { .cp = CP_REG_ARM64_SYSREG_CP, .name = "REG_QEMU_CALL",
      .opc0 = 3, .opc1 = 3, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL0_RW, .type = ARM_CP_IO, .state = ARM_CP_STATE_AA64,
      .readfn = qemu_call_status,
      .writefn = qemu_call },

    REGINFO_SENTINEL,
};

static uint32_t g_nop_inst = NOP_INST;

static void n66_add_cpregs(N66MachineState *nms)
{
    ARMCPU *cpu = nms->cpu;

    nms->N66_CPREG_VAR_NAME(ARM64_REG_HID11) = 0;
    nms->N66_CPREG_VAR_NAME(ARM64_REG_HID3) = 0;
    nms->N66_CPREG_VAR_NAME(ARM64_REG_HID5) = 0;
    nms->N66_CPREG_VAR_NAME(ARM64_REG_HID8) = 0;
    nms->N66_CPREG_VAR_NAME(ARM64_REG_HID7) = 0;
    nms->N66_CPREG_VAR_NAME(ARM64_REG_LSU_ERR_STS) = 0;
    nms->N66_CPREG_VAR_NAME(PMC0) = 0;
    nms->N66_CPREG_VAR_NAME(PMC1) = 0;
    nms->N66_CPREG_VAR_NAME(PMCR1) = 0;
    nms->N66_CPREG_VAR_NAME(PMSR) = 0;
    define_arm_cp_regs_with_opaque(cpu, n66_cp_reginfo, nms);
}

static void n66_create_s3c_uart(const N66MachineState *nms, Chardev *chr)
{
    qemu_irq irq;
    DeviceState *d;
    SysBusDevice *s;
    hwaddr base = nms->uart_mmio_pa;

    //hack for now. create a device that is not used just to have a dummy
    //unused interrupt
    d = qdev_create(NULL, TYPE_PLATFORM_BUS_DEVICE);
    s = SYS_BUS_DEVICE(d);
    sysbus_init_irq(s, &irq);
    //pass a dummy irq as we don't need nor want interrupts for this UART
    DeviceState *dev = exynos4210_uart_create(base, 256, 0, chr, irq);
    if (!dev) {
        abort();
    }
}

static void n66_ns_memory_setup(MachineState *machine, MemoryRegion *sysmem,
                                AddressSpace *nsas)
{
    uint64_t used_ram_for_blobs = 0;
    hwaddr kernel_low;
    hwaddr kernel_high;
    hwaddr virt_base;
    hwaddr dtb_va;
    uint64_t dtb_size;
    hwaddr kbootargs_pa;
    hwaddr top_of_kernel_data_pa;
    hwaddr mem_size;
    hwaddr remaining_mem_size;
    hwaddr allocated_ram_pa;
    hwaddr phys_ptr;
    hwaddr phys_pc;
    N66MachineState *nms = N66_MACHINE(machine);

    //setup the memory layout:

    //At the beginning of the non-secure ram we have the raw kernel file.
    //After that we have the static trust cache.
    //After that we have all the kernel sections.
    //After that we have ramdosk
    //After that we have the device tree
    //After that we have the kernel boot args
    //After that we have the rest of the RAM

    macho_file_highest_lowest_base(nms->kernel_filename, N66_PHYS_BASE,
                                   &virt_base, &kernel_low, &kernel_high);

    g_virt_base = virt_base;
    g_phys_base = N66_PHYS_BASE;
    phys_ptr = N66_PHYS_BASE;

    //now account for the static trust cache
    phys_ptr += align_64k_high(STATIC_TRUST_CACHE_OFFSET);
    nms->tc_file_dev.pa = phys_ptr;
    xnu_file_mmio_dev_create(sysmem, &nms->tc_file_dev,
                             "tc_file_mmio_dev", nms->tc_filename);

    //now account for the loaded kernel
    arm_load_macho(nms->kernel_filename, nsas, sysmem, "kernel.n66",
                    N66_PHYS_BASE, virt_base, kernel_low,
                    kernel_high, &phys_pc);
    nms->kpc_pa = phys_pc;
    used_ram_for_blobs += (align_64k_high(kernel_high) - kernel_low);

    //patch the smc instruction to nop since we no longer use a secure
    //monitor because we disabled KPP this way
    address_space_rw(nsas, vtop_static(SMC_INST_VADDR_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);

    phys_ptr = align_64k_high(vtop_static(kernel_high));

    //now account for the ramdisk
    nms->ramdisk_file_dev.pa = 0;
    hwaddr ramdisk_size = 0;
    if (0 != nms->ramdisk_filename[0]) {
        nms->ramdisk_file_dev.pa = phys_ptr;
        xnu_file_mmio_dev_create(sysmem, &nms->ramdisk_file_dev,
                               "ramdisk_file_mmio_dev", nms->ramdisk_filename);
        ramdisk_size = nms->ramdisk_file_dev.size;
        phys_ptr += align_64k_high(nms->ramdisk_file_dev.size);
    }

    //now account for device tree
    macho_load_dtb(nms->dtb_filename, nsas, sysmem, "dtb.n66", phys_ptr,
                   &dtb_size, nms->ramdisk_file_dev.pa,
                   ramdisk_size, nms->tc_file_dev.pa,
                   nms->tc_file_dev.size, &nms->uart_mmio_pa);
    dtb_va = ptov_static(phys_ptr);
    phys_ptr += align_64k_high(dtb_size);
    used_ram_for_blobs += align_64k_high(dtb_size);

    //now account for kernel boot args
    used_ram_for_blobs += align_64k_high(sizeof(struct xnu_arm64_boot_args));
    kbootargs_pa = phys_ptr;
    nms->kbootargs_pa = kbootargs_pa;
    phys_ptr += align_64k_high(sizeof(struct xnu_arm64_boot_args));
    nms->extra_data_pa = phys_ptr;
    allocated_ram_pa = phys_ptr;
    phys_ptr += align_64k_high(sizeof(AllocatedData));
    top_of_kernel_data_pa = phys_ptr;
    remaining_mem_size = machine->ram_size - used_ram_for_blobs;
    mem_size = allocated_ram_pa - N66_PHYS_BASE + remaining_mem_size;
    macho_setup_bootargs("k_bootargs.n66", nsas, sysmem, kbootargs_pa,
                         virt_base, N66_PHYS_BASE, mem_size,
                         top_of_kernel_data_pa, dtb_va, dtb_size,
                         nms->kern_args);

    allocate_ram(sysmem, "n66.ram", allocated_ram_pa, remaining_mem_size);
}

static void n66_memory_setup(MachineState *machine,
                             MemoryRegion *sysmem,
                             MemoryRegion *secure_sysmem,
                             AddressSpace *nsas)
{
    n66_ns_memory_setup(machine, sysmem, nsas);
}

static void n66_cpu_setup(MachineState *machine, MemoryRegion **sysmem,
                          MemoryRegion **secure_sysmem, ARMCPU **cpu,
                          AddressSpace **nsas)
{
    Object *cpuobj = object_new(machine->cpu_type);
    *cpu = ARM_CPU(cpuobj);
    CPUState *cs = CPU(*cpu);

    *sysmem = get_system_memory();

    object_property_set_link(cpuobj, OBJECT(*sysmem), "memory",
                             &error_abort);

    //set secure monitor to false
    object_property_set_bool(cpuobj, false, "has_el3", NULL);

    object_property_set_bool(cpuobj, false, "has_el2", NULL);

    object_property_set_bool(cpuobj, true, "realized", &error_fatal);

    *nsas = cpu_get_address_space(cs, ARMASIdx_NS);

    //disable exceptions on FP operations
    xnu_cpacr_intercept_write_const_val(*cpu, 0x300000);

    object_unref(cpuobj);
    //currently support only a single CPU and thus
    //use no interrupt controller and wire IRQs from devices directly to the CPU
}

static void n66_bootargs_setup(MachineState *machine)
{
    N66MachineState *nms = N66_MACHINE(machine);
    nms->bootinfo.firmware_loaded = true;
}

static void n66_cpu_reset(void *opaque)
{
    N66MachineState *nms = N66_MACHINE((MachineState *)opaque);
    ARMCPU *cpu = nms->cpu;
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;

    cpu_reset(cs);

    env->xregs[0] = nms->kbootargs_pa;
    env->pc = nms->kpc_pa;
}

static void n66_machine_init(MachineState *machine)
{
    N66MachineState *nms = N66_MACHINE(machine);
    MemoryRegion *sysmem;
    MemoryRegion *secure_sysmem;
    AddressSpace *nsas;
    ARMCPU *cpu;
    CPUState *cs;
    DeviceState *cpudev;

    n66_cpu_setup(machine, &sysmem, &secure_sysmem, &cpu, &nsas);

    nms->cpu = cpu;

    n66_memory_setup(machine, sysmem, secure_sysmem, nsas);

    nms->ktpp.as = nsas;

    cpudev = DEVICE(cpu);
    cs = CPU(cpu);
    nms->ktpp.cs = cs;
    AllocatedData *allocated_data = (AllocatedData *)nms->extra_data_pa;
    nms->ktpp.fake_port_pa = (hwaddr)&allocated_data->fake_port[0];
    nms->ktpp.remap_kernel_task_pa = (hwaddr)&allocated_data->kernel_task[0];

    if (0 != nms->driver_filename[0]) {
        xnu_hook_tr_setup(nsas, cpu);
        uint8_t *code = NULL;
        unsigned long size;
        if (!g_file_get_contents(nms->driver_filename, (char **)&code,
                                 &size, NULL)) {
            abort();
        }
        nms->ktpp.hook.va = UBC_INIT_VADDR_16B92;
        nms->ktpp.hook.pa = vtop_static(UBC_INIT_VADDR_16B92);
        nms->ktpp.hook.buf_va =
                        ptov_static((hwaddr)&allocated_data->hook_code[0]);
        nms->ktpp.hook.buf_pa = (hwaddr)&allocated_data->hook_code[0];
        nms->ktpp.hook.buf_size = HOOK_CODE_ALLOC_SIZE;
        nms->ktpp.hook.code = (uint8_t *)code;
        nms->ktpp.hook.code_size = size;
        nms->ktpp.hook.scratch_reg = 2;
    }

    if (0 != nms->qc_file_0_filename[0]) {
        qc_file_open(0, &nms->qc_file_0_filename[0]);
    }

    n66_add_cpregs(nms);

    n66_create_s3c_uart(nms, serial_hd(0));

    //wire timer to FIQ as expected by Apple's SoCs
    qdev_connect_gpio_out(cpudev, GTIMER_PHYS,
                          qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));

    n66_bootargs_setup(machine);

    nms->ptr_ntf.pa = vtop_static(KERNEL_TASK_PTR_16B92);
    nms->ptr_ntf.as = nsas;
    nms->ptr_ntf.cb = setup_fake_task_port;
    nms->ptr_ntf.cb_opaque = (void *)&nms->ktpp;
    xnu_dev_ptr_ntf_create(sysmem, &nms->ptr_ntf, "kernel_task_dev");

    qemu_register_reset(n66_cpu_reset, nms);
}

static void n66_set_ramdisk_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->ramdisk_filename, value, sizeof(nms->ramdisk_filename));
}

static char *n66_get_ramdisk_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->ramdisk_filename);
}

static void n66_set_kernel_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->kernel_filename, value, sizeof(nms->kernel_filename));
}

static char *n66_get_kernel_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->kernel_filename);
}

static void n66_set_dtb_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->dtb_filename, value, sizeof(nms->dtb_filename));
}

static char *n66_get_dtb_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->dtb_filename);
}

static void n66_set_tc_filename(Object *obj, const char *value,
                                Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->tc_filename, value, sizeof(nms->tc_filename));
}

static char *n66_get_tc_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->tc_filename);
}

static void n66_set_kern_args(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->kern_args, value, sizeof(nms->kern_args));
}

static char *n66_get_kern_args(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->kern_args);
}

static void n66_set_tunnel_port(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    nms->tunnel_port = atoi(value);
}

static char *n66_get_tunnel_port(Object *obj, Error **errp)
{
    char buf[128];
    N66MachineState *nms = N66_MACHINE(obj);
    snprintf(buf, 128, "%d", nms->tunnel_port);
    return g_strdup(buf);
}

static void n66_set_driver_filename(Object *obj, const char *value,
                                    Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->driver_filename, value, sizeof(nms->driver_filename));
}

static char *n66_get_driver_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->driver_filename);
}
static void n66_set_qc_file_0_filename(Object *obj, const char *value,
                                       Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->qc_file_0_filename, value, sizeof(nms->qc_file_0_filename));
}

static char *n66_get_qc_file_0_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->qc_file_0_filename);
}


static void n66_instance_init(Object *obj)
{
    object_property_add_str(obj, "ramdisk-filename", n66_get_ramdisk_filename,
                            n66_set_ramdisk_filename, NULL);
    object_property_set_description(obj, "ramdisk-filename",
                                    "Set the ramdisk filename to be loaded",
                                    NULL);

    object_property_add_str(obj, "kernel-filename", n66_get_kernel_filename,
                            n66_set_kernel_filename, NULL);
    object_property_set_description(obj, "kernel-filename",
                                    "Set the kernel filename to be loaded",
                                    NULL);

    object_property_add_str(obj, "dtb-filename", n66_get_dtb_filename,
                            n66_set_dtb_filename, NULL);
    object_property_set_description(obj, "dtb-filename",
                                    "Set the dev tree filename to be loaded",
                                    NULL);

    object_property_add_str(obj, "tc-filename", n66_get_tc_filename,
                            n66_set_tc_filename, NULL);
    object_property_set_description(obj, "tc-filename",
                                    "Set the trust cache filename to be loaded",
                                    NULL);

    object_property_add_str(obj, "kern-cmd-args", n66_get_kern_args,
                            n66_set_kern_args, NULL);
    object_property_set_description(obj, "kern-cmd-args",
                                    "Set the XNU kernel cmd args",
                                    NULL);

    object_property_add_str(obj, "tunnel-port", n66_get_tunnel_port,
                            n66_set_tunnel_port, NULL);
    object_property_set_description(obj, "tunnel-port",
                                    "Set the port for the tunnel connection",
                                    NULL);

    object_property_add_str(obj, "driver-filename", n66_get_driver_filename,
                            n66_set_driver_filename, NULL);
    object_property_set_description(obj, "driver-filename",
                                    "Set the driver filename to be loaded",
                                    NULL);

    object_property_add_str(obj, "qc-file-0-filename",
                            n66_get_qc_file_0_filename,
                            n66_set_qc_file_0_filename, NULL);
    object_property_set_description(obj, "qc-file-0-filename",
                                    "Set the qc file 0 filename to be loaded",
                                    NULL);
}

static void n66_machine_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    mc->desc = "iPhone 6s plus (n66 - s8000)";
    mc->init = n66_machine_init;
    mc->max_cpus = 1;
    //this disables the error message "Failed to query for block devices!"
    //when starting qemu - must keep at least one device
    //mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a57");
    mc->minimum_page_bits = 12;
}

static const TypeInfo n66_machine_info = {
    .name          = TYPE_N66_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(N66MachineState),
    .class_size    = sizeof(N66MachineClass),
    .class_init    = n66_machine_class_init,
    .instance_init = n66_instance_init,
};

static void n66_machine_types(void)
{
    type_register_static(&n66_machine_info);
}

type_init(n66_machine_types)
