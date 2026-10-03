# Verification

Run from the project directory:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Repeat with release, asan-ubsan, or tsan. Sanitizers use separate builds; CMake
rejects enabling ASan and TSan together. The test executable checks actual
behavior in Release too; it does not rely on assertions removed by NDEBUG.

Tests cover immutable source preservation, ordered operations, owned inputs,
old snapshot lifetime, independent transaction copies, moved-from reuse,
embedded-zero keys/values, limit failures and exact boundaries, empty-batch
versioning, and eight simultaneous readers. Atomic publication tests belong to
Phase 2 adds publication tests below. Allocation failure injection and version
exhaustion coverage is described in the Phase 3 section below.

Formatting:

```bash
find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) -exec clang-format --dry-run --Werror {} +
```

When clang-tidy is available, configure a separate build with
`-DCMAKE_CXX_CLANG_TIDY=clang-tidy` to run the checked-in analyzer settings.
CI includes this check and an external installed-package consumer.

## Local results — 2026-10-02

- GCC 13.3, CMake 3.28.3, Ninja 1.11.1.
- Debug and Release: compilation with warnings-as-errors and tests passed.
- ASan/UBSan: full tests passed outside the sandbox, including default leak
  detection. In the sandbox LeakSanitizer reported unsupported ptrace.
- TSan: compilation passed; execution outside the sandbox failed before tests
  with `FATAL: ThreadSanitizer: unexpected memory mapping`. Runtime validation
  is unavailable on this host; no TSan-clean claim is made.
- Installation and separate find_package consumer compiled, linked, and ran.
- clang-format 18 formatting checked locally.
- Clang compiler/clang-tidy are unavailable locally. Their CI checks are
  configured but have not run remotely; the project has not been published.

This foundation does not yet measure map update concurrency or production
performance. Those are tracked explicitly in ROADMAP.md.

## Phase 2 verification — 2026-10-02

Both CTest groups passed in Debug, Release, and ASan/UBSan (including default
leak detection outside the sandbox). The new map group checks conditional
conflicts, replacement/duplicate keys, empty updates, close/idempotence,
retained handles after destruction, failed-update preservation, concurrent
unique-key writers, competing version-conditional writers, and complete-batch
reader visibility. Installed-package consumption now exercises ReadMostlyMap
and its exported thread dependency successfully.

The TSan map group passed on this run, but the snapshot group failed during
runtime startup with unexpected memory mapping. The overall TSan suite is
therefore not validated on this host. Formatting and diff checks passed.
Clang/clang-tidy and remote CI have not been run for these local changes.
Performance has not yet been benchmarked.

## Phase 3 verification — 2026-10-02

Four CTest groups cover the snapshot domain, publication, allocation failures,
and extended correctness. The allocation-failure executable replaces ordinary
global new/delete only in that test process. A thread-local budget sweeps each
ordinary allocation position until success, using long strings to exercise
heap storage. In the GCC build, commit exercised 8 failure positions and
replace_all exercised 13. Every failure preserves the published data/version
and retained handles; a subsequent write succeeds. Rejected closed/conflicting
updates are checked with a zero-allocation budget. This does not test aligned
allocation or arbitrary user-defined allocators, which the current API does
not use.

A seeded 500-batch reference model validates operations and old snapshots.
Four writers also execute mixed clear/erase/assign updates: recorded commit
versions establish a replay order, and retained snapshots are compared with
the corresponding reference state. Close races verify that successful writes
account for exactly the final version and all writes after close are rejected.

Overflow tests exercise the internal version-increment function used by the
builder at zero, max-1, and max. They do not perform 2^64 commits or inject an
arbitrary version into the public map.

Debug and Release tests pass. ASan/UBSan tests run outside the sandbox with leak
detection. TSan builds, but this run failed during startup with unexpected
memory mapping (and one startup segmentation fault); the suite is not
TSan-validated. CI and Clang checks for Phase 3 have not run remotely.

## Phase 4 verification — 2026-10-02

Five groups pass in Debug/Release with warnings-as-errors. ASan/UBSan runs
outside the sandbox with default leak detection. Installation and an external
consumer succeed. Formatting and diff checks pass.

Operational tests cover retained handle copies, byte and snapshot-count
budgets, rejection/retry after release, zero-payload versions, disabled metrics,
writer counters, exception events, callbacks querying statistics outside the
writer mutex, concurrent callbacks, drain timeout/recovery, and invalid options.
An allocation gate pauses a writer under its mutex: reads proceed and timed
close fails without interrupting the commit.

The allocation sweep now also enables tracking/counters while retaining old
versions to force registry growth. GCC exercises 9 failing positions for
tracked commit and 14 for tracked replacement, including registry allocation;
untracked counts remain 8/13. Failed attempts preserve state and record one
exception; the next write succeeds.

TSan compilation succeeds but execution fails during runtime startup with
unexpected memory mapping/startup segmentation faults. Full TSan validation
still needs a compatible host. These local Phase 4 changes have not run
through remote CI or Clang tools.

## Phase 5 verification — 2026-10-03

Release benchmark preset builds with warnings-as-errors and passes nine CTest
groups: the existing five, mixed workload, batched/burst workload, snapshot
cost/retention experiment, and invalid workload parameters. Local sample
and interpretation limits are recorded in benchmarks/local-smoke.md.
The throughput executable and profiler are separate so allocation-counting
overhead cannot contaminate the normal throughput adapter.
Benchmark CI and expanded formatting/static-analysis coverage are configured;
remote checks for these local changes have not run.
The same nine groups also pass in a separate Debug ASan/UBSan build with
benchmarks enabled and leak detection outside the sandbox. Sanitizer timing
is not included in performance samples. Manual smoke sweeps exercised 1/4
workers, 0/1/10 permille writes, batched burst traffic, and larger keys/values.

## Phase 6 verification — 2026-10-03

Debug passes six test groups; Release benchmark and ASan/UBSan builds pass ten.
Hazard tests cover simplified SC-order enumeration, old-view protection,
independent nesting, same-reader nesting rejection, move behavior, reader/guard
survival after map/reader destruction, owning conversion, delayed-guard budgets
and drain, concurrent readers/writes, concurrent producers, and owning-backend
behavior cross-checks.

Allocation-failure sweeps cover both backends, with/without tracking. A
zero-allocation budget verifies registered guard acquisition/borrowed lookup.
Registration failure and subsequent registration recovery are also tested.
This remains an ordinary-allocation interceptor, not all allocator types.
The internal retirement scan/protection uses SC atomics; the ordering model is
not a comprehensive weak-memory verification tool.

Hazard benchmark contexts register once outside timing and reuse guards.
No production-performance or whole-library lock-free claim is made.
Independent protocol review, compatible-host TSan, and controlled long-run
benchmarking remain promotion requirements.

The installed library and external consumer pass with both backends.
ASan/UBSan's ten groups pass outside the sandbox with default leak detection.
TSan's hazard group passed once; a full clean suite remains unavailable because
other executions fail at startup with unexpected memory mappings. An earlier
operational run reported unlock warnings on the timed-mutex path: this local
libtsan lacks interception for the `pthread_mutex_clocklock` used by libstdc++.
Timed management operations now use ordinary mutex try-lock polling instead.
The subsequent operational reruns failed at runtime startup, so this change
does not establish a clean TSan operational result.

## Phase 7 initial verification — 2026-10-03

Debug passes nine groups. Release benchmark and ASan/UBSan builds pass all
thirteen groups, including both runnable examples and the integration model.
ASan/UBSan runs outside the sandbox with default leak detection. Installed
package/external consumer, format and diff checks pass. Release correctness,
hazard and integration groups also pass ten repetitions each.

Routing tests cover exact matching, missing routes, conflicts, duplicate/input
rejection without publication, retained views and empty reload. Model streams
cover both backends with sixteen fixed seeds each, 250 steps per seed.
Nightly CI is configured but not executed for these uncommitted changes.
Phase 6's full TSan limitation remains unresolved; no production release or
coverage-guided fuzzing result is claimed.

## Release hardening verification — 2026-10-03

GCC Debug passes eleven groups; GCC Release benchmark and ASan/UBSan builds
pass fifteen. Clang 18 with clang-tidy (analyzer/bugprone warnings as errors)
builds the library, tests, examples and benchmarks and passes fifteen groups.
GCC and Clang installed external consumers pass. Clang's package exports the
probed atomic runtime dependency, fixing prior GitHub link failures.

Clang 18 TSan passes all ten applicable groups and repeats each three times
without warnings/failures. Clang TSan has strong new/delete interceptors that
conflict with the allocation-failure executable's overrides; this specific
test is excluded from that runtime and still runs in GCC/Clang ordinary and
GCC ASan builds. Allocation-profile benchmarks require a separate build.
GCC TSan's previously recorded startup mapping limitation is not suppressed.

Clang libFuzzer instruments the complete library and oracle with ASan/UBSan.
A local empty-corpus campaign completed 26,964 runs in 61 seconds without a
crash, abort or sanitizer report, producing 301 corpus entries. Temporary
tool extraction lacked an external symbolizer (warning); no sanitizer
finding was hidden or suppressed. Replay exercises binary keys/values, ordered
operations, replacement duplicates, conflicts, close and retained views.

Synthetic benchmark-gate tests pass. A short pinned capture is rejected as
unstable, as intended; it is not an approved performance baseline. Binary and
source CPack TGZs pass the contents audit without Git/build/local roadmap data.
GitHub's existing fd545b5 run was inspected and failed; these local fixes have
not been pushed or verified remotely. Independent hazard review, remote checks
and dedicated-hardware performance approval remain open. MIT was subsequently
selected with owner authorization and added to source/binary package checks.

## Documentation and current CI verification — 2026-10-03

The historical entries above describe what was known at their recording time.
The subsequent [GitHub CI run for `9e99cd7`](https://github.com/sricharan-vanguru/concurrent-read-mostly-map/actions/runs/37100007892)
passed all eleven jobs: quality, GCC/Clang Debug, Release, ASan/UBSan and TSan,
fuzz, and benchmark smoke. This is push-workflow evidence, not evidence that a
scheduled nightly run completed or that the experimental backend is approved.

The newer documentation changes are local and are not included in that run.
Their offline checker covers 20 Markdown pages and 119 local links. Seven
complete annotated C++ examples compile and execute as the new
`documentation_examples` CTest group. GCC Debug passes 12 groups; GCC Release
and ASan/UBSan pass 16 each. The ASan/UBSan run used default leak detection
outside the restricted sandbox. All ten Mermaid diagrams render successfully
using a local Mermaid runtime and an isolated headless browser; no diagram
source is sent to an external rendering service.

Reproduce the documentation checks using [the contributing guide](contributing.md).
Independent hazard review, scheduled-nightly verification, broader platform
coverage and dedicated-hardware performance approval remain open.

## Direct replacement and hot-key benchmarks — 2026-10-03

Local GCC Debug passes 12 test groups; Release benchmark and ASan/UBSan builds
pass 18 each, including the new hot-workload and invalid-option checks.
ASan/UBSan ran outside the restricted sandbox with default leak detection.
Replacement-builder tests cover duplicates, exact limits, empty input, embedded
NUL bytes, version advancement and retained source data. Existing allocation
failure sweeps and both-backend model tests also pass with the new path.
Five synthetic regression-gate tests pass, including rejection of mismatched
hot-key settings and compatibility with older uniform benchmark CSVs.

A short local instrumented run (`4096 128 3`) recorded 8,194 ordinary allocation
requests and 858,176 requested bytes per direct replacement candidate, versus
16,386 requests and 1,687,376 bytes for copy-then-clear. This comparison excludes
input/transaction preparation, publication, candidate destruction, allocator
overhead and RSS. It is diagnostic evidence, not a dedicated-hardware baseline
or an end-to-end latency claim. These changes have not been pushed or checked
by remote CI.
