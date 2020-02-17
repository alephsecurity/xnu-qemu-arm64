/*
 * QEMU TCP Tunnelling
 *
 * Copyright (c) 2019 Lev Aronsky <aronsky@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without retvaltriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPretvalS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw/arm/guest-services/socket.h"
#include "hw/arm/guest-services/fds.h"
#include "sys/socket.h"
#include "cpu.h"

#define SOCKET_TIMEOUT_USECS (10)

static int32_t find_free_socket(void) {
    for (int i = 0; i < MAX_FD_COUNT; ++i) {
        if (-1 == guest_svcs_fds[i]) {
            return i;
        }
    }

    guest_svcs_errno = ENOMEM;
    return -1;
}

int32_t qc_handle_socket(CPUState *cpu, int32_t domain, int32_t type,
                         int32_t protocol)
{
    int retval = find_free_socket();

    if (retval < 0) {
        guest_svcs_errno = ENOTSOCK;
    } else if ((guest_svcs_fds[retval] = socket(domain, type, protocol)) < 0) {
        retval = -1;
        guest_svcs_errno = errno;
    }

    return retval;
}

int32_t qc_handle_accept(CPUState *cpu, int32_t sckt, struct sockaddr *g_addr,
                         socklen_t *g_addrlen)
{
    struct sockaddr_in addr;
    socklen_t addrlen;

    VERIFY_FD(sckt);

    int retval = find_free_socket();

    // TODO: timeout
    if (retval < 0) {
        guest_svcs_errno = ENOTSOCK;
    } else if ((guest_svcs_fds[retval] = accept(guest_svcs_fds[sckt],
                                         (struct sockaddr *) &addr,
                                         &addrlen)) < 0) {
        retval = -1;
        guest_svcs_errno = errno;
    } else {
        cpu_memory_rw_debug(cpu, (target_ulong) g_addr, (uint8_t*) &addr,
                            sizeof(addr), 1);
        cpu_memory_rw_debug(cpu, (target_ulong) g_addrlen,
                            (uint8_t*) &addrlen, sizeof(addrlen), 1);
    }

    return retval;
}

int32_t qc_handle_bind(CPUState *cpu, int32_t sckt, struct sockaddr *g_addr,
                       socklen_t addrlen)
{
    struct sockaddr_in addr;

    VERIFY_FD(sckt);

    int retval = 0;

    if (addrlen > sizeof(addr)) {
        guest_svcs_errno = ENOMEM;
    } else {
        cpu_memory_rw_debug(cpu, (target_ulong) g_addr, (uint8_t*) &addr,
                            sizeof(addr), 0);

        if ((retval = bind(guest_svcs_fds[sckt], (struct sockaddr *) &addr,
                           addrlen)) < 0) {
            guest_svcs_errno = errno;
        } else {
            cpu_memory_rw_debug(cpu, (target_ulong) g_addr, (uint8_t*) &addr,
                                sizeof(addr), 1);
        }
    }

    return retval;
}

int32_t qc_handle_connect(CPUState *cpu, int32_t sckt, struct sockaddr *g_addr,
                          socklen_t addrlen)
{
    struct sockaddr_in addr;

    VERIFY_FD(sckt);

    int retval = 0;

    if (addrlen > sizeof(addr)) {
        guest_svcs_errno = ENOMEM;
    } else {
        cpu_memory_rw_debug(cpu, (target_ulong) g_addr, (uint8_t*) &addr,
                            sizeof(addr), 0);

        if ((retval = connect(guest_svcs_fds[sckt], (struct sockaddr *) &addr,
                            addrlen)) < 0) {
            guest_svcs_errno = errno;
        } else {
            cpu_memory_rw_debug(cpu, (target_ulong) g_addr, (uint8_t*) &addr,
                                sizeof(addr), 1);
        }
    }

    return retval;
}

int32_t qc_handle_listen(CPUState *cpu, int32_t sckt, int32_t backlog)
{
    VERIFY_FD(sckt);

    int retval = 0;

    if ((retval = listen(guest_svcs_fds[sckt], backlog)) < 0) {
        guest_svcs_errno = errno;
    }

    return retval;
}

int32_t qc_handle_recv(CPUState *cpu, int32_t sckt, void *g_buffer,
                       size_t length, int32_t flags)
{
    VERIFY_FD(sckt);
    uint8_t buffer[MAX_BUF_SIZE];

    int retval = -1;

    // TODO: timeout
    if (length > MAX_BUF_SIZE) {
        guest_svcs_errno = ENOMEM;
    } else if ((retval = recv(guest_svcs_fds[sckt], buffer, length, flags)) <= 0) {
        guest_svcs_errno = errno;
    } else {
        cpu_memory_rw_debug(cpu, (target_ulong) g_buffer, buffer, retval, 1);
    }

    return retval;
}

int32_t qc_handle_send(CPUState *cpu, int32_t sckt, void *g_buffer,
                       size_t length, int32_t flags)
{
    VERIFY_FD(sckt);
    uint8_t buffer[MAX_BUF_SIZE];

    int retval = -1;

    if (length > MAX_BUF_SIZE) {
        guest_svcs_errno = ENOMEM;
    } else {
        cpu_memory_rw_debug(cpu, (target_ulong) g_buffer, buffer, length, 0);

        if ((retval = send(guest_svcs_fds[sckt], buffer, length, flags)) < 0) {
            guest_svcs_errno = errno;
        }
    }

    return retval;
}
