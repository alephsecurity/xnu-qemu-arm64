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
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "hw/platform-bus.h"

#include "hw/arm/n66_iphone6splus.h"

#include "hw/arm/exynos4210.h"

#define RAMLIMIT_GB 255
#define RAMLIMIT_BYTES (RAMLIMIT_GB * 1024ULL * 1024 * 1024)

static const MemMapEntry n66memmap[] = {
    [N66_SECURE_MEM] =  { 0x4100000000, 0x100000 },
    [N66_MEM] =         { 0x40000000  , RAMLIMIT_BYTES },
    [N66_S3C_UART] =    { 0x20a0c0000  , 0x00004000 },
};

static const int n66irqmap[] = {
    [N66_S3C_UART] = 9,
};

#define N66_CPREG_FUNCS(name) \
static uint64_t n66_cpreg_read_##name(CPUARMState *env, \
                                      const ARMCPRegInfo *ri) \
{ \
    N66MachineState *nms = (N66MachineState *)ri->opaque; \
    /*fprintf(stderr, "n66_cpreg_read_" #name "() value: %llx\n", \
            nms->N66_CPREG_VAR_NAME(name));*/ \
    return nms->N66_CPREG_VAR_NAME(name); \
} \
static void n66_cpreg_write_##name(CPUARMState *env, const ARMCPRegInfo *ri, \
                                   uint64_t value) \
{ \
    N66MachineState *nms = (N66MachineState *)ri->opaque; \
    nms->N66_CPREG_VAR_NAME(name) = value; \
    /*fprintf(stderr, "n66_cpreg_write_" #name "() value: %llx\n", value); */\
}

#define N66_CPREG_DEF(p_name, p_op0, p_op1, p_crn, p_crm, p_op2) \
    { .cp = CP_REG_ARM64_SYSREG_CP, \
      .name = #p_name, .opc0 = p_op0, .crn = p_crn, .crm = p_crm, \
      .opc1 = p_op1, .opc2 = p_op2, .access = PL1_RW, .type = ARM_CP_IO, \
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
    N66_CPREG_DEF(ARM64_REG_HID11, 3, 0, 15, 13, 0),
    N66_CPREG_DEF(ARM64_REG_HID3, 3, 0, 15, 3, 0),
    N66_CPREG_DEF(ARM64_REG_HID5, 3, 0, 15, 5, 0),
    N66_CPREG_DEF(ARM64_REG_HID4, 3, 0, 15, 4, 0),
    N66_CPREG_DEF(ARM64_REG_HID8, 3, 0, 15, 8, 0),
    N66_CPREG_DEF(ARM64_REG_HID7, 3, 0, 15, 7, 0),
    N66_CPREG_DEF(ARM64_REG_LSU_ERR_STS, 3, 3, 15, 0, 0),
    N66_CPREG_DEF(PMC0, 3, 2, 15, 0, 0),
    N66_CPREG_DEF(PMC1, 3, 2, 15, 1, 0),
    N66_CPREG_DEF(PMCR1, 3, 1, 15, 1, 0),
    N66_CPREG_DEF(PMSR, 3, 1, 15, 13, 0),
    REGINFO_SENTINEL,
};

static void n66_add_cpregs(ARMCPU *cpu, N66MachineState *nms)
{
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

static void n66_create_s3c_uart(const N66MachineState *nms,
                                int uart, Chardev *chr)
{
    qemu_irq irq;
    DeviceState *d;
    SysBusDevice *s;
    hwaddr base = nms->memmap[uart].base;

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

static void n66_memory_setup(MachineState *machine,
                             MemoryRegion **sysmem,
                             MemoryRegion **secure_sysmem)
{
    N66MachineState *nms = N66_MACHINE(machine);
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *secure_ram = g_new(MemoryRegion, 1);

    *sysmem = get_system_memory();
    *secure_sysmem = g_new(MemoryRegion, 1);

    memory_region_init(*secure_sysmem, OBJECT(machine), "secure-memory",
                       UINT64_MAX);
    memory_region_add_subregion_overlap(*secure_sysmem, 0, *sysmem, -1);

    memory_region_allocate_system_memory(ram, NULL, "n66.ram",
                                         machine->ram_size);
    memory_region_add_subregion(*sysmem, nms->memmap[N66_MEM].base, ram);

    memory_region_allocate_system_memory(secure_ram, NULL,
                                         "n66_secure.ram",
                                         nms->memmap[N66_SECURE_MEM].size);
    memory_region_add_subregion(*secure_sysmem,
                                nms->memmap[N66_SECURE_MEM].base, secure_ram);
}

static void n66_cpu_setup(MachineState *machine, MemoryRegion *sysmem,
                          MemoryRegion *secure_sysmem, ARMCPU **cpu,
                          AddressSpace **sas, AddressSpace **nsas)
{
    Object *cpuobj = object_new(machine->cpu_type);
    *cpu = ARM_CPU(cpuobj);
    CPUState *cs = CPU(*cpu);

    object_property_set_link(cpuobj, OBJECT(sysmem), "memory",
                             &error_abort);

    object_property_set_link(cpuobj, OBJECT(secure_sysmem),
                             "secure-memory", &error_abort);

    //set secure monitor to true
    object_property_set_bool(cpuobj, true, "has_el3", NULL);

    object_property_set_bool(cpuobj, false, "has_el2", NULL);

    object_property_set_bool(cpuobj, true, "realized", &error_fatal);

    *sas = cpu_get_address_space(cs, ARMASIdx_S);
    *nsas = cpu_get_address_space(cs, ARMASIdx_NS);

    object_unref(cpuobj);
    //currently support only a single CPU and thus
    //use no interrupt controller and wire IRQs from devices directly to the CPU
}

static void n66_bootargs_setup(MachineState *machine)
{
    N66MachineState *nms = N66_MACHINE(machine);
    nms->bootinfo.firmware_loaded = true;
}

static void n66_load_images(MachineState *machine, AddressSpace *sas,
                            AddressSpace *nsas, uint64_t ram_size,
                            uint64_t *tz_pc, uint64_t *tz_bootargs)
{
    N66MachineState *nms = N66_MACHINE(machine);
    uint64_t k_pc;
    uint64_t tz_virt_base;
    uint64_t tz_phys_base;
    uint64_t tz_phys_load_base;
    uint64_t tz_virt_load_base;
    uint64_t k_virt_base;
    uint64_t k_phys_base;
    uint64_t k_phys_load_base;
    uint64_t k_virt_load_base;
    uint64_t phys_next_page;
    uint64_t virt_next_page;
    uint64_t tz_phys_next_page;
    uint64_t tz_virt_next_page;
    uint64_t dtb_virt_addr;
    uint64_t dtb_phys_addr;
    uint64_t ramdisk_phys_addr;
    uint64_t ramdisk_virt_addr_end;
    uint64_t tc_phys_addr;
    unsigned long kernel_file_size;
    unsigned long ramdisk_file_size;
    unsigned long tc_file_size;
    unsigned long dtb_size;

    macho_load_raw_file(nms->kernel_filename, nsas,
                        (char *)"kernel_raw_file.n66",
                        nms->memmap[N66_MEM].base,
                        &phys_next_page, &kernel_file_size, NULL);

    tc_phys_addr = phys_next_page + 0x400000;
    macho_load_raw_file(nms->tc_filename, nsas,
                        (char *)"tc_raw_file.n66",
                        tc_phys_addr, &phys_next_page,
                        &tc_file_size, NULL);

    arm_load_macho(nms->kernel_filename, nsas, (char *)"kernel.n66",
                   nms->memmap[N66_MEM].base, false, &k_pc, &phys_next_page,
                   &virt_next_page, &k_virt_base, &k_phys_base,
                   &k_virt_load_base, &k_phys_load_base);

    ramdisk_phys_addr = phys_next_page;
    ramdisk_virt_addr_end = virt_next_page;
    macho_load_raw_file(nms->ramdisk_filename, nsas,
                        (char *)"ramdisk_raw_file.n66",
                        ramdisk_phys_addr, &phys_next_page,
                        &ramdisk_file_size, &ramdisk_virt_addr_end);

    dtb_phys_addr = phys_next_page;
    dtb_virt_addr = ramdisk_virt_addr_end;

    macho_load_dtb(nms->dtb_filename, nsas, (char *)"dtb.n66", dtb_phys_addr,
                   &phys_next_page, &dtb_size, NULL, ramdisk_phys_addr,
                   ramdisk_file_size, tc_phys_addr, tc_file_size);

    macho_setup_bootargs((char *)"k_bootargs.n66", ram_size, nsas,
                         phys_next_page,
                         k_virt_load_base, //virt_base
                         k_phys_load_base, //phys base
                         phys_next_page, dtb_virt_addr, dtb_size,
                         nms->kern_args);

    arm_load_macho(nms->secmon_filename, sas, (char *)"trustzone.n66", 0,
                   true, tz_pc, &tz_phys_next_page, &tz_virt_next_page,
                   &tz_virt_base, &tz_phys_base, &tz_virt_load_base,
                   &tz_phys_load_base);

    *tz_bootargs = tz_phys_next_page;

    macho_tz_setup_bootargs((char *)"tz_bootargs.n66",
                            nms->memmap[N66_SECURE_MEM].size, sas,
                            *tz_bootargs, nms->memmap[N66_SECURE_MEM].base,
                             nms->memmap[N66_SECURE_MEM].base, phys_next_page,
                             k_pc, nms->memmap[N66_MEM].base);
}

static uint64_t g_tz_pc;
static uint64_t g_tz_bootargs;

static void n66_cpu_reset(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;

    cpu_reset(cs);

    //fprintf(stderr, "g_tz_bootargs: %016llX g_tz_pc: %016llX\n", g_tz_bootargs,
    //        g_tz_pc);
    env->xregs[0] = g_tz_bootargs;
    env->pc = g_tz_pc;
}

static void n66_machine_init(MachineState *machine)
{
    N66MachineState *nms = N66_MACHINE(machine);
    MemoryRegion *sysmem;
    MemoryRegion *secure_sysmem;
    AddressSpace *sas;
    AddressSpace *nsas;
    ARMCPU *cpu;
    CPUState *cs;
    DeviceState *cpudev;
    uint64_t tz_pc;
    uint64_t tz_bootargs;

    nms->memmap = n66memmap;
    nms->irqmap = n66irqmap;

    n66_memory_setup(machine, &sysmem, &secure_sysmem);

    n66_cpu_setup(machine, sysmem, secure_sysmem, &cpu, &sas, &nsas);
    cpudev = DEVICE(cpu);
    cs = CPU(cpu);

    n66_add_cpregs(cpu, nms);

    n66_create_s3c_uart(nms, N66_S3C_UART, serial_hd(0));

    //wire timer to FIQ as expected by Apple's SoCs
    qdev_connect_gpio_out(cpudev, GTIMER_PHYS,
                          qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));

    n66_bootargs_setup(machine);

    n66_load_images(machine, sas, nsas, machine->ram_size, &tz_pc,
                    &tz_bootargs);
    g_tz_pc = tz_pc;
    g_tz_bootargs = tz_bootargs;

    qemu_register_reset(n66_cpu_reset, ARM_CPU(cs));
}

static void n66_set_ramdisk_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    strlcpy(nms->ramdisk_filename, value, sizeof(nms->ramdisk_filename));
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

    strlcpy(nms->kernel_filename, value, sizeof(nms->kernel_filename));
}

static char *n66_get_kernel_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->kernel_filename);
}

static void n66_set_secmon_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    strlcpy(nms->secmon_filename, value, sizeof(nms->secmon_filename));
}

static char *n66_get_secmon_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->secmon_filename);
}

static void n66_set_dtb_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    strlcpy(nms->dtb_filename, value, sizeof(nms->dtb_filename));
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

    strlcpy(nms->tc_filename, value, sizeof(nms->tc_filename));
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

    strlcpy(nms->kern_args, value, sizeof(nms->kern_args));
}

static char *n66_get_kern_args(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->kern_args);
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

    object_property_add_str(obj, "secmon-filename", n66_get_secmon_filename,
                            n66_set_secmon_filename, NULL);
    object_property_set_description(obj, "secmon-filename",
                                    "Set the trustzone filename to be loaded",
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
