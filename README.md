

# iOS on QEMU

This project is a fork of the official QEMU repository. Please refer to this [README](https://github.com/qemu/qemu/blob/master/README.rst) for information about the QEMU project.

The goal of this project is to boot a fully functional iOS system on QEMU.

*The project is under active development, follow [@alephsecurity] and [@JonathanAfek] for updates.*

For technical information about the research, follow our blog:
- [Running iOS in QEMU to an interactive bash shell (1)]
- [Running iOS in QEMU to an interactive bash shell (2)]
- [Tunnelling TCP connections into iOS on QEMU]
- [Accelerating iOS on QEMU with hardware virtualization (KVM)]

**Help is wanted!**

If you are passionate about iOS and kernel exploitation and want to help us push this project forward, please refer to the open issues in this repo :)


---
- Current project's functionality:
  - `launchd` services
  - Interactive bash
  - R/W secondary disk device
  - Execution of binaries (also ones that are not signed by Apple)
  - SSH through TCP tunneling
  - Textual FrameBuffer
  - ASLR for usermode apps is disabled
  - ASLR for DYLD shared cache is disabled
  - GDB scripts for kernel debugging
  - KVM support
  - TFP0 from user mode applications

- To run iOS 12.1 on QEMU follow this [tutorial](https://github.com/alephsecurity/xnu-qemu-arm64/wiki/Build-iOS-on-QEMU).

- This project works on QEMU with KVM! Check [this blog post]() for more information.

- We have implemented multiple GDB [scripts](https://github.com/alephsecurity/xnu-qemu-arm64-tools/tree/master/gdb) that will help you to debug the kernel:
  - List current/user/all tasks in XNU kernel.
  - List current/user/all threads in XNU kernel.
  - Print the information about specific task/thread.
  - Many more :).

- To disable ASLR in DYLD shared cache follow this [tutorial](https://github.com/alephsecurity/xnu-qemu-arm64/wiki/Disable-ASLR-for-dyld_shared_cache-load).

- Follow [here](https://alephsecurity.com/2020/03/29/xnu-qemu-tcp-tunnel/) to learn about how we've implemented the TCP tunneling.

- Follow the [code](https://github.com/alephsecurity/xnu-qemu-arm64/blob/master/hw/arm/n66_iphone6splus.c) to see all the patches we've made to the iOS kernel for this project:
  - Disable the Secure Monitor.
  - Bypass iOS's CoreTrust mechanism.
  - Disable ASLR for user mode apps.
  - Enable custom code execution in the kernel to load our own IOKit iOS drivers.
  - Enable KVM support.
  - Support getting TFP0 in usermode applications.
  

[Running iOS in QEMU to an interactive bash shell (1)]: https://alephsecurity.com/2019/06/17/xnu-qemu-arm64-1/
[Running iOS in QEMU to an interactive bash shell (2)]: https://alephsecurity.com/2019/06/25/xnu-qemu-arm64-2/
[Tunnelling TCP connections into iOS on QEMU]: https://alephsecurity.com/2020/03/29/xnu-qemu-tcp-tunnel/
[Accelerating iOS on QEMU with hardware virtualization (KVM)]: https://alephsecurity.com/2020/07/19/xnu-qemu-kvm/
[@alephsecurity]: https://twitter.com/alephsecurity
[@JonathanAfek]: https://twitter.com/JonathanAfek

