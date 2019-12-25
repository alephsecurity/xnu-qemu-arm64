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

#define xnu_kPropNameLength 32

struct xnu_DeviceTreeNodeProperty {
    char name[xnu_kPropNameLength];
    uint32_t length;
    char value[];
};

void macho_load_dtb(char *filename, AddressSpace *as, MemoryRegion *mem,
                    const char *name, hwaddr dtb_pa, unsigned long *size,
                    hwaddr ramdisk_addr, hwaddr ramdisk_size, hwaddr tc_addr,
                    hwaddr tc_size)
{
    struct xnu_DeviceTreeNodeProperty *dt_node = NULL;
    uint8_t *file_data = NULL;
    uint64_t *value_ptr;

    if (g_file_get_contents(filename, (char **)&file_data, size, NULL)) {

        //serach for the "MemoryMapReserved-0" DT node which represents the
        //the memory area we will later use for the root mount ramdisk.
        for (size_t i = 0; i < *size; i++) {
            if (strncmp((const char *)file_data + i, "MemoryMapReserved-0",
                        xnu_kPropNameLength) == 0) {
                dt_node = (struct xnu_DeviceTreeNodeProperty *)(file_data + i);
                break;
            }
        }
        if (!dt_node) {
            abort();
        }
        strncpy(dt_node->name, "RAMDisk", xnu_kPropNameLength);
        value_ptr = (uint64_t *)&dt_node->value;
        value_ptr[0] = ramdisk_addr;
        value_ptr[1] = ramdisk_size;

        //serach for the "MemoryMapReserved-1" DT node which represents the
        //the memory area we will later use for static trust cache.
        dt_node = NULL;
        for (size_t i = 0; i < *size; i++) {
            if (strncmp((const char *)file_data + i, "MemoryMapReserved-1",
                        xnu_kPropNameLength) == 0) {
                dt_node = (struct xnu_DeviceTreeNodeProperty *)(file_data + i);
                break;
            }
        }
        if (!dt_node) {
            abort();
        }
        strncpy(dt_node->name, "TrustCache", xnu_kPropNameLength);
        value_ptr = (uint64_t *)&dt_node->value;
        value_ptr[0] = tc_addr;
        value_ptr[1] = tc_size;

        if (mem) {
            allocate_ram(mem, filename, dtb_pa, align_64k_high(*size));
        }
        rom_add_blob_fixed_as(name, file_data, *size, dtb_pa, as);
        g_free(file_data);
    } else {
        abort();
    }
}

void macho_load_raw_file(char *filename, AddressSpace *as, MemoryRegion *mem,
                         const char *name, hwaddr file_pa, unsigned long *size)
{
    uint8_t* file_data = NULL;
    if (g_file_get_contents(filename, (char **)&file_data, size, NULL)) {
        if (mem) {
            allocate_ram(mem, filename, file_pa, align_64k_high(*size));
        }
        rom_add_blob_fixed_as(name, file_data, *size, file_pa, as);
        g_free(file_data);
    } else {
        abort();
    }
}

void macho_tz_setup_bootargs(const char *name, AddressSpace *as,
                             MemoryRegion *mem, hwaddr bootargs_addr,
                             hwaddr virt_base, hwaddr phys_base,
                             hwaddr mem_size, hwaddr kern_args,
                             hwaddr kern_entry, hwaddr kern_phys_base)
{
    struct xnu_arm64_monitor_boot_args boot_args;
    hwaddr size = align_64k_high(sizeof(boot_args));
    memset(&boot_args, 0, sizeof(boot_args));
    boot_args.version = xnu_arm64_kBootArgsVersion2;
    boot_args.virtBase = virt_base;
    boot_args.physBase = phys_base;
    boot_args.memSize = mem_size;
    boot_args.kernArgs = kern_args;
    boot_args.kernEntry = kern_entry;
    boot_args.kernPhysBase = kern_phys_base;

    boot_args.kernPhysSlide = 0;
    boot_args.kernVirtSlide = 0;

    if (mem) {
        allocate_ram(mem, name, bootargs_addr, align_64k_high(size));
    }
    rom_add_blob_fixed_as(name, &boot_args, sizeof(boot_args), bootargs_addr,
                          as);
}

void macho_setup_bootargs(const char *name, AddressSpace *as,
                          MemoryRegion *mem, hwaddr bootargs_pa,
                          hwaddr virt_base, hwaddr phys_base, hwaddr mem_size,
                          hwaddr top_of_kernel_data_pa, hwaddr dtb_va,
                          hwaddr dtb_size, char *kern_args)
{
    struct xnu_arm64_boot_args boot_args;
    hwaddr size = align_64k_high(sizeof(boot_args));
    memset(&boot_args, 0, sizeof(boot_args));
    boot_args.Revision = xnu_arm64_kBootArgsRevision2;
    boot_args.Version = xnu_arm64_kBootArgsVersion2;
    boot_args.virtBase = virt_base;
    boot_args.physBase = phys_base;
    boot_args.memSize = mem_size;

    boot_args.topOfKernelData = top_of_kernel_data_pa;
    boot_args.deviceTreeP = dtb_va;
    boot_args.deviceTreeLength = dtb_size;
    boot_args.memSizeActual = 0;
    if (kern_args) {
        strlcpy(boot_args.CommandLine, kern_args,
                sizeof(boot_args.CommandLine));
    }

    if (mem) {
        allocate_ram(mem, name, bootargs_pa, align_64k_high(size));
    }
    rom_add_blob_fixed_as(name, &boot_args, sizeof(boot_args),
                          bootargs_pa, as);
}

static void macho_highest_lowest(struct mach_header_64* mh, uint64_t *lowaddr,
                                 uint64_t *highaddr)
{
    struct load_command* cmd = (struct load_command*)((uint8_t*)mh +
                                                sizeof(struct mach_header_64));
    // iterate all the segments once to find highest and lowest addresses
    uint64_t low_addr_temp = ~0;
    uint64_t high_addr_temp = 0;
    for (unsigned int index = 0; index < mh->ncmds; index++) {
        switch (cmd->cmd) {
            case LC_SEGMENT_64: {
                struct segment_command_64 *segCmd =
                                        (struct segment_command_64 *)cmd;
                if (segCmd->vmaddr < low_addr_temp) {
                    low_addr_temp = segCmd->vmaddr;
                }
                if (segCmd->vmaddr + segCmd->vmsize > high_addr_temp) {
                    high_addr_temp = segCmd->vmaddr + segCmd->vmsize;
                }
                break;
            }
        }
        cmd = (struct load_command*)((char*)cmd + cmd->cmdsize);
    }
    *lowaddr = low_addr_temp;
    *highaddr = high_addr_temp;
}

static void macho_file_highest_lowest(const char *filename, hwaddr *lowest,
                                      hwaddr *highest)
{
    gsize len;
    uint8_t *data = NULL;
    if (!g_file_get_contents(filename, (char **)&data, &len, NULL)) {
        abort();
    }
    struct mach_header_64* mh = (struct mach_header_64*)data;
    macho_highest_lowest(mh, lowest, highest);
    g_free(data);
}

void macho_file_highest_lowest_base(const char *filename, hwaddr phys_base,
                                    hwaddr *virt_base, hwaddr *lowest,
                                    hwaddr *highest)
{
    uint8_t high_Low_dif_bit_index;
    uint8_t phys_base_non_zero_bit_index;
    hwaddr bit_mask_for_index;

    macho_file_highest_lowest(filename, lowest, highest);
    high_Low_dif_bit_index =
        get_highest_different_bit_index(align_64k_high(*highest),
                                        align_64k_low(*lowest));
    if (phys_base) {
        phys_base_non_zero_bit_index =
            get_lowest_non_zero_bit_index(phys_base);

        //make sure we have enough zero bits to have all the diffrent kernel
        //image addresses have the same non static bits in physical and in
        //virtual memory.
        if (high_Low_dif_bit_index > phys_base_non_zero_bit_index) {
            abort();
        }
        bit_mask_for_index =
            get_low_bits_mask_for_bit_index(phys_base_non_zero_bit_index);

        *virt_base = align_64k_low(*lowest) & (~bit_mask_for_index);
    }

}

void arm_load_macho(char *filename, AddressSpace *as, MemoryRegion *mem,
                    const char *name, hwaddr phys_base, hwaddr virt_base,
                    hwaddr low_virt_addr, hwaddr high_virt_addr, hwaddr *pc)
{
    uint8_t *data = NULL;
    gsize len;
    uint8_t* rom_buf = NULL;

    if (!g_file_get_contents(filename, (char **)&data, &len, NULL)) {
        abort();
    }
    struct mach_header_64* mh = (struct mach_header_64*)data;
    struct load_command* cmd = (struct load_command*)(data +
                                                sizeof(struct mach_header_64));

    uint64_t rom_buf_size = align_64k_high(high_virt_addr) - low_virt_addr;
    rom_buf = g_malloc0(rom_buf_size);
    for (unsigned int index = 0; index < mh->ncmds; index++) {
        switch (cmd->cmd) {
            case LC_SEGMENT_64: {
                struct segment_command_64 *segCmd =
                                            (struct segment_command_64 *)cmd;
                memcpy(rom_buf + (segCmd->vmaddr - low_virt_addr),
                       data + segCmd->fileoff, segCmd->filesize);
                break;
            }
            case LC_UNIXTHREAD: {
                // grab just the entry point PC
                uint64_t* ptrPc = (uint64_t*)((char*)cmd + 0x110);
                // 0x110 for arm64 only.
                *pc = vtop_bases(*ptrPc, phys_base, virt_base);
                break;
            }
        }
        cmd = (struct load_command*)((char*)cmd + cmd->cmdsize);
    }
    hwaddr low_phys_addr = vtop_bases(low_virt_addr, phys_base, virt_base);
    if (mem) {
        allocate_ram(mem, name, low_phys_addr, rom_buf_size);
    }
    rom_add_blob_fixed_as(name, rom_buf, rom_buf_size, low_phys_addr, as);

    if (data) {
        g_free(data);
    }
    if (rom_buf) {
        g_free(rom_buf);
    }
}
