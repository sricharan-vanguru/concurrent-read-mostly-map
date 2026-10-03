# Integration guide

Start with the complete consumer project in [getting started](getting-started.md).
Link the installed CMake target rather than copying implementation files.
This fragment belongs after your CMake `project(...)` declaration:

```cmake
find_package(ReadMostlyMap 0.1 CONFIG REQUIRED)
target_link_libraries(your_service PRIVATE read_mostly::map)
```

Build and run the executable examples:

```sh
cmake --preset release
cmake --build --preset release
./build/release/examples/config_reload_example
./build/release/examples/routing_example
```

Set `READ_MOSTLY_BUILD_EXAMPLES=OFF` to omit executables. The routing adapter
is illustrative domain code, not an installed library API. It supports only
exact paths: no longest-prefix matching, URL normalization, transport, or
destination authorization. These belong to a service's validated domain layer.

## Reload safely

Parse and validate external input away from the live map. Use `replace_all`
for an authoritative full configuration; use a transaction for ordered patches.
Acquire one snapshot per request and use it for every related lookup, including
feature flags and endpoint selection. Separate `find_copy` calls on the map may
observe different versions. Do not let a borrowed lookup pointer escape its
snapshot or guard lifetime.

Pass the version used to prepare an edit as `expected_version`. A conflict is
not success: reload the latest state and deliberately recompute the edit or
report the conflict. Never blindly retry a stale full replacement. Handle
`closed` and `memory_budget_exceeded` separately; validation/allocation failures
are exceptions. Do not hold an experimental guard across blocking I/O.

The owner must stop/join all threads using the map before destroying it.
Owning snapshots remain usable after map destruction; experimental registered
readers/guards also keep their domain alive, not the facade itself. `close()`
rejects writes but permits reads; it does not stop application workers.
Optional drain waits only for retired versions, not current-version handles.
See [operations](operations.md) and [hazard lifecycle](hazard-reclamation.md).

## Compatibility and licensing

Version 0.1 is pre-stable: source and binary compatibility are not guaranteed.
Rebuild consumers with the same headers, compiler/standard library ABI, and
compatible build flags. The installed CMake package checks minor-version
compatibility, not ABI identity. Private implementation types reduce coupling
but do not by themselves guarantee ABI compatibility. Experimental APIs may
change or be removed. A stable release must satisfy its release gates and adopt
the documented stable deprecation policy.

The project is licensed under [MIT](../LICENSE). Reuse, modification and
commercial distribution are permitted; preserve the copyright and permission
notice with copies or substantial portions. The license provides no warranty.
See the [versioning/deprecation policy](compatibility.md) for future stable releases.
