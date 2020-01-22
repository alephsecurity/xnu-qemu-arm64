#ifndef HW_ARM_XNU_RAMFB_CFG_H
#define HW_ARM_XNU_RAMFB_CFG_H

#include "qemu/osdep.h"

#define RAMFB_SIZE 0x2D4C00

#define V_DEPTH     16
#define V_HEIGHT    800
#define V_WIDTH     600
#define V_DISPLAY   0
#define V_LINESIZE  (V_WIDTH * 3)

void xnu_define_ramfb_device(AddressSpace* as, hwaddr ramfb_pa);
void xnu_get_video_bootargs(void *opaque, hwaddr ramfb_pa);

#endif