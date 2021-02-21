/*
 * iPhone 11 - n104
 *
 * Copyright (c) 2020 Jonathan Afek <jonyafek@me.com>
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
 
 //TODO: JONATHANA restore global hook data and log file if we want to revive the
 // IPC messages sniffer that was implemented for iOS 12 for use of sharing the global locks

 //TODO: JONATHANA implement the TFP0 hooks for iOS 14

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

#include "hw/arm/n104_iphone11.h"

#include "hw/arm/exynos4210.h"
#include "hw/arm/guest-services/general.h"

#include "hw/arm/guest-services/xnu_general.h"

#define N104_PHYS_BASE (0x40000000)
#define ONE_MB (0x100000)
#define N104_MORE_ALLOC_DATA_PADDR (N104_PHYS_BASE + ONE_MB)
#define N104_MORE_ALLOC_DATA_SIZE (64 * ONE_MB)

//compiled nop instruction: mov x0, x0
#define NOP_INST (0xaa0003e0)
#define MOV_W0_01_INST (0x52800020)
#define MOV_X0_00_INST (0xD2800000)
#define MOV_X8_00_INST (0xD2800008)
#define CMP_X9_x9_INST (0xeb09013f)
#define RET_INST (0xd65f03c0)
#define RETAB_INST (0xd65f0fff)
#define MOV_X14_0_INST (0xd280000e)
//compiled  instruction: mov w7, #0
#define W7_ZERO_INST (0x52800007)
#define J_170_INST (0x1400005c)
//TODO: JONATHANA QEMU doesn't know this instruction??
//so for now move to "tlbi vmalle1is" instead of "tlbi vmalls12e1is"
//as in QEMU it seems to do the same but will need to do differently with
//KVM
//#define TLBI_VMALLS12E1IS (0xd50c83df)
#define TLBI_VMALLE1IS (0xd508831f)

//TODO: JONATHANA implement a more general and correct patch mechanism that
//includes a patch finder - maybe take one from an open source JB??
//Also make the patch mechanism cleaner and general and in a different module.
//no need for a different static variable to hold the patch values
#define FAULT_PPL_LOCKED_DOWN_CHECK_M1_INST_VADDR_18A5342e (0xFFFFFFF008131C60)
#define FAULT_PPL_LOCKED_DOWN_CHECK_INST_VADDR_18A5342e (0xFFFFFFF008131C64)
#define RORGN_STASH_RANGE_2ND_INST_VADDR_18A5342e (0xFFFFFFF007B622F0)
#define RORGN_LOCKDOWN_2ND_INST_VADDR_18A5342e (0xFFFFFFF007B62C80)
#define SPR_LOCKDOWN_FUNC_VADDR_18A5342e (0xFFFFFFF007B72350)
#define GXF_ENABLE_FUNC_VADDR_18A5342e (0xFFFFFFF008131E90)

#define UNKNOWN_INST_18A5342e (0xFFFFFFF0097FF8B0)
#define UNKNOWN_INST2_18A5342e (0xFFFFFFF007B58F40)

//TODO: JONATHANA can probably find bettter patches.
//all related to code signing and to allow execution
#define CORE_TRUST_CHECK_18A5342e (0xFFFFFFF0083052B0)
#define CS_ASSOC_MAP_1_18A5342e (0xFFFFFFF007ED54F0)
#define CS_ASSOC_MAP_2_18A5342e (0xFFFFFFF007ED54F4)
#define PMAP_CS_ENF_1_18A5342e (0xFFFFFFF0098035A8)
#define PMAP_CS_ENF_2_18A5342e (0xFFFFFFF0098035AC)
#define CS_INVALID_PAGE_1_18A5342e (0xFFFFFFF007E56D88)
#define CS_INVALID_PAGE_2_18A5342e (0xFFFFFFF007E56D8C)
#define CS_PAGE_VAL_1_18A5342e (0xFFFFFFF007AE370C)
#define CS_PAGE_VAL_2_18A5342e (0xFFFFFFF007AE3710)

//SANDBOX bypass
#define SB_EVALUAET_INTERNAL_1_18A5342e (0xFFFFFFF0092C5A24)
#define SB_EVALUAET_INTERNAL_2_18A5342e (0xFFFFFFF0092C5A28)

//memory mapping patches
#define VM_MAP_PROTECT_1_18A5342e (0xFFFFFFF007AF719C)
#define VM_MAP_PROTECT_2_18A5342e (0xFFFFFFF007AF71A8)
#define VM_MAP_PROTECT_3_18A5342e (0xFFFFFFF007AF71B0)
#define VM_MAP_PROTECT_4_18A5342e (0xFFFFFFF007AF71B4)
#define VM_MAP_PROTECT_5_18A5342e (0xFFFFFFF007AF71C0)
#define VM_MAP_PROTECT_6_18A5342e (0xFFFFFFF007AF71CC)

//TODO: JONATHANA
//#define CHECK_FOR_SIG_1_18A5342e (0xFFFFFFF007E7A1B4)
//#define CHECK_FOR_SIG_2_18A5342e (0xFFFFFFF007E7A1B8)
//#define MAC_CRED_LAB_EXECVE_1_18A5342e (0xFFFFFFF008116818)
//#define MAC_CRED_LAB_EXECVE_2_18A5342e (0xFFFFFFF00811681C)

#define IS_IMGPF_DISABLE_ASLR_18A5342e (0xFFFFFFF007F068F0)
#define MAP_SLIDE_2_DISABLE_ASLR_18A5342e (0xFFFFFFF007F1F768)

#define BSD_INIT_SP_SUB_18A5342e (0xFFFFFFF007E45E14)

#define UNUSED_BCM_DRIVER_SECTION (0xFFFFFFF009424000)

#define STATIC_PTABLE_VADDR (0xFFFFFFF007734000)
#define STATIC_PTABLE_SIZE (0x38000)

//hook the kernel to execute our "driver" code in this function
//after things are already running in the kernel but the root mount is not
//yet mounted.
//We chose this place in the beginning net_init_run() in bsd_init()
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
#define NET_INIT_RUN_VADDR_18A5342e (0xFFFFFFF007C5F228)


//TODO: JONATHANA move these MACROs to a common file
//(common between ios devices)
#define N104_CPREG_FUNCS(name) \
static uint64_t n104_cpreg_read_##name(CPUARMState *env, \
                                      const ARMCPRegInfo *ri) \
{ \
    N104MachineState *nms = (N104MachineState *)ri->opaque; \
    return nms->N104_CPREG_VAR_NAME(name); \
} \
static void n104_cpreg_write_##name(CPUARMState *env, const ARMCPRegInfo *ri, \
                                    uint64_t value) \
{ \
    N104MachineState *nms = (N104MachineState *)ri->opaque; \
    nms->N104_CPREG_VAR_NAME(name) = value; \
}

#define N104_CPREG_DEF(p_name, p_op0, p_op1, p_crn, p_crm, p_op2, p_access) \
    { .cp = CP_REG_ARM64_SYSREG_CP, \
      .name = #p_name, .opc0 = p_op0, .crn = p_crn, .crm = p_crm, \
      .opc1 = p_op1, .opc2 = p_op2, .access = p_access, .type = ARM_CP_IO, \
      .state = ARM_CP_STATE_AA64, .readfn = n104_cpreg_read_##p_name, \
      .writefn = n104_cpreg_write_##p_name }

N104_CPREG_FUNCS(ARM64_REG_MIGSTS_EL1)
N104_CPREG_FUNCS(ARM64_REG_KERNELKEYLO_EL1)
N104_CPREG_FUNCS(ARM64_REG_KERNELKEYHI_EL1)
N104_CPREG_FUNCS(ARM64_REG_HID4)
N104_CPREG_FUNCS(ARM64_REG_EHID4)
N104_CPREG_FUNCS(ARM64_REG_APPL_1)
N104_CPREG_FUNCS(ARM64_REG_CYC_OVRD)
N104_CPREG_FUNCS(ARM64_REG_APPL_2)
N104_CPREG_FUNCS(ARM64_REG_APPL_3)
N104_CPREG_FUNCS(ARM64_REG_APPL_4)
N104_CPREG_FUNCS(ARM64_REG_APPL_5)
N104_CPREG_FUNCS(ARM64_REG_ACC_CFG)
N104_CPREG_FUNCS(ARM64_REG_APPL_6)
N104_CPREG_FUNCS(ARM64_REG_APPL_7)
N104_CPREG_FUNCS(ARM64_REG_APPL_8)
N104_CPREG_FUNCS(ARM64_REG_APPL_9)
N104_CPREG_FUNCS(ARM64_REG_APPL_10)
N104_CPREG_FUNCS(ARM64_REG_APPL_11)
N104_CPREG_FUNCS(ARM64_REG_APPL_12)
N104_CPREG_FUNCS(ARM64_REG_APPL_13)
N104_CPREG_FUNCS(ARM64_REG_VMSA_LOCK_EL1)
N104_CPREG_FUNCS(ARM64_REG_APPL_14)
N104_CPREG_FUNCS(ARM64_REG_APPL_15)
N104_CPREG_FUNCS(ARM64_REG_APPL_16)
N104_CPREG_FUNCS(ARM64_REG_APPL_17)
N104_CPREG_FUNCS(ARM64_REG_IPI_SR)
N104_CPREG_FUNCS(ARM64_REG_UPMPCM)
N104_CPREG_FUNCS(ARM64_REG_UPMCR0)

static const ARMCPRegInfo n104_cp_reginfo_kvm[] = {

    // Aleph-specific registers for communicating with QEMU

    // REG_QEMU_CALL:
    { .cp = CP_REG_ARM64_SYSREG_CP, .name = "REG_QEMU_CALL",
      .opc0 = 3, .opc1 = 3, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL0_RW, .type = ARM_CP_IO, .state = ARM_CP_STATE_AA64,
      .readfn = qemu_call_status,
      .writefn = qemu_call },

    N104_CPREG_DEF(ARM64_REG_MIGSTS_EL1, 3, 4, 15, 0, 4, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_KERNELKEYLO_EL1, 3, 4, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_KERNELKEYHI_EL1, 3, 4, 15, 1, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_HID4, 3, 0, 15, 4, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_EHID4, 3, 0, 15, 4, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_1, 3, 4, 15, 1, 4, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_CYC_OVRD, 3, 5, 15, 5, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_2, 3, 2, 15, 0, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_3, 3, 2, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_4, 3, 6, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_5, 3, 1, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_ACC_CFG, 3, 5, 15, 4, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_6, 3, 6, 15, 1, 5, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_7, 3, 6, 15, 1, 6, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_8, 3, 6, 15, 1, 7, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_9, 3, 6, 15, 3, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_10, 3, 6, 15, 1, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_11, 3, 6, 15, 1, 2, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_12, 3, 6, 15, 8, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_13, 3, 6, 15, 8, 2, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_VMSA_LOCK_EL1, 3, 4, 15, 1, 2, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_14, 1, 0, 8, 2, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_15, 3, 4, 15, 1, 3, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_16, 3, 6, 15, 3, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_17, 3, 6, 15, 8, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_IPI_SR, 3, 5, 15, 1, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_UPMPCM, 3, 7, 15, 5, 4, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_UPMCR0, 3, 7, 15, 0, 4, PL1_RW),

    REGINFO_SENTINEL,
};

// This is the same as the array for kvm, but without
// the L2ACTLR_EL1, which is already defined in TCG.
// Duplicating this list isn't a perfect solution,
// but it's quick and reliable.
static const ARMCPRegInfo n104_cp_reginfo_tcg[] = {
    // Aleph-specific registers for communicating with QEMU

    // REG_QEMU_CALL:
    { .cp = CP_REG_ARM64_SYSREG_CP, .name = "REG_QEMU_CALL",
      .opc0 = 3, .opc1 = 3, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL0_RW, .type = ARM_CP_IO, .state = ARM_CP_STATE_AA64,
      .readfn = qemu_call_status,
      .writefn = qemu_call },

    // Apple-specific registers
    N104_CPREG_DEF(ARM64_REG_MIGSTS_EL1, 3, 4, 15, 0, 4, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_KERNELKEYLO_EL1, 3, 4, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_KERNELKEYHI_EL1, 3, 4, 15, 1, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_HID4, 3, 0, 15, 4, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_EHID4, 3, 0, 15, 4, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_1, 3, 4, 15, 1, 4, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_CYC_OVRD, 3, 5, 15, 5, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_2, 3, 2, 15, 0, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_3, 3, 2, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_4, 3, 6, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_5, 3, 1, 15, 1, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_ACC_CFG, 3, 5, 15, 4, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_6, 3, 6, 15, 1, 5, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_7, 3, 6, 15, 1, 6, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_8, 3, 6, 15, 1, 7, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_9, 3, 6, 15, 3, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_10, 3, 6, 15, 1, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_11, 3, 6, 15, 1, 2, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_12, 3, 6, 15, 8, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_13, 3, 6, 15, 8, 2, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_VMSA_LOCK_EL1, 3, 4, 15, 1, 2, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_14, 1, 0, 8, 2, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_15, 3, 4, 15, 1, 3, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_16, 3, 6, 15, 3, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_APPL_17, 3, 6, 15, 8, 0, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_IPI_SR, 3, 5, 15, 1, 1, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_UPMPCM, 3, 7, 15, 5, 4, PL1_RW),
    N104_CPREG_DEF(ARM64_REG_UPMCR0, 3, 7, 15, 0, 4, PL1_RW),

    REGINFO_SENTINEL,
};

//TODO: JONATHANA make a better mechanism for this w/o these vars
static uint32_t g_nop_inst = NOP_INST;
static uint32_t g_ret_inst = RET_INST;
static uint32_t g_retab_inst = RETAB_INST;
static uint32_t g_mov_x14_0_inst = MOV_X14_0_INST;
static uint32_t g_flush_tlb_inst = TLBI_VMALLE1IS;
static uint32_t g_mov_w0_01_inst = MOV_W0_01_INST;
static uint32_t g_mov_x0_00_inst = MOV_X0_00_INST;
static uint32_t g_mov_x8_00_inst = MOV_X8_00_INST;
static uint32_t g_j_170_inst = J_170_INST;
static uint32_t g_qemu_call = 0xd51bff1f;

static void n104_add_cpregs(N104MachineState *nms)
{
    ARMCPU *cpu = nms->cpu;

    nms->N104_CPREG_VAR_NAME(ARM64_REG_MIGSTS_EL1) = 2;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_KERNELKEYLO_EL1) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_KERNELKEYHI_EL1) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_HID4) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_EHID4) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_1) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_CYC_OVRD) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_2) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_3) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_4) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_5) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_ACC_CFG) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_6) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_7) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_8) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_9) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_10) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_11) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_12) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_13) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_VMSA_LOCK_EL1) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_14) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_15) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_16) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_APPL_17) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_IPI_SR) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_UPMPCM) = 0;
    nms->N104_CPREG_VAR_NAME(ARM64_REG_UPMCR0) = 0;

    //TODO: currently KVM is not yet supported
    if (kvm_enabled()) {
        define_arm_cp_regs_with_opaque(cpu, n104_cp_reginfo_kvm, nms);
    } else {
        define_arm_cp_regs_with_opaque(cpu, n104_cp_reginfo_tcg, nms);
    }
}

static int64_t n104_qemu_call_fb(qc_general_cb_args_t *args, void *opaque)
{
    if (XNU_FB_GET_VADDR_QEMU_CALL == args->id) {
        N104MachineState *nms = (N104MachineState *)opaque;
        MoreAllocatedData *md = (MoreAllocatedData *)nms->more_extra_data_pa;
        args->data1 = ptov_static((hwaddr)&md->framebuffer);
        return 0;
    }
    if (XNU_GET_MORE_ALLOCATED_DATA == args->id) {
        N104MachineState *nms = (N104MachineState *)opaque;
        args->data1 = ptov_static(nms->more_extra_data_pa);
        args->data2 = N104_MORE_ALLOC_DATA_SIZE;
        return 0;
    }
    assert(false);
}

static void n104_create_s3c_uart(const N104MachineState *nms, Chardev *chr)
{
    qemu_irq irq;
    DeviceState *d;
    SysBusDevice *s;
    hwaddr base = nms->uart_mmio_pa;

    //hack for now. create a device that is not used just to have a dummy
    //unused interrupt
    d = qdev_new(TYPE_PLATFORM_BUS_DEVICE);
    s = SYS_BUS_DEVICE(d);
    sysbus_init_irq(s, &irq);
    //pass a dummy irq as we don't need nor want interrupts for this UART
    DeviceState *dev = exynos4210_uart_create(base, 256, 0, chr, irq);
    if (!dev) {
        fprintf(stderr,
                "n104_create_s3c_uart(): exynos4210_uart_create() failed\n");
        abort();
    }
}

static void n104_patch_kernel(AddressSpace *nsas)
{

    //TODO: JONATHANA write a patch wraper  (xnu.c) that doesn't need the intetrmidiate
    //variables to hold the const inst bytes and move all the consts to a common file
    address_space_rw(nsas, vtop_static(SPR_LOCKDOWN_FUNC_VADDR_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
                     sizeof(g_ret_inst), 1);

    address_space_rw(nsas, vtop_static(GXF_ENABLE_FUNC_VADDR_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
                     sizeof(g_ret_inst), 1);

    address_space_rw(nsas,
                   vtop_static(RORGN_STASH_RANGE_2ND_INST_VADDR_18A5342e),
                   MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_retab_inst,
                   sizeof(g_retab_inst), 1);

    address_space_rw(nsas,
                     vtop_static(RORGN_LOCKDOWN_2ND_INST_VADDR_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_retab_inst,
                     sizeof(g_retab_inst), 1);

    //in arm_fast_fault_ppl there seems to be a check related to flushing
    //MMU cache. Make sure we flush all TLBs at this point by execuing
    //TLBI_VMALLS12E1IS instead.
    address_space_rw(nsas,
               vtop_static(FAULT_PPL_LOCKED_DOWN_CHECK_M1_INST_VADDR_18A5342e),
               MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_flush_tlb_inst,
               sizeof(g_flush_tlb_inst), 1);
    //make sure pmap_ppl_locked_down is always 0 when checked
    //in arm_fast_fault_ppl
    address_space_rw(nsas,
                  vtop_static(FAULT_PPL_LOCKED_DOWN_CHECK_INST_VADDR_18A5342e),
                  MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x14_0_inst,
                  sizeof(g_mov_x14_0_inst), 1);

    address_space_rw(nsas, vtop_static(CORE_TRUST_CHECK_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_w0_01_inst,
                     sizeof(g_mov_w0_01_inst), 1);

    //in context switch code change an unknown instruction to flushing
    //the TLB cache
    address_space_rw(nsas, vtop_static(UNKNOWN_INST_18A5342e),
                     MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&g_flush_tlb_inst,
                     sizeof(g_flush_tlb_inst), 1);

    //in flush_mmu_tlb_region_asid_async()an unsupported instruction for
    //flushing TLBs
    address_space_rw(nsas, vtop_static(UNKNOWN_INST2_18A5342e),
                     MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&g_flush_tlb_inst,
                     sizeof(g_flush_tlb_inst), 1);

    address_space_rw(nsas, vtop_static(IS_IMGPF_DISABLE_ASLR_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_j_170_inst,
                     sizeof(g_j_170_inst), 1);

    //disable ASLR for dyld cache
    address_space_rw(nsas, vtop_static(MAP_SLIDE_2_DISABLE_ASLR_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x8_00_inst,
                     sizeof(MOV_X8_00_INST), 1);

    //a few code sign relaed patches. Some might not be necessary
    address_space_rw(nsas, vtop_static(CS_ASSOC_MAP_1_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x0_00_inst,
                     sizeof(g_mov_x0_00_inst), 1);
    address_space_rw(nsas, vtop_static(CS_ASSOC_MAP_2_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
                     sizeof(g_ret_inst), 1);

    address_space_rw(nsas, vtop_static(PMAP_CS_ENF_1_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x0_00_inst,
                     sizeof(g_mov_x0_00_inst), 1);
    address_space_rw(nsas, vtop_static(PMAP_CS_ENF_2_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
                     sizeof(g_ret_inst), 1);

   address_space_rw(nsas, vtop_static(CS_INVALID_PAGE_1_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x0_00_inst,
                     sizeof(g_mov_x0_00_inst), 1);
    address_space_rw(nsas, vtop_static(CS_INVALID_PAGE_2_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
                     sizeof(g_ret_inst), 1);

    address_space_rw(nsas, vtop_static(CS_PAGE_VAL_1_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_w0_01_inst,
                     sizeof(g_mov_w0_01_inst), 1);
    address_space_rw(nsas, vtop_static(CS_PAGE_VAL_2_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
                     sizeof(g_ret_inst), 1);

    //disable sandbox
   address_space_rw(nsas, vtop_static(SB_EVALUAET_INTERNAL_1_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x0_00_inst,
                     sizeof(g_mov_x0_00_inst), 1);
    address_space_rw(nsas, vtop_static(SB_EVALUAET_INTERNAL_2_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
                     sizeof(g_ret_inst), 1);

//memory mapping patches
   address_space_rw(nsas, vtop_static(VM_MAP_PROTECT_1_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);
   address_space_rw(nsas, vtop_static(VM_MAP_PROTECT_2_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);
   address_space_rw(nsas, vtop_static(VM_MAP_PROTECT_3_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);
   address_space_rw(nsas, vtop_static(VM_MAP_PROTECT_4_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);
   address_space_rw(nsas, vtop_static(VM_MAP_PROTECT_5_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);
   address_space_rw(nsas, vtop_static(VM_MAP_PROTECT_6_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_nop_inst,
                     sizeof(g_nop_inst), 1);

    //TODO: JONATHANA
    //address_space_rw(nsas, vtop_static(CHECK_FOR_SIG_1_18A5342e),
    //                 MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x0_00_inst,
    //                 sizeof(g_mov_x0_00_inst), 1);
    //address_space_rw(nsas, vtop_static(CHECK_FOR_SIG_2_18A5342e),
    //                 MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
    //                 sizeof(g_ret_inst), 1);
    //address_space_rw(nsas, vtop_static(MAC_CRED_LAB_EXECVE_1_18A5342e),
    //                 MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_mov_x0_00_inst,
    //                 sizeof(g_mov_x0_00_inst), 1);
    //address_space_rw(nsas, vtop_static(MAC_CRED_LAB_EXECVE_2_18A5342e),
    //                 MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_ret_inst,
    //                 sizeof(g_ret_inst), 1);

    //patch the beginning of bsd_init() to install our hooks
    address_space_rw(nsas, vtop_static(BSD_INIT_SP_SUB_18A5342e),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&g_qemu_call,
                     sizeof(g_qemu_call), 1);
}

static void n104_ns_memory_setup(MachineState *machine, MemoryRegion *sysmem,
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
    video_boot_args v_bootargs = {0};
    N104MachineState *nms = N104_MACHINE(machine);

    allocate_ram(sysmem, "n104.more_extra_data",
                 N104_MORE_ALLOC_DATA_PADDR, N104_MORE_ALLOC_DATA_SIZE);
    used_ram_for_blobs += N104_MORE_ALLOC_DATA_SIZE;

    hwaddr tc_pa = N104_MORE_ALLOC_DATA_PADDR + N104_MORE_ALLOC_DATA_SIZE;
    uint64_t tc_size = 0;
    //it seems that iOS 14 requires a trustcache in the device tree
    //so we create an empty one just for the parsing
    macho_setup_trustcache("trustcache.n104", nsas,
                           sysmem, tc_pa, &tc_size);

    macho_file_highest_lowest_base(nms->kernel_filename, N104_PHYS_BASE,
                                   &virt_base, &kernel_low, &kernel_high);

    g_virt_base = virt_base;
    g_phys_base = N104_PHYS_BASE;
    phys_ptr = N104_PHYS_BASE;

    fprintf(stderr, "n104_ns_memory_setup() vbase: %016llx pbase: %016llx\n",
            g_virt_base, g_phys_base);

    //now account for the loaded kernel
    arm_load_macho(nms->kernel_filename, nsas, sysmem, "kernel.n104",
                    N104_PHYS_BASE, virt_base, kernel_low,
                    kernel_high, &phys_pc);
    nms->kpc_pa = phys_pc;
    used_ram_for_blobs += (align_64k_high(kernel_high) - kernel_low);

    n104_patch_kernel(nsas);

    phys_ptr = align_64k_high(vtop_static(kernel_high));

    //now account for the ramdisk
    nms->ramdisk_file_dev.pa = 0;
    hwaddr ramdisk_size = 0;
    if (0 != nms->ramdisk_filename[0]) {
        nms->ramdisk_file_dev.pa = phys_ptr;
        macho_map_raw_file(nms->ramdisk_filename, nsas, sysmem,
                           "ramdisk_raw_file.n104", nms->ramdisk_file_dev.pa,
                           &nms->ramdisk_file_dev.size);
        ramdisk_size = nms->ramdisk_file_dev.size;
        phys_ptr += align_64k_high(nms->ramdisk_file_dev.size);
    }

    //now account for device tree
    macho_load_dtb(nms->dtb_filename, nsas, sysmem, "dtb.n104", phys_ptr,
                   &dtb_size, nms->ramdisk_file_dev.pa, ramdisk_size,
                   tc_pa, tc_size, &nms->uart_mmio_pa);
    dtb_va = ptov_static(phys_ptr);
    phys_ptr += align_64k_high(dtb_size);
    used_ram_for_blobs += align_64k_high(dtb_size);

    //now account for kernel boot args
    used_ram_for_blobs += align_64k_high(sizeof(struct xnu_arm64_boot_args));
    kbootargs_pa = phys_ptr;
    nms->kbootargs_pa = kbootargs_pa;
    phys_ptr += align_64k_high(sizeof(struct xnu_arm64_boot_args));
    allocated_ram_pa = phys_ptr;
    top_of_kernel_data_pa = phys_ptr;
    remaining_mem_size = machine->ram_size - used_ram_for_blobs;
    mem_size = allocated_ram_pa - N104_PHYS_BASE + remaining_mem_size;
    macho_setup_bootargs("k_bootargs.n104", nsas, sysmem, kbootargs_pa,
                         virt_base, N104_PHYS_BASE, mem_size,
                         top_of_kernel_data_pa, dtb_va, dtb_size,
                         v_bootargs, nms->kern_args);

    allocate_ram(sysmem, "n104.ram", allocated_ram_pa, remaining_mem_size);

    //TODO: JONATHANA hack for now, can't find a place with static mapping
    //to add extra code so overiding driver code that is unused
    //Note: make sure this address is page aligned - the loaded driver
    //code assumes to be in a page aligned address
    nms->extra_data_pa = vtop_static(UNUSED_BCM_DRIVER_SECTION);
    nms->more_extra_data_pa = N104_MORE_ALLOC_DATA_PADDR;
}

static void n104_memory_setup(MachineState *machine,
                             MemoryRegion *sysmem,
                             MemoryRegion *secure_sysmem,
                             AddressSpace *nsas)
{
    n104_ns_memory_setup(machine, sysmem, nsas);
}

static void n104_cpu_setup(MachineState *machine, MemoryRegion **sysmem,
                          MemoryRegion **secure_sysmem, ARMCPU **cpu,
                          AddressSpace **nsas)
{
    Object *cpuobj = object_new(machine->cpu_type);
    *cpu = ARM_CPU(cpuobj);
    CPUState *cs = CPU(*cpu);

    *sysmem = get_system_memory();

    object_property_set_link(cpuobj, "memory", OBJECT(*sysmem),
                             &error_abort);

    //set secure monitor to false
    object_property_set_bool(cpuobj, "has_el3", false, NULL);

    object_property_set_bool(cpuobj, "has_el2", false, NULL);

    object_property_set_bool(cpuobj, "realized", true, &error_fatal);

    *nsas = cpu_get_address_space(cs, ARMASIdx_NS);

    object_unref(cpuobj);
    //currently support only a single CPU and thus
    //use no interrupt controller and wire IRQs from devices directly to the CPU
}

static void n104_bootargs_setup(MachineState *machine)
{
    N104MachineState *nms = N104_MACHINE(machine);
    nms->bootinfo.firmware_loaded = true;
}

static void n104_cpu_reset(void *opaque)
{
    N104MachineState *nms = N104_MACHINE((MachineState *)opaque);
    ARMCPU *cpu = nms->cpu;
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;

    cpu_reset(cs);

    //enable PAN before we begin, checked and required in context switch code
    //in XNU (iOS 14 for iPhone 11)
    env->pstate |= PSTATE_PAN;

    env->xregs[0] = nms->kbootargs_pa;
    env->pc = nms->kpc_pa;
}

static void n104_qemu_call_install_hooks_callback(void)
{

    static uint8_t hooks_installed = false;

    N104MachineState *nms = N104_MACHINE(qdev_get_machine());
    ARMCPU *cpu = nms->cpu;
    CPUARMState *env = &cpu->env;
    CPUState *cs = env_cpu(env);
    AddressSpace *as = cpu_get_address_space(cs, ARMASIdx_NS);
    KernelTrHookParams *hook = &nms->hook;

    //install the hook here because we need the MMU to be already
    //configured and all the memory mapped before installing the hook
    if (!hooks_installed) {
        if (0 != hook->va) {
            xnu_hook_tr_copy_install(hook->va, hook->pa, hook->buf_va,
                                     hook->buf_pa, hook->code, hook->code_size,
                                     hook->buf_size, hook->scratch_reg);
        }

        //TODO: JONATHANA have to move this to after the MoreAllocatedData
        //is actually allocatetd in the firstt hook
        //Must make another qcall for this
        //for (uint64_t i = 0; i < nms->hook_funcs_count; i++) {
        //    xnu_hook_tr_copy_install(nms->hook_funcs[i].va,
        //                             nms->hook_funcs[i].pa,
        //                             nms->hook_funcs[i].buf_va,
        //                             nms->hook_funcs[i].buf_pa,
        //                             nms->hook_funcs[i].code,
        //                             nms->hook_funcs[i].code_size,
        //                             nms->hook_funcs[i].buf_size,
        //                             nms->hook_funcs[i].scratch_reg);
        //}

        //We will later map more static physical addresses in the driver
        //code so we need to change permissions for the static page table
        //to be writable
        va_make_exec(cpu, as, STATIC_PTABLE_VADDR, STATIC_PTABLE_SIZE);

        //The qcall data structure is also in a non-writable section
        //(overwrite some unused driver code) so we need to make it writable
        //as well
        AllocatedData *ad = (AllocatedData *)nms->extra_data_pa;
        va_make_exec(cpu, as, ptov_static((hwaddr)&ad->qemu_call),
                     sizeof(ad->qemu_call));

        hooks_installed = true;
    }

    //emulate original opcode: SP, SP, #0x210
    env->xregs[31] -= 0x210;
}

static void n104_setup_driver_hook(N104MachineState *nms,
                                   AddressSpace *nsas,
                                   ARMCPU *cpu)
{
    AllocatedData *ad = (AllocatedData *)nms->extra_data_pa;

    if (0 != nms->driver_filename[0]) {
        xnu_hook_tr_setup(nsas, cpu);
        uint8_t *code = NULL;
        unsigned long size;
        if (!g_file_get_contents(nms->driver_filename, (char **)&code,
                                 &size, NULL)) {
            abort();
        }
        nms->hook.va = NET_INIT_RUN_VADDR_18A5342e;
        nms->hook.pa = vtop_static(NET_INIT_RUN_VADDR_18A5342e);
        nms->hook.buf_va =
                        ptov_static((hwaddr)&ad->hook_code[0]);
        nms->hook.buf_pa = (hwaddr)&ad->hook_code[0];
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

    qemu_call_install_callback(n104_qemu_call_install_hooks_callback);

    qemu_call_set_cmd_paddr((hwaddr)&ad->qemu_call);
    qemu_call_set_cmd_vaddr(ptov_static((hwaddr)&ad->qemu_call));
}

//hooks arg is expected like this:
//"hookfilepath@va@scratch_reg#hookfilepath@va@scratch_reg#..."

static void n104_machine_init_hook_funcs(N104MachineState *nms,
                                         AddressSpace *nsas)
{
    MoreAllocatedData *md = (MoreAllocatedData *)nms->more_extra_data_pa;
    nms->hook_funcs_count = parse_hooks(&nms->hook_funcs[0],
                                        nms->hook_funcs_cfg, md);
}

static void n104_machine_init(MachineState *machine)
{
    N104MachineState *nms = N104_MACHINE(machine);
    MemoryRegion *sysmem;
    MemoryRegion *secure_sysmem;
    AddressSpace *nsas;
    ARMCPU *cpu;
    DeviceState *cpudev;

    n104_cpu_setup(machine, &sysmem, &secure_sysmem, &cpu, &nsas);

    nms->cpu = cpu;

    n104_memory_setup(machine, sysmem, secure_sysmem, nsas);

    //TODO: JONATHANA make this conditional based on args?
    MoreAllocatedData *md = (MoreAllocatedData *)nms->more_extra_data_pa;
    xnu_iomfb_ramfb_init(&nms->xnu_ramfb_state,
                         (hwaddr)&md->framebuffer);
    qemu_call_install_value_callback(&n104_qemu_call_fb, nms);

    cpudev = DEVICE(cpu);

    n104_setup_driver_hook(nms,nsas, cpu);

    n104_machine_init_hook_funcs(nms, nsas);

    n104_add_cpregs(nms);

    n104_create_s3c_uart(nms, serial_hd(0));

    //wire timer to FIQ as expected by Apple's SoCs
    qdev_connect_gpio_out(cpudev, GTIMER_VIRT,
                          qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));

    n104_bootargs_setup(machine);

    qemu_register_reset(n104_cpu_reset, nms);
}

static void n104_set_ramdisk_filename(Object *obj, const char *value,
                                      Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->ramdisk_filename, value, sizeof(nms->ramdisk_filename));
}

static char *n104_get_ramdisk_filename(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->ramdisk_filename);
}

static void n104_set_kernel_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->kernel_filename, value, sizeof(nms->kernel_filename));
}

static char *n104_get_kernel_filename(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->kernel_filename);
}

static void n104_set_dtb_filename(Object *obj, const char *value,
                                  Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->dtb_filename, value, sizeof(nms->dtb_filename));
}

static char *n104_get_dtb_filename(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->dtb_filename);
}

static void n104_set_kern_args(Object *obj, const char *value,
                               Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->kern_args, value, sizeof(nms->kern_args));
}

static char *n104_get_kern_args(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->kern_args);
}

static void n104_set_tunnel_port(Object *obj, const char *value,
                                 Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    nms->tunnel_port = atoi(value);
}

static char *n104_get_tunnel_port(Object *obj, Error **errp)
{
    char buf[128];
    N104MachineState *nms = N104_MACHINE(obj);
    snprintf(buf, 128, "%d", nms->tunnel_port);
    return g_strdup(buf);
}

static void n104_set_hook_funcs(Object *obj, const char *value, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->hook_funcs_cfg, value, sizeof(nms->hook_funcs_cfg));
}

static char *n104_get_hook_funcs(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->hook_funcs_cfg);
}

static void n104_set_driver_filename(Object *obj, const char *value,
                                     Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->driver_filename, value, sizeof(nms->driver_filename));
}

static char *n104_get_driver_filename(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->driver_filename);
}

static void n104_set_qc_file_0_filename(Object *obj, const char *value,
                                        Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->qc_file_0_filename, value, sizeof(nms->qc_file_0_filename));
}

static char *n104_get_qc_file_0_filename(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->qc_file_0_filename);
}

static void n104_set_qc_file_1_filename(Object *obj, const char *value,
                                        Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);

    g_strlcpy(nms->qc_file_1_filename, value, sizeof(nms->qc_file_1_filename));
}

static char *n104_get_qc_file_1_filename(Object *obj, Error **errp)
{
    N104MachineState *nms = N104_MACHINE(obj);
    return g_strdup(nms->qc_file_1_filename);
}

static void n104_instance_init(Object *obj)
{
    object_property_add_str(obj, "ramdisk-filename", n104_get_ramdisk_filename,
                            n104_set_ramdisk_filename);
    object_property_set_description(obj, "ramdisk-filename",
                                    "Set the ramdisk filename to be loaded");

    object_property_add_str(obj, "kernel-filename", n104_get_kernel_filename,
                            n104_set_kernel_filename);
    object_property_set_description(obj, "kernel-filename",
                                    "Set the kernel filename to be loaded");

    object_property_add_str(obj, "dtb-filename", n104_get_dtb_filename,
                            n104_set_dtb_filename);
    object_property_set_description(obj, "dtb-filename",
                                    "Set the dev tree filename to be loaded");

    object_property_add_str(obj, "kern-cmd-args", n104_get_kern_args,
                            n104_set_kern_args);
    object_property_set_description(obj, "kern-cmd-args",
                                    "Set the XNU kernel cmd args");

    object_property_add_str(obj, "tunnel-port", n104_get_tunnel_port,
                            n104_set_tunnel_port);
    object_property_set_description(obj, "tunnel-port",
                                    "Set the port for the tunnel connection");

    object_property_add_str(obj, "hook-funcs", n104_get_hook_funcs,
                            n104_set_hook_funcs);
    object_property_set_description(obj, "hook-funcs",
                                    "Set the hook funcs to be loaded");

    object_property_add_str(obj, "driver-filename", n104_get_driver_filename,
                            n104_set_driver_filename);
    object_property_set_description(obj, "driver-filename",
                                    "Set the driver filename to be loaded");

    object_property_add_str(obj, "qc-file-0-filename",
                            n104_get_qc_file_0_filename,
                            n104_set_qc_file_0_filename);
    object_property_set_description(obj, "qc-file-0-filename",
                                    "Set the qc file 0 filename to be loaded");

    object_property_add_str(obj, "qc-file-1-filename",
                            n104_get_qc_file_1_filename,
                            n104_set_qc_file_1_filename);
    object_property_set_description(obj, "qc-file-1-filename",
                                    "Set the qc file 1 filename to be loaded");

}

static void n104_machine_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    mc->desc = "iPhone 11 (n104)";
    mc->init = n104_machine_init;
    mc->max_cpus = 1;
    //this disables the error message "Failed to query for block devices!"
    //when starting qemu - must keep at least one device
    //mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("max");
    mc->minimum_page_bits = 12;
}

static const TypeInfo n104_machine_info = {
    .name          = TYPE_N104_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(N104MachineState),
    .class_size    = sizeof(N104MachineClass),
    .class_init    = n104_machine_class_init,
    .instance_init = n104_instance_init,
};

static void n104_machine_types(void)
{
    type_register_static(&n104_machine_info);
}

type_init(n104_machine_types)
