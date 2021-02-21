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

#include "qemu/osdep.h"
#include "hw/loader.h"
#include "hw/display/xnu_iomfb_ramfb.h"
#include "ui/console.h"
#include "cpu.h"
#include "hw/arm/guest-services/xnu_general.h"

static void xnu_iomfb_ramfb_display_update(void *opaque)
{
    xnu_iomfb_ramfb_state *state = (xnu_iomfb_ramfb_state *)(opaque);
    dpy_gfx_update_full(state->con);
}

static const GraphicHwOps wrapper_ops = {
    .gfx_update = xnu_iomfb_ramfb_display_update,
};

static void xnu_iomfb_ramfb_unmap_display_surface(pixman_image_t *image,
                                                  void *unused)
{
    void *data = pixman_image_get_data(image);
    uint32_t size = pixman_image_get_stride(image) *
        pixman_image_get_height(image);
    cpu_physical_memory_unmap(data, size, 0, 0);
}

void xnu_iomfb_ramfb_init(xnu_iomfb_ramfb_state *state, hwaddr fb_paddr)
{
    uint32_t linesize = XNU_FB_WIDTH * 4;
    hwaddr mapsize = MAX_FB_SIZE;

    void *data = cpu_physical_memory_map(fb_paddr, &mapsize, false);
    assert(MAX_FB_SIZE == mapsize);

    DisplaySurface *surface = qemu_create_displaysurface_from(XNU_FB_WIDTH,
                                                              XNU_FB_HEIGHT,
                                                              FB_FORMAT,
                                                              linesize, data);
    assert(NULL != surface);
    pixman_image_set_destroy_function(surface->image,
                                      xnu_iomfb_ramfb_unmap_display_surface,
                                      NULL);

    state->con = (void *)graphic_console_init(NULL, 0, &wrapper_ops, state);
    assert(NULL != state->con);
    dpy_gfx_replace_surface(state->con, surface);
}
