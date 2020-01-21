#ifndef HW_ARM_XNU_IOS_RAMFB_H
#define HW_ARM_XNU_IOS_RAMFB_H

#define RAMFB_SIZE 0x2D4C00

void xnu_define_ramfb_device(AddressSpace* as, hwaddr ramfb_pa);
void xnu_get_video_bootargs(void *opaque, hwaddr ramfb_pa);

#endif