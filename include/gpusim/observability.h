#pragma once

#include "gpusim/contracts.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gpusim {

struct ObsConfig final {
  bool enabled = true;
  std::uint64_t trace_capacity = 65536;
};

class TraceBuffer final {
public:
  explicit TraceBuffer(std::uint64_t capacity);

  void push(Event e);
  std::vector<Event> snapshot() const;

private:
  std::uint64_t capacity_;
  mutable std::mutex mu_;
  std::vector<Event> ring_;
  std::uint64_t head_ = 0;
  std::uint64_t size_ = 0;
};

class Counters final {
public:
  void inc(const std::string& key, std::uint64_t delta = 1);
  std::vector<std::pair<std::string, std::uint64_t>> snapshot() const;

private:
  mutable std::mutex mu_;
  std::vector<std::pair<std::string, std::uint64_t>> items_;
};

class ObsControl final {
public:
  explicit ObsControl(ObsConfig cfg);

  void emit(Event e);
  void counter(const std::string& key, std::uint64_t delta = 1);

  std::vector<Event> trace_snapshot() const;
  std::vector<std::pair<std::string, std::uint64_t>> counters_snapshot() const;

private:
  ObsConfig cfg_;
  TraceBuffer trace_;
  Counters counters_;
};

std::string event_to_json_line(const Event& e);
std::string stats_to_json(const std::vector<std::pair<std::string, std::uint64_t>>& counters);

} // namespace gpusim
