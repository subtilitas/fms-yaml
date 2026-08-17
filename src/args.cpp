// SPDX-License-Identifier: MIT
#include "fms/args.hpp"

namespace fms {
namespace {

constexpr bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool equals_ignore_case(StringView text, const char* literal) noexcept {
  std::size_t i = 0;
  for (; literal[i] != '\0'; ++i) {
    if (i >= text.size()) {
      return false;
    }
    char c = text[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != literal[i]) {
      return false;
    }
  }
  return i == text.size();
}

}  // namespace

bool parse_int(StringView text, std::int32_t& out) noexcept {
  if (text.empty()) {
    return false;
  }

  std::size_t i        = 0;
  bool        negative = false;
  if (text[0] == '+' || text[0] == '-') {
    negative = (text[0] == '-');
    i        = 1;
    if (text.size() == 1) {
      return false;
    }
  }

  std::int64_t value = 0;
  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (c < '0' || c > '9') {
      return false;  // not an integer; "30.5" and "fast" both land here
    }
    value = value * 10 + (c - '0');
    if (value > 2147483648LL) {
      return false;  // out of range for an int32
    }
  }

  if (negative) {
    value = -value;
  }
  if (value > 2147483647LL || value < -2147483648LL) {
    return false;
  }
  out = static_cast<std::int32_t>(value);
  return true;
}

Status Args::parse(StringView text) noexcept {
  clear();
  raw_ = text;

  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && is_space(text[i])) {
      ++i;
    }
    if (i >= text.size()) {
      break;
    }

    const std::size_t start = i;
    while (i < text.size() && !is_space(text[i])) {
      ++i;
    }
    const StringView token(text.data() + start, i - start);

    const std::size_t equals = token.find('=');
    if (equals == StringView::npos || equals == 0) {
      clear();
      return Status::ArgumentError;  // "pedal" or "=42"
    }

    const StringView key(token.data(), equals);
    const StringView value(token.data() + equals + 1, token.size() - equals - 1);

    Name stored_key;
    if (!assign_checked(stored_key, key)) {
      clear();
      return Status::NameTooLong;
    }
    if (map_.find(stored_key) != map_.end()) {
      clear();
      return Status::DuplicateName;
    }
    if (map_.full()) {
      clear();
      return Status::CapacityExceeded;
    }
    map_.insert(Map::value_type(stored_key, value));
  }
  return Status::Ok;
}

void Args::clear() noexcept {
  map_.clear();
  raw_ = StringView{};
}

bool Args::has(StringView name) const noexcept {
  Name key;
  if (!assign_checked(key, name)) {
    return false;
  }
  return map_.find(key) != map_.end();
}

StringView Args::text(StringView name) const noexcept {
  Name key;
  if (!assign_checked(key, name)) {
    return StringView{};
  }
  const auto it = map_.find(key);
  return (it == map_.end()) ? StringView{} : it->second;
}

bool Args::as_int(StringView name, std::int32_t& out) const noexcept {
  return parse_int(text(name), out);
}

bool Args::as_bool(StringView name, bool& out) const noexcept {
  const StringView value = text(name);
  if (value.empty()) {
    return false;
  }
  if (equals_ignore_case(value, "true") || equals_ignore_case(value, "on") ||
      equals_ignore_case(value, "yes") || equals_ignore_case(value, "1")) {
    out = true;
    return true;
  }
  if (equals_ignore_case(value, "false") || equals_ignore_case(value, "off") ||
      equals_ignore_case(value, "no") || equals_ignore_case(value, "0")) {
    out = false;
    return true;
  }
  return false;
}

const Args& Args::none() noexcept {
  static const Args empty;
  return empty;
}

}  // namespace fms
