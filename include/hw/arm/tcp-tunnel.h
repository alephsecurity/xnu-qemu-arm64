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

#ifndef HW_ARM_TCP_TUNNEL_H
#define HW_ARM_TCP_TUNNEL_H

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

#define TCP_RECV_TIMEOUT (10)
#define TCP_TUNNEL_BUFFER_SIZE (0xfff)

typedef struct __attribute__((packed)) {
    ssize_t len;
    ssize_t err;
} tcp_tunnel_header_t;

typedef struct __attribute__((packed)) {
    tcp_tunnel_header_t header;
    uint8_t data[TCP_TUNNEL_BUFFER_SIZE - sizeof(tcp_tunnel_header_t)];
} tcp_tunnel_buffer_t;

static_assert (sizeof(tcp_tunnel_buffer_t) == TCP_TUNNEL_BUFFER_SIZE, "Wrong size!");

uint64_t tcp_tunnel_ready_to_send(CPUARMState *env, const ARMCPRegInfo *ri);
void tcp_tunnel_send(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value);

uint64_t tcp_tunnel_ready_to_recv(CPUARMState *env, const ARMCPRegInfo *ri);
void tcp_tunnel_recv(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value);

void *tunnel_accept_connection(void *arg);

#endif // HW_ARM_TCP_TUNNEL_H