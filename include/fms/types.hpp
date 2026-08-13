// SPDX-License-Identifier: MIT
#ifndef FMS_TYPES_HPP
#define FMS_TYPES_HPP

#include <cstdint>

#include <etl/string.h>
#include <etl/string_view.h>

#include "fms/limits.hpp"

namespace fms {

using StateId   = std::uint16_t;
using TriggerId = std::uint16_t;

inline constexpr StateId   kNoState   = 0xFFFFu;
inline constexpr TriggerId kNoTrigger = 0xFFFFu;

using Name    = etl::string<limits::kMaxNameLength>;
using Channel = etl::string<limits::kMaxChannelLength>;
using Message = etl::string<limits::kMaxMessageLength>;

using StringView = etl::string_view;

/// Makes a view of an ETL string.
template <typename TString>
StringView view(const TString& text) noexcept {
  return StringView(text.c_str(), text.size());
}

/// Copies into a fixed-capacity ETL string, refusing to truncate.
/// Returns false if the source does not fit - callers turn that into a Status.
template <typename TString>
bool assign_checked(TString& destination, StringView source) noexcept {
  if (source.size() > destination.max_size()) {
    return false;
  }
  destination.assign(source.data(), source.size());
  return true;
}

/// Appends as much as fits.  Used for diagnostic text, where a clipped message
/// beats no message.
template <typename TString>
void append_clipped(TString& destination, StringView source) noexcept {
  const std::size_t room = destination.max_size() - destination.size();
  const std::size_t take = (source.size() < room) ? source.size() : room;
  destination.append(source.data(), take);
}

}  // namespace fms

#endif  // FMS_TYPES_HPP
