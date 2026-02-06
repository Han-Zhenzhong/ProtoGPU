#include "gpusim/observability.h"

#include "gpusim/json.h"

#include <algorithm>
#include <sstream>

namespace gpusim {

TraceBuffer::TraceBuffer(std::uint64_t capacity) : capacity_(capacity), ring_() {
  ring_.reserve(static_cast<std::size_t>(capacity_));
}

void TraceBuffer::push(Event e) {
  std::lock_guard<std::mutex> lock(mu_);
  if (capacity_ == 0) return;

  if (ring_.size() < capacity_) {
    ring_.push_back(std::move(e));
    size_ = ring_.size();
    head_ = 0;
    return;
  }

  ring_[static_cast<std::size_t>(head_)] = std::move(e);
  head_ = (head_ + 1) % capacity_;
  size_ = capacity_;
}

std::vector<Event> TraceBuffer::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<Event> out;
  out.reserve(static_cast<std::size_t>(size_));
  if (ring_.empty()) return out;

  if (ring_.size() < capacity_) {
    out = ring_;
    return out;
  }

  for (std::uint64_t k = 0; k < size_; k++) {
    std::uint64_t idx = (head_ + k) % capacity_;
    out.push_back(ring_[static_cast<std::size_t>(idx)]);
  }
  return out;
}

void Counters::inc(const std::string& key, std::uint64_t delta) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto& [k, v] : items_) {
    if (k == key) {
      v += delta;
      return;
    }
  }
  items_.push_back({ key, delta });
}

std::vector<std::pair<std::string, std::uint64_t>> Counters::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return items_;
}

ObsControl::ObsControl(ObsConfig cfg) : cfg_(cfg), trace_(cfg.trace_capacity), counters_() {}

void ObsControl::emit(Event e) {
  if (!cfg_.enabled) return;
  trace_.push(std::move(e));
}

void ObsControl::counter(const std::string& key, std::uint64_t delta) {
  if (!cfg_.enabled) return;
  counters_.inc(key, delta);
}

std::vector<Event> ObsControl::trace_snapshot() const { return trace_.snapshot(); }

std::vector<std::pair<std::string, std::uint64_t>> ObsControl::counters_snapshot() const {
  return counters_.snapshot();
}

static void set_opt(gpusim::json::Object& o, const char* k, const std::optional<std::uint64_t>& v) {
  if (v) o.emplace(k, gpusim::json::Value(static_cast<double>(*v)));
}

static void set_opt(gpusim::json::Object& o, const char* k, const std::optional<std::int64_t>& v) {
  if (v) o.emplace(k, gpusim::json::Value(static_cast<double>(*v)));
}

static void set_opt(gpusim::json::Object& o, const char* k, const std::optional<std::string>& v) {
  if (v) o.emplace(k, gpusim::json::Value(*v));
}

std::string event_to_json_line(const Event& e) {
  gpusim::json::Object o;
  o.emplace("ts", gpusim::json::Value(static_cast<double>(e.ts)));
  o.emplace("cat", gpusim::json::Value(to_string(e.category)));
  o.emplace("action", gpusim::json::Value(e.action));
  if (e.kernel_id) o.emplace("kernel_id", gpusim::json::Value(static_cast<double>(*e.kernel_id)));
  if (e.cta_id) o.emplace("cta_id", gpusim::json::Value(static_cast<double>(*e.cta_id)));
  if (e.warp_id) o.emplace("warp_id", gpusim::json::Value(static_cast<double>(*e.warp_id)));
  if (e.thread_id) o.emplace("thread_id", gpusim::json::Value(static_cast<double>(*e.thread_id)));
  if (e.sm_id) o.emplace("sm_id", gpusim::json::Value(static_cast<double>(*e.sm_id)));
  if (e.stream_id) o.emplace("stream_id", gpusim::json::Value(static_cast<double>(*e.stream_id)));
  if (e.cmd_id) o.emplace("cmd_id", gpusim::json::Value(static_cast<double>(*e.cmd_id)));
  if (e.event_id) o.emplace("event_id", gpusim::json::Value(static_cast<double>(*e.event_id)));
  if (e.pc) o.emplace("pc", gpusim::json::Value(static_cast<double>(*e.pc)));
  if (e.lane_mask) o.emplace("lane_mask", gpusim::json::Value(lane_mask_to_hex(*e.lane_mask)));
  set_opt(o, "opcode_or_uop", e.opcode_or_uop);
  if (e.addr) o.emplace("addr", gpusim::json::Value(static_cast<double>(*e.addr)));
  if (e.size) o.emplace("size", gpusim::json::Value(static_cast<double>(*e.size)));
  set_opt(o, "extra", e.extra_json);
  return gpusim::json::stringify(gpusim::json::Value(std::move(o))) + "\n";
}

std::string stats_to_json(const std::vector<std::pair<std::string, std::uint64_t>>& counters) {
  gpusim::json::Object o;
  gpusim::json::Object cc;
  for (const auto& [k, v] : counters) {
    cc.emplace(k, gpusim::json::Value(static_cast<double>(v)));
  }
  o.emplace("counters", gpusim::json::Value(std::move(cc)));
  return gpusim::json::stringify(gpusim::json::Value(std::move(o)));
}

} // namespace gpusim
