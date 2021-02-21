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
#include "hw/arm/boot.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "hw/platform-bus.h"
#include "cpu.h"

#include "hw/arm/guest-services/general.h"

int32_t guest_svcs_errno = 0;
qemu_call_callback_t g_callback = NULL;
qemu_call_value_callback_t g_value_cb = NULL;
void *g_value_cb_opaque = NULL;
hwaddr g_vaddr_qemu_call = 0;
hwaddr g_paddr_qemu_call = 0;

void qemu_call_set_cmd_vaddr(hwaddr vaddr)
{
    g_vaddr_qemu_call = vaddr;
}

void qemu_call_set_cmd_paddr(hwaddr paddr)
{
    g_paddr_qemu_call = paddr;
}

void qemu_call_install_callback(qemu_call_callback_t callback)
{
    g_callback = callback;
}

void qemu_call_install_value_callback(qemu_call_value_callback_t callback,
                                      void *opaque)
{
    g_value_cb = callback;
    g_value_cb_opaque = opaque;
}

uint64_t qemu_call_status(CPUARMState *env, const ARMCPRegInfo *ri)
{
    //we want to make sure that the qcall address is set by the
    //machine before it is being used by the gueset
    assert(0 != g_vaddr_qemu_call);
    assert(0 != g_paddr_qemu_call);

    return g_vaddr_qemu_call;
}

void qemu_call(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    CPUState *cs = env_cpu(env);
    AddressSpace *as = cpu_get_address_space(cs, ARMASIdx_NS);
    qemu_call_t qcall;

    if (!value) {
        // Special case: not a regular QEMU call.
        if (NULL == g_callback) {
            fprintf(stderr,
                    "qemu_call withh value 0 but no callback installed\n");
            abort();
        }

        g_callback();
        return;
    }

    //TODO: JONATHANA to support multi-CPU need to lock here or to not use
    //a static qemu address for all transactions but use a different allocated
    //one for each transaction.


    //we want to make sure that the qcall address is set by the
    //machine before it is being used by the gueset
    assert(0 != g_vaddr_qemu_call);
    assert(0 != g_paddr_qemu_call);

    //we expect the value pasased is the same as the value configured by
    //the machine
    assert(value == g_vaddr_qemu_call);

    // Read the request
    address_space_rw(as, g_paddr_qemu_call, MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&qcall, sizeof(qcall), 0);

    switch (qcall.call_number) {
        // File Descriptors
        case QC_CLOSE:
            qcall.retval = qc_handle_close(cs, qcall.args.close.fd);
            break;
        case QC_FCNTL:
            switch (qcall.args.fcntl.cmd) {
                case F_GETFL:
                    qcall.retval = qc_handle_fcntl_getfl(
                        cs, qcall.args.fcntl.fd);
                    break;
                case F_SETFL:
                    qcall.retval = qc_handle_fcntl_setfl(
                        cs, qcall.args.fcntl.fd, qcall.args.fcntl.flags);
                    break;
                default:
                    guest_svcs_errno = EINVAL;
                    qcall.retval = -1;
            }
            break;

        // Socket API
        case QC_SOCKET:
            qcall.retval = qc_handle_socket(cs, qcall.args.socket.domain,
                                            qcall.args.socket.type,
                                            qcall.args.socket.protocol);
            break;
        case QC_ACCEPT:
            qcall.retval = qc_handle_accept(cs, qcall.args.accept.socket,
                                            qcall.args.accept.addr,
                                            qcall.args.accept.addrlen);
            break;
        case QC_BIND:
            qcall.retval = qc_handle_bind(cs, qcall.args.bind.socket,
                                          qcall.args.bind.addr,
                                          qcall.args.bind.addrlen);
            break;
        case QC_CONNECT:
            qcall.retval = qc_handle_connect(cs, qcall.args.connect.socket,
                                             qcall.args.connect.addr,
                                             qcall.args.connect.addrlen);
            break;
        case QC_LISTEN:
            qcall.retval = qc_handle_listen(cs, qcall.args.listen.socket,
                                            qcall.args.listen.backlog);
            break;
        case QC_RECV:
            qcall.retval = qc_handle_recv(cs, qcall.args.recv.socket,
                                          qcall.args.recv.buffer,
                                          qcall.args.recv.length,
                                          qcall.args.recv.flags);
            break;
        case QC_SEND:
            qcall.retval = qc_handle_send(cs, qcall.args.send.socket,
                                          qcall.args.send.buffer,
                                          qcall.args.send.length,
                                          qcall.args.send.flags);
            break;
        case QC_WRITE_FILE:
            qcall.retval = qc_handle_write_file(cs,
                                     qcall.args.write_file.buffer_guest_paddr,
                                     qcall.args.write_file.length,
                                     qcall.args.write_file.offset,
                                     qcall.args.write_file.index);
            break;
        case QC_READ_FILE:
            qcall.retval = qc_handle_read_file(cs,
                                     qcall.args.read_file.buffer_guest_paddr,
                                     qcall.args.read_file.length,
                                     qcall.args.read_file.offset,
                                     qcall.args.read_file.index);
            break;
        case QC_SIZE_FILE:
            qcall.retval = qc_handle_size_file(qcall.args.size_file.index);
            break;

        // General value callback
        case QC_VALUE_CB:
            assert(NULL != g_value_cb);
            qcall.retval = g_value_cb(&qcall.args.general, g_value_cb_opaque);
            break;
        default:
            // TODO: handle unknown call numbers
            break;
    }

    qcall.error = guest_svcs_errno;

    // Write the response
    address_space_rw(as, g_paddr_qemu_call, MEMTXATTRS_UNSPECIFIED,
                     (uint8_t *)&qcall, sizeof(qcall), 1);
}
