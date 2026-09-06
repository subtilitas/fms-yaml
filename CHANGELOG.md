# Changelog

Notable changes per release. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); what the version
numbers promise is in [docs/stability.md](docs/stability.md).

## Unreleased

### Added

- `doc_figures` checks the ETL boundary table in `docs/testing.md` in every
  build, not only at the pin. Each column belongs to one side of ETL's 20.40.1
  layout boundary, so each `etl-range` leg confirms the column its own ETL falls
  in, and an ETL in neither column fails — the columns name their ends, so a
  matrix that grew past them would be measured against nothing.

- `tools/etl_latest_check.sh` and the weekly `Supply chain` workflow: the pinned
  ETL is compared against what ETL has published, and one issue is opened and
  kept updated when it falls behind. It reports rather than bumps — a pin
  crossing ETL's 20.40.1 layout boundary changes the sizes the documentation
  quotes, so moving it is a decision. The pin is 20.39.4 and ETL is already
  ahead of it; the issue carries how far.

- `tools/doc_figures_check.py` and the `doc_figures` test: the type sizes
  `docs/architecture.md` quotes, and the pair `include/fms/abi.hpp` and
  `tools/abi_guard_check.sh` repeat in their comments, are measured and compared
  against the build. They were correct for the pinned ETL at the default
  capacities and nothing checked them, so moving the pin above ETL 20.40.1 would
  have left every one of them describing a build nobody makes. The figures are
  measured, never listed in the gate.

- ETL 20.48.1 in the `etl-range` matrix. It is another ETL-based library's pin,
  and two libraries that both fetch ETL into one binary compile against
  whichever declared it first, so a composed build can put this library on a
  version it did not choose. It builds clean and passes the suite, and its
  container sizes are the ones every version from 20.40.1 up reports, so it adds
  a leg rather than a band.

### Fixed

- `tools/etl_range_check.sh` reports the tag as well as the version the headers
  declare. The two differ on the one tag that sits on the layout boundary —
  20.40.1 ships an `etl/version.h` reading 20.41.1 — so the row that documents
  where the layout moved was labelled 20.41.1, and the table had no 20.40.1 row
  at all while the matrix leg that produced it was named 20.40.1.

- Each `etl-range` leg writes a complete table to its step summary. The rows do
  not accumulate across legs: `GITHUB_STEP_SUMMARY` is per job, and every leg is
  its own job, so a bare row rendered as a line of pipes.

## [1.0.1] - 2026-09-05

A build that was already wrong now fails to build. If a consumer's ETL lays out
its containers differently from the ETL `fms_core` was compiled with, the link
step refuses it, where 1.0.0 accepted it and produced two layouts for one type.
Nothing else changes: no interface moves, and a consumer whose ETL agrees with
the library sees no difference.

The versions that agree with each other are the ones that lay
`etl::vector`, `etl::flat_map` and `etl::string` out identically, not the ones
that share a version number. ETL 20.39.0, 20.39.4 and 20.40.0 are
interchangeable here; 20.40.1 and later are not interchangeable with them.

This is a patch release because no promise on
[docs/stability.md](docs/stability.md) changes. Binary compatibility was never
offered in either direction; the guard that was supposed to enforce that only
covered half of what decides a layout.

### Fixed

- **The ABI guard now covers ETL, not only the capacities.** ETL decides what
  the containers inside `Model`, `Setup`, `Args` and `Runtime` cost, and it
  moved `sizeof(etl::vector)` by 8 bytes between its 20.40.0 and 20.40.1 tags
  with no interface change — `sizeof(fms::Model)` is 49 728 below that boundary
  and 47 376 above it. Neither `find_package(etl)` nor the exported
  `find_dependency(etl)` states a version, so a consumer whose ETL differed from
  the one the library was built with compiled, linked and ran with two layouts
  for one type, exactly as a consumer differing in a capacity did before
  `fms/abi.hpp` existed. `fms::abi::pin()` now names a second symbol carrying
  the container sizes, and the mismatch is an unresolved
  `fms::abi::etl_pin<...>` at link time.

  It pins the layout rather than the ETL version: two versions that lay the
  containers out identically still link, so 20.39.0, 20.39.4 and 20.40.0 are
  interchangeable against each other and 20.41.0 and later are not.

### Added

- `fms_yaml_ETL_VERSION` in the installed package config, recording which ETL
  the library was built against. The link error names the consumer's numbers;
  this is the other half of the comparison.

- `tools/etl_range_check.sh` and the `etl-range` CI matrix: the library is built
  and tested against eight ETL versions through `find_package`, which is the
  path a consumer supplying its own ETL takes. ETL moved `sizeof(etl::vector)`
  by 8 bytes between the `20.40.0` and `20.40.1` tags with no interface change,
  so `fms::Model` is 49 728 bytes below that boundary and 47 376 above it while
  every version compiles clean and passes. Two versions below the boundary stay
  in the matrix so it keeps being measured. Sizes are recorded in the step
  summary, not asserted: `docs/stability.md` promises no binary compatibility in
  either direction. Each leg asserts that `find_package` resolved the ETL it was
  given, because `FMS_FETCH_DEPS` defaults to on and a leg that quietly falls
  back to the pin tests the pin eight times.

- `tools/etl_range_check.sh --check` fails when the `etl-range` matrix stops
  listing the pinned ETL version, which would leave the matrix testing a range
  the project does not itself build at.

## [1.0.0] - 2026-09-04

Released from `v1.0.0-rc1` and `v1.0.0-rc2`. Neither candidate nor this release
changes `src/` or `include/` against the other: rc2 carried the
`tools/install_check.sh` version-file probe described below, which rc1 predates,
and 1.0.0 adds `docs/testing.md` and the links to it.

1.0 is not a statement that the code changed. It is a statement that
[docs/stability.md](docs/stability.md) is now a promise rather than a
description: the headers listed there are the interface, the YAML schema and the
exported CMake targets are covered with them, and what is deliberately not
covered is deliberate.

### Changed

- **The major is the breaking number.** Through 0.x the minor carried that
  meaning, which is why 0.9 and 0.10 are not interchangeable. From here 1.2 is
  compatible with 1.0 and 2.0 need not be.
- `write_basic_package_version_file` moves from `SameMinorVersion` to
  `SameMajorVersion` to match, so `find_package(fms_yaml 1.0)` accepts a later
  1.x install. `tools/install_check.sh` asserts whichever of the two the
  declared version means, and that the mode declared in `CMakeLists.txt` is that
  one. The two rules cannot be told apart by `find_package` at a `.0` version,
  so the mode is compared directly, and the installed
  `fms_yamlConfigVersion.cmake` is asked a second time through a copy relabelled
  to the next minor — the one place the two rules give different answers.

### Added

- [docs/testing.md](docs/testing.md): what is verified, at what scale, and what
  is not covered — the suite, the coverage numbers, the capacity sweep, the
  toolchains, the adversarial testing that runs outside this repository, and
  how a release is verified from its published archive.

- `tools/install_check.sh` checks what `find_package` accepts. The
  compatibility `docs/stability.md` describes was prose with no gate:
  `tests/consumer` asks for no version at all, so changing the mode would have
  left the page quietly false. A `project()` version that is not
  MAJOR.MINOR.PATCH is refused with a stated reason, because every rule the
  script derives reads exactly three components.

## [0.10.0] - 2026-09-04

Not compatible with 0.9.x. `Status::FileNotReadable` is a status a call could
not previously return: a path that opens and whose first read fails — a
directory, a device that refuses to be read — answered `ParseError` in 0.9.0 and
answers this instead. Before 1.0 the minor is the breaking number; see
[docs/stability.md](docs/stability.md).

### Added

- `tools/capacity_sweep.sh` and the `capacities` CI job: the suite is built and
  run at three capacity configurations, including the tuning
  `docs/architecture.md` gives for the car example. Every other gate builds at
  the defaults, which is one instantiation of the types the capacities size.

- `Status::FileNotReadable`, for a path that opens and whose first read fails.

### Fixed

- Eight test cases asserted the default capacity configuration rather than the
  library: argument lists written out as four pairs, a 400-character line
  against a buffer sized by two capacities, and whole diagnostic sentences
  against a message buffer that clips them. They derive from `fms::limits::`
  now, or skip with a printed reason where a configuration cannot express them.
- A configuration on a source that cannot be reopened — a FIFO, a socket — was
  parsed from its second byte. The readability check opened the path, read one
  byte and closed it, and yaml-cpp then opened it again; a regular file starts
  from the beginning both times, a FIFO does not. The result was a document
  silently altered before parsing, reported as a schema error. The path is now
  opened once and parsed from that stream, and readability is decided with
  `peek`, which takes nothing from it.
- A `FileNotReadable` message put the path before the reason, so a path longer
  than `FMS_MAX_MESSAGE_LENGTH` clipped the reason away. The reason comes first.
- A path that opened and could not be read — a directory, or a device such as
  `/proc/self/mem` — reached yaml-cpp, which leaked 2048 bytes per call:
  `YAML::Stream` allocates its prefetch buffer with a raw `new[]` in its
  member-initializer list and frees it only in `~Stream`, and the
  `std::ios_failure` libstdc++ raises from the failed read escapes the
  constructor, so that destructor never runs. A loader retrying such a path
  leaked it once per attempt. The path is now rejected before yaml-cpp sees it,
  and reported as `FileNotReadable` rather than `ParseError`. Upstream defect,
  reported against yaml-cpp 0.8.0.

## [0.9.0] - 2026-09-03

The version says 0.9 rather than 0.3 because what changed is the ground the
library stands on rather than what it does: it can be installed, it says which
version it is, it states what a version number promises, and the constraints it
has always claimed are now read out of the built artefacts. The machine, the
schema and the linter are unchanged.

Not compatible with 0.2.x. Before 1.0 the minor is the breaking number; see
[docs/stability.md](docs/stability.md).

### Added

- `find_package(fms_yaml)`: install rules, an export set and a package config.
  The exported targets are `fms::core`, `fms::config`, `fms::console`,
  `fms::inspect` and `fms::alloc_guard`, and only those that were built are
  exported. Installing requires `-DFMS_FETCH_DEPS=OFF`.
- `fms/version.hpp`, generated by CMake from `project()`:
  `FMS_VERSION_MAJOR`, `FMS_VERSION_MINOR`, `FMS_VERSION_PATCH`,
  `FMS_VERSION_STRING`, a comparable `FMS_VERSION`, and `fms::version()`.
- `car_console --version`, reporting the version and the capacities.
- `fms/abi.hpp`: the capacities are pasted into the name of a symbol that the
  constructors of `Model`, `Setup`, `Args` and `Runtime` reference, so a
  translation unit compiled with different ones fails to link instead of
  disagreeing about `sizeof(fms::Model)`.
- `FMS_BUILD_CONFIG`. Off drops the YAML loader and yaml-cpp with it, which is
  the shape a firmware build takes and what makes the tree cross-compilable.
- [docs/stability.md](docs/stability.md): which headers are the public
  interface, what is deliberately not covered, and what each kind of change
  costs in version numbers.
- This file, [CONTRIBUTING.md](CONTRIBUTING.md) and
  [SECURITY.md](SECURITY.md).
- Gates: `abi_guard`, `symbol_check` and `car_version` in ctest; `install` and
  `cross` in CI. `release.yml` checks the tag against `project()` before
  publishing, and dependabot watches the actions.

### Changed

- The `FMS_MAX_*` capacities are cache variables that become `PUBLIC` compile
  definitions on `fms_core`, so `cmake -DFMS_MAX_STATES=8` configures the whole
  tree and reaches installed consumers. It previously set a cache variable
  nothing read.
- MSVC builds with `/W4 /WX`, as GCC and Clang already did with `-Werror`.
- Every target states its own exception policy rather than inheriting one.

### Fixed

- A capacity set on one target only was an ODR violation with no diagnostic:
  `sizeof(fms::Model)` is 49728 with the defaults and 21120 with
  `FMS_MAX_STATES=8 FMS_MAX_TRIGGERS=12`, and the two linked together.
- `-DFMS_MAX_STATES=0` configured a tree whose containers hold nothing.
- The documented ways to override a capacity were both wrong:
  `docs/architecture.md` showed a form that did nothing, `include/fms/limits.hpp`
  and `docs/schema.md` a form that caused the mismatch above.
- `$<INSTALL_INTERFACE:include>` was hardcoded, so any layout but the default
  gave a consumer an include directory the headers were not in.
- `FMS_VERSION_AT` packed three fields into two digits, making 1.0.0, 0.100.0
  and 0.99.100 the same number.
- `tests/test_lint.cpp` failed when `2 * FMS_MAX_STATES` equalled
  `FMS_MAX_FINDINGS` exactly.
- `docs/architecture.md` claimed `fms_core` compiles freestanding. It does not:
  ETL 20.39.4's `etl/limits.h` includes `<math.h>` unconditionally, and
  libstdc++ 13 refuses `<cmath>` when `__STDC_HOSTED__` is 0. A target build
  needs a hosted C++ library such as newlib's.
- `src/config/yaml_loader.cpp` used `std::fopen`, which MSVC deprecates.

## [0.2.1] - 2026-09-01

### Added

- CodeQL over a real build, and a badge per quality gate.

### Changed

- The docs are written as a specification, and the claims that did not hold
  when checked against the code are corrected — notably the scope of the
  no-allocation proof.
- Coverage is published to codecov.io from the report `gcovr.cfg` filtered,
  rather than uploaded from a search of the workspace. The floor moved to 90%
  and lives in `tools/coverage_report.py`, not in the service.

### Fixed

- `project()` said 0.2.0 in the 0.2.1 release.

## [0.2.0] - 2026-08-31

First tagged release.

### Added

- A state machine described by two YAML files: a machine file (triggers,
  states, guarded alternatives) and a setup file (instance name, initial state,
  io). Neither references the other; the one cross-file check is in
  `StateMachine::init`.
- Trigger arguments as `key=value` views over the port's buffer, and guards as
  declarative comparisons against them.
- `fms::IPort`, eight methods, five with defaults. A console port and an
  in-memory test port ship; the core has no transport dependency.
- `fms::lint`: six checks for machines that load and still cannot mean what
  they say.
- `fms::diagram`: mermaid and graphviz, generated from the loaded machine.
- The heap trap (`fms_alloc_guard`) and the no-allocation proof that uses it.
- CI, coverage, clang-tidy, cppcheck, the sanitizers, and a release workflow.

[1.0.1]: https://github.com/subtilitas/fms-yaml/releases/tag/v1.0.1
[1.0.0]: https://github.com/subtilitas/fms-yaml/releases/tag/v1.0.0
[0.10.0]: https://github.com/subtilitas/fms-yaml/releases/tag/v0.10.0
[0.9.0]: https://github.com/subtilitas/fms-yaml/releases/tag/v0.9.0
[0.2.1]: https://github.com/subtilitas/fms-yaml/releases/tag/v0.2.1
[0.2.0]: https://github.com/subtilitas/fms-yaml/releases/tag/v0.2.0
