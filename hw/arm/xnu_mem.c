/*
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
#include "hw/arm/xnu_mem.h"

hwaddr g_virt_base = 0;
hwaddr g_phys_base = 0;

hwaddr vtop_static(hwaddr va)
{
    if ((0 == g_virt_base) || (0 == g_phys_base)) {
        abort();
    }
    return va - g_virt_base + g_phys_base;
}

hwaddr ptov_static(hwaddr pa)
{
    if ((0 == g_virt_base) || (0 == g_phys_base)) {
        abort();
    }
    return pa - g_phys_base + g_virt_base;
}

hwaddr vtop_mmu(hwaddr va, CPUState *cs)
{
    hwaddr phys_addr;
    MemTxAttrs attrs = {};

    phys_addr = arm_cpu_get_phys_page_attrs_debug(cs, va, &attrs);
    if (-1 == phys_addr) {
        abort();
    }

    return phys_addr;
}
