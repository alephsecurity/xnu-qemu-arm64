/*
 * QEMU TCP Tunnelling
 *
 * Copyright (c) 2019 Lev Aronsky <aronsky@gmail.com>
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

#ifndef HW_ARM_GUEST_SERVICES_FDS_H
#define HW_ARM_GUEST_SERVICES_FDS_H

#ifndef OUT_OF_TREE_BUILD
#include "qemu/osdep.h"
#else
#include "sys/types.h"
#include "sys/socket.h"
#endif

#define MAX_FD_COUNT (256)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
extern int32_t guest_svcs_errno;
#pragma GCC diagnostic pop
extern int32_t guest_svcs_fds[MAX_FD_COUNT];

#define VERIFY_FD(s) \
    if ((s < 0) || (s >= MAX_FD_COUNT) || (-1 == guest_svcs_fds[s])) return -1;

typedef struct __attribute__((packed)) {
    int32_t fd;
} qc_close_args_t;

typedef struct __attribute__((packed)) {
    int32_t fd;
    int32_t cmd;
    union {
        int32_t flags;
    };
} qc_fcntl_args_t;

#ifndef OUT_OF_TREE_BUILD
int32_t qc_handle_close(CPUState *cpu, int32_t fd);
int32_t qc_handle_fcntl_getfl(CPUState *cpu, int32_t fd);
int32_t qc_handle_fcntl_setfl(CPUState *cpu, int32_t fd, int32_t flags);
#else
int qc_close(int fd);
int qc_fcntl(int fd, int cmd, ...);
#endif

#endif // HW_ARM_GUEST_SERVICES_FDS_H
