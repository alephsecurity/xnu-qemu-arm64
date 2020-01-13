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
#include "hw/arm/boot.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "hw/platform-bus.h"
#include "hw/arm/xnu_fake_task_port.h"

static void copy_kernel_task_port(hwaddr kernel_port_new_addr,
                                  hwaddr kernel_task_new_addr,
                                  hwaddr kernel_port_orig_addr,
                                  AddressSpace *as)
{
    uint8_t port_buffer[KERNEL_PORT_SIZE_16B92];

    //read out the kernel task port
    address_space_rw(as, kernel_port_orig_addr, MEMTXATTRS_UNSPECIFIED,
                     &port_buffer[0], KERNEL_PORT_SIZE_16B92, 0);

    //ovr write the kernel task pointer with the new copy
    *(uint64_t *)&port_buffer[KERNEL_TASK_OFFSET_16B92] = \
        ptov_static(kernel_task_new_addr);

    //write the new kernel task port to memory
    address_space_rw(as, kernel_port_new_addr,
                     MEMTXATTRS_UNSPECIFIED, &port_buffer[0],
                     KERNEL_PORT_SIZE_16B92, 1);
}

void setup_fake_task_port(void *opaque, hwaddr global_kernel_ptr)
{
    static bool done = false;
    MemoryRegion *sysmem = NULL;
    KernelTaskPortParams *ktpp = (KernelTaskPortParams *)opaque;
    hwaddr task_port_ptr = 0;
    hwaddr fake_port_paddr = ktpp->fake_port_pa;
    hwaddr fake_port_vaddr = ptov_static(fake_port_paddr);
    hwaddr remap_task_paddr = ktpp->remap_kernel_task_pa;
    hwaddr special_port_4_vaddr = KERNEL_REALHOST_SPECIAL_16B92 + \
                                  (4 * sizeof(hwaddr));

    if (done) {
        return;
    }

    if ((0 == global_kernel_ptr) ||
        (0 == (0xffffffff00000000 & global_kernel_ptr)) ||
        (0 == (0x00000000ffffffff & global_kernel_ptr))) {
        return;
    }

    address_space_rw(ktpp->as, vtop_mmu(global_kernel_ptr, ktpp->cs) + \
                     KERNEL_PORT_OFFSET_16B92,
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&task_port_ptr,
                     sizeof(hwaddr), 0);

    if (0 == task_port_ptr) {
        return;
    }

    if (0 != ktpp->hook.va) {
        //install the hook here because we need the MMU to be already
        //configured and all the memory mapped before installing the hook
        xnu_hook_tr_copy_install(ktpp->hook.va, ktpp->hook.pa,
                                 ktpp->hook.buf_va, ktpp->hook.buf_pa,
                                 ktpp->hook.code, ktpp->hook.code_size,
                                 ktpp->hook.buf_size, ktpp->hook.scratch_reg);
    }

    done = true;
    sysmem = get_system_memory();

    ktpp->remap.orig_pa = vtop_mmu(global_kernel_ptr,  ktpp->cs);
    ktpp->remap.pa = remap_task_paddr;
    ktpp->remap.orig_as = ktpp->as;
    ktpp->remap.size = KERNEL_TASK_ALLOC_SIZE;

    xnu_dev_remap_create(sysmem, &ktpp->remap, "kernel_task_remap_dev");

    copy_kernel_task_port(fake_port_paddr, remap_task_paddr,
                          vtop_mmu(task_port_ptr, ktpp->cs), ktpp->as);

    address_space_rw(ktpp->as, vtop_mmu(special_port_4_vaddr, ktpp->cs),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&fake_port_vaddr,
                     sizeof(hwaddr), 1);
}
