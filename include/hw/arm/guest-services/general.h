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

#ifndef HW_ARM_GUEST_SERVICES_GENERAL_H
#define HW_ARM_GUEST_SERVICES_GENERAL_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "hw/platform-bus.h"

#include "hw/arm/n66_iphone6splus.h"
#include "hw/arm/guest-services/socket.h"

typedef enum {
    // Socket API
    QC_SOCKET = 0x100,
    QC_ACCEPT,
    QC_BIND,
    QC_CONNECT,
    QC_LISTEN,
    QC_RECV,
    QC_SEND,
    QC_CLOSE
} qemu_call_number_t;

typedef struct __attribute__((packed)) {
    // Request
    qemu_call_number_t call_number;
    union {
        qc_socket_args_t socket;
        qc_accept_args_t accept;
        qc_bind_args_t bind;
        qc_connect_args_t connect;
        qc_listen_args_t listen;
        qc_recv_args_t recv;
        qc_send_args_t send;
        qc_close_args_t close;
    } args;

    // Response
    int64_t retval;
    int64_t error;
} qemu_call_t;

uint64_t qemu_call_status(CPUARMState *env, const ARMCPRegInfo *ri);
void qemu_call(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value);

#endif // HW_ARM_GUEST_SERVICES_GENERAL_H