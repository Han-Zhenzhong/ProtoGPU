#pragma once

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  PROTOGPU_UAPI_VERSION_V1 = 1u,
};

typedef enum protogpu_status_code {
  PROTOGPU_STATUS_OK = 0,
  PROTOGPU_STATUS_INVALID_JOB = 1,
  PROTOGPU_STATUS_INVALID_BUFFER = 2,
  PROTOGPU_STATUS_INVALID_ARGUMENT = 3,
  PROTOGPU_STATUS_RUNTIME_ERROR = 4,
  PROTOGPU_STATUS_SIMULATION_ERROR = 5,
} protogpu_status_code;

typedef enum protogpu_arg_kind {
  PROTOGPU_ARG_U32 = 1,
  PROTOGPU_ARG_U64 = 2,
  PROTOGPU_ARG_BUFFER_HANDLE = 3,
  PROTOGPU_ARG_BYTES = 4,
} protogpu_arg_kind;

typedef enum protogpu_buffer_flags {
  PROTOGPU_BUFFER_INPUT = 1u << 0,
  PROTOGPU_BUFFER_OUTPUT = 1u << 1,
} protogpu_buffer_flags;

typedef struct protogpu_dim3 {
  uint32_t x;
  uint32_t y;
  uint32_t z;
} protogpu_dim3;

typedef struct protogpu_blob_ref {
  uint64_t offset;
  uint64_t size;
} protogpu_blob_ref;

typedef struct protogpu_record_ref {
  uint64_t offset;
  uint32_t count;
  uint32_t reserved;
} protogpu_record_ref;

typedef struct protogpu_arg_desc {
  uint32_t kind;
  uint32_t reserved;
  uint64_t value;
  protogpu_blob_ref bytes;
} protogpu_arg_desc;

typedef struct protogpu_buffer_binding {
  uint64_t handle;
  uint32_t flags;
  uint32_t reserved;
} protogpu_buffer_binding;

typedef struct protogpu_module_desc {
  protogpu_blob_ref ptx_text;
  protogpu_blob_ref ptx_isa_json;
  protogpu_blob_ref inst_desc_json;
  protogpu_blob_ref config_json;
  protogpu_blob_ref entry_name;
} protogpu_module_desc;

typedef struct protogpu_launch_desc {
  protogpu_dim3 grid_dim;
  protogpu_dim3 block_dim;
  uint64_t shared_mem_bytes;
  protogpu_record_ref args;
} protogpu_launch_desc;

typedef struct protogpu_submit_job {
  uint32_t uapi_version;
  uint32_t reserved0;
  protogpu_module_desc module;
  protogpu_launch_desc launch;
  protogpu_record_ref buffer_bindings;
} protogpu_submit_job;

#ifdef __cplusplus
}  // extern "C"
#endif
