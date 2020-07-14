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

#ifndef HW_ARM_XNU_TRAMPOLINE_HOOK_H
#define HW_ARM_XNU_TRAMPOLINE_HOOK_H

#include "qemu-common.h"
#include "hw/arm/boot.h"
#include "cpu.h"

#define HOOK_CODE_ALLOC_SIZE (1 << 20)

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

void xnu_hook_tr_copy_install(hwaddr va, hwaddr pa, hwaddr buf_va,
                              hwaddr buf_pa, uint8_t *code, uint64_t code_size,
                              uint64_t buf_size, uint8_t scratch_reg);
void xnu_hook_tr_install(hwaddr va, hwaddr pa, hwaddr cb_va, hwaddr tr_buf_va,
                         hwaddr tr_buf_pa, uint8_t scratch_reg);
void xnu_hook_tr_setup(AddressSpace *as, ARMCPU *cpu);

#endif
