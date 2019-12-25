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
#include "hw/arm/xnu_remap_dev.h"

static uint64_t xnu_dev_remap_read(void *opaque, hwaddr addr, unsigned size)
{
    RemapDev *remap = opaque;
    AddressSpace *as = remap->orig_as;
    hwaddr offset = 0;
    uint64_t ret = 0;

    if (addr + size > remap->size) {
        abort();
    }

    offset = addr;

    address_space_rw(as, remap->orig_pa + offset,
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&ret, size, 0);
    return ret;
}

static void xnu_dev_remap_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    RemapDev *remap = opaque;
    AddressSpace *as = remap->orig_as;
    hwaddr offset = 0;

    if (addr + size > remap->size) {
        abort();
    }

    offset = addr;

    address_space_rw(as, remap->orig_pa + offset,
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&val, size, 1);
}

const MemoryRegionOps xnu_dev_remap_ops = {
    .read = xnu_dev_remap_read,
    .write = xnu_dev_remap_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void xnu_dev_remap_create(MemoryRegion *sysmem, RemapDev *remap,
                          const char *name)
{
    MemoryRegion *iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, NULL, &xnu_dev_remap_ops, remap,
                          name, remap->size);
    memory_region_add_subregion(sysmem, remap->pa, iomem);
}
