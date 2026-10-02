# Concurrent Read-Mostly Map

A production-minded C++20 library for configuration maps, feature-flag tables,
and routing tables with many concurrent readers and rare writes. The intended
public contract is:

- Readers never take the application writer mutex; strict progress depends on
  the publication/reclamation backend.
- Each read observes one immutable, internally consistent map snapshot.
- A writer builds a replacement snapshot privately, then publishes it atomically.
- A reader retains ownership of its snapshot for the entire lookup, preventing
  use-after-free after a writer publishes a newer version.

The Phase 2 map implementation will use copy-on-write snapshots with
`std::atomic<std::shared_ptr<const Snapshot>>`. This is a deliberately safe,
portable C++20 baseline. `load(memory_order_acquire)` pairs with a writer's
`store(memory_order_release)`: after a reader observes the new pointer, it also
observes the fully initialized immutable map. A writer mutex serializes
read-copy-modify-publish operations so two writers cannot lose each other's
updates. Shared ownership keeps a retired snapshot alive until its last reader
finishes.

This does **not** promise that `shared_ptr` reference-count operations are
lock-free. Its purpose is a portable, memory-safe reference implementation.
An opt-in epoch/RCU implementation will use lightweight read guards and
reclaim retired snapshots only after a grace period. It will ship only after it
passes the same API, safety, and benchmark suite.

## Planned shape

```text
reader: atomic load snapshot -> retain ownership -> lookup immutable map

writer: lock writer mutex -> copy current snapshot -> modify copy
        -> release-store replacement -> unlock
```

The implementation roadmap is kept locally. See
[the architecture](docs/architecture.md) for component
boundaries, policies, API shape, and performance trade-offs.

## Build and current status

Phases 0 and 1 implement compiled string-table snapshots, ordered transactions,
validation limits, tests, and package installation. Concurrent map publication
starts in Phase 2. See [API contracts](docs/api-contract.md) and
[verification](docs/testing.md).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Repository layout

```text
include/read_mostly/  Small public declarations
src/                  Compiled storage, transaction, and builder implementation
tests/                Unit, stress, and sanitizer tests
benchmarks/           Read/write throughput and latency benchmarks
docs/                 API and concurrency-contract documentation
```
