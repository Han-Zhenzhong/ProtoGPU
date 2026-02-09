#include "gpusim/memory.h"

#include <algorithm>

#include <mutex>

namespace gpusim {

static std::uint64_t align_up(std::uint64_t v, std::uint64_t a) {
  if (a == 0) return v;
  auto r = v % a;
  return r == 0 ? v : (v + (a - r));
}

bool AddrSpaceManager::global_range_allocated_nolock(std::uint64_t addr, std::uint64_t size) const {
  if (size == 0) return true;
  const auto end = addr + size;
  if (end < addr) return false; // overflow

  for (const auto& a : allocs_) {
    const auto a_end = a.base + a.size;
    if (a_end < a.base) continue; // overflow / corrupt
    if (addr >= a.base && end <= a_end) return true;
  }
  return false;
}

DevicePtr AddrSpaceManager::alloc_global(std::uint64_t bytes, std::uint64_t align) {
  std::lock_guard<std::mutex> lock(mu_);
  if (align == 0) align = 1;
  next_global_addr_ = align_up(next_global_addr_, align);
  auto base = next_global_addr_;
  next_global_addr_ += std::max<std::uint64_t>(bytes, 1);
  allocs_.push_back(Allocation{ base, bytes });
  return base;
}

bool AddrSpaceManager::write_global(std::uint64_t addr, const std::vector<std::uint8_t>& bytes) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!global_range_allocated_nolock(addr, static_cast<std::uint64_t>(bytes.size()))) return false;
  for (std::size_t i = 0; i < bytes.size(); i++) {
    global_[addr + static_cast<std::uint64_t>(i)] = bytes[i];
  }
  return true;
}

std::optional<std::vector<std::uint8_t>> AddrSpaceManager::read_global(std::uint64_t addr, std::uint64_t size) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!global_range_allocated_nolock(addr, size)) return std::nullopt;
  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(size));
  for (std::uint64_t i = 0; i < size; i++) {
    auto it = global_.find(addr + i);
    // Sparse global memory: treat unwritten bytes within an allocated range as 0.
    out.push_back(it == global_.end() ? static_cast<std::uint8_t>(0) : it->second);
  }
  return out;
}

void AddrSpaceManager::set_param_layout(const std::vector<ParamDesc>& layout) {
  std::lock_guard<std::mutex> lock(mu_);
  param_layout_.clear();
  for (const auto& p : layout) {
    param_layout_.emplace(p.name, p);
  }
}

void AddrSpaceManager::set_param_blob(const std::vector<std::uint8_t>& blob) {
  std::lock_guard<std::mutex> lock(mu_);
  param_blob_ = blob;
}

std::optional<std::vector<std::uint8_t>> AddrSpaceManager::read_param_symbol(const std::string& name, std::uint64_t size) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = param_layout_.find(name);
  if (it == param_layout_.end()) return std::nullopt;
  const auto& p = it->second;
  auto off = static_cast<std::uint64_t>(p.offset);
  if (off + size > param_blob_.size()) return std::nullopt;
  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(size));
  for (std::uint64_t i = 0; i < size; i++) {
    out.push_back(param_blob_[static_cast<std::size_t>(off + i)]);
  }
  return out;
}

} // namespace gpusim
