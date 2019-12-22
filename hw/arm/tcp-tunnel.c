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

#include "hw/arm/tcp-tunnel.h"

static pthread_mutex_t tunnel_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct {
    int socket;
    int closed;
} connections[] = { {
    .socket = 0,
    .closed = 1,
} };

uint64_t tcp_tunnel_ready_to_send(CPUARMState *env, const ARMCPRegInfo *ri)
{
    // Return value: 0 when ready to send, non-zero otherwises
    uint64_t res;

    pthread_mutex_lock(&tunnel_mutex);
    res = (connections[0].socket && !connections[0].closed);
    pthread_mutex_unlock(&tunnel_mutex);

    return res;
}

void tcp_tunnel_send(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    uint64_t guest_addr = value;
    tcp_tunnel_buffer_t buf;
    uint16_t size = (value & (0xfffULL << 48)) >> 48;
    CPUState *cpu = qemu_get_cpu(0);

    if (value & (1ULL << 63)) {
        guest_addr |= (0xffffULL << 48);
    } else {
        guest_addr &= ~(0xffffULL << 48);
    }

    // Read the request
    cpu_memory_rw_debug(cpu, guest_addr, (uint8_t*) &buf, size, 0);

    // TODO: handle socket errors
    pthread_mutex_lock(&tunnel_mutex);
    if (connections[0].socket && !connections[0].closed) {
        if ((buf.header.len = send(connections[0].socket, buf.data, buf.header.len, 0)) < 0) {
            buf.header.err = errno;
            connections[0].closed = 1;
        } else {
            buf.header.err = 0;
        }
    } else {
        buf.header.len = -1;
        buf.header.err = ECONNRESET;
    }
    pthread_mutex_unlock(&tunnel_mutex);

    // Write back the status
    cpu_memory_rw_debug(cpu, guest_addr, (uint8_t*) &buf.header, sizeof(buf.header), 1);
}

uint64_t tcp_tunnel_ready_to_recv(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint64_t res;

    pthread_mutex_lock(&tunnel_mutex);
    res = (connections[0].socket && !connections[0].closed);
    pthread_mutex_unlock(&tunnel_mutex);

    return res;
}

void tcp_tunnel_recv(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    uint64_t guest_addr = value;
    uint16_t size = (value & (0xfffULL << 48)) >> 48;
    tcp_tunnel_buffer_t buf;
    CPUState *cpu = qemu_get_cpu(0);

    if (value & (1ULL << 63)) {
        guest_addr |= (0xffffULL << 48);
    } else {
        guest_addr &= ~(0xffffULL << 48);
    }

    // TODO: handle socket errors
    pthread_mutex_lock(&tunnel_mutex);
    if (connections[0].socket && !connections[0].closed) {
        buf.header.len = recv(connections[0].socket, buf.data, sizeof(buf.data), 0);
        buf.header.err = 0;

        if (0 == buf.header.len) {
            buf.header.err = ECONNRESET;
        } else if (buf.header.len < 0) {
            buf.header.err = errno;
        }

        if (buf.header.err && (buf.header.err != EAGAIN)) {
            connections[0].closed = 1;
        }
    } else {
        buf.header.len = -1;
        buf.header.err = ECONNRESET;
    }

    pthread_mutex_unlock(&tunnel_mutex);

    cpu_memory_rw_debug(cpu, guest_addr, (uint8_t*) &buf, size, 1);
}

void *tunnel_accept_connection(void *arg)
{
    int keep_going = 1;
    int tunnel_server_socket = *(int*)arg;

    while (keep_going) {
        // TODO: support more concurrent connections, and design an exit strategy
        int tmp_socket = accept(tunnel_server_socket, NULL, NULL);

        struct timeval timeout = { .tv_usec = 10 }; // TODO: define!

        if (setsockopt(tmp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                       sizeof(timeout)) < 0) {
            perror("setsockopt failed");
            keep_going = false;
        }

        pthread_mutex_lock(&tunnel_mutex);
        connections[0].socket = tmp_socket;
        connections[0].closed = 0;
        pthread_mutex_unlock(&tunnel_mutex);

        for (;;) {
            pthread_mutex_lock(&tunnel_mutex);
            if (connections[0].closed) {
                pthread_mutex_unlock(&tunnel_mutex);
                break;
            }
            pthread_mutex_unlock(&tunnel_mutex);
            sleep(1); // TODO: define!
        }

        close(tmp_socket);
    }

    return NULL;
}
