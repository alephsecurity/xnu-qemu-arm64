

# iOS on QEMU

This project is a fork of the official QEMU repository. Please refer this [README](https://github.com/qemu/qemu/blob/master/README.rst) for information about QEMU project.

The goal of this project is to enable fully functional iOS being booted on QEMU.

*The project is under active development, follow [@alephsecurity] and [@JonathanAfek] for updates.*

For technical information about the research, follow our blog:
- [Running iOS in QEMU to an interactive bash shell (1)]
- [Running iOS in QEMU to an interactive bash shell (2)]

**Help is wanted!**

If you passionate about iOS and kernel exploitation and want to help us make the magic happen please refer the open issues in this repo and just PR to us with your awesome code :)

---
To start the process we first need to prepare a kernel image, a device tree, a static trust cache, the main and the secondary disk images.
To get the all that we first need to get the update file from Apple: [iOS 12.1 update file].
This is actually a zip file which we can extract:
```
$ unzip iPhone_5.5_12.1_16B92_Restore.ipsw
```

Next, we need to clone the supporting scripts repository:
```
$ git clone https://github.com/alephsecurity/xnu-qemu-arm64-tools-private.git
```
**Get the Kernel image**
Extract the ASN1 encoded kernel image ([pyasn1] should be installed first):
```
$ pip install pyasn1
$ python3 xnu-qemu-arm64-tools/bootstrap_scripts/asn1kerneldecode.py kernelcache.release.n66 kernelcache.release.n66.asn1decoded
```

This decoded image now includes the lzss compressed kernel.
You can use [this][lzss] code to decompress it or use this translated code with python*2*.
```
$ python2 xnu-qemu-arm64-tools/bootstrap_scripts/decompress_lzss.py kernelcache.release.n66.asn1decoded kernelcache.release.n66.out
```
**Get the divice tree**

Extract the device tree from the ASN1 encoded file:
```
$ python3 xnu-qemu-arm64-tools/bootstrap_scripts/asn1dtredecode.py Firmware/all_flash/DeviceTree.n66ap.im4p Firmware/all_flash/DeviceTree.n66ap.im4p.out
```

## Create the Disk Devices for iOS system

Some tweaks should be done to use the all currently implemented capabilities; bash, many familiar binary tools, all iOS's launchd services, r/w secondary disk device and SSH. 

The following instructions will describe how to create the disk devices and what changes should be made within them to enable the system start with all the functionality mentioned above.

### Create the primary disk device
To create a block device that will run on the system we will use ramdisk device available in the [iOS 12.1 update file]. 

The disk devices will be attached to the iOS system by *custom* block device driver.
Follow the instructions [here][Block Device Driver] to create the driver. Then copy the driver `aleph_bdev_drv.bin` to your work directory.

Next, decode the ramdisk and resize it. Attach the ramdisk device and the main disk image to the research computer.
```
$ python3 xnu-qemu-arm64-tools/bootstrap_scripts/asn1rdskdecode.py ./048-32651-104.dmg ./048-32651-104.dmg.out
$ cp ./048-32651-104.dmg.out ./hfs.main
$ hdiutil resize -size 6G -imagekey diskimage-class=CRawDiskImage ./hfs.main
$ hdiutil attach -imagekey diskimage-class=CRawDiskImage ./hfs.main
$ hdiutil attach ./048-31952-103.dmg 		//main disk image
```
Remove all contents of the ramdisk and sync the ramdisk with the main disk image (the latter will take some time).
```
$ sudo rm -rf /Volumes/PeaceB16B92.arm64UpdateRamDisk/*
$ sudo rsync -avhW --compress-level=0 --progress /Volumes/PeaceB16B92.N56N66OS/* /Volumes/PeaceB16B92.arm64UpdateRamDisk/
```
Remove contents of `/private/var`. We will put it to a secondary disk later.
```
$ sudo rm -rf /Volumes/PeaceB16B92.arm64UpdateRamDisk/private/var/*
```

**Get pre-compiled binaries**

We will use [rootlessJB] for pre-compiled binary tools (you can use any other project of your choice).
```
$ git clone https://github.com/jakeajames/rootlessJB
$ cd rootlessJB/rootlessJB/bootstrap/tars/
$ tar xvf iosbinpack.tar
$ sudo cp -R iosbinpack64 /Volumes/PeaceB16B92.arm64UpdateRamDisk/
```
**Add programs to be executed at system start**

Four executables will be added to ״Launch Daemons״ directory and start at the system load.
 
1) **bash** - run bash

Create the `plist` file and save it as `/Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/LaunchDaemons/bash.plist` 

```
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>EnablePressuredExit</key>
	<false/>
	<key>Label</key>
	<string>com.apple.bash</string>
	<key>POSIXSpawnType</key>
	<string>Interactive</string>
	<key>ProgramArguments</key>
	<array>
		<string>/iosbinpack64/bin/bash</string>
	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>StandardErrorPath</key>
	<string>/dev/console</string>
	<key>StandardInPath</key>
	<string>/dev/console</string>
	<key>StandardOutPath</key>
	<string>/dev/console</string>
	<key>Umask</key>
	<integer>0</integer>
	<key>UserName</key>
	<string>root</string>
</dict>
</plist>
```

1) **mount_sec** - mount the secondary block device (disk1) to primary block device (disk0).
   
Create the `plist` file and save it as `/Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/LaunchDaemons/mount_sec.plist` 
```
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleIdentifier</key>
	<string>com.apple.mount_sec</string>
	<key>EnablePressuredExit</key>
	<false/>
	<key>EnableTransactions</key>
	<false/>
	<key>HighPriorityIO</key>
	<true/>
	<key>Label</key>
	<string>mount_sec</string>
	<key>POSIXSpawnType</key>
	<string>Interactive</string>
	<key>ProgramArguments</key>
	<array>
		<string>/sbin/mount</string>
		<string>/private/var</string>
	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>Umask</key>
	<integer>0</integer>
	<key>UserName</key>
	<string>root</string>
</dict>
</plist>

```
  3) [tcptunnel] - opens TCP Tunnel on port 2222 between the host and the guest. SSH will run above this tunnel.

Create the `plist` file and save it as `/Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/LaunchDaemons/tcptunnel.plist` 
```
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleIdentifier</key>
	<string>com.apple.tcptunnel</string>
	<key>EnablePressuredExit</key>
	<false/>
	<key>EnableTransactions</key>
	<false/>
	<key>HighPriorityIO</key>
	<false/>
	<key>KeepAlive</key>
	<true/>
	<key>Label</key>
	<string>TcpTunnel</string>
	<key>POSIXSpawnType</key>
	<string>Interactive</string>
	<key>ProgramArguments</key>
	<array>
		<string>/bin/tunnel</string>
		<string>2222:127.0.0.1:22</string>
	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>Umask</key>
	<integer>0</integer>
	<key>UserName</key>
	<string>root</string>
</dict>
</plist>

```
4) [dropbear] - will be used as SSH server.

Create the `plist` file and save it as `/Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/LaunchDaemons/dropbear.plist` 
```
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleIdentifier</key>
	<string>com.apple.dropbear</string>
	<key>EnablePressuredExit</key>
	<false/>
	<key>EnableTransactions</key>
	<false/>
	<key>HighPriorityIO</key>
	<true/>
	<key>KeepAlive</key>
	<true/>
	<key>Label</key>
	<string>Dropbear</string>
	<key>POSIXSpawnType</key>
	<string>Interactive</string>
	<key>ProgramArguments</key>
	<array>
		<string>/iosbinpack64/usr/local/bin/dropbear</string>
		<string>--shell</string>
		<string>/iosbinpack64/bin/bash</string>
		<string>-R</string>
		<string>-E</string>
		<string>-F</string>
	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>Umask</key>
	<integer>0</integer>
	<key>UserName</key>
	<string>root</string>
</dict>
</plist>
```

As a side note, you can always convert the binary plist files that you find natively in iOS images to text xml format and back to binary format with:
```
$ plutil -convert xml1 file.plist
$ vim file.plist
$ plutil -convert binary1 file.plist
```
For launch daemon, iOS accepts both xml and binary plist files.

Now we need to make sure that we have all the binaries in the system according to their path in `ProgramArguments`.

`/iosbinpack64/bin/bash` - *part of the iosbinpack64*

`/sbin/mount` - *part of the iOS system* 

`/bin/tunnel` - *follow [this][tcptunnel] tutorial to get the binary and copy it to* `/bin`

`/iosbinpack64/usr/local/bin/dropbear` - *part of the iosbinpack64*

**Create the static Trust Cache**

Since the new binaries are signed, but not by Apple, they need to be trusted by the static trust cache that we will create. To do this, we need to get [jtool]. We have to run it on every binary we wish to be trusted, extract the first 40 characters of its CDHash, and put it in a new file named `tchashes`. A sample execution of jtool looks like this:
```
$ jtool --sig --ent /Volumes/PeaceB16B92.arm64UpdateRamDisk/iosbinpack64/bin/bash
Blob at offset: 1308032 (10912 bytes) is an embedded signature
Code Directory (10566 bytes)
				Version:     20001
				Flags:       none
				CodeLimit:   0x13f580
				Identifier:  /Users/jakejames/Desktop/jelbreks/multi_path/multi_path/iosbinpack64/bin/bash (0x58)
				CDHash:      7ad4d4c517938b6fdc0f5241cd300d17fbb52418b1a188e357148f8369bacad1 (computed)
				# of Hashes: 320 code + 5 special
				Hashes @326 size: 32 Type: SHA-256
 Empty requirement set (12 bytes)
 ```
In the above case, we need to write down `7ad4d4c517938b6fdc0f5241cd300d17fbb52418` in `tchashes` file.
For convenience, the following command will extract the needed part of the hash from each of the binaries in *iosbinpack64*:
```
$ touch ./tchashes
$ for filename in $(find /Volumes/PeaceB16B92.arm64UpdateRamDisk/iosbinpack64 -type f); do jtool --sig --ent $filename 2&>/dev/null; done | grep CDHash | cut -d' ' -f6 | cut -c 1-40 >> ./tchashes
```
Note that the `/bin/tunnel` that we've created before is not signed yet. Sign it with jtool.
```
$ sudo jtool --sign --ent ent.xml --inplace /Volumes/PeaceB16B92.arm64UpdateRamDisk/bin/tunnel
```
Example of `ent.xml` that will work in most cases:
```
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
	<dict>
		<key>platform-application</key>
		<true/>
		<key>com.apple.private.security.container-required</key>
		<false/>
	</dict>
</plist>
```
Add its hash to `tchashes` as well.
```
$ jtool --sig --ent /Volumes/PeaceB16B92.arm64UpdateRamDisk/bin/tunnel | grep CDHash | cut -d' ' -f6 | cut -c 1-40 >> ./tchashes
```
Now we can create the static trust cache blob:
```
$ python3 xnu-qemu-arm64-tools/bootstrap_scripts/create_trustcache.py tchashes static_tc
```
**General changes**

1. *Replace the* `fstab` *file*
```
$ sudo cp /Volumes/PeaceB16B92.arm64UpdateRamDisk/etc/fstab /Volumes/PeaceB16B92.arm64UpdateRamDisk/etc/fstab_orig
$ sudo vi /Volumes/PeaceB16B92.arm64UpdateRamDisk/etc/fstab
```
Remove the content from the file and copy the following
```
/dev/disk0 / hfs ro 0 1
/dev/disk1 /private/var hfs rw,nosuid,nodev 0 2
```
2. *Prevent from* `keybagd` *daemon to run on system launch*
```
$ sudo rm /Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/LaunchDaemons/com.apple.mobile.keybagd.plist
```
**Patch the launchd binary**

To get the `launchd` load the programs we had added before, instead of looking at the instructions in `xpcd_cache.dylib`, we need to patch the binary file.

We will demonstrate the patch on [Ghidra] (you can use any other disassembler of your choice).

Import the binary file `/Volumes/PeaceB16B92.arm64UpdateRamDisk/sbin/launchd` and analyze it.

![](https://user-images.githubusercontent.com/9990629/74609174-37659a00-50f0-11ea-9633-a85b32375092.png)

Patch the instruction at `0x10002fb18` from `cset w20,ne` to `mov w20,#0x01`

![](https://user-images.githubusercontent.com/9990629/74609219-b4910f00-50f0-11ea-8818-6c3ea3c41bd3.png)

![](https://user-images.githubusercontent.com/9990629/74609221-b8249600-50f0-11ea-8c83-851e554bf961.png)

Save and export the binary

![](https://user-images.githubusercontent.com/9990629/74609220-b78bff80-50f0-11ea-9826-b999f0a319f6.png)

Replace the original `launchd` with the patched binary
```
$ sudo mv /Volumes/PeaceB16B92.arm64UpdateRamDisk/sbin/launchd /Volumes/PeaceB16B92.arm64UpdateRamDisk/sbin/launchd.orig
$ sudo cp exported.bin /Volumes/PeaceB16B92.arm64UpdateRamDisk/sbin/launchd
```
Sign the binary with jtool and keep its identity.
```
$ sudo jtool --sign --ent ent.xml --ident com.apple.xpc.launchd --inplace /Volumes/PeaceB16B92.arm64UpdateRamDisk/sbin/launchd
```
Do not forget to ddd its hash to the `tchashes`.
```
$ jtool --sig --ent /Volumes/PeaceB16B92.arm64UpdateRamDisk/sbin/launchd | grep CDHash | cut -d' ' -f6 | cut -c 1-40 >> ./tchashes
```
Update the static_tc file:
```
$ python3 xnu-qemu-arm64-tools/bootstrap_scripts/create_trustcache.py tchashes static_tc
```

Now the disks can be ejected - we've done!
```
$ hdiutil detach /Volumes/PeaceB16B92.arm64UpdateRamDisk
$ hdiutil detach /Volumes/PeaceB16B92.N56N66OS
```

### Create the secondary disk device
As with the main disk let us use the ramdisk structure as well.
```
$ cp ./048-32651-104.dmg.out ./hfs.sec
$ hdiutil resize -size 6G -imagekey diskimage-class=CRawDiskImage ./hfs.sec
$ hdiutil attach -imagekey diskimage-class=CRawDiskImage ./hfs.sec
$ hdiutil attach ./048-31952-103.dmg
```
Remove all contents of the ramdisk and sync the ramdisk with `/private/var` directory from the main disk image.
```
$ sudo rm -rf /Volumes/PeaceB16B92.arm64UpdateRamDisk/*
$ sudo rsync -avhW --compress-level=0 --progress /Volumes/PeaceB16B92.N56N66OS/private/var /Volumes/PeaceB16B92.arm64UpdateRamDisk/
```
Create a directory for the dropbear
```
$ sudo mkdir /Volumes/PeaceB16B92.arm64UpdateRamDisk/dropbear
```
Eject the disks
```
$ hdiutil detach /Volumes/PeaceB16B92.arm64UpdateRamDisk
$ hdiutil detach /Volumes/PeaceB16B92.N56N66OS
```
---
We now have all the images and files prepared. Let's get the modified QEMU code (more detailed info on the work done in QEMU will be in the second post in the series):
```
$ git clone git@github.com:alephsecurity/xnu-qemu-arm64.git
```

and compile it:
```
$ cd xnu-qemu-arm64
$ ./configure --target-list=aarch64-softmmu --disable-capstone --disable-pie --disable-slirp
$ make -j16
$ cd -
```

And all there's left to do is execute:
```
$ xnu-qemu-arm64/aarch64-softmmu/qemu-system-aarch64 -M iPhone6splus-n66-s8000,kernel-filename=kernelcache.release.n66.out,dtb-filename=Firmware/all_flash/DeviceTree.n66ap.im4p.out,driver-filename=aleph_bdev_drv.bin,qc-file-0-filename=hfs.main,qc-file-1-filename=hfs.sec,tc-filename=static_tc,kern-cmd-args="debug=0x8 kextlog=0xfff cpus=1 rd=disk0 serial=2",xnu-ramfb=off -cpu max -m 6G -serial mon:stdio
```

To use the binaries in the `iosbinpack64` update the `PATH`
```
export PATH=$PATH:/iosbinpack64/usr/bin:/iosbinpack64/bin:/iosbinpack64/usr/sbin:/iosbinpack64/sbin 
```
And we have an interactive bash shell with mounted r/w disk and SSH enabled!!


\* `xnu-ramfb=on` for textual framebuffer

\* SSH password - `alpine`

:heavy_exclamation_mark: When exiting QEMU ensure to remount the `hfs.sec` (on the research computer), otherwise it won't be mounted on the next run and will fail to load

[zhuowei-tutorial]: https://worthdoingbadly.com/xnuqemu2/
[qemu-aleph-git]: https://github.com/alephsecurity/xnu-qemu-arm64
[qemu-scripts-aleph-git]: https://github.com/alephsecurity/xnu-qemu-arm64-too
[iOS 12.1 update file]: http://updates-http.cdn-apple.com/2018FallFCS/fullrestores/091-91479/964118EC-D4BE-11E8-BC75-A45C715A3354/iPhone_5.5_12.1_16B92_Restore.ipsw
[rootlessJB]: https://github.com/jakeajames/rootlessJB
[iOSBinaries]: http://newosxbook.com/tools/iOSBinaries.html
[azad-console-bin]: https://bazad.github.io/2018/04/xcode-command-line-targets-ios/
[jtool]: http://www.newosxbook.com/tools/jtool.html
[gdbserver-ios]: https://0xabe.io/howto/ios/debug/2015/11/01/remote-iOS-app-debugging-from-linux.html
[homebrew]: https://brew.sh/
[macports]: https://www.macports.org
[dyld-cache]: http://iphonedevwiki.net/index.php/Dyld_shared_cache
[Block Device Driver]: https://github.com/alephsecurity/xnu-qemu-arm64-tools-private/tree/master/aleph_bdev_drv
[tcptunnel]: https://github.com/alephsecurity/xnu-qemu-arm64-tools-private/tree/master/tcp-tunnel
[dropbear]: https://github.com/mkj/dropbear
[Ghidra]: https://ghidra-sre.org/
[Running iOS in QEMU to an interactive bash shell (1)]: https://alephsecurity.com/2019/06/17/xnu-qemu-arm64-1/
[Running iOS in QEMU to an interactive bash shell (2)]: https://alephsecurity.com/2019/06/25/xnu-qemu-arm64-2/
[@alephsecurity]: https://twitter.com/alephsecurity
[@JonathanAfek]: https://twitter.com/JonathanAfek
[pyasn1]: https://pypi.org/project/pyasn1/
[lzss]: https://opensource.apple.com/source/BootX/BootX-75/bootx.tproj/sl.subproj/lzss.c
