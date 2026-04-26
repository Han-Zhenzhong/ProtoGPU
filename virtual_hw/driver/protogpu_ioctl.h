#pragma once

#include <linux/ioctl.h>

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#include "protogpu/uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  PROTOGPU_IOCTL_UAPI_VERSION_V1 = 1u,
};

enum {
  PROTOGPU_IOCTL_FD_PATH_MAX = 108,
};

typedef struct protogpu_ioctl_create_ctx {
  uint32_t uapi_version;
  uint32_t flags;
  uint64_t ctx_id;
} protogpu_ioctl_create_ctx;

typedef struct protogpu_ioctl_destroy_ctx {
  uint64_t ctx_id;
} protogpu_ioctl_destroy_ctx;

typedef struct protogpu_ioctl_alloc_bo {
  uint64_t ctx_id;
  uint64_t bytes;
  uint64_t align;
  uint32_t flags;
  uint32_t reserved0;
  uint64_t handle;
  uint64_t mmap_offset;
} protogpu_ioctl_alloc_bo;

typedef struct protogpu_ioctl_free_bo {
  uint64_t ctx_id;
  uint64_t handle;
} protogpu_ioctl_free_bo;

typedef struct protogpu_ioctl_submit_job {
  uint64_t ctx_id;
  uint64_t job_id;
  protogpu_submit_job job;
  uint64_t arena_user_ptr;
  uint64_t arena_bytes;
} protogpu_ioctl_submit_job;

typedef struct protogpu_ioctl_wait_job {
  uint64_t ctx_id;
  uint64_t job_id;
  uint64_t timeout_ns;
  uint32_t status;
  uint32_t reserved0;
  uint64_t steps;
} protogpu_ioctl_wait_job;

typedef struct protogpu_ioctl_query_job {
  uint64_t ctx_id;
  uint64_t job_id;
  uint32_t status;
  uint32_t reserved0;
  uint64_t steps;
  uint64_t diagnostic_bytes;
  uint64_t trace_bytes;
  uint64_t stats_bytes;
} protogpu_ioctl_query_job;

typedef struct protogpu_ioctl_read_diag {
  uint64_t ctx_id;
  uint64_t job_id;
  uint64_t user_ptr;
  uint64_t capacity;
  uint64_t bytes_written;
} protogpu_ioctl_read_diag;

typedef struct protogpu_ioctl_set_broker {
  char unix_socket_path[PROTOGPU_IOCTL_FD_PATH_MAX];
} protogpu_ioctl_set_broker;

#define PROTOGPU_IOCTL_BASE 'G'

#define PROTOGPU_IOCTL_CREATE_CTX _IOWR(PROTOGPU_IOCTL_BASE, 0x01, protogpu_ioctl_create_ctx)
#define PROTOGPU_IOCTL_DESTROY_CTX _IOW(PROTOGPU_IOCTL_BASE, 0x02, protogpu_ioctl_destroy_ctx)
#define PROTOGPU_IOCTL_ALLOC_BO _IOWR(PROTOGPU_IOCTL_BASE, 0x03, protogpu_ioctl_alloc_bo)
#define PROTOGPU_IOCTL_FREE_BO _IOW(PROTOGPU_IOCTL_BASE, 0x04, protogpu_ioctl_free_bo)
#define PROTOGPU_IOCTL_SUBMIT_JOB _IOWR(PROTOGPU_IOCTL_BASE, 0x05, protogpu_ioctl_submit_job)
#define PROTOGPU_IOCTL_WAIT_JOB _IOWR(PROTOGPU_IOCTL_BASE, 0x06, protogpu_ioctl_wait_job)
#define PROTOGPU_IOCTL_QUERY_JOB _IOWR(PROTOGPU_IOCTL_BASE, 0x07, protogpu_ioctl_query_job)
#define PROTOGPU_IOCTL_READ_DIAG _IOWR(PROTOGPU_IOCTL_BASE, 0x08, protogpu_ioctl_read_diag)
#define PROTOGPU_IOCTL_SET_BROKER _IOW(PROTOGPU_IOCTL_BASE, 0x09, protogpu_ioctl_set_broker)

#ifdef __cplusplus
}  // extern "C"
#endif
