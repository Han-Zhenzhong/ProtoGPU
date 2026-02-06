#include "gpusim/json.h"

#include <charconv>
#include <cctype>
#include <sstream>

namespace gpusim::json {

namespace {

struct Cursor final {
  std::string_view s;
  std::size_t i = 0;

  char peek() const { return i < s.size() ? s[i] : '\0'; }
  bool eof() const { return i >= s.size(); }
  char get() { return i < s.size() ? s[i++] : '\0'; }
};

[[noreturn]] void fail(const Cursor& c, const std::string& msg) {
  std::ostringstream oss;
  oss << "JSON parse error at offset " << c.i << ": " << msg;
  throw ParseError(oss.str());
}

void skip_ws(Cursor& c) {
  while (!c.eof() && std::isspace(static_cast<unsigned char>(c.peek()))) {
    c.i++;
  }
}

bool consume(Cursor& c, char ch) {
  if (c.peek() == ch) {
    c.i++;
    return true;
  }
  return false;
}

void expect(Cursor& c, char ch) {
  if (!consume(c, ch)) {
    std::string m;
    m += "expected '";
    m += ch;
    m += "'";
    fail(c, m);
  }
}

std::string parse_string(Cursor& c) {
  expect(c, '"');
  std::string out;
  while (!c.eof()) {
    char ch = c.get();
    if (ch == '"') {
      return out;
    }
    if (ch == '\\') {
      if (c.eof()) fail(c, "unterminated escape");
      char e = c.get();
      switch (e) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u':
        fail(c, "\\u escapes not supported in this parser");
      default:
        fail(c, "invalid escape");
      }
      continue;
    }
    out.push_back(ch);
  }
  fail(c, "unterminated string");
}

double parse_number(Cursor& c) {
  std::size_t start = c.i;
  if (c.peek() == '-') c.i++;
  if (!std::isdigit(static_cast<unsigned char>(c.peek()))) {
    fail(c, "invalid number");
  }
  if (c.peek() == '0') {
    c.i++;
  } else {
    while (std::isdigit(static_cast<unsigned char>(c.peek()))) c.i++;
  }
  if (c.peek() == '.') {
    c.i++;
    if (!std::isdigit(static_cast<unsigned char>(c.peek()))) fail(c, "invalid number fraction");
    while (std::isdigit(static_cast<unsigned char>(c.peek()))) c.i++;
  }
  if (c.peek() == 'e' || c.peek() == 'E') {
    c.i++;
    if (c.peek() == '+' || c.peek() == '-') c.i++;
    if (!std::isdigit(static_cast<unsigned char>(c.peek()))) fail(c, "invalid number exponent");
    while (std::isdigit(static_cast<unsigned char>(c.peek()))) c.i++;
  }
  auto sv = c.s.substr(start, c.i - start);
  char* endp = nullptr;
  std::string tmp(sv);
  double v = std::strtod(tmp.c_str(), &endp);
  if (endp == tmp.c_str()) fail(c, "number conversion failed");
  return v;
}

Value parse_value(Cursor& c);

Array parse_array(Cursor& c) {
  expect(c, '[');
  skip_ws(c);
  Array a;
  if (consume(c, ']')) return a;
  while (true) {
    skip_ws(c);
    a.push_back(parse_value(c));
    skip_ws(c);
    if (consume(c, ']')) return a;
    expect(c, ',');
  }
}

Object parse_object(Cursor& c) {
  expect(c, '{');
  skip_ws(c);
  Object o;
  if (consume(c, '}')) return o;
  while (true) {
    skip_ws(c);
    if (c.peek() != '"') fail(c, "object key must be string");
    auto k = parse_string(c);
    skip_ws(c);
    expect(c, ':');
    skip_ws(c);
    o.emplace(std::move(k), parse_value(c));
    skip_ws(c);
    if (consume(c, '}')) return o;
    expect(c, ',');
  }
}

bool match_lit(Cursor& c, std::string_view lit) {
  if (c.s.substr(c.i, lit.size()) == lit) {
    c.i += lit.size();
    return true;
  }
  return false;
}

Value parse_value(Cursor& c) {
  skip_ws(c);
  char ch = c.peek();
  if (ch == '"') return Value(parse_string(c));
  if (ch == '{') return Value(parse_object(c));
  if (ch == '[') return Value(parse_array(c));
  if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return Value(parse_number(c));
  if (match_lit(c, "true")) return Value(true);
  if (match_lit(c, "false")) return Value(false);
  if (match_lit(c, "null")) return Value(nullptr);
  fail(c, "unexpected token");
}

void stringify_impl(std::ostringstream& out, const Value& v);

void stringify_string(std::ostringstream& out, const std::string& s) {
  out << '"';
  for (char ch : s) {
    switch (ch) {
    case '"': out << "\\\""; break;
    case '\\': out << "\\\\"; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default: out << ch; break;
    }
  }
  out << '"';
}

void stringify_impl(std::ostringstream& out, const Value& v) {
  if (v.is_null()) {
    out << "null";
  } else if (v.is_bool()) {
    out << (v.as_bool() ? "true" : "false");
  } else if (v.is_number()) {
    out << v.as_number();
  } else if (v.is_string()) {
    stringify_string(out, v.as_string());
  } else if (v.is_array()) {
    out << '[';
    const auto& a = v.as_array();
    for (std::size_t i = 0; i < a.size(); i++) {
      if (i) out << ',';
      stringify_impl(out, a[i]);
    }
    out << ']';
  } else {
    out << '{';
    const auto& o = v.as_object();
    bool first = true;
    for (const auto& [k, vv] : o) {
      if (!first) out << ',';
      first = false;
      stringify_string(out, k);
      out << ':';
      stringify_impl(out, vv);
    }
    out << '}';
  }
}

} // namespace

const bool& Value::as_bool() const {
  if (!is_bool()) throw std::runtime_error("json: not a bool");
  return std::get<bool>(storage);
}

double Value::as_number() const {
  if (!is_number()) throw std::runtime_error("json: not a number");
  return std::get<double>(storage);
}

std::int64_t Value::as_i64() const {
  return static_cast<std::int64_t>(as_number());
}

std::uint64_t Value::as_u64() const {
  return static_cast<std::uint64_t>(as_number());
}

const std::string& Value::as_string() const {
  if (!is_string()) throw std::runtime_error("json: not a string");
  return std::get<std::string>(storage);
}

const Array& Value::as_array() const {
  if (!is_array()) throw std::runtime_error("json: not an array");
  return std::get<Array>(storage);
}

const Object& Value::as_object() const {
  if (!is_object()) throw std::runtime_error("json: not an object");
  return std::get<Object>(storage);
}

const Value* Value::get(const std::string& key) const {
  if (!is_object()) return nullptr;
  const auto& o = std::get<Object>(storage);
  auto it = o.find(key);
  if (it == o.end()) return nullptr;
  return &it->second;
}

const Value& Value::at(const std::string& key) const {
  const auto* v = get(key);
  if (!v) throw std::runtime_error("json: missing key: " + key);
  return *v;
}

Value parse(std::string_view text) {
  Cursor c{ text, 0 };
  auto v = parse_value(c);
  skip_ws(c);
  if (!c.eof()) fail(c, "trailing characters");
  return v;
}

std::string stringify(const Value& value) {
  std::ostringstream out;
  stringify_impl(out, value);
  return out.str();
}

} // namespace gpusim::json
