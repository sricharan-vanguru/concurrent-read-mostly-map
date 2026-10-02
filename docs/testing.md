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
Phase 2. Allocation failure injection and version exhaustion need Phase 3.

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
