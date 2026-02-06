#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gpusim::json {

class ParseError final : public std::runtime_error {
public:
  explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value final {
  using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

  Storage storage;

  Value() : storage(nullptr) {}
  Value(std::nullptr_t) : storage(nullptr) {}
  Value(bool v) : storage(v) {}
  Value(double v) : storage(v) {}
  Value(std::string v) : storage(std::move(v)) {}
  Value(const char* v) : storage(std::string(v)) {}
  Value(Array v) : storage(std::move(v)) {}
  Value(Object v) : storage(std::move(v)) {}

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(storage); }
  bool is_bool() const { return std::holds_alternative<bool>(storage); }
  bool is_number() const { return std::holds_alternative<double>(storage); }
  bool is_string() const { return std::holds_alternative<std::string>(storage); }
  bool is_array() const { return std::holds_alternative<Array>(storage); }
  bool is_object() const { return std::holds_alternative<Object>(storage); }

  const bool& as_bool() const;
  double as_number() const;
  std::int64_t as_i64() const;
  std::uint64_t as_u64() const;
  const std::string& as_string() const;
  const Array& as_array() const;
  const Object& as_object() const;

  const Value* get(const std::string& key) const;
  const Value& at(const std::string& key) const;
};

Value parse(std::string_view text);
std::string stringify(const Value& value);

} // namespace gpusim::json
