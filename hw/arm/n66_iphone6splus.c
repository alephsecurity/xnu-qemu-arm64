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

//compiled nop instruction: mov x0, x0
#define NOP_INST (0xaa0003e0)
#define MOV_W0_01_INST (0x52800020)
#define CMP_X9_x9_INST (0xeb09013f)
//compiled  instruction: mov w7, #0
#define W7_ZERO_INST (0x52800007)

#define INITIAL_BRANCH_VADDR_16B92 (0xfffffff0070a5098)
#define BZERO_COND_BRANCH_VADDR_16B92 (0xfffffff0070996d8)
#define SMC_INST_VADDR_16B92 (0xfffffff0070a7d3c)
#define SLIDE_SET_INST_VADDR_16B92 (0xfffffff00748ef30)
#define NOTIFY_KERNEL_TASK_PTR_16B92 (0xfffffff0070f4d90)
#define CORE_TRUST_CHECK_16B92 (0xfffffff0061e136c)
#define TFP0_TASK_FOR_PID_16B92 (0xfffffff0074a27bc)
#define TFP0_CNVRT_PORT_TO_TASK_16B92 (0xfffffff0070d7cb8)
#define TFP0_PORT_NAME_TO_TASK_16B92 (0xfffffff0070d82d8)
#define TFP0_KERNEL_TASK_CMP_1_16B92 (0xfffffff0070d7b04)
#define TFP0_KERNEL_TASK_CMP_2_16B92 (0xfffffff0070d810c)

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
N66_CPREG_FUNCS(L2ACTLR_EL1)

static const ARMCPRegInfo n66_cp_reginfo_kvm[] = {
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
    N66_CPREG_DEF(L2ACTLR_EL1, 3, 1, 15, 0, 0, PL1_RW),

    // Aleph-specific registers for communicating with QEMU

    // REG_QEMU_CALL:
    { .cp = CP_REG_ARM64_SYSREG_CP, .name = "REG_QEMU_CALL",
      .opc0 = 3, .opc1 = 3, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL0_RW, .type = ARM_CP_IO, .state = ARM_CP_STATE_AA64,
      .readfn = qemu_call_status,
      .writefn = qemu_call },

    REGINFO_SENTINEL,
};

// This is the same as the array for kvm, but without
// the L2ACTLR_EL1, which is already defined in TCG.
// Duplicating this list isn't a perfect solution,
// but it's quick and reliable.
static const ARMCPRegInfo n66_cp_reginfo_tcg[] = {
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
static uint32_t g_mov_w0_01_inst = MOV_W0_01_INST;
static uint32_t g_compare_true_inst = CMP_X9_x9_INST;
static uint32_t g_w7_zero_inst = W7_ZERO_INST;
static uint32_t g_set_cpacr_and_branch_inst[] = {
    //  91400c21       add x1, x1, 3, lsl 12    # x1 = x1 + 0x3000
    //  d378dc21       lsl x1, x1, 8            # x1 = x1 * 0x100 (x1 = 0x300000)
    //  d5181041       msr cpacr_el1, x1        # cpacr_el1 = x1 (enable FP)
    //  aa1f03e1       mov x1, xzr              # x1 = 0
    //  140007ec       b 0x1fc0                 # branch to regular start
    0x91400c21, 0xd378dc21, 0xd5181041, 0xaa1f03e1, 0x140007ec
};
static uint32_t g_bzero_branch_unconditionally_inst = 0x14000039;
static uint32_t g_qemu_call = 0xd51bff1f;

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
    nms->N66_CPREG_VAR_NAME(L2ACTLR_EL1) = 0;

    if (kvm_enabled()) {
        define_arm_cp_regs_with_opaque(cpu, n66_cp_reginfo_kvm, nms);
    } else {
        define_arm_cp_regs_with_opaque(cpu, n66_cp_reginfo_tcg, nms);
    }
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

static void n66_patch_kernel(AddressSpace *nsas)
{
    //patch the initial branch with instructions that set up CPACR
    address_space_rw(nsas, vtop_static(INITIAL_BRANCH_VADDR_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_set_cpacr_and_branch_inst,
                     sizeof(g_set_cpacr_and_branch_inst), 1);

    //patch bzero to avoid "dc zva", which zeroes out different amount of bytes
    //under different CPUs
    address_space_rw(nsas, vtop_static(BZERO_COND_BRANCH_VADDR_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_bzero_branch_unconditionally_inst,
                     sizeof(g_bzero_branch_unconditionally_inst), 1);

    //patch the smc instruction to nop since we no longer use a secure
    //monitor because we disabled KPP this way
    address_space_rw(nsas, vtop_static(SMC_INST_VADDR_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);

    //patch the slide set instruction when creating a new process
    //in parse_machfile() in order to disable aslr for user mode apps
    address_space_rw(nsas, vtop_static(SLIDE_SET_INST_VADDR_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_w7_zero_inst,
                     sizeof(g_w7_zero_inst), 1);

    //patch the instruction that writes the kernel task port pointer
    //in task_create_internal so we can catch it without MMIO
    address_space_rw(nsas, vtop_static(NOTIFY_KERNEL_TASK_PTR_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_qemu_call,
                     sizeof(g_qemu_call), 1);
    address_space_rw(nsas, vtop_static(TFP0_TASK_FOR_PID_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);
    address_space_rw(nsas, vtop_static(TFP0_CNVRT_PORT_TO_TASK_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_compare_true_inst,
                     sizeof(g_compare_true_inst), 1);
    address_space_rw(nsas, vtop_static(TFP0_PORT_NAME_TO_TASK_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_compare_true_inst,
                     sizeof(g_compare_true_inst), 1);
    address_space_rw(nsas, vtop_static(TFP0_KERNEL_TASK_CMP_1_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_compare_true_inst,
                     sizeof(g_compare_true_inst), 1);
    address_space_rw(nsas, vtop_static(TFP0_KERNEL_TASK_CMP_2_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_compare_true_inst,
                     sizeof(g_compare_true_inst), 1);
    address_space_rw(nsas, vtop_static(CORE_TRUST_CHECK_16B92),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_w0_01_inst,
                     sizeof(g_mov_w0_01_inst), 1);
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
    hwaddr ramfb_pa = 0;
    video_boot_args v_bootargs = {0};
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

    //now account for the loaded kernel
    arm_load_macho(nms->kernel_filename, nsas, sysmem, "kernel.n66",
                    N66_PHYS_BASE, virt_base, kernel_low,
                    kernel_high, &phys_pc);
    nms->kpc_pa = phys_pc;
    used_ram_for_blobs += (align_64k_high(kernel_high) - kernel_low);

    n66_patch_kernel(nsas);

    phys_ptr = align_64k_high(vtop_static(kernel_high));

    //now account for the ramdisk
    nms->ramdisk_file_dev.pa = 0;
    hwaddr ramdisk_size = 0;
    if (0 != nms->ramdisk_filename[0]) {
        nms->ramdisk_file_dev.pa = phys_ptr;
        macho_map_raw_file(nms->ramdisk_filename, nsas, sysmem,
                           "ramdisk_raw_file.n66", nms->ramdisk_file_dev.pa,
                           &nms->ramdisk_file_dev.size);
        ramdisk_size = nms->ramdisk_file_dev.size;
        phys_ptr += align_64k_high(nms->ramdisk_file_dev.size);
    }

    //now account for device tree
    macho_load_dtb(nms->dtb_filename, nsas, sysmem, "dtb.n66", phys_ptr,
                   &dtb_size, nms->ramdisk_file_dev.pa,
                   ramdisk_size, &nms->uart_mmio_pa);
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

    if (nms->use_ramfb){
        ramfb_pa = ((hwaddr)&((AllocatedData *)nms->extra_data_pa)->ramfb[0]);
        xnu_define_ramfb_device(nsas,ramfb_pa);
        xnu_get_video_bootargs(&v_bootargs, ramfb_pa);
    }

    phys_ptr += align_64k_high(sizeof(AllocatedData));
    top_of_kernel_data_pa = phys_ptr;
    remaining_mem_size = machine->ram_size - used_ram_for_blobs;
    mem_size = allocated_ram_pa - N66_PHYS_BASE + remaining_mem_size;
    macho_setup_bootargs("k_bootargs.n66", nsas, sysmem, kbootargs_pa,
                         virt_base, N66_PHYS_BASE, mem_size,
                         top_of_kernel_data_pa, dtb_va, dtb_size,
                         v_bootargs, nms->kern_args);

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

//hooks arg is expected like this:
//"hookfilepath@va@scratch_reg#hookfilepath@va@scratch_reg#..."

static void n66_machine_init_hook_funcs(N66MachineState *nms,
                                        AddressSpace *nsas)
{
    AllocatedData *allocated_data = (AllocatedData *)nms->extra_data_pa;
    uint64_t i = 0;
    char *orig_pos = NULL;
    size_t orig_len = 0;
    char *pos = NULL;
    char *next_pos = NULL;
    size_t len = 0;
    char *elem = NULL;
    char *next_elem = NULL;
    size_t elem_len = 0;
    char *end;

    //ugly solution but a simple one for now, use this memory which is fixed at
    //(pa: 0x0000000049BF4C00 va: 0xFFFFFFF009BF4C00) for globals to be common
    //between drivers/hooks. Please adjust address if anything changes in
    //the layout of the memory the "boot loader" sets up
    uint64_t zero_var = 0;
    address_space_rw(nsas, (hwaddr)&allocated_data->hook_globals[0],
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&zero_var,
                     sizeof(zero_var), 1);

    nms->hook_funcs_count = 0;

    pos = &nms->hook_funcs_cfg[0];
    if ((NULL == pos) || (0 == strlen(pos))) {
        //fprintf(stderr, "no function hooks configured\n");
        return;
    }

    orig_pos = pos;
    orig_len = strlen(pos);

    do {
        next_pos = memchr(pos, '#', strlen(pos));
        if (NULL != next_pos) {
            len = next_pos - pos;
        } else {
            len = strlen(pos);
        }

        elem = pos;
        next_elem = memchr(elem, '@', len);
        if (NULL == next_elem) {
            fprintf(stderr, "hook[%llu] failed to find '@' in %s\n", i, elem);
            abort();
        }
        elem_len = next_elem - elem;
        elem[elem_len] = 0;

        uint8_t *code = NULL;
        size_t size = 0;
        if (!g_file_get_contents(elem, (char **)&code, &size, NULL)) {
            fprintf(stderr, "hook[%llu] failed to read filepath: %s\n",
                    i, elem);
            abort();
        }

        elem += elem_len + 1;
        next_elem = memchr(elem, '@', len);
        if (NULL == next_elem) {
            fprintf(stderr, "hook[%llu] failed to find '@' in %s\n", i, elem);
            abort();
        }
        elem_len = next_elem - elem;
        elem[elem_len] = 0;

        nms->hook_funcs[i].va = strtoull(elem, &end, 16);
        nms->hook_funcs[i].pa = vtop_static(nms->hook_funcs[i].va);
        nms->hook_funcs[i].buf_va =
                   ptov_static((hwaddr)&allocated_data->hook_funcs_code[i][0]);
        nms->hook_funcs[i].buf_pa =
                                (hwaddr)&allocated_data->hook_funcs_code[i][0];
        nms->hook_funcs[i].buf_size = HOOK_CODE_ALLOC_SIZE;
        nms->hook_funcs[i].code = (uint8_t *)code;
        nms->hook_funcs[i].code_size = size;

        elem += elem_len + 1;
        if (NULL != next_pos) {
            elem_len = next_pos - elem;
        } else {
            elem_len = strlen(elem);
        }
        elem[elem_len] = 0;

        nms->hook_funcs[i].scratch_reg = (uint8_t)strtoul(elem, &end, 10);

        i++;
        pos += len + 1;
    } while ((NULL != pos) && (pos < (orig_pos + orig_len)));

    nms->hook_funcs_count = i;
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

    cpudev = DEVICE(cpu);
    cs = CPU(cpu);
    AllocatedData *allocated_data = (AllocatedData *)nms->extra_data_pa;

    if (0 != nms->driver_filename[0]) {
        xnu_hook_tr_setup(nsas, cpu);
        uint8_t *code = NULL;
        unsigned long size;
        if (!g_file_get_contents(nms->driver_filename, (char **)&code,
                                 &size, NULL)) {
            abort();
        }
        nms->hook.va = UBC_INIT_VADDR_16B92;
        nms->hook.pa = vtop_static(UBC_INIT_VADDR_16B92);
        nms->hook.buf_va =
                        ptov_static((hwaddr)&allocated_data->hook_code[0]);
        nms->hook.buf_pa = (hwaddr)&allocated_data->hook_code[0];
        nms->hook.buf_size = HOOK_CODE_ALLOC_SIZE;
        nms->hook.code = (uint8_t *)code;
        nms->hook.code_size = size;
        nms->hook.scratch_reg = 2;
    }

    if (0 != nms->qc_file_0_filename[0]) {
        qc_file_open(0, &nms->qc_file_0_filename[0]);
    }

    if (0 != nms->qc_file_1_filename[0]) {
        qc_file_open(1, &nms->qc_file_1_filename[0]);
    }

    if (0 != nms->qc_file_log_filename[0]) {
        qc_file_open(2, &nms->qc_file_log_filename[0]);
    }

    n66_machine_init_hook_funcs(nms, nsas);

    n66_add_cpregs(nms);

    n66_create_s3c_uart(nms, serial_hd(0));

    //wire timer to FIQ as expected by Apple's SoCs
    qdev_connect_gpio_out(cpudev, GTIMER_PHYS,
                          qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));

    n66_bootargs_setup(machine);

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

static void n66_set_hook_funcs(Object *obj, const char *value, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->hook_funcs_cfg, value, sizeof(nms->hook_funcs_cfg));
}

static char *n66_get_hook_funcs(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->hook_funcs_cfg);
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

static void n66_set_qc_file_1_filename(Object *obj, const char *value,
                                       Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->qc_file_1_filename, value, sizeof(nms->qc_file_1_filename));
}

static char *n66_get_qc_file_1_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->qc_file_1_filename);
}

static void n66_set_qc_file_log_filename(Object *obj, const char *value,
                                       Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);

    g_strlcpy(nms->qc_file_log_filename, value,
              sizeof(nms->qc_file_log_filename));
}

static char *n66_get_qc_file_log_filename(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    return g_strdup(nms->qc_file_log_filename);
}

static void n66_set_xnu_ramfb(Object *obj, const char *value,
                                       Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    if (strcmp(value,"on") == 0)
        nms->use_ramfb = true;
    else {
        if (strcmp(value,"off") != 0)
            fprintf(stderr,"NOTE: the value of xnu-ramfb is not valid,\
the framebuffer will be disabled.\n");
        nms->use_ramfb = false;
    }
}

static char* n66_get_xnu_ramfb(Object *obj, Error **errp)
{
    N66MachineState *nms = N66_MACHINE(obj);
    if (nms->use_ramfb)
        return g_strdup("on");
    else 
        return g_strdup("off");
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

    object_property_add_str(obj, "hook-funcs", n66_get_hook_funcs,
                            n66_set_hook_funcs, NULL);
    object_property_set_description(obj, "hook-funcs",
                                    "Set the hook funcs to be loaded",
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

    object_property_add_str(obj, "qc-file-1-filename",
                            n66_get_qc_file_1_filename,
                            n66_set_qc_file_1_filename, NULL);
    object_property_set_description(obj, "qc-file-1-filename",
                                    "Set the qc file 1 filename to be loaded",
                                    NULL);

    object_property_add_str(obj, "qc-file-log-filename",
                            n66_get_qc_file_log_filename,
                            n66_set_qc_file_log_filename, NULL);
    object_property_set_description(obj, "qc-file-log-filename",
                                   "Set the qc file log filename to be loaded",
                                    NULL);

    object_property_add_str(obj, "xnu-ramfb",
                            n66_get_xnu_ramfb,
                            n66_set_xnu_ramfb, NULL);
    object_property_set_description(obj, "xnu-ramfb",
                                    "Turn on the display framebuffer",
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
