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

pthread_mutex_t qemu_send_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t qemu_recv_mutex = PTHREAD_MUTEX_INITIALIZER;
uint8_t qemu_send_buf[4096];
uint8_t qemu_recv_buf[4096];

static void *guest_send_recv(void *arg)
{
    N66MachineState *nms = N66_MACHINE(((thread_arg_t*)arg)->nms);
    int sock = ((thread_arg_t*)arg)->socket;
    free(arg);

    uint8_t tmp[4096];
    time_t start;
    int len = 0;
    bool keep_going = true;
    struct timeval timeout = { .tv_usec = 10 }; // TODO: define!

    if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("setsockopt failed");
        keep_going = false;
    }

    while (keep_going) {
        // Receive...
        if ((len = recv(sock, tmp, sizeof(tmp), 0)) > 0) {
            start = time(NULL);
            while (len && ((time(NULL) - start) < QEMU_RECV_TIMEOUT)) {
                // we attempt to write the date to the guest; if the guest
                // hasn't yet read the previous data, we wait for
                // QEMU_RECV_TIMEOUT seconds, and then drop it
                pthread_mutex_lock(&qemu_recv_mutex);
                if (!nms->N66_CPREG_VAR_NAME(REG_QEMU_RECV)) {
                    nms->N66_CPREG_VAR_NAME(REG_QEMU_RECV) = len;
                    memcpy(qemu_recv_buf, tmp, len);
                    len = 0;
                }
                pthread_mutex_unlock(&qemu_recv_mutex);
                usleep(10); // TODO: define!
            }
        } else if (!len || (errno != EAGAIN)) {
            keep_going = false;
        }

        // Send...
        pthread_mutex_lock(&qemu_send_mutex);
        len = nms->N66_CPREG_VAR_NAME(REG_QEMU_SEND);
        nms->N66_CPREG_VAR_NAME(REG_QEMU_SEND) = 0;
        if (len){
            if (send(sock, qemu_send_buf, len, 0) < 0) {
                // connection closed or another error
                keep_going = false;
            }
        }
        pthread_mutex_unlock(&qemu_send_mutex);
    }

    close(sock);

    return NULL;
}

void *tunnel_accept_connection(void *arg)
{
    int tunnel_server_socket = ((thread_arg_t*)arg)->socket;
    void *nms = ((thread_arg_t*)arg)->nms;
    free(arg);

    pthread_t pt_guest_send_recv;
    thread_arg_t *thread_arg;

    while (1) {
        // TODO: support more concurrent connections, and design an exit strategy
        thread_arg = malloc(sizeof(*thread_arg));
        thread_arg->nms = nms;
        thread_arg->socket = accept(tunnel_server_socket, NULL, NULL);

        pthread_create(&pt_guest_send_recv, NULL, guest_send_recv, thread_arg);
        pthread_join(pt_guest_send_recv, NULL);
    }

    return NULL;
}
