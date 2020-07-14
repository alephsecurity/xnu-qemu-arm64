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

#define PHYS_ADDR_SIZE (40)
#define TG_16K_SIZE (14)
#define PAGE_MASK_16K (~(((uint64_t)1 << TG_16K_SIZE) - 1))
#define TE_PHYS_ADDR_MASK ((((uint64_t)1 << PHYS_ADDR_SIZE) - 1) & \
                           PAGE_MASK_16K)

#define TG_16KB (1)
#define TG_16KB_LEVEL0_INDEX (47)
#define TG_16KB_LEVEL0_SIZE (1)
#define TG_16KB_LEVEL1_INDEX (36)
#define TG_16KB_LEVEL1_SIZE (11)
#define TG_16KB_LEVEL2_INDEX (25)
#define TG_16KB_LEVEL2_SIZE (11)
#define TG_16KB_LEVEL3_INDEX (14)
#define TG_16KB_LEVEL3_SIZE (11)

#define TCR_IPS_INDEX (32)
#define TCR_IPS_SIZE (3)
#define TCR_IPS_40_ADDR_SIZE (2)
#define TCR_TG1_INDEX (30)
#define TCR_TG1_SIZE (2)
#define TCR_T1SZ_INDEX (16)
#define TCR_T1SZ_SIZE (6)
#define TCR_TG0_INDEX (14)
#define TCR_TG0_SIZE (2)
#define TCR_T0SZ_INDEX (0)
#define TCR_T0SZ_SIZE (6)

#define TE_ACCESS_PERMS_INDEX (6)
#define TE_ACCESS_PERMS_SIZE (2)
#define TE_ACCESS_PERMS_MASK ((((uint64_t)1 << TE_ACCESS_PERMS_SIZE) - 1) << \
                              TE_ACCESS_PERMS_INDEX)
#define TE_ACCESS_PERMS_ZERO_MASK (~TE_ACCESS_PERMS_MASK)
#define TE_ACCESS_PERMS_KERN_RW (0)
#define TE_XN_INDEX (53)
#define TE_XN_SIZE (2)
#define TE_XN_MASK ((((uint64_t)1 << TE_XN_SIZE) - 1) << TE_XN_INDEX)
#define TE_XN_ZERO_MASK (~TE_XN_MASK)
#define TE_XN_KERN_EXE ((uint64_t)2 << TE_XN_INDEX)
#define TE_TYPE_INDEX (0)
#define TE_TYPE_SIZE (2)
#define TE_TYPE_TABLE_DESC (3)
#define TE_TYPE_L3_BLOCK (3)

hwaddr pt_tte_el1(ARMCPU *cpu, AddressSpace *as, hwaddr va, bool make_exe)
{
    CPUARMState *env = &cpu->env;
    uint64_t tcr = env->cp15.tcr_el[1].raw_tcr;
    uint64_t tcr_ips = extract64(tcr, TCR_IPS_INDEX, TCR_IPS_SIZE);
    uint64_t tcr_tg1 = extract64(tcr, TCR_TG1_INDEX, TCR_TG1_SIZE);
    uint64_t tcr_t1sz = extract64(tcr, TCR_T1SZ_INDEX, TCR_T1SZ_SIZE);
    uint64_t tcr_tg0 = extract64(tcr, TCR_TG0_INDEX, TCR_TG0_SIZE);
    uint64_t tcr_t0sz = extract64(tcr, TCR_T0SZ_INDEX, TCR_T0SZ_SIZE);

    hwaddr tt = 0;
    hwaddr te = 0;
    uint64_t tg = 0;
    uint64_t tsz = 0;

    //currently only support 40bit addresses configuration
    if (TCR_IPS_40_ADDR_SIZE != tcr_ips) {
        abort();
    }
    //fprintf(stderr, "pt_tte_el1: tcr: 0x%016llx tcr_ips: 0x%016llx tcr_tg1: 0x%016llx tcr_t1sz: 0x%016llx tcr_tg0: %016llx tcr_t0sz: 0x%016llx va: 0x%016llx\n",
    //        tcr, tcr_ips, tcr_tg1, tcr_t1sz, tcr_tg0, tcr_t0sz, va);

    if (extract64(va, 63, 1) == 1) {
        uint64_t one_bits = extract64(va, 64 - tcr_t1sz, tcr_t1sz);
        uint64_t one_bits_verify = (1 << tcr_t1sz) - 1;
        //fprintf(stderr, "90 pt_tte_el1: te: 0x%016llx\n", te);
        if ((one_bits & one_bits_verify) != one_bits_verify) {
            fprintf(stderr, "9 pt_tte_el1: te: 0x%016llx\n", te);
            abort();
        }
        tt = env->cp15.ttbr1_el[1];
        tg = tcr_tg1;
        tsz = tcr_t1sz;
    } else {
        uint64_t zero_bits = extract64(va, 64 - tcr_t0sz, tcr_t0sz);
        //fprintf(stderr, "91 pt_tte_el1: te: 0x%016lx\n", te);
        if (0 != zero_bits) {
            fprintf(stderr, "10 pt_tte_el1: te: 0x%016llx\n", te);
            abort();
        }
        tt = env->cp15.ttbr0_el[1];
        tg = tcr_tg0;
        tsz = tcr_t0sz;
    }

    //currently only support parsing 16kg granule page tables
    if (TG_16KB != tg) {
        fprintf(stderr, "8 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    //currently only support level 1 base entries
    if ((tsz < (64 - TG_16KB_LEVEL0_INDEX)) ||
        (tsz >= (64 - TG_16KB_LEVEL1_INDEX))) {
        fprintf(stderr, "7 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    uint64_t l1_index_size = 64 - tsz - TG_16KB_LEVEL1_INDEX;
    uint64_t l1_idx = extract64(va, TG_16KB_LEVEL1_INDEX, l1_index_size);
    uint64_t l2_idx = extract64(va, TG_16KB_LEVEL2_INDEX, TG_16KB_LEVEL2_SIZE);
    uint64_t l3_idx = extract64(va, TG_16KB_LEVEL3_INDEX, TG_16KB_LEVEL3_SIZE);

    address_space_rw(as, (tt + (sizeof(hwaddr) * l1_idx)),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&te, sizeof(te), 0);
    if (0 == te) {
        fprintf(stderr, "6 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    uint64_t te_type = extract64(te, TE_TYPE_INDEX, TE_TYPE_SIZE);
    //currently only support table description level1 entries
    if (TE_TYPE_TABLE_DESC != te_type) {
        fprintf(stderr, "5 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    //layer 2
    tt = te & TE_PHYS_ADDR_MASK;
    address_space_rw(as, (tt + (sizeof(hwaddr) * l2_idx)),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&te, sizeof(te), 0);
    if (0 == te) {
        fprintf(stderr, "4 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    te_type = extract64(te, TE_TYPE_INDEX, TE_TYPE_SIZE);
    //currently only support table description level2 entries
    if (TE_TYPE_TABLE_DESC != te_type) {
        fprintf(stderr, "3 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    //layer 3
    tt = te & TE_PHYS_ADDR_MASK;
    hwaddr l3_te_addr = tt + (sizeof(hwaddr) * l3_idx);
    address_space_rw(as, l3_te_addr, MEMTXATTRS_UNSPECIFIED, (uint8_t *)&te,
                     sizeof(te), 0);
    if (0 == te) {
        fprintf(stderr, "2 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    te_type = extract64(te, TE_TYPE_INDEX, TE_TYPE_SIZE);
    //sanity - l3 entries can only be block entries or invalid entries
    if (TE_TYPE_L3_BLOCK != te_type) {
        fprintf(stderr, "1 pt_tte_el1: te: 0x%016llx\n", te);
        abort();
    }

    //fprintf(stderr, "pt_tte_el1: te: 0x%016llx\n", te);

    if (make_exe) {
        //fprintf(stderr, "pt_tte_el1: TE_ACCESS_PERMS_ZERO_MASK: 0x%016llx\n", TE_ACCESS_PERMS_ZERO_MASK);
        //fprintf(stderr, "pt_tte_el1: TE_XN_ZERO_MASK: 0x%016llx\n", TE_XN_ZERO_MASK);
        //fprintf(stderr, "pt_tte_el1: TE_ACCESS_PERMS_KERN_RW: 0x%016llx\n", TE_ACCESS_PERMS_KERN_RW);
        //fprintf(stderr, "pt_tte_el1: TE_XN_KERN_EXE: 0x%016llx\n", TE_XN_KERN_EXE);
        te &= TE_ACCESS_PERMS_ZERO_MASK & TE_XN_ZERO_MASK;
        te |= TE_ACCESS_PERMS_KERN_RW | TE_XN_KERN_EXE;
        address_space_rw(as, l3_te_addr, MEMTXATTRS_UNSPECIFIED,
                         (uint8_t *)&te, sizeof(te), 1);
        tlb_flush(CPU(cpu));
    }

    //fprintf(stderr, "pt_tte_el1: te: 0x%016llx\n", te);

    uint64_t page_offset = extract64(va, 0, TG_16K_SIZE);
    return (te & TE_PHYS_ADDR_MASK) + page_offset;
}

void va_make_exec(ARMCPU *cpu, AddressSpace *as, hwaddr va, hwaddr size)
{
    hwaddr curr_va = va & PAGE_MASK_16K;
    while (curr_va < va + size) {
        pt_tte_el1(cpu, as, curr_va, true);
        curr_va += ((uint64_t)1 << TG_16K_SIZE);
    }
}
