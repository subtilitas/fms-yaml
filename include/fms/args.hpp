// SPDX-License-Identifier: MIT
//
// The arguments a trigger carries: `pedal=42 mode=sport`.
//
// Parsed into a fixed-capacity map of *views* into the port's buffer - the text
// is never copied, so a trigger with arguments costs no more than one without.
// That also means an Args is only valid while the input it was parsed from is:
// it lives for one dispatch, which is exactly how Runtime uses it.
//
// Values stay text until someone asks for a number, because the machine itself
// never needs one: only guards and application code do.
#ifndef FMS_ARGS_HPP
#define FMS_ARGS_HPP

#include <cstdint>

#include <etl/flat_map.h>

#include "fms/limits.hpp"
#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms {

class Args {
 public:
  using Map = etl::flat_map<Name, StringView, limits::kMaxArguments>;

  Args() noexcept = default;

  /// Parses `text` - whitespace separated `key=value` pairs - keeping views into
  /// it.  `text` must outlive this object.
  ///   Status::Ok               parsed, possibly empty
  ///   Status::ArgumentError    a token without '=' , or an empty key
  ///   Status::DuplicateName    the same key twice
  ///   Status::NameTooLong      a key longer than FMS_MAX_NAME_LENGTH
  ///   Status::CapacityExceeded more than FMS_MAX_ARGUMENTS pairs
  Status parse(StringView text) noexcept;

  void clear() noexcept;

  std::size_t size() const noexcept { return map_.size(); }
  bool        empty() const noexcept { return map_.empty(); }

  bool has(StringView name) const noexcept;

  /// The raw value, or an empty view when the argument is absent.
  StringView text(StringView name) const noexcept;

  /// Integer value.  False when absent or not an integer.
  bool as_int(StringView name, std::int32_t& out) const noexcept;

  /// true/false, on/off, yes/no, 1/0.  False when absent or not one of those.
  bool as_bool(StringView name, bool& out) const noexcept;

  /// Everything that was parsed, for logging.
  StringView raw() const noexcept { return raw_; }

  const Map& map() const noexcept { return map_; }

  /// A shared empty instance, for callers with nothing to pass.
  static const Args& none() noexcept;

 private:
  Map        map_{};
  StringView raw_{};
};

/// Parses a decimal integer, optionally signed.  The whole view must be digits.
bool parse_int(StringView text, std::int32_t& out) noexcept;

}  // namespace fms

#endif  // FMS_ARGS_HPP
