
# iOS on QEMU

This project is a fork of the official QEMU repository. Please refer this [README](https://github.com/qemu/qemu/blob/master/README.rst) for information about QEMU project.

The goal of this project is to enable fully functional iOS being booted on QEMU.

The project is under active development, follow [@alephsecurity](https://twitter.com/alephsecurity)  and [@JonathanAfek](https://twitter.com/JonathanAfek) for updates.

For technical information about the research, follow our blog:
- [Running iOS in QEMU to an interactive bash shell (1)](https://alephsecurity.com/2019/06/17/xnu-qemu-arm64-1/)
- [Running iOS in QEMU to an interactive bash shell (2)](https://alephsecurity.com/2019/06/25/xnu-qemu-arm64-2/)


Help is wanted!
If you passionate about iOS and kernel exploitation and want to help us make the magic happen please refer the open issues in this repo and just PR to us with your awesome code :)

## Run iOS on QEMU ##

To start the process we first need to prepare a kernel image, a device tree, a static trust cache, and ram disk images.
To get the images we first need to get the update file from Apple: [iOS 12.1 update file].
This is actually a zip file which we can extract:
```
$ unzip iPhone_5.5_12.1_16B92_Restore.ipsw
```

Next, we need to clone the supporting scripts repository:
```
$ git clone git@github.com:alephsecurity/xnu-qemu-arm64-tools.git
```

And extract the ASN1 encoded kernel image:
```
$ python xnu-qemu-arm64-tools/asn1kerneldecode.py kernelcache.release.n66 kernelcache.release.n66.asn1decoded
```

This decoded image now includes the compressed kernel and the secure monitor image. To extract both of them:
```
$ python xnu-qemu-arm64-tools/decompress_lzss.py kernelcache.release.n66.asn1decoded kernelcache.release.n66.out
```

Now let's prepare a device tree which we can boot with (more details about the device tree in the second post).
First, extract it from the ASN1 encoded file:
```
$ python xnu-qemu-arm64-tools/asn1dtredecode.py Firmware/all_flash/DeviceTree.n66ap.im4p Firmware/all_flash/DeviceTree.n66ap.im4p.out
```

Now we have to set up the ram disk. First, ASN1 decode it:
```
$ python xnu-qemu-arm64-tools/asn1rdskdecode.py ./048-32651-104.dmg ./048-32651-104.dmg.out
```

Next, resize it so it has room for the [dynamic loader cache file][dyld-cache] (needed by bash and other executables), mount it, and force usage of file permissions on it:
```
$ hdiutil resize -size 1.5G -imagekey diskimage-class=CRawDiskImage 048-32651-104.dmg.out
$ hdiutil attach -imagekey diskimage-class=CRawDiskImage 048-32651-104.dmg.out
$ sudo diskutil enableownership /Volumes/PeaceB16B92.arm64UpdateRamDisk/
```

Now let's mount the regular update disk image by double clicking on it: `048-31952-103.dmg`

Create a directory for the dynamic loader cache in the ram disk, copy the cache from the update image and chown it to root:
```
$ sudo mkdir -p /Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/Caches/com.apple.dyld/
$ sudo cp /Volumes/PeaceB16B92.N56N66OS/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64 /Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/Caches/com.apple.dyld/
$ sudo chown root /Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64
```

Get precompiled user mode tools for iOS, including bash, from [rootlessJB][rootlessJB] and/or [iOSBinaries][iOSBinaries]. Alternatively, compile your own iOS console binaries as described [here][azad-console-bin].
```
$ git clone https://github.com/jakeajames/rootlessJB
$ cd rootlessJB/rootlessJB/bootstrap/tars/
$ tar xvf iosbinpack.tar
$ sudo cp -R iosbinpack64 /Volumes/PeaceB16B92.arm64UpdateRamDisk/
$ cd -
```

Configure launchd to not execute any services:
```
$ sudo rm /Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/LaunchDaemons/*
```

And now, configure it to launch the interactive bash shell by creating a new file under `/Volumes/PeaceB16B92.arm64UpdateRamDisk/System/Library/LaunchDaemons/com.apple.bash.plist` with the following contents:
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

As a side note, you can always convert the binary plist files that you find natively in iOS images to text xml format and back to binary format with:
```
$ plutil -convert xml1 file.plist
$ vim file.plist
$ plutil -convert binary1 file.plist
```

For launch daemon, iOS accepts both xml and binary plist files.

Since the new binaries are signed, but not by Apple, they need to be trusted by the static trust cache that we will create. To do this, we need to get [jtool][jtool] (also available via [Homebrew][homebrew]: `brew cask install jtool`). Once we have the tool, we have to run it on every binary we wish to be trusted, extract the first 40 characters of its CDHash, and put it in a new file named `tchashes`. A sample execution of jtool looks like this:
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
In the above case, we need to write down `7ad4d4c517938b6fdc0f5241cd300d17fbb52418` in `tchashes`.
For convenience, the following command will extract the correct part of the hash from each of the binaries we put in the image:
```
$ for filename in $(find /Volumes/PeaceB16B92.arm64UpdateRamDisk/iosbinpack64 -type f); do jtool --sig --ent $filename 2&>/dev/null; done | grep CDHash | cut -d' ' -f6 | cut -c 1-40
```

The output of above command should be saved in `tchashes`, and then we can  create the static trust cache blob:
```
$ python xnu-qemu-arm64-tools/create_trustcache.py tchashes static_tc
```

Now is a good time to unmount both volumes.
We now have all the images and files prepared. Let's get the modified QEMU code (more detailed info on the work done in QEMU will be in the second post in the series):
```
$ git clone git@github.com:alephsecurity/xnu-qemu-arm64.git
```

and compile it:
```
$ cd xnu-qemu-arm64
$ ./configure --target-list=aarch64-softmmu --disable-capstone
$ make -j16
$ cd -
```

And all there's left to do is execute:
```
$ ./xnu-qemu-arm64/aarch64-softmmu/qemu-system-aarch64 -M iPhone6splus-n66-s8000,kernel-filename=kernelcache.release.n66.out,dtb-filename=Firmware/all_flash/DeviceTree.n66ap.im4p.out,ramdisk-filename=048-32651-104.dmg.out,tc-filename=static_tc,kern-cmd-args="debug=0x8 kextlog=0xfff cpus=1 rd=md0 serial=2" -cpu max -m 6G -serial mon:stdio
```

And we have an interactive bash shell! :)

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
