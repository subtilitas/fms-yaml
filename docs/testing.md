# How this library is tested

What is verified, at what scale, and what is not covered. The gates themselves
are listed in [the README](../README.md#quality-gates); this page is the
coverage claim they add up to, and the limits of it.

## The suite in this repository

`ctest` registers up to eight cases. How many depends on the build, and the
conditions are in the sections below the table:

| Case | What it proves |
|---|---|
| `fms_tests` | the doctest suites over `src/` and `include/fms/`, including the no-allocation proof |
| `abi_guard` | a translation unit built with a different capacity fails to link, rather than silently disagreeing about a layout |
| `symbol_check` | no exceptions and no allocation, read out of the built archives with `nm` rather than inferred from the flags |
| `doc_figures` | the type sizes the documentation quotes, measured and compared against this build |
| `car_console_pipe` | a scripted console session against the example binary, compared to `tests/car_session.expected` |
| `car_version` | the binary reports the version `project()` declares |
| `car_config_check` | the shipped YAML loaded and linted by the real binary |
| `car_diagram_check` | the diagram in the README regenerated from the example and compared |

Three of them run a script under `tools/`, and each is registered only when the
build can actually run it. A test that fails because its own file is missing, or
because the build it is in cannot express what it checks, tests nothing.

| Case | Registered when |
|---|---|
| `abi_guard` | GCC or Clang, not MSVC, not a coverage or sanitizer build, and `tools/abi_guard_check.sh` present |
| `symbol_check` | the same conditions, with `tools/symbol_check.sh` present |
| `car_diagram_check` | `FMS_BUILD_INSPECT`, `tools/diagram_sync.py` present, and Python 3 found |
| `doc_figures` | the same conditions as `abi_guard`, with `tools/doc_figures_check.py` present, Python 3 found, and an x86-64 target |

`abi_guard` and `symbol_check` read the built archives, which instrumentation
rewrites, so a coverage or sanitizer build cannot answer the question they ask.
`doc_figures` needs the same `abi_probe.env` those two read, and the sizes it
compares are the ones `docs/architecture.md` states for x86-64. In a build
against any ETL but the pinned one it checks the boundary table below instead,
and says which of the two it did — the `etl-range` legs are exactly that build,
and the figures for the pin differ there by design.

Measured on this tree:

| Build | Cases |
|---|---:|
| default Release, GCC, `tools/` present, x86-64 | 8 |
| coverage (`tools/coverage.sh`) | 5 |
| from the release archive, which carries no `tools/` | 4 |

Four from the archive is the package, not a short run.

## Coverage

Measured over `src/` and `include/fms/`, which `gcovr.cfg` decides:

| | |
|---|---|
| Lines | 1502/1629, 92.2% |
| Branches | 1136/1372, 82.8% |
| Functions | 211/219, 96.3% |

The floor is 90% line coverage, enforced in the tree by
`tools/coverage_report.py --fail-under 90` rather than configured in a service.
Reporting also goes to codecov.io, which counts a partially taken branch as a
partial line and therefore reads lower.

Branch coverage compares only within one toolchain, because it counts the edges
the compiler emitted. One tree measured 72.2% branches under GCC 13 and 68.9%
under GCC 11 with line coverage identical in both.

## Capacities

Every constant in `fms/limits.hpp` is a compile-time capacity that becomes part
of the layout of `Model`, `Setup`, `Args` and `Runtime`, so a suite built only
at the defaults exercises one instantiation of each of those types.
`tools/capacity_sweep.sh` builds and runs the suite at three configurations:
the defaults, a tight one, and a wide one.

A test that names a capacity derives it from `fms::limits::` or skips with a
printed reason. Eight assertions here once tested the default configuration
rather than the library, one of them passing vacuously; the sweep is what
notices.

## Toolchains and targets

| | |
|---|---|
| Built and tested on every push | GCC 13 and Clang 18 on Linux, MSVC 2022 on Windows |
| Compiled, not run | `fms_core` and `fms_inspect` for a Cortex-M4, no OS |
| Runtime analysis | ASan and UBSan over the suite, on Linux |

MSVC builds with `/W4 /WX`. It is the only gate that has caught
`C4127 conditional expression is constant` where two compile-time constants are
compared, and the `std::fopen` deprecation.

`fms_core` is not freestanding. See
[architecture.md](architecture.md) for the reason and what a target build needs.

## Testing outside this repository

An adversarial suite runs against tagged commits from a separate tree. It is
not part of this repository, and a reader cannot run it. It is recorded here
because it is a real part of what 1.0 rests on, and because its limits are part
of the claim.

What it does: generates models and drives them against an independent shadow
model, compares the two, and checks the loader against malformed and hostile
input. It runs under ASan, UBSan and LeakSanitizer.

Scale at `v1.0.0-rc2`: 300 runs over 20 seeds, five suites and three capacity
configurations, 203,187,221 checks. A single sweep at one seed across the three
configurations is 10,161,660 checks.

What it has reported, by class:

| Class | Count |
|---|---|
| In the library | 1 |
| In a dependency, refused by the library before it is reached | 1 |
| Pre-existing library behaviour, documented as a limitation rather than fixed | 1 |
| In this project's own test suite | 1 |
| In a quality gate | 3 |

The one library defect was introduced by the fix for the dependency one: a
readability probe added to refuse an unreadable path consumed the first byte of
a non-seekable source. The documented limitation is the writerless FIFO in
`yaml_loader.hpp`, described in [schema.md](schema.md).

What it does not reach:

- One toolchain. GCC 13.3.0 on x86-64 only.
- Twenty seeds is a sample, not a fuzzing campaign.
- The release archive. It builds from the tree.
- A real later release. `SameMajorVersion` is a promise about a 1.1 that does
  not exist yet; `tools/install_check.sh` tests the rule the installed file
  implements by relabelling a copy of it, which is not the same as installing a
  real 1.1 and asking a 1.0 consumer.

## ETL versions

Every other gate builds against the pinned ETL. A consumer supplying its own
takes the `find_package(etl REQUIRED)` branch and may bring another version, so
`tools/etl_range_check.sh` builds and tests against nine: 20.39.0, 20.39.4 (the
pin), 20.40.0, 20.40.1, 20.41.0, 20.43.0, 20.44.0, 20.46.0 and 20.48.1.

20.48.1 is there because it is another ETL-based library's pin, not because
anything here wants it. Two libraries that both fetch ETL into one binary
compile against whichever of them declared it first, so a composed build can put
this library on a version it did not choose.

All of them compile clean under `-Werror` and pass the full suite; the interface
is stable across them. What is not stable is size. Between the `20.40.0` and
`20.40.1` tags `etl::vector` lost 8 bytes per instance, which reaches the `fms`
types that hold ETL containers by value:

| | 20.39.0 – 20.40.0 | 20.40.1 – 20.48.1 |
|---|---:|---:|
| `etl::vector<int,8>` | 64 | 56 |
| `fms::Model` | 49 728 | 47 376 |
| `fms::Args` | 464 | 456 |
| `fms::Runtime` | 688 | 680 |
| `fms::Setup` | 576 | 576 |

`tools/etl_range_check.sh` records these per version rather than asserting them,
because no binary compatibility is promised in either direction. The table
itself is asserted, though — `doc_figures` checks the column its own ETL falls
in, so every leg of the matrix confirms one side of it, and an ETL in neither
column fails because the columns name their ends. A size that moves is news; a
page that describes sizes nothing makes is a defect. Two versions below the boundary
stay in the matrix so that it keeps being measured: a matrix covering one side
only would not notice the next such change.

The ABI guard in `fms/abi.hpp` does not cover this. Its tag is built from the
`FMS_MAX_*` capacities alone, so two translation units compiled against ETL
either side of the boundary carry the same tag, disagree about
`sizeof(fms::Model)` by 2 352 bytes, and link without a diagnostic. Building
the library and its consumer against one ETL version is the reader's
responsibility, and `find_package(etl REQUIRED)` in `CMakeLists.txt` states no
constraint that would enforce it.

One tag reports a version that is not its own: `20.40.1` ships an
`etl/version.h` reading `20.41.1`. `find_package` compares the header, so a
constraint of `20.41.0` or above accepts that tag. `tools/etl_range_check.sh`
reports both in every row, because the version the build compiled against is the
header's and the version the matrix leg is named for is the tag's. The table
above is by tag; the one the job prints carries each.

`tools/etl_latest_check.sh` compares the pin against ETL's published tags, and
the weekly `Supply chain` workflow opens one issue when it falls behind. It
reports; moving the pin is a decision, and a bump across the 20.40.1 boundary
changes the sizes `docs/architecture.md` quotes.

There is no ETL 21.x. The newest tag is 20.48.1, so a version range with an
upper bound of 21.0.0 is a claim about code that does not exist — and because
ETL's package is `SameMajorVersion`, `find_package(etl 20.39.0...21.0.0)` is
refused by every ETL that does. Excluding the endpoint needs `...<21.0.0`.

## Verifying a release

A release is verified from its published archive, not from the tree that
produced it. This has caught a packaging defect before.

```sh
gh release download v1.0.0 --repo subtilitas/fms-yaml
sha256sum -c SHA256SUMS
tar -xzf fms-yaml-1.0.0.tar.gz
cmake -S fms-yaml-1.0.0 -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure    # four cases; see above
build/car_console --version
```

The tag and `project()` are compared by the release workflow before anything is
published, because the two disagreed once. A prerelease suffix is the tag's
business alone, so `v1.0.0-rc2` is compared as `1.0.0`.

## What is not tested anywhere

- Any 32-bit target. `g++ -m32` is installed on the machine that runs the
  adversarial suite and is unused.
- Any compiler other than GCC 13, Clang 18 and MSVC 2022.
- The ARM build is compiled and never run. There is no hardware in the loop and
  no emulator.
- A yaml-cpp or doctest version other than the pinned 0.8.0 and 2.4.11. A newer
  one may work and is not tested. ETL is the exception: the `etl-range` job
  builds and tests against the versions listed under
  [ETL versions](#etl-versions) above.
- Concurrent use. The library is single threaded by design, which
  [architecture.md](architecture.md#threading) states; no test drives one
  `Runtime` from two threads, because doing so is outside what the design
  offers.
