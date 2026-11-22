// SPDX-License-Identifier: MIT
//
// Text scraps shared by the linter and the diagram exporter.
//
// Private to src/inspect/: both of them turn parts of a Model into readable
// text, and neither may allocate, so they need the same three small helpers.
// It stays out of include/ because rendering is a tooling concern - the
// machine itself never needs to print a condition.
#ifndef FMS_INSPECT_TEXT_HPP
#define FMS_INSPECT_TEXT_HPP

#include <cstddef>
#include <cstdint>

#include <etl/char_traits.h>

#include "fms/condition.hpp"
#include "fms/types.hpp"

namespace fms::inspect {

/// The longest C string cstr() reads.  Its callers pass literals, Status and
/// operator texts, and the names a Model holds; none is longer than the
/// largest of the three fixed string capacities.
inline constexpr std::size_t kMaxCStringLength =
    (limits::kMaxNameLength > limits::kMaxChannelLength)
        ? ((limits::kMaxNameLength > limits::kMaxMessageLength) ? limits::kMaxNameLength
                                                                : limits::kMaxMessageLength)
        : ((limits::kMaxChannelLength > limits::kMaxMessageLength) ? limits::kMaxChannelLength
                                                                   : limits::kMaxMessageLength);

inline StringView cstr(const char* text) noexcept {
  return StringView(text, etl::strlen(text, kMaxCStringLength));
}

/// Appends a decimal integer.  Digits are built backwards into a local buffer
/// and reversed, which avoids both a division-free special case and <cstdio>.
/// The negation is done in the wider type so INT32_MIN survives it.
template <typename TString>
void append_int(TString& out, std::int32_t value) noexcept {
  char        digits[12];
  std::size_t count     = 0;
  std::int64_t magnitude = value;

  if (magnitude < 0) {
    append_clipped(out, cstr("-"));
    magnitude = -magnitude;
  }
  do {
    digits[count++] = static_cast<char>('0' + (magnitude % 10));
    magnitude /= 10;
  } while (magnitude > 0 && count < sizeof(digits));

  while (count > 0) {
    --count;
    append_clipped(out, StringView(&digits[count], 1));
  }
}

/// Appends a guard the way it was written: `pedal > 5`, `mode == sport`.
template <typename TString>
void append_condition(TString& out, const Condition& condition) noexcept {
  append_clipped(out, view(condition.arg));
  append_clipped(out, cstr(" "));
  append_clipped(out, cstr(to_string(condition.op)));
  append_clipped(out, cstr(" "));
  if (condition.numeric) {
    append_int(out, condition.number);
  } else {
    append_clipped(out, view(condition.literal));
  }
}

}  // namespace fms::inspect

#endif  // FMS_INSPECT_TEXT_HPP
