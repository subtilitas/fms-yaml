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
using GroupId   = std::uint8_t;

inline constexpr StateId   kNoState   = 0xFFFFu;
inline constexpr TriggerId kNoTrigger = 0xFFFFu;
inline constexpr GroupId   kNoGroup   = 0xFFu;

/// A transition target meaning "the state the machine is in".  Written `~self`
/// in a machine file.  It lets a group say "stay where you are" for each of its
/// members; Model::evaluate resolves it to the state the trigger arrived in.
inline constexpr StateId kSelfState = 0xFFFEu;

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
  // A default-constructed StringView has a null data() and a zero size(), and
  // assign(nullptr, 0) passes null to a parameter declared never to be null.
  // ETL forwards the pair to memmove, so the standard's exemption for a zero
  // count does not apply and UBSan reports it.
  if (source.empty()) {
    destination.clear();
    return true;
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
  // Nothing to append, and the pointer may be null - see assign_checked above.
  if (take == 0) {
    return;
  }
  destination.append(source.data(), take);
}

}  // namespace fms

#endif  // FMS_TYPES_HPP
