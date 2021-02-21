/*
 * QEMU Host file guest access
 *
 * Copyright (c) 2020 Jonathan Afek <jonyafek@me.com>
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

#include "hw/arm/guest-services/file.h"
#include "cpu.h"

static int32_t file_fds[MAX_FILE_FDS] = { [0 ... MAX_FILE_FDS-1] = -1 };

void qc_file_open(uint64_t index, const char *filename)
{
    if (index >= MAX_FILE_FDS) {
        fprintf(stderr, "qc_file_open(): index above MAX_FILE_FDS\n");
        abort();
    }
    if (-1 != file_fds[index]) {
        fprintf(stderr, "qc_file_open(): fd already open at index\n");
        abort();
    }
    file_fds[index] = open(filename, O_RDWR);
    if (-1 == file_fds[index]) {
        fprintf(stderr, "qc_file_open(): open() failed\n");
        abort();
    }
}

int64_t qc_handle_write_file(CPUState *cpu, uint64_t buffer_guest_paddr,
                             uint64_t length, uint64_t offset, uint64_t index)
{
    if (index >= MAX_FILE_FDS) {
        fprintf(stderr, "qc_handle_write_file(): index above MAX_FILE_FDS\n");
        abort();
    }
    int fd = file_fds[index];
    if (-1 == fd) {
        fprintf(stderr, "qc_handle_write_file(): no fd at given index\n");
        abort();
    }
    if (offset != lseek(fd, offset, SEEK_SET)) {
        fprintf(stderr, "qc_handle_write_file(): lseek() failed\n");
        abort();
    }
    if (length > MAX_FILE_TRANSACTION_LEN) {
        fprintf(stderr,
                "qc_handle_write_file(): trans len above max: 0x%016llx\n",
                length);
        abort();
    }
    uint64_t mapsize = length;
    void *data = cpu_physical_memory_map(buffer_guest_paddr, &mapsize, false);
    assert(length == mapsize);
    //TODO: JONATHANA in the future consider using mmap instead of read/write
    //for performance
    if (length != write(fd, data, length)) {
        fprintf(stderr, "qc_handle_write_file(): write() failed\n");
        abort();
    }
    cpu_physical_memory_unmap(data, mapsize, 0, 0);
    return 0;
}

int64_t qc_handle_read_file(CPUState *cpu, uint64_t buffer_guest_paddr,
                            uint64_t length, uint64_t offset, uint64_t index)
{
    if (index >= MAX_FILE_FDS) {
        fprintf(stderr, "qc_handle_read_file(): index above MAX_FILE_FDS\n");
        abort();
    }
    int fd = file_fds[index];
    if (-1 == fd) {
        fprintf(stderr, "qc_handle_read_file(): no fd at given index\n");
        abort();
    }
    if (offset != lseek(fd, offset, SEEK_SET)) {
        fprintf(stderr, "qc_handle_read_file(): lseek() failed\n");
        abort();
    }
    if (length > MAX_FILE_TRANSACTION_LEN) {
        fprintf(stderr,
                "qc_handle_read_file(): trans len above max: 0x%016llx\n",
                length);
        abort();
    }
    uint64_t mapsize = length;
    void *data = cpu_physical_memory_map(buffer_guest_paddr, &mapsize, true);
    assert(length == mapsize);
    //TODO: JONATHANA in the future consider using mmap instead of read/write
    //for performance
    if (length != read(fd, data, length)) {
        fprintf(stderr, "qc_handle_read_file(): read() failed\n");
        abort();
    }
    cpu_physical_memory_unmap(data, mapsize, 0, 0);
    return 0;
}

int64_t qc_handle_size_file(uint64_t index)
{
    struct stat st;

    if (index >= MAX_FILE_FDS) {
        fprintf(stderr, "qc_handle_size_file(): index above MAX_FILE_FDS\n");
        abort();
    }
    int fd = file_fds[index];
    if (-1 == fd) {
        fprintf(stderr, "qc_handle_size_file(): fd is -1\n");
        abort();
    }
    if (-1 == fstat(fd, &st)) {
        fprintf(stderr, "qc_handle_size_file(): fstat() failed\n");
        abort();
    }

    return st.st_size;
}
