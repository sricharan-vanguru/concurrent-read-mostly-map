# Local release audit

## Scope and distribution

The public library is C++20 with CMake >=3.24 and native threads. No downloaded
runtime libraries or framework dependencies are bundled. Some toolchains need
the system atomic runtime; CMake probes this and exports `atomic` when needed.
Python is optional for benchmark-tool tests, not a runtime library dependency.
Clang/compiler-rt are needed only for coverage-guided fuzzing.

CPack produces pre-stable binary/source TGZs. Binary packages contain the
library, public headers, CMake package and documentation. Examples and test
executables are not installed. Source packages exclude Git internals, build
outputs, Python caches and the local roadmap. Source packages include tests,
examples and tools. Both source and binary packages include the MIT license;
redistribution must preserve its copyright and permission notice.

## Concurrency and memory checks

The writer mutex serializes copy/build/admission/publication, preventing lost
updates. Default publication uses release/acquire owning handles. The guarded
backend uses SC protect/revalidate/swap/scan and returns the validation pointer
to handle address reuse. Registrations and scans share a registry mutex; guards
retain slots and slots retain the domain, while the registry holds weak slots.
No reference cycle is introduced. Guard release clears protection before making
the slot reusable. Retirement capacity and admission are prepared before swap,
so failed allocation does not leave a successful write reported as failure.

Reclamation may run on the final domain owner, including a reader thread.
Payload budgets are logical accounting, not physical heap caps. Guard acquisition
can retry indefinitely under writes; no whole-library lock-free/wait-free claim
is warranted. Map destruction still requires joining facade users. Callback
context lifetime and callback concurrency remain the application's responsibility.

These observations are a local author audit, not independent approval of the
hazard protocol. Existing stress, allocation sweeps and simplified SC enumeration
are evidence but not a full C++ memory-model proof.

## Toolchain limits

Prior GitHub CI failed on Clang runtime atomic queries and on deliberate
moved-from contract tests in static analysis. Fixes add a link probe/exported
runtime dependency and narrowly annotate those contract assertions. Matrix
fail-fast is disabled so one failure cannot hide other validation outcomes.
Local GCC TSan has startup mapping failures; no suppression converts them into
passes. Local Clang TSan passes ten applicable groups. Its strong allocation
interceptors conflict with the dedicated allocation-failure executable's
overrides, so that test is excluded specifically from Clang TSan and remains
covered by ordinary and ASan builds. Allocation-profile benchmarks similarly
require a separate build. GitHub CI for `9e99cd7` subsequently passed all eleven
jobs, including both compilers' TSan builds. See the current verification entry
in [testing](testing.md); scheduled-nightly evidence remains a separate gate.

Linux GCC/Clang are the current CI targets. CMake has MSVC warning support, but
Windows/macOS, alternate standard libraries and weak-memory hardware have not
been validated. Do not advertise them as tested platforms.
