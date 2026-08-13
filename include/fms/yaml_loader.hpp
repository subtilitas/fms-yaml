// SPDX-License-Identifier: MIT
//
// The configuration front end, and the boundary of the exception-free world:
// src/config/yaml_loader.cpp is the only TU built with -fexceptions, because
// yaml-cpp reports errors by throwing.  Both entry points are noexcept and
// catch everything, so callers only ever see a Status plus a diagnostic.
//
// This is also the only phase that allocates: yaml-cpp builds its document on
// the heap, we copy what we need into the fixed-capacity Model, and the
// document is destroyed before the function returns.
#ifndef FMS_YAML_LOADER_HPP
#define FMS_YAML_LOADER_HPP

#include "fms/model.hpp"
#include "fms/status.hpp"

namespace fms::config {

/// Where and why a load failed.
struct Diagnostics {
  Status  status = Status::Ok;
  Message message{};   ///< fixed capacity, never allocates
  int     line   = -1; ///< YAML line, when one was reported

  void reset() noexcept {
    status = Status::Ok;
    message.clear();
    line = -1;
  }
};

/// Reads `path` and fills `model`.  `model` is cleared on entry and left empty
/// if loading fails.
Status load_file(const char* path, Model& model, Diagnostics& diagnostics) noexcept;

/// Same, from a YAML document already in memory.
Status load_string(const char* yaml, Model& model, Diagnostics& diagnostics) noexcept;

}  // namespace fms::config

#endif  // FMS_YAML_LOADER_HPP
