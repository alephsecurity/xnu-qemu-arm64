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

#ifndef HW_ARM_GUEST_SERVICES_SOCKET_H
#define HW_ARM_GUEST_SERVICES_SOCKET_H

#ifndef OUT_OF_TREE_BUILD
#include "qemu/osdep.h"
#else
#include "sys/types.h"
#include "sys/socket.h"
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
extern int32_t guest_svcs_errno;
#pragma GCC diagnostic pop

#define MAX_BUF_SIZE (4096)

typedef struct __attribute__((packed)) {
    int32_t domain;
    int32_t type;
    int32_t protocol;
} qc_socket_args_t;

typedef struct __attribute__((packed)) {
    int32_t socket;
    struct sockaddr *addr;
    socklen_t *addrlen;
} qc_accept_args_t;

typedef struct __attribute__((packed)) {
    int32_t socket;
    struct sockaddr *addr;
    socklen_t addrlen;
} qc_bind_args_t, qc_connect_args_t;

typedef struct __attribute__((packed)) {
    int32_t socket;
    int32_t backlog;
} qc_listen_args_t;

typedef struct __attribute__((packed)) {
    int32_t socket;
    void *buffer;
    size_t length;
    int flags;
} qc_recv_args_t, qc_send_args_t;

#ifndef OUT_OF_TREE_BUILD
int32_t qc_handle_socket(CPUState *cpu, int32_t domain, int32_t type,
                         int32_t protocol);
int32_t qc_handle_accept(CPUState *cpu, int32_t sckt, struct sockaddr *addr,
                         socklen_t *addrlen);
int32_t qc_handle_bind(CPUState *cpu, int32_t sckt, struct sockaddr *addr,
                       socklen_t addrlen);
int32_t qc_handle_connect(CPUState *cpu, int32_t sckt, struct sockaddr *addr,
                          socklen_t addrlen);
int32_t qc_handle_listen(CPUState *cpu, int32_t sckt, int32_t backlog);
int32_t qc_handle_recv(CPUState *cpu, int32_t sckt, void *buffer,
                       size_t length, int32_t flags);
int32_t qc_handle_send(CPUState *cpu, int32_t sckt, void *buffer,
                       size_t length, int32_t flags);
#else
int qc_socket(int domain, int type, int protocol);
int qc_accept(int sckt, struct sockaddr *addr, socklen_t *addrlen);
int qc_bind(int sckt, const struct sockaddr *addr, socklen_t addrlen);
int qc_connect(int sckt, const struct sockaddr *addr, socklen_t addrlen);
int qc_listen(int sckt, int backlog);
ssize_t qc_recv(int sckt, void *buffer, size_t length, int flags);
ssize_t qc_send(int sckt, const void *buffer, size_t length, int flags);
#endif

#endif // HW_ARM_GUEST_SERVICES_SOCKET_H
