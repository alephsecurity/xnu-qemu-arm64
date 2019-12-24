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
#include "hw/arm/guest-services/general.h"

int32_t qemu_errno = 0;

uint64_t qemu_call_status(CPUARMState *env, const ARMCPRegInfo *ri)
{
    // NOT USED FOR NOW
    return 0;
}

void qemu_call(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    CPUState *cpu = qemu_get_cpu(0);
    qemu_call_t qcall;

    // Read the request
    cpu_memory_rw_debug(cpu, value, (uint8_t*) &qcall, sizeof(qcall), 0);

    switch (qcall.call_number) {
        // File Descriptors
        case QC_CLOSE:
            qcall.retval = qc_handle_close(cpu, qcall.args.close.fd);
            break;
        case QC_FCNTL:
            switch (qcall.args.fcntl.cmd) {
                case F_GETFL:
                    qcall.retval = qc_handle_fcntl_getfl(
                        cpu, qcall.args.fcntl.fd);
                    break;
                case F_SETFL:
                    qcall.retval = qc_handle_fcntl_setfl(
                        cpu, qcall.args.fcntl.fd, qcall.args.fcntl.flags);
                    break;
                default:
                    qemu_errno = EINVAL;
                    qcall.retval = -1;
            }
            break;

        // Socket API
        case QC_SOCKET:
            qcall.retval = qc_handle_socket(cpu, qcall.args.socket.domain,
                                            qcall.args.socket.type,
                                            qcall.args.socket.protocol);
            break;
        case QC_ACCEPT:
            qcall.retval = qc_handle_accept(cpu, qcall.args.accept.socket,
                                            qcall.args.accept.addr,
                                            qcall.args.accept.addrlen);
            break;
        case QC_BIND:
            qcall.retval = qc_handle_bind(cpu, qcall.args.bind.socket,
                                          qcall.args.bind.addr,
                                          qcall.args.bind.addrlen);
            break;
        case QC_CONNECT:
            qcall.retval = qc_handle_connect(cpu, qcall.args.connect.socket,
                                             qcall.args.connect.addr,
                                             qcall.args.connect.addrlen);
            break;
        case QC_LISTEN:
            qcall.retval = qc_handle_listen(cpu, qcall.args.listen.socket,
                                            qcall.args.listen.backlog);
            break;
        case QC_RECV:
            qcall.retval = qc_handle_recv(cpu, qcall.args.recv.socket,
                                          qcall.args.recv.buffer,
                                          qcall.args.recv.length,
                                          qcall.args.recv.flags);
            break;
        case QC_SEND:
            qcall.retval = qc_handle_send(cpu, qcall.args.send.socket,
                                          qcall.args.send.buffer,
                                          qcall.args.send.length,
                                          qcall.args.send.flags);
            break;
        default:
            // TODO: handle unknown call numbers
            break;
    }

    qcall.error = qemu_errno;

    // Write the response
    cpu_memory_rw_debug(cpu, value, (uint8_t*) &qcall, sizeof(qcall), 1);
}
