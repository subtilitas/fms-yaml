// SPDX-License-Identifier: MIT
//
// "No dynamic memory after setup" is a claim, and claims should be testable.
//
// Link fms_alloc_guard into a binary and it replaces global operator new /
// delete.  Call arm() once the config has been loaded: from that point on any
// heap allocation is recorded and, if fatal mode is on, aborts the process with
// a message.  disarm() lifts the trap (used by tests around the setup phase).
//
// The guard is a diagnostic aid, not a requirement - production builds can
// simply not link it.
#ifndef FMS_ALLOC_GUARD_HPP
#define FMS_ALLOC_GUARD_HPP

#include <cstddef>

namespace fms::alloc_guard {

/// Starts trapping heap use.  `fatal` aborts on the first violation.
void arm(bool fatal = true) noexcept;

/// Stops trapping.  Counters are kept.
void disarm() noexcept;

bool armed() noexcept;

/// Number of allocations that happened while armed.
std::size_t violations() noexcept;

/// Total allocations since process start (armed or not).
std::size_t total_allocations() noexcept;

void reset_counters() noexcept;

/// RAII helper: arms on construction, disarms on destruction.
class Scope {
 public:
  explicit Scope(bool fatal = true) noexcept { arm(fatal); }
  ~Scope() noexcept { disarm(); }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};

}  // namespace fms::alloc_guard

#endif  // FMS_ALLOC_GUARD_HPP
