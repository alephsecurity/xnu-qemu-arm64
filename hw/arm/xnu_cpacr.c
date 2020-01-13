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
#include "hw/arm/xnu_cpacr.h"
#include "hw/loader.h"

static CpacrIntCtx ctx = {0};

static void cpacr_a32_s_int_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    ctx.wfn_a32_s(env, ri, ctx.val);
}

static void cpacr_a32_s_int_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    cpacr_a32_s_int_write(env, ri, ctx.val);
}

static void cpacr_a32_ns_int_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    ctx.wfn_a32_ns(env, ri, ctx.val);
}

static void cpacr_a32_ns_int_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    cpacr_a32_ns_int_write(env, ri, ctx.val);
}

static void cpacr_a64_intercept_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                      uint64_t value)
{
    ctx.wfn_a64(env, ri, ctx.val);
}

static void cpacr_a64_intercept_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    cpacr_a64_intercept_write(env, ri, ctx.val);
}

void xnu_cpacr_intercept_write_const_val(ARMCPU *cpu, uint64_t val)
{
    ctx.val = val;
    uint32_t key = ENCODE_AA64_CP_REG(19, 1, 0, 3, 0, 2);
    ARMCPRegInfo *ri = (ARMCPRegInfo *)get_arm_cp_reginfo(cpu->cp_regs, key);
    ctx.wfn_a64 = ri->writefn;
    ri->writefn = cpacr_a64_intercept_write;
    ri->resetfn = cpacr_a64_intercept_reset;
    key = ENCODE_CP_REG(15, 0, 1, 1, 0, 0, 2);
    ri = (ARMCPRegInfo *)get_arm_cp_reginfo(cpu->cp_regs, key);
    ctx.wfn_a32_ns = ri->writefn;
    ri->writefn = cpacr_a32_ns_int_write;
    ri->resetfn = cpacr_a32_ns_int_reset;
    key = ENCODE_CP_REG(15, 0, 0, 1, 0, 0, 2);
    ri = (ARMCPRegInfo *)get_arm_cp_reginfo(cpu->cp_regs, key);
    ctx.wfn_a32_s = ri->writefn;
    ri->writefn = cpacr_a32_s_int_write;
    ri->resetfn = cpacr_a32_s_int_reset;
}
