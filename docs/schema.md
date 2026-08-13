# YAML schema

The configuration comes in **two files**, loaded independently:

| File | Sections | Loaded into | Answers |
|---|---|---|---|
| setup | `fsm`, `io` | `fms::Setup` | where this instance runs, how it talks, where it starts |
| machine | `triggers`, `states` | `fms::Model` | what it does |

Neither file knows about the other, and each loader rejects the other's
sections with a diagnostic naming the file it belongs in. The one cross-file
reference — the initial state named by the setup — is checked when the two are
bound in `StateMachine::init`.

---

# The setup file

## `fsm` (required)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | `""` | name of *this instance*, e.g. `car-ecu-01`; diagnostics only |
| `initial` | string | — | **required**, the state to start in. It is only a string here: the setup is loaded on its own and cannot know which states exist |

## `io` (optional)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `state_channel` | string | `""` | where the machine announces each new state |
| `error_channel` | string | `""` | where it reports refused triggers and unknown channels |
| `endpoint` | string | `""` | opaque: broker URI, device path, socket address |
| `identity` | string | `""` | opaque: client id, node name |

All four are handed to the port verbatim through `IPort::configure()`, before
the port is opened. The core never interprets them, and a port may ignore any of
them — `ConsolePort` writes states to `std::cout` and errors to `std::cerr`
whatever the channels say.

```yaml
fsm:
  name: car-ecu-01
  initial: power_off

io:
  state_channel: "car/state"
  error_channel: "car/error"
  endpoint: "tcp://localhost:1883"
  identity: "car-ecu-01"
```

---

# The machine file

## `fsm` (optional)

| Key | Type | Meaning |
|---|---|---|
| `name` | string | name of the *definition*, e.g. `car`; diagnostics only |

`fsm.initial` here is an error: it is a setup key, and the loader says so.

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

---

## Why the split

The machine file is behaviour, reviewable on its own with no endpoints in it.
The setup file is deployment. So the same machine definition can be run several
ways without touching it:

```sh
./car_console car.setup.yaml   car.machine.yaml   # starts in power_off
./car_console bench.setup.yaml car.machine.yaml   # same machine, starts in standing
```

That second file is how you resume after a reset, or drop a test straight into
the state it cares about.

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
