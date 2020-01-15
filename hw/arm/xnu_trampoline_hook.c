/*
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
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "hw/arm/xnu_trampoline_hook.h"
#include "hw/arm/xnu_mem.h"
#include "hw/arm/xnu_pagetable.h"
#include "hw/loader.h"
#include "qemu/osdep.h"
#include "target/arm/idau.h"
#include "trace.h"
#include "cpu.h"
#include "internals.h"
#include "exec/gdbstub.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "sysemu/arch_init.h"
#include "sysemu/sysemu.h"
#include "qemu/bitops.h"
#include "qemu/crc32c.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "arm_ldst.h"
#include "hw/semihosting/semihost.h"
#include "sysemu/hw_accel.h"

#define TRAMPOLINE_CODE_INSTS (1024)
#define TRAMPOLINE_CODE_SIZE (TRAMPOLINE_CODE_INSTS * 4)

#define PAGE_4K_BITS (12)
#define PAGE_4K_MASK (((uint64_t)1 << PAGE_4K_BITS) - 1)
#define PAGE_4K_ALIGN_MASK (~(PAGE_4K_MASK))

#define ADRP_IMMLO_SIZE (2)
#define ADRP_IMMLO_MASK (((uint32_t)1 << ADRP_IMMLO_SIZE) - 1)
#define ADRP_IMMLO_SHIFT (29)

#define ADRP_IMMHI_SIZE (19)
#define ADRP_IMMHI_MASK (((uint32_t)1 << ADRP_IMMHI_SIZE) - 1)
#define ADRP_IMMHI_SHIFT (5)

#define ADRP_RD_SIZE (5)
#define ADRP_RD_MASK (((uint32_t)1 << ADRP_RD_SIZE) - 1)
#define ADRP_RD_SHIFT (0)

#define ADD_RN_SIZE (5)
#define ADD_RN_MASK (((uint32_t)1 << ADD_RN_SIZE) - 1)
#define ADD_RN_SHIFT (5)

#define ADD_RD_SIZE (5)
#define ADD_RD_MASK (((uint32_t)1 << ADD_RD_SIZE) - 1)
#define ADD_RD_SHIFT (0)

#define ADD_IMM_SIZE (12)
#define ADD_IMM_MASK (((uint32_t)1 << ADD_IMM_SIZE) - 1)
#define ADD_IMM_SHIFT (10)

#define SUB_RN_SIZE (5)
#define SUB_RN_MASK (((uint32_t)1 << SUB_RN_SIZE) - 1)
#define SUB_RN_SHIFT (5)

#define SUB_RD_SIZE (5)
#define SUB_RD_MASK (((uint32_t)1 << SUB_RD_SIZE) - 1)
#define SUB_RD_SHIFT (0)

#define SUB_IMM_SIZE (12)
#define SUB_IMM_MASK (((uint32_t)1 << SUB_IMM_SIZE) - 1)
#define SUB_IMM_SHIFT (10)

#define STR_64_FP_RN_SIZE (5)
#define STR_64_FP_RN_MASK (((uint32_t)1 << STR_64_FP_RN_SIZE) - 1)
#define STR_64_FP_RN_SHIFT (5)

#define STR_64_FP_RT_SIZE (5)
#define STR_64_FP_RT_MASK (((uint32_t)1 << STR_64_FP_RT_SIZE) - 1)
#define STR_64_FP_RT_SHIFT (0)

#define STR_64_FP_IMM_SIZE (12)
#define STR_64_FP_IMM_MASK (((uint32_t)1 << STR_64_FP_IMM_SIZE) - 1)
#define STR_64_FP_IMM_SHIFT (10)

#define LDR_64_FP_RN_SIZE (5)
#define LDR_64_FP_RN_MASK (((uint32_t)1 << LDR_64_FP_RN_SIZE) - 1)
#define LDR_64_FP_RN_SHIFT (5)

#define LDR_64_FP_RT_SIZE (5)
#define LDR_64_FP_RT_MASK (((uint32_t)1 << LDR_64_FP_RT_SIZE) - 1)
#define LDR_64_FP_RT_SHIFT (0)

#define LDR_64_FP_IMM_SIZE (12)
#define LDR_64_FP_IMM_MASK (((uint32_t)1 << LDR_64_FP_IMM_SIZE) - 1)
#define LDR_64_FP_IMM_SHIFT (10)

#define STP_XT1_SIZE (5)
#define STP_XT1_MASK (((uint32_t)1 << STP_XT1_SIZE) - 1)
#define STP_XT1_SHIFT (0)

#define STP_XT2_SIZE (5)
#define STP_XT2_MASK (((uint32_t)1 << STP_XT2_SIZE) - 1)
#define STP_XT2_SHIFT (10)

#define STP_RN_SIZE (5)
#define STP_RN_MASK (((uint32_t)1 << ADD_RN_SIZE) - 1)
#define STP_RN_SHIFT (5)

#define STP_IMM_SIZE (7)
#define STP_IMM_MASK (((uint32_t)1 << STP_IMM_SIZE) - 1)
#define STP_IMM_SHIFT (15)

#define LDP_XT1_SIZE (5)
#define LDP_XT1_MASK (((uint32_t)1 << LDP_XT1_SIZE) - 1)
#define LDP_XT1_SHIFT (0)

#define LDP_XT2_SIZE (5)
#define LDP_XT2_MASK (((uint32_t)1 << LDP_XT2_SIZE) - 1)
#define LDP_XT2_SHIFT (10)

#define LDP_RN_SIZE (5)
#define LDP_RN_MASK (((uint32_t)1 << LDP_RN_SIZE) - 1)
#define LDP_RN_SHIFT (5)

#define LDP_IMM_SIZE (7)
#define LDP_IMM_MASK (((uint32_t)1 << LDP_IMM_SIZE) - 1)
#define LDP_IMM_SHIFT (15)

#define BR_RN_SIZE (5)
#define BR_RN_MASK (((uint32_t)1 << BR_RN_SIZE) - 1)
#define BR_RN_SHIFT (5)

#define BLR_RN_SIZE (5)
#define BLR_RN_MASK (((uint32_t)1 << BLR_RN_SIZE) - 1)
#define BLR_RN_SHIFT (5)

static AddressSpace *xnu_hook_tr_as = NULL;
static ARMCPU *xnu_hook_tr_cpu = NULL;

static uint32_t get_adrp_inst(hwaddr source, hwaddr target, uint8_t reg_id)
{
    //general adrp inst
    uint32_t adrp_inst = 0x90000000;

    adrp_inst |= (reg_id & ADRP_RD_MASK) << ADRP_RD_SHIFT;

    hwaddr source_page = source & PAGE_4K_ALIGN_MASK;
    hwaddr target_page = target & PAGE_4K_ALIGN_MASK;

    hwaddr adrp_inst_diff_imm = ((target_page - source_page) >> PAGE_4K_BITS);

    adrp_inst |= (adrp_inst_diff_imm & ADRP_IMMLO_MASK) << ADRP_IMMLO_SHIFT;
    adrp_inst |= ((adrp_inst_diff_imm >> ADRP_IMMLO_SIZE) &
                  ADRP_IMMHI_MASK) << ADRP_IMMHI_SHIFT;

    return adrp_inst;
}

static uint32_t get_add_inst(uint8_t xd, uint8_t xn, hwaddr imm)
{
    //general add inst
    uint32_t add_inst = 0x91000000;

    add_inst |= (xn & ADD_RN_MASK) << ADD_RN_SHIFT;
    add_inst |= (xd & ADD_RD_MASK) << ADD_RD_SHIFT;
    add_inst |= (imm & ADD_IMM_MASK) << ADD_IMM_SHIFT;

    return add_inst;
}

static uint32_t get_sub_inst(uint8_t xd, uint8_t xn, hwaddr imm)
{
    //general sub inst
    uint32_t sub_inst = 0xD1000000;

    sub_inst |= (xn & SUB_RN_MASK) << SUB_RN_SHIFT;
    sub_inst |= (xd & SUB_RD_MASK) << SUB_RD_SHIFT;
    sub_inst |= (imm & SUB_IMM_MASK) << SUB_IMM_SHIFT;

    return sub_inst;
}

static uint32_t get_str_64_fp_inst(uint8_t dt, uint8_t xn, hwaddr imm)
{
    //general str inst
    uint32_t str_inst = 0xFD000000;

    //has to be devided by 8
    if (imm % 8 != 0) {
        abort();
    }

    imm = imm / 8;

    str_inst |= (xn & STR_64_FP_RN_MASK) << STR_64_FP_RN_SHIFT;
    str_inst |= (dt & STR_64_FP_RT_MASK) << STR_64_FP_RT_SHIFT;
    str_inst |= (imm & STR_64_FP_IMM_MASK) << STR_64_FP_IMM_SHIFT;

    return str_inst;
}

static uint32_t get_ldr_64_fp_inst(uint8_t dt, uint8_t xn, hwaddr imm)
{
    //general ldr inst
    uint32_t ldr_inst = 0xFD400000;

    //has to be devided by 8
    if (imm % 8 != 0) {
        abort();
    }

    imm = imm / 8;

    ldr_inst |= (xn & LDR_64_FP_RN_MASK) << LDR_64_FP_RN_SHIFT;
    ldr_inst |= (dt & LDR_64_FP_RT_MASK) << LDR_64_FP_RT_SHIFT;
    ldr_inst |= (imm & LDR_64_FP_IMM_MASK) << LDR_64_FP_IMM_SHIFT;

    return ldr_inst;
}

static uint32_t get_stp_inst(uint8_t xt1, uint8_t xt2, uint8_t rn, hwaddr imm)
{
    //general stp inst
    uint32_t stp_inst = 0xA9000000;

    //has to be devided by 8
    if (imm % 8 != 0) {
        abort();
    }

    imm = imm / 8;

    if ((imm & STP_IMM_MASK) != imm) {
        abort();
    }

    stp_inst |= (xt1 & STP_XT1_MASK) << STP_XT1_SHIFT;
    stp_inst |= (xt2 & STP_XT2_MASK) << STP_XT2_SHIFT;
    stp_inst |= (rn & STP_RN_MASK) << STP_RN_SHIFT;
    stp_inst |= (imm & STP_IMM_MASK) << STP_IMM_SHIFT;

    return stp_inst;
}

static uint32_t get_ldp_inst(uint8_t xt1, uint8_t xt2, uint8_t rn, hwaddr imm)
{
    //general ldp inst
    uint32_t ldp_inst = 0xA9400000;

    //has to be devided by 8
    if (imm % 8 != 0) {
        abort();
    }

    imm = imm / 8;

    if ((imm & LDP_IMM_MASK) != imm) {
        abort();
    }

    ldp_inst |= (xt1 & LDP_XT1_MASK) << LDP_XT1_SHIFT;
    ldp_inst |= (xt2 & LDP_XT2_MASK) << LDP_XT2_SHIFT;
    ldp_inst |= (rn & LDP_RN_MASK) << LDP_RN_SHIFT;
    ldp_inst |= (imm & LDP_IMM_MASK) << LDP_IMM_SHIFT;

    return ldp_inst;
}

static uint32_t get_br_inst(uint8_t reg_id)
{
    //general br inst
    uint32_t br_inst = 0xD61F0000;

    br_inst |= (reg_id & BR_RN_MASK) << BR_RN_SHIFT;

    return br_inst;
}

static uint32_t get_blr_inst(uint8_t reg_id)
{
    //general blr inst
    uint32_t blr_inst = 0xD63F0000;

    blr_inst |= (reg_id & BLR_RN_MASK) << BLR_RN_SHIFT;

    return blr_inst;
}

void xnu_hook_tr_install(hwaddr va, hwaddr pa, hwaddr cb_va, hwaddr tr_buf_va,
                         hwaddr tr_buf_pa, uint8_t scratch_reg)
{
    //must run setup before installing hook
    if ((NULL == xnu_hook_tr_as) || (NULL == xnu_hook_tr_cpu)) {
        abort();
    }

    if ((0 == va) || (0 == pa) || (0 == cb_va) || (0 == tr_buf_va) ||
        (0 == tr_buf_pa)) {
        abort();
    }

    uint32_t backup_insts[3] = {0};
    uint32_t new_insts[3] = {0};
    uint32_t tr_insts[TRAMPOLINE_CODE_INSTS] = {0};
    uint64_t i = 0;

    address_space_rw(xnu_hook_tr_as, pa, MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&backup_insts[0], sizeof(backup_insts), 0);

    new_insts[0] = get_adrp_inst(va, tr_buf_va, scratch_reg);
    new_insts[1] = get_add_inst(scratch_reg, scratch_reg,
                                tr_buf_va & PAGE_4K_MASK);
    new_insts[2] = get_br_inst(scratch_reg);

    address_space_rw(xnu_hook_tr_as, pa, MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&new_insts[0], sizeof(new_insts), 1);

    //31 is treated as sp in aarch64
    tr_insts[i++] = get_add_inst(scratch_reg, 31, 0);
    tr_insts[i++] = get_sub_inst(scratch_reg ,scratch_reg, 0x200);

    tr_insts[i++] = get_stp_inst(0, 1, scratch_reg, 0);
    tr_insts[i++] = get_stp_inst(2, 3, scratch_reg, 0x10);
    tr_insts[i++] = get_stp_inst(4, 5, scratch_reg, 0x20);
    tr_insts[i++] = get_stp_inst(6, 7, scratch_reg, 0x30);
    tr_insts[i++] = get_stp_inst(8, 9, scratch_reg, 0x40);
    tr_insts[i++] = get_stp_inst(10, 11, scratch_reg, 0x50);
    tr_insts[i++] = get_stp_inst(12, 13, scratch_reg, 0x60);
    tr_insts[i++] = get_stp_inst(14, 15, scratch_reg, 0x70);
    tr_insts[i++] = get_stp_inst(16, 17, scratch_reg, 0x80);
    tr_insts[i++] = get_stp_inst(18, 19, scratch_reg, 0x90);
    tr_insts[i++] = get_stp_inst(20, 21, scratch_reg, 0xa0);
    tr_insts[i++] = get_stp_inst(22, 23, scratch_reg, 0x30);
    tr_insts[i++] = get_stp_inst(24, 25, scratch_reg, 0xb0);
    tr_insts[i++] = get_stp_inst(26, 27, scratch_reg, 0xc0);
    tr_insts[i++] = get_stp_inst(28, 29, scratch_reg, 0xd0);
    tr_insts[i++] = get_stp_inst(0, 30, scratch_reg, 0xe0);

    tr_insts[i++] = get_str_64_fp_inst(0, scratch_reg, 0xf0);
    tr_insts[i++] = get_str_64_fp_inst(1, scratch_reg, 0xf8);
    tr_insts[i++] = get_str_64_fp_inst(2, scratch_reg, 0x100);
    tr_insts[i++] = get_str_64_fp_inst(3, scratch_reg, 0x108);
    tr_insts[i++] = get_str_64_fp_inst(4, scratch_reg, 0x110);
    tr_insts[i++] = get_str_64_fp_inst(5, scratch_reg, 0x118);
    tr_insts[i++] = get_str_64_fp_inst(6, scratch_reg, 0x120);
    tr_insts[i++] = get_str_64_fp_inst(7, scratch_reg, 0x128);
    tr_insts[i++] = get_str_64_fp_inst(8, scratch_reg, 0x130);
    tr_insts[i++] = get_str_64_fp_inst(9, scratch_reg, 0x138);
    tr_insts[i++] = get_str_64_fp_inst(10, scratch_reg, 0x140);
    tr_insts[i++] = get_str_64_fp_inst(11, scratch_reg, 0x148);
    tr_insts[i++] = get_str_64_fp_inst(12, scratch_reg, 0x150);
    tr_insts[i++] = get_str_64_fp_inst(13, scratch_reg, 0x158);
    tr_insts[i++] = get_str_64_fp_inst(14, scratch_reg, 0x160);
    tr_insts[i++] = get_str_64_fp_inst(15, scratch_reg, 0x168);
    tr_insts[i++] = get_str_64_fp_inst(16, scratch_reg, 0x170);
    tr_insts[i++] = get_str_64_fp_inst(17, scratch_reg, 0x178);
    tr_insts[i++] = get_str_64_fp_inst(18, scratch_reg, 0x180);
    tr_insts[i++] = get_str_64_fp_inst(19, scratch_reg, 0x188);
    tr_insts[i++] = get_str_64_fp_inst(20, scratch_reg, 0x190);
    tr_insts[i++] = get_str_64_fp_inst(21, scratch_reg, 0x198);
    tr_insts[i++] = get_str_64_fp_inst(22, scratch_reg, 0x1a0);
    tr_insts[i++] = get_str_64_fp_inst(23, scratch_reg, 0x1a8);
    tr_insts[i++] = get_str_64_fp_inst(24, scratch_reg, 0x1b0);
    tr_insts[i++] = get_str_64_fp_inst(25, scratch_reg, 0x1b8);
    tr_insts[i++] = get_str_64_fp_inst(26, scratch_reg, 0x1c0);
    tr_insts[i++] = get_str_64_fp_inst(27, scratch_reg, 0x1c8);
    tr_insts[i++] = get_str_64_fp_inst(28, scratch_reg, 0x1d0);
    tr_insts[i++] = get_str_64_fp_inst(29, scratch_reg, 0x1d8);
    tr_insts[i++] = get_str_64_fp_inst(30, scratch_reg, 0x1e0);
    tr_insts[i++] = get_str_64_fp_inst(31, scratch_reg, 0x1e8);

    tr_insts[i++] = get_sub_inst(31 ,31, 0x200);

    tr_insts[i] = get_adrp_inst(tr_buf_va  + (i * 4), cb_va, scratch_reg);
    i++;
    tr_insts[i++] = get_add_inst(scratch_reg, scratch_reg,
                                 cb_va & PAGE_4K_MASK);
    tr_insts[i++] = get_blr_inst(scratch_reg);

    tr_insts[i++] = get_add_inst(scratch_reg, 31, 0);

    tr_insts[i++] = get_ldp_inst(0, 1, scratch_reg, 0);
    tr_insts[i++] = get_ldp_inst(2, 3, scratch_reg, 0x10);
    tr_insts[i++] = get_ldp_inst(4, 5, scratch_reg, 0x20);
    tr_insts[i++] = get_ldp_inst(6, 7, scratch_reg, 0x30);
    tr_insts[i++] = get_ldp_inst(8, 9, scratch_reg, 0x40);
    tr_insts[i++] = get_ldp_inst(10, 11, scratch_reg, 0x50);
    tr_insts[i++] = get_ldp_inst(12, 13, scratch_reg, 0x60);
    tr_insts[i++] = get_ldp_inst(14, 15, scratch_reg, 0x70);
    tr_insts[i++] = get_ldp_inst(16, 17, scratch_reg, 0x80);
    tr_insts[i++] = get_ldp_inst(18, 19, scratch_reg, 0x90);
    tr_insts[i++] = get_ldp_inst(20, 21, scratch_reg, 0xa0);
    tr_insts[i++] = get_ldp_inst(22, 23, scratch_reg, 0x30);
    tr_insts[i++] = get_ldp_inst(24, 25, scratch_reg, 0xb0);
    tr_insts[i++] = get_ldp_inst(26, 27, scratch_reg, 0xc0);
    tr_insts[i++] = get_ldp_inst(28, 29, scratch_reg, 0xd0);
    tr_insts[i++] = get_ldp_inst(0, 30, scratch_reg, 0xe0);

    tr_insts[i++] = get_ldr_64_fp_inst(0, scratch_reg, 0xf0);
    tr_insts[i++] = get_ldr_64_fp_inst(1, scratch_reg, 0xf8);
    tr_insts[i++] = get_ldr_64_fp_inst(2, scratch_reg, 0x100);
    tr_insts[i++] = get_ldr_64_fp_inst(3, scratch_reg, 0x108);
    tr_insts[i++] = get_ldr_64_fp_inst(4, scratch_reg, 0x110);
    tr_insts[i++] = get_ldr_64_fp_inst(5, scratch_reg, 0x118);
    tr_insts[i++] = get_ldr_64_fp_inst(6, scratch_reg, 0x120);
    tr_insts[i++] = get_ldr_64_fp_inst(7, scratch_reg, 0x128);
    tr_insts[i++] = get_ldr_64_fp_inst(8, scratch_reg, 0x130);
    tr_insts[i++] = get_ldr_64_fp_inst(9, scratch_reg, 0x138);
    tr_insts[i++] = get_ldr_64_fp_inst(10, scratch_reg, 0x140);
    tr_insts[i++] = get_ldr_64_fp_inst(11, scratch_reg, 0x148);
    tr_insts[i++] = get_ldr_64_fp_inst(12, scratch_reg, 0x150);
    tr_insts[i++] = get_ldr_64_fp_inst(13, scratch_reg, 0x158);
    tr_insts[i++] = get_ldr_64_fp_inst(14, scratch_reg, 0x160);
    tr_insts[i++] = get_ldr_64_fp_inst(15, scratch_reg, 0x168);
    tr_insts[i++] = get_ldr_64_fp_inst(16, scratch_reg, 0x170);
    tr_insts[i++] = get_ldr_64_fp_inst(17, scratch_reg, 0x178);
    tr_insts[i++] = get_ldr_64_fp_inst(18, scratch_reg, 0x180);
    tr_insts[i++] = get_ldr_64_fp_inst(19, scratch_reg, 0x188);
    tr_insts[i++] = get_ldr_64_fp_inst(20, scratch_reg, 0x190);
    tr_insts[i++] = get_ldr_64_fp_inst(21, scratch_reg, 0x198);
    tr_insts[i++] = get_ldr_64_fp_inst(22, scratch_reg, 0x1a0);
    tr_insts[i++] = get_ldr_64_fp_inst(23, scratch_reg, 0x1a8);
    tr_insts[i++] = get_ldr_64_fp_inst(24, scratch_reg, 0x1b0);
    tr_insts[i++] = get_ldr_64_fp_inst(25, scratch_reg, 0x1b8);
    tr_insts[i++] = get_ldr_64_fp_inst(26, scratch_reg, 0x1c0);
    tr_insts[i++] = get_ldr_64_fp_inst(27, scratch_reg, 0x1c8);
    tr_insts[i++] = get_ldr_64_fp_inst(28, scratch_reg, 0x1d0);
    tr_insts[i++] = get_ldr_64_fp_inst(29, scratch_reg, 0x1d8);
    tr_insts[i++] = get_ldr_64_fp_inst(30, scratch_reg, 0x1e0);
    tr_insts[i++] = get_ldr_64_fp_inst(31, scratch_reg, 0x1e8);

    tr_insts[i++] = get_add_inst(31 ,31, 0x200);

    tr_insts[i] = get_adrp_inst(tr_buf_va + (i * 4),
                                va + sizeof(backup_insts), scratch_reg);
    i++;
    tr_insts[i++] = get_add_inst(scratch_reg, scratch_reg,
                                 (va + sizeof(backup_insts)) & PAGE_4K_MASK);
    tr_insts[i++] = backup_insts[0];
    tr_insts[i++] = backup_insts[1];
    tr_insts[i++] = backup_insts[2];
    tr_insts[i++] = get_br_inst(scratch_reg);

    if (i >= TRAMPOLINE_CODE_INSTS) {
        abort();
    }

    address_space_rw(xnu_hook_tr_as, tr_buf_pa, MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&tr_insts[0], sizeof(tr_insts), 1);

    va_make_exec(xnu_hook_tr_cpu, xnu_hook_tr_as, tr_buf_va,
                 TRAMPOLINE_CODE_SIZE);
}

void xnu_hook_tr_copy_install(hwaddr va, hwaddr pa, hwaddr buf_va,
                              hwaddr buf_pa, uint8_t *code, uint64_t code_size,
                              uint64_t buf_size, uint8_t scratch_reg)
{
    //must run setup before installing hook
    if ((NULL == xnu_hook_tr_as) || (NULL == xnu_hook_tr_cpu)) {
        abort();
    }

    if ((0 == va) || (0 == pa) || (0 == buf_va) || (0 == buf_pa) ||
        (NULL == code) || (0 == code_size) || (0 == buf_size)) {
        abort();
    }

    if ((code_size + TRAMPOLINE_CODE_SIZE) >= buf_size) {
        abort();
    }

    address_space_rw(xnu_hook_tr_as, (buf_pa + TRAMPOLINE_CODE_SIZE),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)code, code_size, 1);
    va_make_exec(xnu_hook_tr_cpu, xnu_hook_tr_as, buf_va, buf_size);
    xnu_hook_tr_install(va, pa, buf_va + TRAMPOLINE_CODE_SIZE, buf_va, buf_pa,
                        scratch_reg);
}

void xnu_hook_tr_setup(AddressSpace *as, ARMCPU *cpu)
{
    //allow setup only once
    if ((NULL != xnu_hook_tr_as) || (NULL != xnu_hook_tr_cpu)) {
        abort();
    }

    //must setup with a non NULL AddressSpace
    if (NULL == as) {
        abort();
    }

    //must setup with a non NULL CPU
    if (NULL == cpu) {
        abort();
    }

    xnu_hook_tr_cpu = cpu;
    xnu_hook_tr_as = as;
}
