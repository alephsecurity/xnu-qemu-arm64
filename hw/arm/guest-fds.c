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

#include <stdarg.h>

#include "hw/arm/guest-services/fds.h"
#include "cpu.h"

int32_t guest_svcs_fds[MAX_FD_COUNT] = { [0 ... MAX_FD_COUNT-1] = -1 };

int32_t qc_handle_close(CPUState *cpu, int32_t fd)
{
    VERIFY_FD(fd);

    int retval = -1;

    if ((retval = close(guest_svcs_fds[fd])) < 0) {
        guest_svcs_errno = errno;
    } else {
        // TODO: should this be in the "else" clause, or performed regardless?
        guest_svcs_fds[fd] = -1;
    }

    return retval;
}

int32_t qc_handle_fcntl_getfl(CPUState *cpu, int32_t fd)
{
    VERIFY_FD(fd);

    int retval = -1;

    if ((retval = fcntl(guest_svcs_fds[fd], F_GETFL)) < 0) {
        guest_svcs_errno = errno;
    }

    return retval;
}

int32_t qc_handle_fcntl_setfl(CPUState *cpu, int32_t fd, int32_t flags)
{
    VERIFY_FD(fd);

    int retval = -1;

    if ((retval = fcntl(guest_svcs_fds[fd], F_SETFL, flags)) < 0) {
        guest_svcs_errno = errno;
    }

    return retval;
}
