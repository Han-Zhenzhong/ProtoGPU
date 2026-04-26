# ProtoGPU Virtual HW Driver Skeleton

This directory contains an out-of-tree Linux kernel module skeleton for `/dev/protogpu0`.

## What this skeleton implements

- Character device node via `miscdevice`:
  - `/dev/protogpu0`
- Ioctl handling from `protogpu_ioctl.h` for:
  - `PROTOGPU_IOCTL_CREATE_CTX`
  - `PROTOGPU_IOCTL_DESTROY_CTX`
  - `PROTOGPU_IOCTL_SET_BROKER`
  - `PROTOGPU_IOCTL_ALLOC_BO`
  - `PROTOGPU_IOCTL_FREE_BO`
  - `PROTOGPU_IOCTL_SUBMIT_JOB`
  - `PROTOGPU_IOCTL_WAIT_JOB`
  - `PROTOGPU_IOCTL_QUERY_JOB`
  - `PROTOGPU_IOCTL_READ_DIAG`

This is intentionally a skeleton:

- only a minimal blocking one-job-at-a-time model is implemented
- trace/stats payload retrieval is not wired yet (diagnostics are wired)

Implemented in this phase:

- `SUBMIT_JOB` forwards requests to the configured broker Unix socket using the same wire format as `protogpu-vhw-remote-demo`
- response status, steps, and diagnostics are propagated into `WAIT_JOB` / `QUERY_JOB` / `READ_DIAG`
- BOs have vmalloc-backed storage and are mappable via `mmap` using `ALLOC_BO.mmap_offset`

## Build

From this directory:

```bash
make
```

If you are not using "/lib/modules/$(shell uname -r)/build" as linux root folder to build this driver, specify your own linux root folder when execute make, command like "make KDIR=/your_own_linux_root_folder".

## Load/Unload

```bash
sudo insmod protogpu_drv.ko
ls -l /dev/protogpu0

sudo rmmod protogpu_drv
```

## Quick user-space checks

From repo root:

```bash
./build/protogpu-vhw-ioctl-sample
./build/protogpu-vhw-device-node-tests
```

Both tools will skip/fail gracefully if `/dev/protogpu0` is not present.
