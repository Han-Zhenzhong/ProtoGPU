With QEMU, the clean flow is:

```text
WSL host
  ├── Linux kernel source/build tree
  ├── your driver source
  ├── rootfs image
  └── QEMU VM running that kernel
```

## 1. Install tools in WSL

```bash
sudo apt update
sudo apt install build-essential git flex bison libssl-dev libelf-dev bc \
                 qemu-system-x86 initramfs-tools cpio dwarves
```

## 2. Download Linux kernel source

Example:

```bash
mkdir -p ~/kernel-dev
cd ~/kernel-dev
git clone --depth=1 https://github.com/torvalds/linux.git
cd linux
```

Or use a stable version:

```bash
git clone --depth=1 --branch v6.8 https://github.com/torvalds/linux.git linux-6.8
cd linux-6.8
```

## 3. Configure and build kernel

For simple QEMU testing:

```bash
make defconfig
make -j$(nproc)
```

After build, kernel image is usually:

```bash
arch/x86/boot/bzImage
```

## 4. Compile your module against this kernel tree

ProtoGPU driver is in:

```bash
~/ProtoGPU/virtual_hw/driver/protogpu_drv.c
~/ProtoGPU/virtual_hw/driver/Makefile
```

Makefile:

```makefile
obj-m += protogpu_drv.o
ccflags-y += -I$(src) -I$(src)/../include

KDIR ?= ~/kernel-dev/linux/
PWD := $(shell pwd)

all:
        $(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
        $(MAKE) -C $(KDIR) M=$(PWD) clean
```

Then:

```bash
cd ~/ProtoGPU/virtual_hw/driver
make
```

You should get:

```bash
protogpu_drv.ko
```

Key rule:

```text
The module must be built against the same kernel tree that QEMU boots.
```

## 5. Prepare a small root filesystem

Simplest way: use BusyBox.

```bash
cd ~/kernel-dev
wget https://busybox.net/downloads/busybox-1.36.1.tar.bz2
tar xf busybox-1.36.1.tar.bz2
cd busybox-1.36.1
make defconfig
make menuconfig
```

In menuconfig, enable:

```text
Settings  --->
  [*] Build static binary
```

Then:

```bash
make -j$(nproc)
make install
```

Create rootfs:

```bash
cd ~/kernel-dev
mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,tmp,lib/modules}
cp -a busybox-1.36.1/_install/* rootfs/
```

Create init script:

```bash
cat > rootfs/init <<'EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo "QEMU Linux started"
sh
EOF

chmod +x rootfs/init
```

Copy your module into rootfs:

```bash
mkdir -p rootfs/root
cp ~/ProtoGPU/virtual_hw/driver/protogpu_drv.ko rootfs/root/
```

Package initramfs:

```bash
cd rootfs
find . | cpio -H newc -o | gzip > ../rootfs.cpio.gz
cd ..
```

## 6. Boot kernel in QEMU

```bash
qemu-system-x86_64 \
  -kernel ~/kernel-dev/linux/arch/x86/boot/bzImage \
  -initrd ~/kernel-dev/rootfs.cpio.gz \
  -append "console=ttyS0 init=/init" \
  -nographic
```

Inside QEMU, test:

```bash
uname -a
cd /root
insmod protogpu_drv.ko
dmesg
rmmod protogpu_drv
dmesg
```

Exit QEMU:

```bash
poweroff
```

or press:

```text
Ctrl+A, then X
```

## Important note

If your module uses `modprobe`, dependencies, or `/lib/modules/$(uname -r)`, you need to install modules into rootfs:

```bash
cd ~/kernel-dev/linux
make modules_install INSTALL_MOD_PATH=~/kernel-dev/rootfs
```

Then rebuild initramfs:

```bash
cd ~/kernel-dev/rootfs
find . | cpio -H newc -o | gzip > ../rootfs.cpio.gz
```

For a simple learning driver, `insmod protogpu_drv.ko` is enough.

## 7. Run user-space app to call driver and gpu sim

After `insmod` succeeds, run two user-space processes in the guest:

- broker daemon (`protogpu-vhw-brokerd`) to execute jobs with ProtoGPU runtime
- ioctl client app/tests to talk to `/dev/protogpu0`

### 7.1 Build user-space binaries on host

```bash
cd ~/ProtoGPU
cmake --build build --target protogpu-vhw-brokerd protogpu-vhw-ioctl-sample protogpu-vhw-device-node-tests
```

### 7.2 Copy binaries into rootfs

```bash
mkdir -p ~/kernel-dev/rootfs/root/protogpu
cp ~/ProtoGPU/build/protogpu-vhw-brokerd ~/kernel-dev/rootfs/root/protogpu/
cp ~/ProtoGPU/build/protogpu-vhw-ioctl-sample ~/kernel-dev/rootfs/root/protogpu/
cp ~/ProtoGPU/build/protogpu-vhw-device-node-tests ~/kernel-dev/rootfs/root/protogpu/
cp ~/ProtoGPU/virtual_hw/driver/protogpu_drv.ko ~/kernel-dev/rootfs/root/
```

If your rootfs is BusyBox-only (dynamic libs missing), check and copy required shared libraries:

```bash
ldd ~/ProtoGPU/build/protogpu-vhw-brokerd
ldd ~/ProtoGPU/build/protogpu-vhw-ioctl-sample
```

For a typical x86_64 Ubuntu/WSL host, copy these runtime files into matching rootfs paths:

```bash
mkdir -p ~/kernel-dev/rootfs/lib64
mkdir -p ~/kernel-dev/rootfs/lib/x86_64-linux-gnu

cp -av /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 ~/kernel-dev/rootfs/lib/x86_64-linux-gnu/
rm -f ~/kernel-dev/rootfs/lib64/ld-linux-x86-64.so.2
ln -s ../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 ~/kernel-dev/rootfs/lib64/ld-linux-x86-64.so.2
cp -av /lib/x86_64-linux-gnu/libstdc++.so.6* ~/kernel-dev/rootfs/lib/x86_64-linux-gnu/
cp -av /lib/x86_64-linux-gnu/libm.so.6 ~/kernel-dev/rootfs/lib/x86_64-linux-gnu/
cp -av /lib/x86_64-linux-gnu/libgcc_s.so.1 ~/kernel-dev/rootfs/lib/x86_64-linux-gnu/
cp -av /lib/x86_64-linux-gnu/libc.so.6 ~/kernel-dev/rootfs/lib/x86_64-linux-gnu/

chmod 755 ~/kernel-dev/rootfs/root/protogpu/protogpu-vhw-brokerd
chmod 755 ~/kernel-dev/rootfs/root/protogpu/protogpu-vhw-ioctl-sample
chmod 755 ~/kernel-dev/rootfs/root/protogpu/protogpu-vhw-device-node-tests
```

Notes:

- these libs come from host userspace (`/lib` and `/lib64`), not from `~/kernel-dev/linux`
- if `ldd` shows extra libs on your system, copy those too into the same relative paths under `~/kernel-dev/rootfs`
- `sh: ... not found` for an existing binary usually means loader/ELF runtime issue, not missing file

For easily debugging GPU sim execution in qemu, copy debugability enabling script to rootfs and give the script execution permission.

```bash
cp ~/ProtoGPU/virtual_hw/demo/enable_all_debuggabilities_in_qemu.sh ~/kernel-dev/rootfs/root/protogpu/

chmod +x ~/kernel-dev/rootfs/root/protogpu/enable_all_debuggabilities_in_qemu.sh
```

### 7.3 Repack initramfs

```bash
cd ~/kernel-dev/rootfs
find . | cpio -H newc -o | gzip > ../rootfs.cpio.gz
cd ..
```

### 7.4 Boot QEMU and load driver

```bash
qemu-system-x86_64 \
  -kernel ~/kernel-dev/linux/arch/x86/boot/bzImage \
  -initrd ~/kernel-dev/rootfs.cpio.gz \
  -append "console=ttyS0 init=/init" \
  -nographic
```

Inside guest:

```bash
insmod /root/protogpu_drv.ko
lsmod
ls -l /dev/protogpu0
```

### 7.5 Start broker daemon in guest

Inside guest:

```bash
mkdir -p /tmp
ls -l /root/protogpu/protogpu-vhw-brokerd
ls -l /lib64/ld-linux-x86-64.so.2
ls -l /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
ls -l /lib/x86_64-linux-gnu/libstdc++.so.6
/root/protogpu/protogpu-vhw-brokerd /tmp/protogpu-broker.sock &
```

### 7.6 Run client app/tests in guest

Inside guest:

```bash
/root/protogpu/enable_all_debuggabilities_in_qemu.sh

/root/protogpu/protogpu-vhw-ioctl-sample
/root/protogpu/protogpu-vhw-device-node-tests
```

Expected:

- sample prints created context/BO, submit/wait status, diagnostic text, and mapped output value
- tests print `OK` instead of `SKIP`

### 7.7 Quick troubleshooting

- `open /dev/protogpu0 failed`: module not loaded or device node missing
- `broker transport failed: connect failed`: broker daemon not running or socket path mismatch
- `No such file or directory` when executing binary: missing dynamic loader/shared libraries in rootfs
- `/lib64/ld-linux-x86-64.so.2` exists but binary still not found: check symlink target `/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2` also exists
- runtime/simulation error: inspect diagnostic text returned by `READ_DIAG`
