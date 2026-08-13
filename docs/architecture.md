# Architecture notes

## The two phases

```
                setup phase                     |            run phase
  (allocation allowed, happens once)            |   (no allocation, no exceptions)
-------------------------------------------------------------------------------------
  config::load_file --> yaml-cpp heap document  |   transport.poll()
                    --> copied into Model       |     -> Model::find_trigger_for_topic()
                    --> document destroyed      |       -> StateMachine::fire()
  PahoTransport::open (MQTTClient_create)       |         -> publish state, or
  StateMachine::init                            |            publish rejection
  Runtime::init / start (connect, subscribe)    |
```

The boundary is `Runtime::start()`. Everything the run phase touches was sized at
compile time from `fms/limits.hpp` and filled during setup.

## The engine

```cpp
StateId Model::target_of(StateId from, TriggerId trigger) const {
  const StateNode* node = state(from);
  const auto it = node->transitions.find(trigger);   // binary search
  return (it == node->transitions.end()) ? kNoState : it->second;
}
```

`StateMachine::fire()` calls that, and either assigns the result to `current_`
or returns `Status::NoTransition`. There is no other decision anywhere: no guard
to evaluate, no action to dispatch, no payload to parse, no clock to consult.
That is deliberate — a state machine you can hold in your head is one you can
trust in a car.

The cost of a trigger is one `flat_map::find` over at most
`FMS_MAX_TRANSITIONS_PER_STATE` entries, plus one `find` to turn the topic into
a trigger id. Both are binary searches over contiguous memory.

## Why a data-driven table instead of `etl::fsm`

`etl::fsm` models states as classes with compile-time ids, which is excellent
when the machine is known at compile time. Here the machine comes from a file,
so states cannot be types. The transition table is therefore data:

```
flat_map<StateId, StateNode>
                    └── flat_map<TriggerId, StateId>
```

Both are ETL flat containers: a sorted vector of pointers plus a pool, so lookup
is a binary search, insertion happens only during setup, and there are no nodes,
no hashing and no allocation. A plain `flat_map` (not a multimap) is enough
because a trigger has exactly one meaning in a given state.

## Why the loader is a separate target

yaml-cpp reports every problem by throwing, and the project may not use
exceptions. Rather than pretend otherwise, `src/config/yaml_loader.cpp` is built
as its own static library with `-fexceptions`, and its two entry points are
`noexcept` with a `try`/`catch(...)` around the whole body. An exception cannot
cross that boundary; callers get a `Status` and a `Diagnostics` record with a
line number.

This is enforced in CMake, not by convention: `fms_disable_exceptions()` is
applied to every target except `fms_config`.

ETL is configured consistently on both sides — `ETL_THROW_EXCEPTIONS` is never
defined, so ETL never throws anywhere, and `ETL_LOG_ERRORS` is a `PUBLIC` compile
definition so `ETL_ASSERT` expands identically in every TU (an ODR requirement).
`fms::install_etl_error_handler()` routes ETL's internal error reports to stderr
and `abort()`; they only fire on programming errors such as overrunning a fixed
container.

## The transport seam

```
        Runtime
           |  (one virtual call per message, one per publish)
     mqtt::ITransport
       /            \
PahoTransport   LoopbackTransport
 (paho.mqtt.c)    (tests, dry runs)
```

`ITransport` is the only virtual interface in the system. It exists so the
machine can be tested end to end with no broker running, and so paho can be
replaced without touching anything above it.

`PahoTransport` uses the **synchronous** `MQTTClient` API and pumps the socket
from `poll()`, so there is no paho callback thread, no locking, and the state
machine runs on the same thread as the main loop.

The honest limitation: paho itself `malloc`s while a message is in flight.
`PahoTransport::poll()` copies the topic and payload into fixed members and frees
paho's buffers before the handler runs, so nothing heap-allocated reaches the
state machine and nothing this class owns is on the heap — but the last few
hundred bytes inside paho are outside our control. For a hard no-allocation
requirement down to the socket, implement `ITransport` over a static-buffer MQTT
client; nothing above the seam changes.

## Proving the allocation claim

`fms_alloc_guard` replaces global `operator new`/`delete`. `alloc_guard::arm()`
makes any subsequent allocation a counted violation (and, in fatal mode, an
`abort()` with a message on stderr).

`tests/test_no_alloc.cpp` loads a config, starts the runtime, arms the guard and
runs 1250 dispatch cycles covering routing, accepted triggers, rejected triggers,
unknown topics and publishing. It asserts zero violations, and a second test
allocates on purpose to prove the guard would have noticed.

Production builds simply do not link the target.

## Footprint

With the default limits (32 states, 32 triggers, 8 transitions per state), on
x86-64:

| Type | Size |
|---|---|
| `fms::Model` | 27 128 B |
| `fms::StateNode` | 288 B |
| `fms::StateMachine` | 24 B |
| `fms::Runtime` | 216 B |

Tuned to the car config (`-DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
-DFMS_MAX_TRANSITIONS_PER_STATE=5`), `fms::Model` drops to 8 888 B. Set the
macros to what the config actually needs rather than leaving the defaults.

Verifying the two hard constraints on the built artefacts:

```sh
nm -C libfms_core.a | grep -cE '__cxa_throw|_Unwind_Resume'      # 0
nm -C libfms_core.a | grep -E ' U (operator new|malloc)'         # empty
nm -C libfms_config.a | grep -cE '__cxa_throw|_Unwind_Resume'    # non-zero: the firewall
```

## Threading

Single threaded by design. `Runtime::service()` is the only place anything
happens: it polls the transport, which dispatches the message that arrived. To
raise a trigger from another thread, post it to your own queue and drain it from
the same loop — do not call `fire()` concurrently.

## Porting to a bare-metal target

1. Keep `fms_core`; it needs only `<cstdint>`, `<cstddef>`, `<cstring>` and ETL.
2. Replace the transport with your own `ITransport` (CAN, serial, shared memory).
3. Load the config on the host and ship the `Model`, or keep yaml-cpp on a target
   that can afford a heap during boot. Nothing downstream cares where the `Model`
   came from — it is a plain object.
4. Tune `fms/limits.hpp` down, then check `sizeof(fms::Model)` against your
   budget.
