# Changelog

All notable changes to undoPLC are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.2] - 2026-10-04

### Added

- `UndoLatch` (`include/undoLatch.hpp`), a one-shot countdown latch built on
  `std::mutex` and `std::condition_variable`.
- `tests/test_latch`, covering the latch opens only after the last arrival, that
  it never opens early, that `wait()` is idempotent and thread safe, the
  Master/Worker registration handshake, and concurrent arrivals.
- `tests/CMakeLists.txt`, so `-DBUILD_TESTS=ON` configures instead of failing on
  a missing `CMakeLists.txt`.

### Changed

- `UndoMasterTaskBase` now uses `UndoLatch` instead of `std::latch`.
- CMake standard lowered from C++20 to C++17 across the library and the tests.

### Fixed

- The project version in `CMakeLists.txt` was left at 0.2.0 while
  `include/version.hpp` and the `v0.2.1` tag moved to 0.2.1, so packaging
  reported the wrong version. Both now agree.

`std::latch`, introduced in 0.1.1, was the only C++20 construct undoPLC used, so
this removes the C++20 requirement from undoPLC's own code.

Two notes:

- `undoCore` still declares `target_compile_features(undoCore INTERFACE
  cxx_std_20)`, which raises consumers to C++20 through CMake even though its
  headers are C++17 clean. Dropping that line in undoCore is what actually
  reaches consumers.
- `UndoLatch::count_down()` asserts if called more often than the initial count,
  where `std::latch` has undefined behaviour. A double arrival now fails loudly
  instead of surfacing later as an unexplained hang.

## [0.2.1] - 2026-07-05

### Changed

- `UndoSys::setCpuNominalFrequency()` returns an `int` instead of a `bool`: `1`
  for success, `0` when a core does not expose `base_frequency`, `-1` when a
  sysfs write fails.
- Its contract is now documented as strict rather than best effort: every
  requested core must be fully controllable, and the operation is aborted if any
  of them is not.
- Added `_isNominalFreqPossible` to record whether frequency locking is available.

### Removed

- `src/libundoPLC.a` was tracked in the repository and is no longer committed.

## [0.2.0] - 2026-06-27

### Added

- `undoCore` added as a submodule, replacing the previously vendored copy.
- `UndoMasterTaskBase::setIoBus()` and a `waitCycle()` abstraction. When an
  `undoCore::IoBus` is set, the master follows the bus cycle and `copyIn()`
  happens inside `waitCycle()`; otherwise it falls back to
  `clock_nanosleep()`.
- `readInputBus()` and `writeOutputBus()` became virtual with no-op defaults, so
  a busless master no longer has to implement them.
- Project logo and CI workflow updates.

## [0.1.1] - 2026-06-23

### Added

- Thread registration handshake in `UndoMasterTaskBase`, using `std::latch` with
  `waitAllRegistered()` and `countDownRegistration()` so the master does not
  start cycling before every worker has registered.
- Basic diagnostics struct for the Master.

### Changed

- Rewrote the README.

This is the release that made C++20 a requirement, through `std::latch`.

## [0.1.0] - 2026-06-20

### Added

- First release, from the `v0.1.0-release` branch.
- `UndoSys` singleton with TSC frequency helpers, and `version.hpp` with the
  version macros.
- `UndoLog` with multi-domain deferred logging.
- CPU core handling, and vendored Boost and `tsc_freq_khz` under `third_party/`.

[Unreleased]: https://github.com/undoRT/undoPLC/compare/v0.2.2...HEAD
[0.2.2]: https://github.com/undoRT/undoPLC/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/undoRT/undoPLC/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/undoRT/undoPLC/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/undoRT/undoPLC/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/undoRT/undoPLC/releases/tag/v0.1.0
