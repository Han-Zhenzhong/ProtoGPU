#include "gpusim/contracts.h"

#include <iomanip>
#include <sstream>

namespace gpusim {

LaneMask lane_mask_all(std::uint32_t warp_size) {
  LaneMask m;
  m.width = warp_size;
  if (warp_size >= 32) {
    m.bits_lo = 0xFFFF'FFFFu;
  } else {
    m.bits_lo = (warp_size == 0) ? 0u : ((1u << warp_size) - 1u);
  }
  return m;
}

LaneMask lane_mask_and(const LaneMask& a, const LaneMask& b) {
  LaneMask r;
  r.width = a.width;
  r.bits_lo = a.bits_lo & b.bits_lo;
  return r;
}

bool lane_mask_test(const LaneMask& m, std::uint32_t lane) {
  if (lane >= m.width) return false;
  if (lane >= 32) return false;
  return (m.bits_lo >> lane) & 1u;
}

std::string lane_mask_to_hex(const LaneMask& m) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::setw(8) << std::setfill('0') << m.bits_lo;
  return oss.str();
}

ValueType parse_value_type(const std::string& type_mod) {
  if (type_mod == "u32") return ValueType::U32;
  if (type_mod == "s32") return ValueType::S32;
  if (type_mod == "b32") return ValueType::U32;
  if (type_mod == "u64") return ValueType::U64;
  if (type_mod == "s64") return ValueType::S64;
  if (type_mod == "b64") return ValueType::U64;
  if (type_mod == "f32") return ValueType::F32;
  return ValueType::U32;
}

std::string to_string(EventCategory c) {
  switch (c) {
  case EventCategory::Stream: return "STREAM";
  case EventCategory::Copy: return "COPY";
  case EventCategory::Fetch: return "FETCH";
  case EventCategory::Exec: return "EXEC";
  case EventCategory::Ctrl: return "CTRL";
  case EventCategory::Mem: return "MEM";
  case EventCategory::Commit: return "COMMIT";
  }
  return "STREAM";
}

} // namespace gpusim
