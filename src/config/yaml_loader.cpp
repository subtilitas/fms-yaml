// SPDX-License-Identifier: MIT
//
// The exception firewall.
//
// This is the only translation unit compiled with -fexceptions, because
// yaml-cpp reports every problem by throwing.  Every public entry point wraps
// its whole body in try/catch, so an exception can never leave this file.
#include "fms/yaml_loader.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include <yaml-cpp/yaml.h>

namespace fms::config {
namespace {

// Formats into the fixed diagnostic buffer.
//
// This used to take two optional `const char*` defaulted to "" and hand both to
// snprintf regardless of what the format asked for.  That was safe - excess
// arguments to snprintf are ignored - but nothing checked that a format with
// two %s was ever given two arguments, and cppcheck was right to object
// (wrongPrintfScanfArgNum).  Varargs plus the format attribute turn the
// convention into something the compiler verifies at every call site.
#if defined(__GNUC__) || defined(__clang__)
#define FMS_PRINTF_LIKE(fmt_index, first_arg) \
  __attribute__((format(printf, fmt_index, first_arg)))
#else
#define FMS_PRINTF_LIKE(fmt_index, first_arg)
#endif

FMS_PRINTF_LIKE(4, 5)
// A parameter pack would need a formatting library to consume it, and the
// point of this function is to build a diagnostic without allocating.  The
// format attribute above gives back the type checking the ellipsis loses.
// NOLINTNEXTLINE(cert-dcl50-cpp)
void set_message(Diagnostics& diagnostics, Status status, int line, const char* fmt, ...) {
  diagnostics.status = status;
  diagnostics.line   = line;

  char buffer[limits::kMaxMessageLength + 1];
  std::va_list args;
  va_start(args, fmt);
  (void)std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  std::size_t length = std::strlen(buffer);
  if (length > diagnostics.message.max_size()) {
    length = diagnostics.message.max_size();
  }
  diagnostics.message.assign(buffer, length);
}

int line_of(const YAML::Node& node) {
  return node.IsDefined() ? node.Mark().line + 1 : -1;
}

StringView as_view(const std::string& text) { return StringView(text.data(), text.size()); }

/// Reads a scalar into a fixed-capacity ETL string, refusing to truncate.
template <typename TString>
bool read_string(const YAML::Node& node, TString& out) {
  const std::string value = node.as<std::string>();
  if (value.size() > out.max_size()) {
    return false;
  }
  out.assign(value.data(), value.size());
  return true;
}

/// The two files are separate on purpose, so a section that wandered into the
/// wrong one is a mistake worth naming rather than ignoring.
bool reject_foreign_section(const YAML::Node& root, const char* key, const char* belongs_in,
                            Diagnostics& diagnostics) {
  const YAML::Node node = root[key];
  if (!node) {
    return false;
  }
  set_message(diagnostics, Status::SchemaError, line_of(node),
              "'%s' belongs in the %s file", key, belongs_in);
  return true;
}

// ---------------------------------------------------------------------------
// setup file: fsm + io
// ---------------------------------------------------------------------------

Status parse_io(const YAML::Node& root, Setup& setup, Diagnostics& diagnostics) {
  const YAML::Node node = root["io"];
  if (!node) {
    return Status::Ok;  // optional section
  }
  if (!node.IsMap()) {
    set_message(diagnostics, Status::SchemaError, line_of(node), "'io' must be a mapping");
    return Status::SchemaError;
  }

  IoConfig& io = setup.mutable_io();

  if (node["state_channel"] && !read_string(node["state_channel"], io.state_channel)) {
    set_message(diagnostics, Status::ChannelTooLong, line_of(node["state_channel"]),
                "io.state_channel is too long");
    return Status::ChannelTooLong;
  }
  if (node["error_channel"] && !read_string(node["error_channel"], io.error_channel)) {
    set_message(diagnostics, Status::ChannelTooLong, line_of(node["error_channel"]),
                "io.error_channel is too long");
    return Status::ChannelTooLong;
  }
  if (node["endpoint"] && !read_string(node["endpoint"], io.endpoint)) {
    set_message(diagnostics, Status::ChannelTooLong, line_of(node["endpoint"]),
                "io.endpoint is too long");
    return Status::ChannelTooLong;
  }
  if (node["identity"] && !read_string(node["identity"], io.identity)) {
    set_message(diagnostics, Status::NameTooLong, line_of(node["identity"]),
                "io.identity is too long");
    return Status::NameTooLong;
  }
  return Status::Ok;
}

Status parse_setup(const YAML::Node& root, Setup& setup, Diagnostics& diagnostics) {
  if (!root.IsMap()) {
    set_message(diagnostics, Status::SchemaError, line_of(root),
                "the document root must be a mapping");
    return Status::SchemaError;
  }
  if (reject_foreign_section(root, "states", "machine", diagnostics) ||
      reject_foreign_section(root, "triggers", "machine", diagnostics)) {
    return Status::SchemaError;
  }

  const YAML::Node fsm = root["fsm"];
  if (!fsm || !fsm.IsMap() || !fsm["initial"]) {
    set_message(diagnostics, Status::SchemaError, line_of(fsm),
                "'fsm' must be a mapping containing 'initial'");
    return Status::SchemaError;
  }

  if (fsm["name"]) {
    const std::string name = fsm["name"].as<std::string>();
    if (!is_ok(setup.set_name(as_view(name)))) {
      set_message(diagnostics, Status::NameTooLong, line_of(fsm["name"]), "fsm.name is too long");
      return Status::NameTooLong;
    }
  }

  const std::string initial = fsm["initial"].as<std::string>();
  const Status      status  = setup.set_initial(as_view(initial));
  if (!is_ok(status)) {
    set_message(diagnostics, status, line_of(fsm["initial"]), "fsm.initial: %s",
                to_string(status));
    return status;
  }

  return parse_io(root, setup, diagnostics);
}

// ---------------------------------------------------------------------------
// machine file: triggers + states
// ---------------------------------------------------------------------------

Status parse_triggers(const YAML::Node& root, Model& model, Diagnostics& diagnostics) {
  const YAML::Node node = root["triggers"];
  if (!node || !node.IsSequence() || node.size() == 0) {
    set_message(diagnostics, Status::SchemaError, line_of(node),
                "'triggers' must be a non-empty sequence");
    return Status::SchemaError;
  }

  for (const YAML::Node& entry : node) {
    if (!entry.IsMap() || !entry["name"]) {
      set_message(diagnostics, Status::SchemaError, line_of(entry),
                  "each trigger needs a 'name'");
      return Status::SchemaError;
    }

    const std::string name = entry["name"].as<std::string>();
    std::string       channel;  // empty: declare_trigger defaults it to the name
    if (entry["channel"]) {
      channel = entry["channel"].as<std::string>();
    }

    TriggerId    id     = kNoTrigger;
    const Status status = model.declare_trigger(as_view(name), as_view(channel), id);
    if (!is_ok(status)) {
      set_message(diagnostics, status, line_of(entry), "trigger '%s': %s", name.c_str(),
                  to_string(status));
      return status;
    }
  }
  return Status::Ok;
}

/// First pass: declare every state, so transitions may refer to states defined
/// further down the file.
Status declare_states(const YAML::Node& node, Model& model, Diagnostics& diagnostics) {
  for (const YAML::Node& entry : node) {
    if (!entry.IsMap() || !entry["name"]) {
      set_message(diagnostics, Status::SchemaError, line_of(entry), "each state needs a 'name'");
      return Status::SchemaError;
    }
    const std::string name = entry["name"].as<std::string>();

    StateId      id     = kNoState;
    const Status status = model.declare_state(as_view(name), id);
    if (!is_ok(status)) {
      set_message(diagnostics, status, line_of(entry), "state '%s': %s", name.c_str(),
                  to_string(status));
      return status;
    }
  }
  return Status::Ok;
}

/// Reads the `when` of one alternative: a single comparison, or a sequence of
/// them, which are ANDed.
Status parse_when(const YAML::Node& node, const char* trigger_name, ConditionList& out,
                  Diagnostics& diagnostics) {
  if (!node) {
    return Status::Ok;  // unguarded
  }

  const auto read_one = [&](const YAML::Node& scalar) -> Status {
    const std::string text = scalar.as<std::string>();
    if (out.full()) {
      set_message(diagnostics, Status::CapacityExceeded, line_of(scalar),
                  "'%s': more than FMS_MAX_CONDITIONS_PER_GUARD conditions", trigger_name);
      return Status::CapacityExceeded;
    }
    Condition    condition;
    const Status status = parse_condition(as_view(text), condition);
    if (!is_ok(status)) {
      set_message(diagnostics, status, line_of(scalar),
                  "'%s': cannot read the guard '%s' - expected <arg> <op> <value>", trigger_name,
                  text.c_str());
      return status;
    }
    out.push_back(condition);
    return Status::Ok;
  };

  if (node.IsSequence()) {
    for (const YAML::Node& scalar : node) {
      const Status status = read_one(scalar);
      if (!is_ok(status)) {
        return status;
      }
    }
    if (out.empty()) {
      set_message(diagnostics, Status::SchemaError, line_of(node), "'%s': empty 'when'",
                  trigger_name);
      return Status::SchemaError;
    }
    return Status::Ok;
  }
  return read_one(node);
}

/// One alternative: `{when: ..., target: ...}`, where `when` is optional.
Status parse_alternative(const YAML::Node& node, StateId from, TriggerId trigger,
                         const char* trigger_name, Model& model, Diagnostics& diagnostics) {
  if (!node.IsMap() || !node["target"]) {
    set_message(diagnostics, Status::SchemaError, line_of(node),
                "'%s': an alternative needs a 'target'", trigger_name);
    return Status::SchemaError;
  }

  const std::string target_name = node["target"].as<std::string>();
  const StateId     target      = model.find_state(as_view(target_name));
  if (target == kNoState) {
    set_message(diagnostics, Status::UnknownState, line_of(node["target"]),
                "target state '%s' does not exist", target_name.c_str());
    return Status::UnknownState;
  }

  ConditionList conditions;
  const Status  guard = parse_when(node["when"], trigger_name, conditions, diagnostics);
  if (!is_ok(guard)) {
    return guard;
  }

  const Status status = model.add_transition(from, trigger, target, conditions);
  if (!is_ok(status)) {
    set_message(diagnostics, status, line_of(node), "transition on '%s': %s", trigger_name,
                to_string(status));
  }
  return status;
}

/// Second pass: wire the transitions.  Three accepted spellings, so the simple
/// case stays a single line:
///
///   ignition_on: self_test                     unguarded
///   throttle_pressed: {when: "pedal > 5", target: accelerating}
///   self_test_passed:                          ordered alternatives
///     - {when: "errors == 0", target: standing}
///     - {target: fault}                        unguarded fallback
Status link_states(const YAML::Node& node, Model& model, Diagnostics& diagnostics) {
  for (const YAML::Node& entry : node) {
    const std::string state_name = entry["name"].as<std::string>();
    const StateId     from       = model.find_state(as_view(state_name));

    const YAML::Node transitions = entry["transitions"];
    if (!transitions) {
      continue;  // a state with no way out is allowed, if unusual
    }
    if (!transitions.IsMap()) {
      set_message(diagnostics, Status::SchemaError, line_of(transitions),
                  "'transitions' must be a mapping of trigger -> state or alternatives");
      return Status::SchemaError;
    }

    for (const auto& pair : transitions) {
      const std::string trigger_name = pair.first.as<std::string>();
      const TriggerId   trigger      = model.find_trigger(as_view(trigger_name));
      if (trigger == kNoTrigger) {
        set_message(diagnostics, Status::UnknownTrigger, line_of(pair.first),
                    "trigger '%s' is not declared", trigger_name.c_str());
        return Status::UnknownTrigger;
      }

      const YAML::Node& outcome = pair.second;

      if (outcome.IsScalar()) {
        const std::string target_name = outcome.as<std::string>();
        const StateId     target      = model.find_state(as_view(target_name));
        if (target == kNoState) {
          set_message(diagnostics, Status::UnknownState, line_of(outcome),
                      "target state '%s' does not exist", target_name.c_str());
          return Status::UnknownState;
        }
        const Status status = model.add_transition(from, trigger, target);
        if (!is_ok(status)) {
          set_message(diagnostics, status, line_of(pair.first), "transition on '%s': %s",
                      trigger_name.c_str(), to_string(status));
          return status;
        }
        continue;
      }

      if (outcome.IsMap()) {
        const Status status = parse_alternative(outcome, from, trigger, trigger_name.c_str(),
                                                model, diagnostics);
        if (!is_ok(status)) {
          return status;
        }
        continue;
      }

      if (outcome.IsSequence()) {
        if (outcome.size() == 0) {
          set_message(diagnostics, Status::SchemaError, line_of(outcome),
                      "'%s': empty list of alternatives", trigger_name.c_str());
          return Status::SchemaError;
        }
        for (const YAML::Node& alternative : outcome) {
          const Status status = parse_alternative(alternative, from, trigger,
                                                  trigger_name.c_str(), model, diagnostics);
          if (!is_ok(status)) {
            return status;
          }
        }
        continue;
      }

      set_message(diagnostics, Status::SchemaError, line_of(outcome),
                  "'%s': expected a state name, one alternative, or a list of them",
                  trigger_name.c_str());
      return Status::SchemaError;
    }
  }
  return Status::Ok;
}

Status parse_machine(const YAML::Node& root, Model& model, Diagnostics& diagnostics) {
  if (!root.IsMap()) {
    set_message(diagnostics, Status::SchemaError, line_of(root),
                "the document root must be a mapping");
    return Status::SchemaError;
  }
  if (reject_foreign_section(root, "io", "setup", diagnostics)) {
    return Status::SchemaError;
  }
  // 'fsm' is allowed here, but only as a label for the definition: an 'initial'
  // key in the machine file is a setup key in the wrong place.
  const YAML::Node fsm = root["fsm"];
  if (fsm && fsm.IsMap() && fsm["initial"]) {
    set_message(diagnostics, Status::SchemaError, line_of(fsm["initial"]),
                "'fsm.initial' belongs in the setup file");
    return Status::SchemaError;
  }
  if (fsm && fsm.IsMap() && fsm["name"]) {
    const std::string name = fsm["name"].as<std::string>();
    if (!is_ok(model.set_name(as_view(name)))) {
      set_message(diagnostics, Status::NameTooLong, line_of(fsm["name"]), "fsm.name is too long");
      return Status::NameTooLong;
    }
  }

  Status status = parse_triggers(root, model, diagnostics);
  if (!is_ok(status)) {
    return status;
  }

  const YAML::Node states = root["states"];
  if (!states || !states.IsSequence() || states.size() == 0) {
    set_message(diagnostics, Status::SchemaError, line_of(states),
                "'states' must be a non-empty sequence");
    return Status::SchemaError;
  }

  status = declare_states(states, model, diagnostics);
  if (!is_ok(status)) {
    return status;
  }
  status = link_states(states, model, diagnostics);
  if (!is_ok(status)) {
    return status;
  }

  status = model.validate();
  if (!is_ok(status)) {
    set_message(diagnostics, status, -1, "validation failed: %s", to_string(status));
  }
  return status;
}

// ---------------------------------------------------------------------------
// shared plumbing for the four entry points
// ---------------------------------------------------------------------------

bool file_is_readable(const char* path, Diagnostics& diagnostics) {
  std::FILE* probe = std::fopen(path, "rb");
  if (probe == nullptr) {
    set_message(diagnostics, Status::FileNotFound, -1, "cannot open '%s'", path);
    return false;
  }
  (void)std::fclose(probe);
  return true;
}

/// Runs `parse` over a document and funnels every exception into a Status.
/// `Target` is Setup or Model; both have clear().
template <typename Target, typename Parse, typename Produce>
Status guarded(Target& target, Diagnostics& diagnostics, Produce produce, Parse parse) noexcept {
  install_etl_error_handler();
  diagnostics.reset();
  target.clear();

  try {
    const YAML::Node root   = produce();
    const Status     status = parse(root, target, diagnostics);
    if (!is_ok(status)) {
      target.clear();
    }
    return status;
  } catch (const YAML::Exception& e) {
    set_message(diagnostics, Status::ParseError, e.mark.line + 1, "%s", e.what());
    target.clear();
    return Status::ParseError;
  } catch (const std::exception& e) {
    set_message(diagnostics, Status::ParseError, -1, "%s", e.what());
    target.clear();
    return Status::ParseError;
  } catch (...) {
    set_message(diagnostics, Status::ParseError, -1, "unknown error while reading YAML");
    target.clear();
    return Status::ParseError;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// public entry points - nothing throws past this line
// ---------------------------------------------------------------------------

Status load_setup_string(const char* yaml, Setup& setup, Diagnostics& diagnostics) noexcept {
  if (yaml == nullptr) {
    diagnostics.reset();
    set_message(diagnostics, Status::InvalidArgument, -1, "null YAML buffer");
    return Status::InvalidArgument;
  }
  return guarded(setup, diagnostics, [yaml] { return YAML::Load(yaml); }, parse_setup);
}

Status load_setup_file(const char* path, Setup& setup, Diagnostics& diagnostics) noexcept {
  diagnostics.reset();
  if (path == nullptr) {
    set_message(diagnostics, Status::InvalidArgument, -1, "null path");
    return Status::InvalidArgument;
  }
  if (!file_is_readable(path, diagnostics)) {
    setup.clear();
    return Status::FileNotFound;
  }
  return guarded(setup, diagnostics, [path] { return YAML::LoadFile(path); }, parse_setup);
}

Status load_machine_string(const char* yaml, Model& model, Diagnostics& diagnostics) noexcept {
  if (yaml == nullptr) {
    diagnostics.reset();
    set_message(diagnostics, Status::InvalidArgument, -1, "null YAML buffer");
    return Status::InvalidArgument;
  }
  return guarded(model, diagnostics, [yaml] { return YAML::Load(yaml); }, parse_machine);
}

Status load_machine_file(const char* path, Model& model, Diagnostics& diagnostics) noexcept {
  diagnostics.reset();
  if (path == nullptr) {
    set_message(diagnostics, Status::InvalidArgument, -1, "null path");
    return Status::InvalidArgument;
  }
  if (!file_is_readable(path, diagnostics)) {
    model.clear();
    return Status::FileNotFound;
  }
  return guarded(model, diagnostics, [path] { return YAML::LoadFile(path); }, parse_machine);
}

}  // namespace fms::config
