# Coverage-guided fuzzing and replay

The same independent sequential oracle supports deterministic CTest replay and
Clang libFuzzer with ASan/UBSan. The compiler instruments the library as well
as the entry point; GCC's replay test alone is not coverage-guided fuzzing.
Build/run/minimization options follow the [LLVM libFuzzer guide](https://llvm.org/docs/LibFuzzer.html).

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DREAD_MOSTLY_BUILD_FUZZER=ON -DREAD_MOSTLY_BUILD_TESTS=OFF \
  -DREAD_MOSTLY_BUILD_EXAMPLES=OFF
cmake --build build/fuzz
mkdir -p build/fuzz/corpus build/fuzz/artifacts
./build/fuzz/map_fuzzer build/fuzz/corpus -max_total_time=60 -max_len=2049 \
  -artifact_prefix=build/fuzz/artifacts/
```

First byte selects the backend; subsequent four-byte instructions select clear,
erase, assign, assign-then-erase, replacement with duplicate keys, conflicts or
close. Keys and values exercise binary strings. Every step checks published
version/result, contents and retained old-view immutability. Processing is
bounded to 512 instructions. This is sequential model fuzzing; concurrent
schedules and reclamation ordering are tested separately, not proved here.

CI uploads corpus and crash artifacts even on failure.
The scheduled workflow runs a longer five-minute campaign; push/PR CI runs
one minute. Artifact retention follows GitHub's repository retention setting.
To minimize and replay:

```sh
./build/fuzz/map_fuzzer -minimize_crash=1 -exact_artifact_path=build/fuzz/minimized INPUT
./build/fuzz/map_fuzzer -runs=1 build/fuzz/minimized
./build/benchmarks/tests/fuzz_replay build/fuzz/minimized
```

Keep original and minimized inputs, exact revision, compiler/options, sanitizer
output and reproduction command with any bug report. No minimized failures
are checked in unless a real failure has been found. Do not treat a time-bounded
campaign as proof of absence of bugs.
