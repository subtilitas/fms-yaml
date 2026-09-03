# Security

## Reporting

Report a suspected vulnerability through GitHub's private reporting: the
**Security** tab of this repository, then **Report a vulnerability**. That
opens a private advisory visible only to the maintainers.

Do not open a public issue for something you believe is exploitable.

Expect an acknowledgement within seven days. If a report is accepted, the fix
and the advisory are published together.

## What is in scope

The library reads two YAML files at run time and nothing else. Its attack
surface is therefore:

| Component | Handles | Notes |
|---|---|---|
| `fms_config` | the configuration files | the only code that parses untrusted bytes; it wraps yaml-cpp 0.8.0 |
| `fms_core` | trigger channels and `key=value` arguments from a port | fixed capacity throughout, no allocation, no exceptions |
| `fms_console` | lines on `std::cin` | a fixed line buffer |

A crash, a read or write outside an object, or an unbounded allocation reached
from any of those — from a configuration file or from port input — is a
vulnerability. So is anything that gets an exception out of `fms_config`, since
the rest of the library is compiled without them.

Ports other than the two shipped ones are yours. `fms::IPort` is documented in
[docs/architecture.md](docs/architecture.md#writing-a-port), and the contract
there — no allocation, no exceptions, views that stay valid until the next call
— is what the core relies on.

## What is not

* **A machine that describes something nobody meant.** An unreachable state or
  a guard that can never hold is a configuration mistake; `--lint` reports six
  kinds of them. The loader accepts any file that is a valid description.
* **Exceeding a compile-time capacity.** Too many states, a name longer than
  `FMS_MAX_NAME_LENGTH`, more arguments than `FMS_MAX_ARGUMENTS`: each is a
  load-time or dispatch-time error with a status code, never a truncation and
  never an overrun.
* **A trigger being refused.** `NoTransition` and `GuardRejected` are the
  machine working.
* **Mixing capacity configurations.** Two translation units that disagree about
  an `FMS_MAX_*` would be a memory-safety problem, which is why they do not
  link: see [the capacity guard](docs/architecture.md#the-capacity-guard).
* **A configuration file you did not review.** The file is the program. Treat it
  the way you would treat the source.

## Supported versions

Before 1.0, only the most recent release. What a version number promises is in
[docs/stability.md](docs/stability.md).
