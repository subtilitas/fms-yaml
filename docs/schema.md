# YAML schema

Two files, loaded independently:

| File | Sections | Loaded into | Answers |
|---|---|---|---|
| setup | `fsm`, `io` | `fms::Setup` | where this instance runs, how it talks, where it starts |
| machine | `triggers`, `states` | `fms::Model` | what it does |

Neither references the other; each loader rejects the other's sections with a
diagnostic naming the file they belong in. The one cross-file reference — the
initial state named by the setup — is checked in `StateMachine::init`.

Both are read when the program runs. Nothing in the build system opens, copies,
validates or depends on them, so a mistake surfaces at start-up: every
diagnostic carries a line number, and `--check` loads a pair, reports what they
describe and exits.

This page is what the **loader** enforces. A file can satisfy all of it and
still describe a machine nobody meant — an unreachable state, an alternative
behind a fallback, a guard that cannot hold. The linter reports those, over the
loaded machine rather than the text; see
[what a valid file can still get wrong](../README.md#what-a-valid-file-can-still-get-wrong).

---

# The setup file

## `fsm` (required)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | `""` | name of *this instance*, e.g. `car-ecu-01`; diagnostics only |
| `initial` | string | — | **required**, the state to start in. Only a string here: the setup is loaded alone and cannot know which states exist |

## `io` (optional)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `state_channel` | string | `""` | where the machine announces each new state |
| `error_channel` | string | `""` | where it reports refused triggers and unknown channels |
| `endpoint` | string | `""` | opaque: broker URI, device path, socket address |
| `identity` | string | `""` | opaque: client id, node name |

All four reach the port verbatim through `IPort::configure()`, before it is
opened. The core never interprets them and a port may ignore any of them —
`ConsolePort` writes to `std::cout` and `std::cerr` whatever the channels say.

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

A channel is an opaque address: a word on stdin, an MQTT topic, a CAN
identifier. One channel per trigger and one trigger per channel, so routing is a
single lookup and input is never ambiguous. Two triggers on one channel is
`Status::DuplicateName`.

```yaml
triggers:
  - {name: brake_pressed}                              # channel == "brake_pressed"
  - {name: brake_released, channel: "car/brakes/off"}  # explicit
```

### Arguments

A trigger may carry `key=value` pairs — on the console, everything after the
first word:

```
> self_test_passed errors=0
> throttle_pressed pedal=42 mode=sport
```

They are declared nowhere: the port hands the text over, the core parses it into
views over the port's buffer, and guards compare against it. Values stay text
until something asks for a number. Up to `FMS_MAX_ARGUMENTS` pairs; a malformed
list (a token without `=`, a repeated key) is reported on the error channel and
changes nothing.

## `states` (required, non-empty sequence)

| Key | Type | Meaning |
|---|---|---|
| `name` | string | **required**, unique |
| `transitions` | mapping `trigger: outcome` | optional; a state with none accepts nothing |

An outcome has three spellings:

```yaml
states:
  - name: self_test
    transitions:
      ignition_off: power_off                            # 1. a state name

      self_test_failed: {when: "errors > 0", target: fault}   # 2. one guarded alternative

      self_test_passed:                                  # 3. ordered alternatives
        - {when: "errors == 0", target: standing}
        - {target: fault}                                #    unguarded: the fallback
```

Alternatives are tried in the order written; the first whose guard holds wins.
An entry without a `when` always holds, so it is the fallback and anything after
it is unreachable.

`transitions` is a mapping, so a state cannot list the same trigger twice and
all alternatives for a trigger are in one place.

Self-transitions are allowed (`brake_pressed: standing` inside `standing`) and
are how you say "accepted, but nothing changes". An accepted trigger always
republishes the state, even when it did not change.

### Guards

One comparison against one argument:

```yaml
when: "pedal > 30"                            # a single condition
when: ["severity >= 2", "system == engine"]   # a list is ANDed
```

| | |
|---|---|
| Operators | `==` (or `=`), `!=`, `<`, `<=`, `>`, `>=` |
| Types | integers and text. `<` `<=` `>` `>=` need an integer literal; text takes only `==` and `!=` |
| AND | several conditions under one `when` |
| OR | several alternatives |
| Missing argument | the condition is false — a guard decides, it never errors |
| Unparsable value | same: `pedal=fast` against `pedal > 30` is false |

Parsed at load time, so `when: "pedal"` or `when: "mode > sport"` is a config
error with a line number. No arithmetic, no nesting, no negation beyond `!=`.

### What rejection looks like

| Situation | `fire()` returns | Reported on the error channel |
|---|---|---|
| the state does not list the trigger | `Status::NoTransition` | `rejected: <trigger> in state <state>` |
| it lists it, but no guard held | `Status::GuardRejected` | `rejected: <trigger> in state <state>: no guard matched (<arguments>)` |
| the arguments were malformed | — | `bad arguments for <trigger>: <reason>` |
| nothing listens on that channel | — | `unknown channel: <channel>` |

The state is unchanged in every case. The guard message includes the arguments,
because that is what you need when a trigger you expected to work does not.

---

## Why the split

The machine file is behaviour, reviewable with no endpoints in it; the setup
file is deployment. One definition, several deployments:

```sh
./car_console car.setup.yaml   car.machine.yaml   # starts in power_off
./car_console bench.setup.yaml car.machine.yaml   # same machine, starts in standing
```

That second file is how you resume after a reset, or drop a test straight into
the state it cares about.

## Limits

Every string and container is fixed capacity. Exceeding one is a load-time
error — `NameTooLong`, `ChannelTooLong`, `CapacityExceeded` — never a silent
truncation. Defaults from `include/fms/limits.hpp`:

| Macro | Default | |
|---|---|---|
| `FMS_MAX_STATES` | 32 | |
| `FMS_MAX_TRIGGERS` | 32 | |
| `FMS_MAX_TRANSITIONS_PER_STATE` | 8 | triggers one state may list |
| `FMS_MAX_ALTERNATIVES` | 4 | guarded outcomes for one trigger |
| `FMS_MAX_CONDITIONS_PER_GUARD` | 3 | conditions ANDed in one `when` |
| `FMS_MAX_CONDITIONS` | 64 | machine-wide condition pool |
| `FMS_MAX_ARGUMENTS` | 4 | `key=value` pairs one trigger may carry |
| `FMS_MAX_NAME_LENGTH` | 31 | |
| `FMS_MAX_CHANNEL_LENGTH` | 95 | |
| `FMS_MAX_MESSAGE_LENGTH` | 127 | |

```sh
cmake -S . -B build -DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
```

They are compile-wide on purpose: the sizes are baked into the types, so every
translation unit must agree. The build carries whatever is set here to every
target as a `PUBLIC` compile definition on `fms_core`, and a translation unit
compiled outside the build with different values fails to link rather than
disagreeing silently — see
[the capacity guard](architecture.md#the-capacity-guard).
