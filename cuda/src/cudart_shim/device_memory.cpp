#include "cudart_shim/device_memory.h"

#include "cudart_shim/error_state.h"

#include "gpusim/runtime.h"

#include <cstring>
#include <vector>

namespace gpusim_cudart_shim {

std::optional<Allocation> DeviceMemory::find_allocation_containing(gpusim::DevicePtr ptr) const {
  auto it = allocs_.upper_bound(ptr);
  if (it == allocs_.begin()) return std::nullopt;
  --it;
  const auto& a = it->second;
  if (ptr < a.base) return std::nullopt;
  const auto off = ptr - a.base;
  if (off >= a.bytes) return std::nullopt;
  return a;
}

bool DeviceMemory::is_valid_device_range(gpusim::DevicePtr ptr, std::uint64_t bytes) const {
  auto a = find_allocation_containing(ptr);
  if (!a) return false;
  const auto off = ptr - a->base;
  return off + bytes <= a->bytes;
}

cudaError_t DeviceMemory::malloc(gpusim::Runtime& rt, void** out_dev_ptr, std::size_t bytes) {
  if (!out_dev_ptr) return cudaErrorInvalidValue;
  if (bytes == 0) {
    *out_dev_ptr = nullptr;
    return cudaSuccess;
  }

  try {
    const std::uint64_t align = 16;
    auto p = rt.device_malloc(static_cast<std::uint64_t>(bytes), align);
    allocs_[p] = Allocation{ p, static_cast<std::uint64_t>(bytes), align };
    *out_dev_ptr = encode_device_ptr(p);
    return cudaSuccess;
  } catch (const std::exception& ex) {
    ErrorState::tls().set(cudaErrorMemoryAllocation, ex.what());
    return cudaErrorMemoryAllocation;
  }
}

cudaError_t DeviceMemory::free(void* dev_ptr) {
  if (!dev_ptr) return cudaSuccess;
  auto p = decode_device_ptr(dev_ptr);
  auto it = allocs_.find(p);
  if (it == allocs_.end()) return cudaErrorInvalidDevicePointer;
  allocs_.erase(it);
  // TODO: seems there is a memory leak.
  return cudaSuccess;
}

cudaError_t DeviceMemory::memcpy(gpusim::Runtime& rt,
                                void* dst,
                                const void* src,
                                std::size_t bytes,
                                cudaMemcpyKind kind) {
  if (bytes == 0) return cudaSuccess;
  if (!dst || !src) return cudaErrorInvalidValue;

  try {
    if (kind == cudaMemcpyHostToDevice) {
      const auto dst_dev = decode_device_ptr(dst);
      if (!is_valid_device_range(dst_dev, bytes)) return cudaErrorInvalidDevicePointer;

      auto host = rt.host_alloc(static_cast<std::uint64_t>(bytes));
      std::vector<std::uint8_t> tmp(bytes);
      std::memcpy(tmp.data(), src, bytes);
      rt.host_write(host, 0, tmp);
      rt.memcpy_h2d(dst_dev, host, 0, static_cast<std::uint64_t>(bytes));
      return cudaSuccess;
    }

    if (kind == cudaMemcpyDeviceToHost) {
      const auto src_dev = decode_device_ptr(src);
      if (!is_valid_device_range(src_dev, bytes)) return cudaErrorInvalidDevicePointer;

      auto host = rt.host_alloc(static_cast<std::uint64_t>(bytes));
      rt.memcpy_d2h(host, 0, src_dev, static_cast<std::uint64_t>(bytes));
      auto data = rt.host_read(host, 0, static_cast<std::uint64_t>(bytes));
      if (!data) return cudaErrorUnknown;
      std::memcpy(dst, data->data(), bytes);
      return cudaSuccess;
    }

    if (kind == cudaMemcpyDeviceToDevice) {
      const auto dst_dev = decode_device_ptr(dst);
      const auto src_dev = decode_device_ptr(src);
      if (!is_valid_device_range(dst_dev, bytes)) return cudaErrorInvalidDevicePointer;
      if (!is_valid_device_range(src_dev, bytes)) return cudaErrorInvalidDevicePointer;

      auto host = rt.host_alloc(static_cast<std::uint64_t>(bytes));
      rt.memcpy_d2h(host, 0, src_dev, static_cast<std::uint64_t>(bytes));
      rt.memcpy_h2d(dst_dev, host, 0, static_cast<std::uint64_t>(bytes));
      return cudaSuccess;
    }

    if (kind == cudaMemcpyHostToHost) {
      std::memcpy(dst, src, bytes);
      return cudaSuccess;
    }

    return cudaErrorInvalidMemcpyDirection;
  } catch (const std::exception& ex) {
    ErrorState::tls().set(cudaErrorUnknown, ex.what());
    return cudaErrorUnknown;
  }
}

} // namespace gpusim_cudart_shim
