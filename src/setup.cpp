// SPDX-License-Identifier: MIT
#include "fms/setup.hpp"

namespace fms {

void Setup::clear() noexcept {
  name_.clear();
  initial_.clear();
  io_ = IoConfig{};
}

Status Setup::set_name(StringView name) noexcept {
  return assign_checked(name_, name) ? Status::Ok : Status::NameTooLong;
}

Status Setup::set_initial(StringView state_name) noexcept {
  if (state_name.empty()) {
    return Status::InvalidArgument;
  }
  return assign_checked(initial_, state_name) ? Status::Ok : Status::NameTooLong;
}

StateId Setup::initial_in(const Model& model) const noexcept {
  return model.find_state(view(initial_));
}

Status Setup::validate_against(const Model& model) const noexcept {
  if (initial_.empty()) {
    return Status::SchemaError;  // no initial state configured at all
  }
  return (initial_in(model) == kNoState) ? Status::UnknownState : Status::Ok;
}

}  // namespace fms
