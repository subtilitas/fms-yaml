// SPDX-License-Identifier: MIT
//
// The exception firewall.
//
// This is the only translation unit compiled with -fexceptions, because
// yaml-cpp reports every problem by throwing.  Both public entry points wrap
// their whole body in try/catch, so an exception can never leave this file.
#include "fms/yaml_loader.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <yaml-cpp/yaml.h>

namespace fms::config {
namespace {

void set_message(Diagnostics& diagnostics, Status status, int line, const char* fmt,
                 const char* arg0 = "", const char* arg1 = "") {
  diagnostics.status = status;
  diagnostics.line   = line;

  char buffer[limits::kMaxMessageLength + 1];
  std::snprintf(buffer, sizeof(buffer), fmt, arg0, arg1);

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

// ---------------------------------------------------------------------------

/// The 'io' section is deliberately thin: two channels the machine talks on,
/// and two opaque strings the port may interpret however it likes.  Nothing
/// here is specific to a transport.
Status parse_io(const YAML::Node& root, Model& model, Diagnostics& diagnostics) {
  const YAML::Node node = root["io"];
  if (!node) {
    return Status::Ok;  // optional section
  }
  if (!node.IsMap()) {
    set_message(diagnostics, Status::SchemaError, line_of(node), "'io' must be a mapping");
    return Status::SchemaError;
  }

  IoConfig& io = model.mutable_io();

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

/// Second pass: wire the transitions.
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
                  "'transitions' must be a mapping of trigger -> state");
      return Status::SchemaError;
    }

    for (const auto& pair : transitions) {
      const std::string trigger_name = pair.first.as<std::string>();
      const std::string target_name  = pair.second.as<std::string>();

      const TriggerId trigger = model.find_trigger(as_view(trigger_name));
      if (trigger == kNoTrigger) {
        set_message(diagnostics, Status::UnknownTrigger, line_of(pair.first),
                    "trigger '%s' is not declared", trigger_name.c_str());
        return Status::UnknownTrigger;
      }

      const StateId target = model.find_state(as_view(target_name));
      if (target == kNoState) {
        set_message(diagnostics, Status::UnknownState, line_of(pair.second),
                    "target state '%s' does not exist", target_name.c_str());
        return Status::UnknownState;
      }

      const Status status = model.add_transition(from, trigger, target);
      if (!is_ok(status)) {
        set_message(diagnostics, status, line_of(pair.first), "transition on '%s': %s",
                    trigger_name.c_str(), to_string(status));
        return status;
      }
    }
  }
  return Status::Ok;
}

Status parse_document(const YAML::Node& root, Model& model, Diagnostics& diagnostics) {
  if (!root.IsMap()) {
    set_message(diagnostics, Status::SchemaError, line_of(root),
                "the document root must be a mapping");
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
    if (!is_ok(model.set_name(as_view(name)))) {
      set_message(diagnostics, Status::NameTooLong, line_of(fsm["name"]), "fsm.name is too long");
      return Status::NameTooLong;
    }
  }

  Status status = parse_io(root, model, diagnostics);
  if (!is_ok(status)) {
    return status;
  }
  status = parse_triggers(root, model, diagnostics);
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

  const std::string initial    = fsm["initial"].as<std::string>();
  const StateId     initial_id = model.find_state(as_view(initial));
  if (initial_id == kNoState) {
    set_message(diagnostics, Status::UnknownState, line_of(fsm["initial"]),
                "initial state '%s' does not exist", initial.c_str());
    return Status::UnknownState;
  }
  model.set_initial(initial_id);

  status = model.validate();
  if (!is_ok(status)) {
    set_message(diagnostics, status, -1, "validation failed: %s", to_string(status));
    return status;
  }
  return Status::Ok;
}

}  // namespace

// ---------------------------------------------------------------------------
// public entry points - nothing throws past this line
// ---------------------------------------------------------------------------

Status load_string(const char* yaml, Model& model, Diagnostics& diagnostics) noexcept {
  install_etl_error_handler();
  diagnostics.reset();
  model.clear();

  if (yaml == nullptr) {
    set_message(diagnostics, Status::InvalidArgument, -1, "null YAML buffer");
    return Status::InvalidArgument;
  }

  try {
    const YAML::Node root   = YAML::Load(yaml);
    const Status     status = parse_document(root, model, diagnostics);
    if (!is_ok(status)) {
      model.clear();
    }
    return status;
  } catch (const YAML::Exception& e) {
    set_message(diagnostics, Status::ParseError, e.mark.line + 1, "%s", e.what());
    model.clear();
    return Status::ParseError;
  } catch (const std::exception& e) {
    set_message(diagnostics, Status::ParseError, -1, "%s", e.what());
    model.clear();
    return Status::ParseError;
  } catch (...) {
    set_message(diagnostics, Status::ParseError, -1, "unknown error while parsing YAML");
    model.clear();
    return Status::ParseError;
  }
}

Status load_file(const char* path, Model& model, Diagnostics& diagnostics) noexcept {
  install_etl_error_handler();
  diagnostics.reset();
  model.clear();

  if (path == nullptr) {
    set_message(diagnostics, Status::InvalidArgument, -1, "null path");
    return Status::InvalidArgument;
  }

  try {
    std::FILE* probe = std::fopen(path, "rb");
    if (probe == nullptr) {
      set_message(diagnostics, Status::FileNotFound, -1, "cannot open '%s'", path);
      return Status::FileNotFound;
    }
    std::fclose(probe);

    const YAML::Node root   = YAML::LoadFile(path);
    const Status     status = parse_document(root, model, diagnostics);
    if (!is_ok(status)) {
      model.clear();
    }
    return status;
  } catch (const YAML::Exception& e) {
    set_message(diagnostics, Status::ParseError, e.mark.line + 1, "%s", e.what());
    model.clear();
    return Status::ParseError;
  } catch (const std::exception& e) {
    set_message(diagnostics, Status::ParseError, -1, "%s", e.what());
    model.clear();
    return Status::ParseError;
  } catch (...) {
    set_message(diagnostics, Status::ParseError, -1, "unknown error while loading YAML");
    model.clear();
    return Status::ParseError;
  }
}

}  // namespace fms::config
