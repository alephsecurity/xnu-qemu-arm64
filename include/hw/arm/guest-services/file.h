/*
 * QEMU Host file guest access
 *
 * Copyright (c) 2020 Jonathan Afek <jonyafek@me.com>
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

#ifndef HW_ARM_GUEST_SERVICES_FILE_H
#define HW_ARM_GUEST_SERVICES_FILE_H

#ifndef OUT_OF_TREE_BUILD
#include "qemu/osdep.h"
#else
#include "sys/types.h"
#endif

#define MAX_FILE_FDS (8)
#define MAX_FILE_TRANSACTION_LEN (0x2000)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
extern int32_t guest_svcs_errno;
#pragma GCC diagnostic pop

typedef struct __attribute__((packed)) {
    uint64_t buffer_guest_ptr;
    uint64_t length;
    uint64_t offset;
    uint64_t index;
} qc_write_file_args_t, qc_read_file_args_t;

typedef struct __attribute__((packed)) {
    uint64_t index;
} qc_size_file_args_t;

#ifndef OUT_OF_TREE_BUILD
void qc_file_open(uint64_t index, const char *filename);

int64_t qc_handle_write_file(CPUState *cpu, uint64_t buffer_guest_ptr,
                             uint64_t length, uint64_t offset, uint64_t index);
int64_t qc_handle_read_file(CPUState *cpu, uint64_t buffer_guest_ptr,
                            uint64_t length, uint64_t offset, uint64_t index);
int64_t qc_handle_size_file(uint64_t index);
#else
int64_t qc_write_file(void *buffer_guest_ptr, uint64_t length,
                      uint64_t offset, uint64_t index);
int64_t qc_read_file(void *buffer_guest_ptr, uint64_t length,
                     uint64_t offset, uint64_t index);
int64_t qc_size_file(uint64_t index);
#endif

#endif
