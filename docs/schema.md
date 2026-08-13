# YAML schema

Four sections: `fsm`, `io`, `triggers`, `states`. Unknown keys are ignored;
missing required keys are a `Status::SchemaError` with a line number in
`Diagnostics`.

## `fsm` (required)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | `""` | machine name, diagnostics only |
| `initial` | string | — | **required**, must name a state |

## `io` (optional)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `state_channel` | string | `""` | where the machine announces each new state |
| `error_channel` | string | `""` | where it reports refused triggers and unknown channels |
| `endpoint` | string | `""` | opaque: broker URI, device path, socket address |
| `identity` | string | `""` | opaque: client id, node name |

All four are handed to the port untouched. The core never interprets them, and
a port is free to ignore any of them — `ConsolePort` writes states to
`std::cout` and errors to `std::cerr` regardless of what the channels say.

## `triggers` (required, non-empty sequence)

| Key | Type | Meaning |
|---|---|---|
| `name` | string | **required**, unique; how `transitions` refers to it |
| `channel` | string | where the port delivers it from. **Defaults to `name`** |

A channel is an opaque address: a word typed on stdin, an MQTT topic, a CAN
identifier. One channel per trigger and one trigger per channel — routing is a
single lookup and input can never be ambiguous. Declaring two triggers on the
same channel is `Status::DuplicateName`.

```yaml
triggers:
  - {name: brake_pressed}                              # channel == "brake_pressed"
  - {name: brake_released, channel: "car/brakes/off"}  # explicit
```

## `states` (required, non-empty sequence)

| Key | Type | Meaning |
|---|---|---|
| `name` | string | **required**, unique |
| `transitions` | mapping `trigger: next_state` | optional; a state with none accepts nothing |

```yaml
states:
  - name: accelerating
    transitions:
      throttle_released: coasting
      brake_pressed:     braking
      engine_fault:      fault
```

Because `transitions` is a mapping, a state cannot list the same trigger twice —
YAML keys are unique, so ambiguity is impossible by construction. A trigger a
state does not list is rejected: the state is unchanged, `fire()` returns
`Status::NoTransition`, and the runtime publishes
`rejected: <trigger> in state <state>` on the error channel.

Self-transitions are allowed (`brake_pressed: standing` inside `standing`) and
are the way to say "accepted, but nothing changes".

## Limits

Every string and container is fixed capacity. Exceeding one is a load-time error
(`NameTooLong`, `ChannelTooLong`, `CapacityExceeded`) — values are never
silently truncated. Defaults from `include/fms/limits.hpp`:

| Macro | Default |
|---|---|
| `FMS_MAX_STATES` | 32 |
| `FMS_MAX_TRIGGERS` | 32 |
| `FMS_MAX_TRANSITIONS_PER_STATE` | 8 |
| `FMS_MAX_NAME_LENGTH` | 31 |
| `FMS_MAX_CHANNEL_LENGTH` | 95 |
| `FMS_MAX_MESSAGE_LENGTH` | 127 |

Override them from the build system:

```cmake
target_compile_definitions(fms_core PUBLIC FMS_MAX_STATES=8 FMS_MAX_TRIGGERS=12)
```

They are compile-wide on purpose: the sizes are baked into the types, so every
translation unit must agree.
