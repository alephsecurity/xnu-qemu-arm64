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
#include "hw/arm/arm.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "hw/arm/xnu.h"
#include "hw/loader.h"

static uint64_t xnu_dev_ptr_ntf_read(void *opaque, hwaddr addr, unsigned size)
{
    PtrNtfDev *ptr_ntf = opaque;
    uint64_t ret = 0;

    if (addr + size > sizeof(hwaddr)) {
        abort();
    }

    memcpy((void *)&ret, (void *)((uint8_t *)&ptr_ntf->val + addr), size);

    //fprintf(stderr,
    //        "xnu_dev_ptr_ntf_read: addr: %016llX size: %016llX ret: %016llX\n",
    //        addr, (uint64_t)size, ret);

    ptr_ntf->cb(ptr_ntf->cb_opaque, ptr_ntf->val);

    return ret;
}

static void xnu_dev_ptr_ntf_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    PtrNtfDev *ptr_ntf = opaque;

    if (addr + size > sizeof(hwaddr)) {
        abort();
    }

    memcpy((void *)((uint8_t *)&ptr_ntf->val + addr), (void *)&val, size);

    //fprintf(stderr,
    //        "xnu_dev_ptr_ntf_write: %016llX val: %016llX size: %016llX\n",
    //        addr, val, (uint64_t)size);
    //fprintf(stderr,
    //        "xnu_dev_ptr_ntf_write: ptr_ntf->val: %016llX\n",
    //        ptr_ntf->val);

    ptr_ntf->cb(ptr_ntf->cb_opaque, ptr_ntf->val);
}

const MemoryRegionOps xnu_dev_ptr_ntf_ops = {
    .read = xnu_dev_ptr_ntf_read,
    .write = xnu_dev_ptr_ntf_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void xnu_dev_ptr_ntf_create(MemoryRegion *sysmem, PtrNtfDev *ptr_ntf,
                            const char *name)
{
    address_space_rw(ptr_ntf->as, ptr_ntf->pa, MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&ptr_ntf->val, sizeof(hwaddr), 0);
    MemoryRegion *iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, NULL, &xnu_dev_ptr_ntf_ops, ptr_ntf,
                          name, sizeof(hwaddr));
    memory_region_add_subregion(sysmem, ptr_ntf->pa, iomem);
    //fprintf(stderr,
    //        "xnu_dev_ptr_ntf_create: pa: %016llX val: %016llX name: %s\n",
    //        ptr_ntf->pa, ptr_ntf->val, name);
}

