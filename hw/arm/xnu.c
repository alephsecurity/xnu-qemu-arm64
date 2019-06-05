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

void macho_load_dtb(char *filename, AddressSpace *as, char *name,
                    hwaddr load_base, uint64_t *next_page_addr,
                    unsigned long *file_size, uint64_t *virt_base_next,
                    uint64_t ramdisk_addr, uint64_t ramdisk_size,
                    uint64_t tc_addr, uint64_t tc_size)
{
    struct xnu_DeviceTreeNodeProperty *dt_node = NULL;
    uint8_t *file_data = NULL;
    uint64_t *value_ptr;

    if (g_file_get_contents(filename, (char **)&file_data, file_size, NULL)) {

        for (size_t i = 0; i < *file_size; i++) {
            if (strncmp((const char *)file_data + i, "MemoryMapReserved-0",
                        xnu_kPropNameLength) == 0) {
                dt_node = (struct xnu_DeviceTreeNodeProperty *)(file_data + i);
                break;
            }
        }
        if (!dt_node) {
            fprintf(stderr, "Can't write device tree node for ramdisk!\n");
            abort();
        }
        strncpy(dt_node->name, "RAMDisk", xnu_kPropNameLength);
        value_ptr = (uint64_t *)&dt_node->value;
        value_ptr[0] = ramdisk_addr;
        value_ptr[1] = ramdisk_size;

        //fprintf(stderr, "RAMDisk dtb propery:\n");
        //unsigned char *temp = (unsigned char *)dt_node;
        //for (int i = 0; i < 8; i++) {
        //    for (int j = 0; j < 8; j++) {
        //        fprintf(stderr, "%02X ", temp[(i * 8) + j]);
        //    }
        //    fprintf(stderr, "\n");
        //}

        dt_node = NULL;
        for (size_t i = 0; i < *file_size; i++) {
            if (strncmp((const char *)file_data + i, "MemoryMapReserved-1",
                        xnu_kPropNameLength) == 0) {
                dt_node = (struct xnu_DeviceTreeNodeProperty *)(file_data + i);
                break;
            }
        }
        if (!dt_node) {
            fprintf(stderr, "Can't write device tree node for trustcache!\n");
            abort();
        }
        strncpy(dt_node->name, "TrustCache", xnu_kPropNameLength);
        value_ptr = (uint64_t *)&dt_node->value;
        value_ptr[0] = tc_addr;
        value_ptr[1] = tc_size;

        //fprintf(stderr, "TrustCache dtb propery:\n");
        //unsigned char *temp = (unsigned char *)dt_node;
        //for (int i = 0; i < 8; i++) {
        //    for (int j = 0; j < 8; j++) {
        //        fprintf(stderr, "%02X ", temp[(i * 8) + j]);
        //    }
        //    fprintf(stderr, "\n");
        //}
        //fprintf(stderr, "TrustCache addr: %llx size: %lld\n",
        //        tc_addr, tc_size);

        rom_add_blob_fixed_as(name, file_data, *file_size, load_base, as);

        *next_page_addr = (load_base + *file_size + 0xffffull) & ~0xffffull;
        g_free(file_data);
        if (NULL != virt_base_next) {
            *virt_base_next += (*next_page_addr - load_base);
        }
    } else {
        fprintf(stderr, "load file failed?!\n");
        fprintf(stderr, "filename: %s\n", filename);
        abort();
    }
}

void macho_load_raw_file(char *filename, AddressSpace *as, char *name,
                         hwaddr load_base, uint64_t *next_page_addr,
                         unsigned long *file_size, uint64_t *virt_base_next)
{
    uint8_t* file_data = NULL;
    if (g_file_get_contents(filename, (char **)&file_data, file_size, NULL)) {
        rom_add_blob_fixed_as(name, file_data, *file_size, load_base, as);
        if (NULL != next_page_addr) {
            *next_page_addr = (load_base + *file_size + 0xffffull) & ~0xffffull;
        }
        g_free(file_data);
        //fprintf(stderr, "macho_load_raw_file() filename: %s\n", filename);
        //fprintf(stderr, "macho_load_raw_file() load_base: %llx\n", load_base);
        //fprintf(stderr, "macho_load_raw_file() file_size: %lx\n", *file_size);
        if (NULL != virt_base_next) {
            //fprintf(stderr, "macho_load_raw_file() *virt_base_next: %llx\n",
            //        *virt_base_next);
            *virt_base_next += (*next_page_addr - load_base);
        }
    } else {
        fprintf(stderr, "load file failed?!\n");
        fprintf(stderr, "filename: %s\n", filename);
        abort();
    }
}

void macho_tz_setup_bootargs(char *name, uint64_t mem_size, AddressSpace *as,
                             uint64_t bootargs_addr, uint64_t virt_base,
                             uint64_t phys_base, uint64_t kern_args,
                             uint64_t kern_entry, uint64_t kern_phys_base)
{
    struct xnu_arm64_monitor_boot_args boot_args;
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

    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.version: %llx\n",
    //        boot_args.version);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.virtBase: %llx\n",
    //        boot_args.virtBase);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.physBase: %llx\n",
    //        boot_args.physBase);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.memSize: %llx\n",
    //        boot_args.memSize);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.kernArgs: %llx\n",
    //        boot_args.kernArgs);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.kernEntry: %llx\n",
    //        boot_args.kernEntry);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.kernPhysBase: %llx\n",
    //        boot_args.kernPhysBase);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.kernPhysSlide: %llx\n",
    //        boot_args.kernPhysSlide);
    //fprintf(stderr, "macho_tz_setup_bootargs() boot_args.kernVirtSlide: %llx\n",
    //        boot_args.kernVirtSlide);

    rom_add_blob_fixed_as(name, &boot_args, sizeof(boot_args),
                          bootargs_addr, as);
}

void macho_setup_bootargs(char *name, uint64_t mem_size, AddressSpace *as,
                          uint64_t bootargs_addr, uint64_t virt_base,
                          uint64_t phys_base, uint64_t top_of_kernel_data,
                          uint64_t dtb_address, unsigned long dtb_size,
                          char *kern_args)
{
    struct xnu_arm64_boot_args boot_args;
    memset(&boot_args, 0, sizeof(boot_args));
    boot_args.Revision = xnu_arm64_kBootArgsRevision2;
    boot_args.Version = xnu_arm64_kBootArgsVersion2;
    boot_args.virtBase = virt_base;
    boot_args.physBase = phys_base;
    boot_args.memSize = mem_size;
    // top of kernel data (kernel, dtb, any ramdisk, any TC) +
    // boot args size + padding to 16k
    boot_args.topOfKernelData = ((top_of_kernel_data + sizeof(boot_args)) +
                                 0xffffull) & ~0xffffull;
    boot_args.deviceTreeP = dtb_address;
    boot_args.deviceTreeLength = dtb_size;
    boot_args.memSizeActual = mem_size;
    if (kern_args) {
        strlcpy(boot_args.CommandLine, kern_args,
                sizeof(boot_args.CommandLine));
    }

    //fprintf(stderr, "macho_setup_bootargs() boot_args.Revision: %d\n",
    //        boot_args.Revision);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.Version: %d\n",
    //        boot_args.Version);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.virtBase: %llx\n",
    //        boot_args.virtBase);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.physBase: %llx\n",
    //        boot_args.physBase);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.memSize: %llx\n",
    //        boot_args.memSize);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.topOfKernelData: %llx\n",
    //        boot_args.topOfKernelData);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.deviceTreeP: %llx\n",
    //        boot_args.deviceTreeP);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.deviceTreeLength: %x\n",
    //        boot_args.deviceTreeLength);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.memSizeActual: %llx\n",
    //        boot_args.memSizeActual);
    //fprintf(stderr, "macho_setup_bootargs() boot_args.CommandLine: %s\n",
    //        boot_args.CommandLine);

    rom_add_blob_fixed_as(name, &boot_args, sizeof(boot_args),
                          bootargs_addr, as);
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

#define VAtoPA(addr) (((addr) & 0x3fffffff) + load_base)

void arm_load_macho(char *filename, AddressSpace *as, char *name,
                    hwaddr load_base, bool image_addr, uint64_t *pc,
                    uint64_t *phys_next_page, uint64_t *virt_next_page,
                    uint64_t *virt_base, uint64_t *phys_base,
                    uint64_t *virt_load_base, uint64_t *phys_load_base)
{
    uint8_t *data = NULL;
    gsize len;
    uint8_t* rom_buf = NULL;

    //fprintf(stderr, "arm_load_macho() filename -> %s\n", filename);
    if (!g_file_get_contents(filename, (char **)&data, &len, NULL)) {
        abort();
    }
    struct mach_header_64* mh = (struct mach_header_64*)data;
    struct load_command* cmd = (struct load_command*)(data +
                                                sizeof(struct mach_header_64));
    // through all the segments once to find highest and lowest addresses
    uint64_t low_addr_temp;
    uint64_t high_addr_temp;
    macho_highest_lowest(mh, &low_addr_temp, &high_addr_temp);

    *virt_base = low_addr_temp;
    *phys_base = VAtoPA(*virt_base);

    //get the base from the macho image
    if (image_addr) {
        load_base = low_addr_temp;
    }

    uint64_t rom_buf_size = high_addr_temp - low_addr_temp;
    rom_buf = g_malloc0(rom_buf_size);
    //fprintf(stderr, "arm_load_macho() ncmds: %d\n", mh->ncmds);
    for (unsigned int index = 0; index < mh->ncmds; index++) {
        switch (cmd->cmd) {
            case LC_SEGMENT_64: {
                struct segment_command_64 *segCmd =
                                            (struct segment_command_64 *)cmd;
                //fprintf(stderr, "arm_load_macho() vmaddr: %llx\n", segCmd->vmaddr);
                //fprintf(stderr, "arm_load_macho() segsize: %llx\n", segCmd->filesize);
                //fprintf(stderr, "arm_load_macho() fileoff: %llx\n", segCmd->fileoff);
                //even if not lowest addr use as base if file offset is 0
                if (0 == segCmd->fileoff) {
                    *virt_base = segCmd->vmaddr;
                    *phys_base = VAtoPA(*virt_base);
                }
                memcpy(rom_buf + (segCmd->vmaddr - low_addr_temp),
                       data + segCmd->fileoff, segCmd->filesize);
                break;
            }
            case LC_UNIXTHREAD: {
                // grab just the entry point PC
                uint64_t* ptrPc = (uint64_t*)((char*)cmd + 0x110);
                // 0x110 for arm64 only.
                *pc = VAtoPA(*ptrPc);
                break;
            }
        }
        cmd = (struct load_command*)((char*)cmd + cmd->cmdsize);
    }
    hwaddr rom_base = VAtoPA(low_addr_temp);
    rom_add_blob_fixed_as(name, rom_buf, rom_buf_size, rom_base, as);

    *phys_load_base = load_base;
    *virt_load_base = *virt_base - *phys_base + (uint64_t)load_base;
    *virt_next_page = (high_addr_temp + 0xffffull) & ~0xffffull;
    *phys_next_page = VAtoPA(*virt_next_page);

    //fprintf(stderr, "arm_load_macho() *virt_load_base: %llx\n",
    //        *virt_load_base);
    //fprintf(stderr, "arm_load_macho() *phys_load_base: %llx\n",
    //        *phys_load_base);
    //fprintf(stderr, "arm_load_macho() *virt_base: %llx\n", *virt_base);
    //fprintf(stderr, "arm_load_macho() *phys_base: %llx\n", *phys_base);
    //fprintf(stderr, "arm_load_macho() low_addr_temp: %llx\n", low_addr_temp);
    //fprintf(stderr, "arm_load_macho() high_addr_temp: %llx\n", high_addr_temp);
    //fprintf(stderr, "arm_load_macho() *pc: %llx\n", *pc);
    //fprintf(stderr, "arm_load_macho() *rom_buf: %llx\n", *(uint64_t*)rom_buf);

    if (data) {
        g_free(data);
    }
    if (rom_buf) {
        g_free(rom_buf);
    }
}

#undef VAtoPA
