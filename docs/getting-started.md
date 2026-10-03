# Getting started

[Documentation home](README.md) · Previous: [Concepts](concepts.md) · Next: [Tutorial](tutorial.md)

## 1. Check your tools

You need a C++20 compiler with atomic `shared_ptr` support, CMake >=3.24, and
Ninja if using presets. Linux GCC and Clang are the CI targets; other platforms
are not advertised as validated. Python runs optional tooling checks, not the
library itself. Installation of system tools is your environment's responsibility.

```sh
c++ --version
cmake --version
ninja --version
```

## 2. Configure, build and test

Run from the repository root. Configure creates build files; build compiles
the `.cpp` sources; CTest executes the verification programs.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The preset creates `build/debug/`. Use a different build directory when changing
compiler or sanitizer mode. Do not reuse a CMake cache from another checkout.
If a test fails, keep its output and consult [troubleshooting](troubleshooting.md).
Test counts depend on optional Python, benchmarks and sanitizer configuration.

## 3. Your first complete program

This program checks the write result before reading. Missing keys are represented
by `std::nullopt`, not an exception. `value_or` supplies a display fallback.

```cpp
// runnable: quick_start
#include <read_mostly/read_mostly_map.hpp>
#include <iostream>

int main() {
    read_mostly::ReadMostlyMap config; // Empty, version 0.
    read_mostly::UpdateTransaction batch;
    batch.insert_or_assign("mode", "fast"); // Recorded, not published yet.
    const auto result = config.commit(batch, 0); // Only if current version is 0.
    if (result.status != read_mostly::CommitStatus::committed) return 1;

    const auto view = config.acquire_snapshot(); // Owning immutable view.
    std::cout << "version=" << view.version() << '\n';
    std::cout << "mode=" << view.find_copy("mode").value_or("missing") << '\n';
    std::cout << "unknown=" << view.find_copy("unknown").value_or("missing") << '\n';
    return view.version() == 1 && view.find_copy("mode") == "fast" ? 0 : 1;
}
```

Expected output:

```text
version=1
mode=fast
unknown=missing
```

The annotated C++ block is compiled and executed by the documentation check.
The other runnable programs are in the [tutorial](tutorial.md). You can also run
the checked-in examples without creating a consumer project:

```sh
./build/debug/examples/config_reload_example
./build/debug/examples/routing_example
```

The config example prints `In-flight request retained v1; next request uses v2`.
The routing example prints `Exact route /checkout -> payments`.

## 4. Link it from your own CMake project

For a complete whole-table reload program, see the tutorial's
[replacement example](tutorial.md#5-replace-the-entire-table). It demonstrates
that omitted keys disappear from the new version while existing readers keep
their old values.

Save the complete program above as `main.cpp` in a separate directory. First
install the library from its repository:

```sh
cmake --preset install
cmake --build --preset install
```

The default install prefix is `<repository>/build/install-prefix`. Use this
complete consumer `CMakeLists.txt` in the directory containing your `main.cpp`:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyConfigService LANGUAGES CXX)
find_package(ReadMostlyMap 0.1 CONFIG REQUIRED)
add_executable(my_config_service main.cpp)
target_link_libraries(my_config_service PRIVATE read_mostly::map)
```

From that consumer directory, replacing the prefix with your absolute path:

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/absolute/path/to/repository/build/install-prefix
cmake --build build
./build/my_config_service
```

The exported target supplies include paths, C++20 and thread/runtime dependencies.
You do not manually compile the library's private sources. The project's own
[package consumer](../tests/package_consumer/main.cpp) verifies this workflow.

## 5. Pick the right build mode

| Preset | Purpose |
|---|---|
| `debug` | Development, checks, examples |
| `release` | Optimized build, same correctness contracts |
| `asan-ubsan` | Memory/undefined-behavior diagnostics |
| `tsan` | Data-race diagnostics in a separate build |
| `benchmarks` | Release workload and cost experiments |
| `install` | Release library plus installed CMake package |

Build ASan and TSan separately. Sanitizer support depends on the compiler and
runtime/host, and sanitized timing is not a production performance measurement.
See [testing evidence](testing.md) for known exclusions and host limitations.
