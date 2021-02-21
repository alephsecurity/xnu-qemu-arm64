/*
 * IOMobileFrameBuffer - framebuffer qemu support
 *
 * Copyright (c) 2021 Jonathan Afek <jonyafek@me.com>
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

#ifndef HW_DISPLAY_XNU_IOMFB_RAMFB_H
#define HW_DISPLAY_XNU_IOMFB_RAMFB_H

//TODO: JONATHANA hardcoded for now, change later
//#define MAX_FB_SIZE (0x1DC000)
#define MAX_FB_SIZE (0x1E4000)
#define FB_FORMAT (PIXMAN_LE_a8r8g8b8)

typedef struct xnu_iomfb_ramfb_state {
    QemuConsole *con;
} xnu_iomfb_ramfb_state;

void xnu_iomfb_ramfb_init(xnu_iomfb_ramfb_state *state, hwaddr fb_paddr);

#endif /* HW_DISPLAY_XNU_IOMFB_RAMFB_H */

