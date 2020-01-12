/* * iPhone 6s plus - n66 - S8000
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

#ifndef HW_ARM_XNU_FAKE_TASK_PORT_H
#define HW_ARM_XNU_FAKE_TASK_PORT_H

#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/arm/arm.h"
#include "hw/arm/xnu.h"
#include "exec/memory.h"
#include "cpu.h"
#include "sysemu/kvm.h"
#include "hw/arm/xnu_remap_dev.h"

//TODO: support more versions
#define KERNEL_TASK_PTR_16B92 (0xfffffff007602078)
#define KERNEL_TASK_SIZE_16B92 (1440)
#define KERNEL_PORT_SIZE_16B92 (168)
#define KERNEL_PORT_OFFSET_16B92 (0xd8)
#define KERNEL_TASK_OFFSET_16B92 (0x68)

#define KERNEL_REALHOST_SPECIAL_16B92 (0xfffffff007607bc8)

#define KERNEL_TASK_ALLOC_SIZE (0x2000)
#define FAKE_PORT_ALLOC_SIZE (0x2000)

typedef struct {
    hwaddr va;
    hwaddr pa;
    hwaddr buf_va;
    hwaddr buf_pa;
    uint64_t buf_size;
    uint8_t *code;
    uint64_t code_size;
    uint8_t scratch_reg;
} KernelTrHookParams;

typedef struct {
    CPUState *cs;
    AddressSpace *as;
    hwaddr fake_port_pa;
    hwaddr remap_kernel_task_pa;
    RemapDev remap;
    KernelTrHookParams hook;
} KernelTaskPortParams;

void setup_fake_task_port(void *opaque, hwaddr global_kernel_ptr);

#endif

