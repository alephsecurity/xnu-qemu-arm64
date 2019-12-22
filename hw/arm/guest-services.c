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

#include "hw/arm/guest-services/general.h"

uint64_t qemu_call_status(CPUARMState *env, const ARMCPRegInfo *ri)
{
    // NOT USED FOR NOW
    return 0;
}

void qemu_call(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    CPUState *cpu = qemu_get_cpu(0);
    qemu_call_t qc_call;

    // Read the request
    cpu_memory_rw_debug(cpu, value, (uint8_t*) &qc_call, sizeof(qc_call), 0);

    switch (qc_call.call_number) {
        case QC_SOCKET:
            qc_call.retval = qc_socket(cpu, qc_call.args.socket.domain,
                                       qc_call.args.socket.type,
                                       qc_call.args.socket.protocol);
            break;
        case QC_ACCEPT:
            qc_call.retval = qc_accept(cpu, qc_call.args.accept.socket,
                                       qc_call.args.accept.addr,
                                       qc_call.args.accept.addrlen);
            break;
        case QC_BIND:
            qc_call.retval = qc_bind(cpu, qc_call.args.bind.socket,
                                     qc_call.args.bind.addr,
                                     qc_call.args.bind.addrlen);
            break;
        case QC_CONNECT:
            qc_call.retval = qc_connect(cpu, qc_call.args.connect.socket,
                                        qc_call.args.connect.addr,
                                        qc_call.args.connect.addrlen);
            break;
        case QC_LISTEN:
            qc_call.retval = qc_listen(cpu, qc_call.args.listen.socket,
                                       qc_call.args.listen.backlog);
            break;
        case QC_RECV:
            qc_call.retval = qc_recv(cpu, qc_call.args.recv.socket,
                                     qc_call.args.recv.buffer,
                                     qc_call.args.recv.length,
                                     qc_call.args.recv.flags);
            break;
        case QC_SEND:
            qc_call.retval = qc_send(cpu, qc_call.args.send.socket,
                                     qc_call.args.send.buffer,
                                     qc_call.args.send.length,
                                     qc_call.args.send.flags);
            break;
        case QC_CLOSE:
            qc_call.retval = qc_close(cpu, qc_call.args.close.socket);
            break;
        default:
            // TODO: handle unknown call numbers
            break;
    }

    qc_call.error = qemu_socket_errno;

    // Write the response
    cpu_memory_rw_debug(cpu, value, (uint8_t*) &qc_call, sizeof(qc_call), 1);
}

/*
void qemu_call(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    uint64_t guest_addr = value;
    tcp_tunnel_buffer_t buf;
    CPUState *cpu = qemu_get_cpu(0);

    // Read the request
    cpu_memory_rw_debug(cpu, value, (uint8_t*) &buf, size, 0);

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
*/