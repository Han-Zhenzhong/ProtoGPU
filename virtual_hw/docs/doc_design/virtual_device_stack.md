# Virtual Hardware Stack Design

## 1. Purpose

This document defines a virtual hardware architecture for exercising Linux driver development while keeping ProtoGPU unchanged as the execution engine.

The design goals are:

- present a driver-managed virtual GPU device to user programs
- allow user programs to call the driver UAPI directly from C or C++
- keep PTX parsing, JSON asset loading, and semantic execution in user space
- reuse ProtoGPU through its in-memory API instead of through the CLI in the steady-state design

This document intentionally does not redesign ProtoGPU itself.

## 2. Non-goals

- no PTX parsing or JSON parsing in kernel space
- no attempt to move ProtoGPU into the kernel
- no CUDA runtime compatibility layer in v1
- no attempt to model a full production GPU driver stack such as DRM scheduling, VM, or firmware management in v1

## 3. Top-Level Architecture

The system is split into four layers.

```text
User Application
  |
  | open/ioctl/mmap/poll
  v
Linux Kernel Driver (/dev/protogpu0)
  |
  | shared command/completion transport
  v
Broker / Backend Daemon
  |
  | C++ API call
  v
ProtoGPU Runtime
```

### 3.1 User Application

The user writes ordinary C or C++ code and calls the driver UAPI directly with system calls. There is no CUDA-like runtime layer in this design.

The application is responsible for preparing:

- PTX text
- entry name
- config JSON
- PTX ISA JSON
- instruction descriptor JSON
- launch dimensions
- kernel argument descriptors or a packed argument blob
- input and output buffer contents

### 3.2 Kernel Driver

The driver owns:

- device node lifetime
- per-open context lifetime
- buffer object allocation and mapping
- job submission and completion tracking
- synchronization primitives such as job IDs or fences
- the transport boundary to the broker

The driver does not understand PTX semantics. It treats kernel code and execution assets as opaque payloads.

### 3.3 Broker / Backend Daemon

The broker is a user-space process that implements the virtual device execution side.

Its responsibilities are:

- connect to the driver as the executor for pending jobs
- read job descriptors and referenced payloads
- translate submitted objects into ProtoGPU API inputs
- invoke ProtoGPU through the in-memory runtime API
- write results, diagnostics, trace, and stats back to shared buffers
- mark job completion back to the driver

This daemon is the reason the kernel driver can remain small while ProtoGPU stays unchanged in user space.

### 3.4 ProtoGPU Runtime

ProtoGPU remains the execution engine. The preferred call boundary is the in-memory API that accepts PTX text, JSON asset text, entry name, arguments, and launch dimensions.

In particular, the broker should target the API shape represented by `Runtime::run_ptx_kernel_with_args_text_entry_launch(...)`.

The CLI is acceptable only as an early bring-up fallback.

## 4. Why a Broker Exists

The kernel cannot sensibly own ProtoGPU execution because ProtoGPU depends on:

- normal user-space C++ runtime facilities
- PTX parsing and binder logic
- JSON asset loading
- higher-level error reporting and trace generation

The broker exists to bridge these worlds:

- the driver exports a kernel UAPI and manages kernel-owned resources
- the broker translates kernel jobs into user-space execution
- ProtoGPU performs the actual semantic execution

## 5. Programming Model

### 5.1 Application Model

The application uses the driver directly. A typical user flow is:

1. `open("/dev/protogpu0", ...)`
2. create a context
3. allocate one or more buffer objects
4. `mmap` the buffer objects into user space
5. write input bytes into mapped buffers
6. submit a module object containing PTX and asset sources
7. submit a launch object containing entry name, dimensions, and args
8. wait for job completion
9. read output bytes from mapped buffers
10. query diagnostics, trace, or stats if desired

### 5.2 Broker Model

The broker runs as a long-lived daemon. It repeatedly:

1. waits for executable jobs from the driver
2. reconstructs the job payloads in memory
3. invokes ProtoGPU
4. writes completion state and output payloads

## 6. Driver UAPI Overview

The v1 UAPI should stay small and explicit.

### 6.1 Required Operations

- `CREATE_CTX`
- `DESTROY_CTX`
- `ALLOC_BO`
- `FREE_BO`
- `SUBMIT_MODULE`
- `SUBMIT_LAUNCH`
- `WAIT_JOB`
- `QUERY_JOB`
- `READ_DIAG`

If desired, `SUBMIT_MODULE` and `SUBMIT_LAUNCH` can be collapsed into one `SUBMIT_JOB` in v1.

### 6.2 Buffer Objects

Each buffer object should expose:

- buffer handle
- size in bytes
- mmap offset token
- flags

The application uses `mmap` to populate or read the buffer contents. This avoids repeated copying through `ioctl` payloads.

### 6.3 Job Identity

Each submitted launch should return a stable job ID. The job ID is used for:

- wait operations
- status queries
- diagnostics lookup
- trace or stats lookup

## 7. Data Model

There are three classes of transferred data.

### 7.1 Control Data

Small structured metadata transmitted through `ioctl` or a command ring:

- context ID
- buffer handle references
- module ID
- job ID
- entry name length and location
- grid and block dimensions
- shared memory bytes
- argument descriptor count
- status and completion flags

### 7.2 Payload Data

Variable-sized data usually placed in shared buffers:

- PTX text
- config JSON text
- PTX ISA JSON text
- instruction descriptor JSON text
- packed kernel arg blob or arg descriptor array
- application input data
- application output data
- diagnostic text
- trace bytes
- stats bytes

### 7.3 Completion Data

Small completion records:

- job state
- success or failure code
- diagnostic availability
- trace availability
- stats availability

## 8. What Must Be Prepared Before a Launch

Without a runtime layer, the application must prepare the full execution request.

### 8.1 Code and Assets

The application must provide:

- PTX text
- entry name
- config JSON text or path
- PTX ISA JSON text or path
- instruction descriptor JSON text or path

The recommended v1 choice is to pass text blobs, not paths. Text blobs make the UAPI self-contained and deterministic.

### 8.2 Launch Descriptor

The application must provide:

- `grid_dim = {x, y, z}`
- `block_dim = {x, y, z}`
- optional dynamic shared memory bytes

### 8.3 Kernel Arguments

Two viable v1 representations exist.

#### Option A: Typed Arg Descriptors

Each argument is described as one of:

- `U32`
- `U64`
- `BUFFER_HANDLE`
- `BYTES`

This is simpler for direct user code.

#### Option B: Packed Arg Blob

The application provides a final packed parameter blob plus a small relocation list for buffer handles.

This is closer to ProtoGPU's internal API, but harder for user code.

Recommended v1 choice:

- use typed arg descriptors in the UAPI
- let the broker translate them into ProtoGPU `KernelArgs`

### 8.4 Buffers

The application must allocate all input and output buffers before launch and populate input buffers through the mapped memory view.

## 9. Driver-to-Broker Interaction

The driver does not call into user space directly. Instead, the broker connects to the driver through a defined transport and consumes work.

### 9.1 Recommended Transport

The recommended steady-state design is:

- shared memory command ring
- shared memory completion ring
- `eventfd` or pollable notification fd for wakeups

This is the closest to a queue-based hardware programming model.

### 9.2 Simpler Bring-Up Transport

For early development, a simpler transport is acceptable:

- a control device node or Unix domain socket for commands
- shared memory file descriptors for large payloads

This is easier to debug before the ring design is in place.

### 9.3 Command Ownership Model

The driver owns queue metadata and resource lifetime. The broker owns execution.

The sequence is:

1. application submits a job to the driver
2. driver validates handles and queue state
3. driver publishes a command record into the broker-visible transport
4. broker reads the command record
5. broker executes the job in ProtoGPU
6. broker publishes completion record and output metadata
7. driver signals the waiting application

## 10. Broker-to-ProtoGPU Interaction

The broker should use the ProtoGPU API, not the CLI, for the steady-state design.

### 10.1 Why API Is Preferred

- avoids temp files
- avoids subprocess management
- keeps job data in memory
- allows direct mapping from driver objects to ProtoGPU runtime objects
- simplifies completion and error propagation

### 10.2 Broker Execution Steps

For a kernel launch, the broker performs:

1. read PTX text and asset text from the submitted payloads
2. resolve the entry name
3. build the ProtoGPU launch config
4. translate UAPI argument descriptors into `KernelArgs`
5. invoke `Runtime::run_ptx_kernel_with_args_text_entry_launch(...)`
6. inspect completion and diagnostic outputs
7. copy generated output bytes into driver-owned shared buffers
8. return status, trace, and stats metadata

### 10.3 CLI as a Bring-Up Fallback

Using `gpu-sim-cli` is acceptable only to bootstrap the stack quickly. It should not be the long-term design because it introduces file staging, process management, and extra translation layers.

## 11. End-to-End Launch Sequence

```text
Application
  open + create ctx
  alloc + mmap buffers
  fill input buffers
  submit module
  submit launch
  wait job
  read output buffers

Driver
  validate submission
  publish command to broker
  sleep until completion
  wake application

Broker
  fetch command
  map payloads
  call ProtoGPU API
  write results
  publish completion
```

Detailed sequence:

1. the application allocates input and output buffers
2. the application submits PTX and asset texts as a module payload
3. the application submits a launch payload that references the module and buffers
4. the driver records the job and makes it visible to the broker
5. the broker reconstructs a ProtoGPU launch request
6. the broker invokes ProtoGPU
7. ProtoGPU executes and returns result state
8. the broker copies output bytes and metadata to shared buffers
9. the broker marks completion
10. the driver returns completion to the application

## 12. Error Model

Errors should be classified by where they arise.

### 12.1 Driver Errors

- invalid handle
- invalid buffer range
- malformed submission record
- unsupported UAPI version
- broker unavailable

### 12.2 Broker Errors

- malformed payload translation
- unsupported argument representation
- ProtoGPU API invocation failure

### 12.3 ProtoGPU Errors

- entry not found
- launch dimension error
- missing ISA mapping
- missing descriptor entry
- workload or argument mismatch

ProtoGPU-originated diagnostics should be preserved as faithfully as possible and surfaced through `READ_DIAG`.

## 13. Bring-Up Plan

### Phase 1: Minimal Path

- single device node
- single context per open file descriptor
- one blocking `SUBMIT_JOB`
- one broker daemon
- one in-flight job at a time
- API-based broker invocation

### Phase 2: Buffer Mapping

- real buffer objects
- `mmap` support
- separate input and output buffers
- explicit wait API

### Phase 3: Queue Model

- shared command ring
- completion ring
- multi-job submission
- eventfd or poll notification

### Phase 4: Better Observability

- structured diagnostics
- trace export
- stats export

## 14. Recommended v1 Choices

To keep implementation small while preserving a sound architecture, v1 should use the following choices.

- application calls the driver directly with `open`, `ioctl`, `mmap`, and `poll`
- PTX and JSON assets are submitted as text blobs
- kernel args are submitted as typed descriptors
- buffer contents move through mapped shared memory
- the driver publishes jobs to a broker-visible queue
- the broker calls ProtoGPU through its in-memory API
- diagnostics, trace, and stats are treated as optional output payloads

## 15. Summary

The virtual hardware design keeps the Linux driver and ProtoGPU cleanly separated.

- the driver owns resource management and synchronization
- the broker owns job translation and execution
- ProtoGPU remains an unchanged user-space execution engine

This split provides real Linux driver development work without forcing ProtoGPU into an unnatural kernel boundary.