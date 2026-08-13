# YAML schema

Three sections: `fsm`, `mqtt`, `triggers`, `states`. Unknown keys are ignored;
missing required keys are a `Status::SchemaError` with a line number in
`Diagnostics`.

## `fsm` (required)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | `""` | machine name, diagnostics only |
| `initial` | string | — | **required**, must name a state |

## `mqtt` (optional, defaults shown)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `broker` | string | `tcp://localhost:1883` | paho server URI |
| `client_id` | string | `fms` | MQTT client id |
| `keep_alive_seconds` | uint16 | `30` | |
| `clean_session` | bool | `true` | |
| `qos` | 0/1/2 | `1` | used for both subscribe and publish |
| `connect_timeout_ms` | uint32 | `5000` | also the QoS>0 publish completion wait |
| `state_topic` | string | `""` | the new state's name is published here on every change; empty disables it |
| `retain_state` | bool | `true` | retain flag for `state_topic` |
| `error_topic` | string | `""` | rejected triggers are reported here; empty disables it |

The error payload is `rejected: <trigger> in state <state>`.

## `triggers` (required, non-empty sequence)

| Key | Type | Meaning |
|---|---|---|
| `name` | string | **required**, unique; how `transitions` refers to it |
| `topic` | string | the topic whose messages raise this trigger, unique across triggers. Omit it for triggers only raised from code via `Runtime::fire_by_name` |

One topic per trigger, one trigger per topic — routing is a single lookup and a
message can never be ambiguous. Wildcards are not supported: a subscription is
an exact topic. The payload is ignored, so publishing an empty message is fine
(`mosquitto_pub -n`).

```yaml
triggers:
  - {name: brake_pressed,  topic: "car/brakes/pressed"}
  - {name: brake_released, topic: "car/brakes/released"}
  - {name: engine_fault,   topic: "car/engine/fault"}
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
YAML keys are unique, so ambiguity is impossible by construction. A trigger that
a state does not list is rejected: the state is unchanged, `fire()` returns
`Status::NoTransition`, and the runtime publishes the rejection on `error_topic`.

Self-transitions are allowed (`brake_pressed: standing` inside `standing`) and
are the way to say "accepted, but nothing changes".

## Limits

Every string and container is fixed capacity. Exceeding one is a load-time
error (`NameTooLong`, `TopicTooLong`, `CapacityExceeded`) — values are never
silently truncated. Defaults from `include/fms/limits.hpp`:

| Macro | Default |
|---|---|
| `FMS_MAX_STATES` | 32 |
| `FMS_MAX_TRIGGERS` | 32 |
| `FMS_MAX_TRANSITIONS_PER_STATE` | 8 |
| `FMS_MAX_NAME_LENGTH` | 31 |
| `FMS_MAX_TOPIC_LENGTH` | 95 |
| `FMS_MAX_PAYLOAD_LENGTH` | 127 |

Override them from the build system:

```cmake
target_compile_definitions(fms_core PUBLIC FMS_MAX_STATES=8 FMS_MAX_TRIGGERS=12)
```

They are compile-wide on purpose: the sizes are baked into the types, so every
translation unit must agree.
