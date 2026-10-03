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

The map implementation uses copy-on-write snapshots with
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

Phases 0–2 implement compiled snapshots, transactions, validation, atomic
publication, serialized writers, conditional commits, and close semantics.
Phase 3 adds allocation-failure injection, seeded reference and concurrent
history tests, close races, and version-increment boundary coverage.
Phase 4 adds optional writer-side live-payload/snapshot budgets, retained-version
metrics, observer events, and deadline close/drain management. See
[operations](docs/operations.md) for limits and contracts.
Phase 5 adds standalone throughput/latency and snapshot-cost benchmarks.
See [benchmark methodology](benchmarks/README.md) for workload controls and
measurement boundaries.
See [API contracts](docs/api-contract.md), [publication](docs/publication.md), and
[verification](docs/testing.md).

Example:

```cpp
#include <read_mostly/read_mostly_map.hpp>

read_mostly::ReadMostlyMap config;
read_mostly::UpdateTransaction batch;
batch.insert_or_assign("mode", "fast");
const auto result = config.commit(batch, 0); // Require initial version.
const auto view = config.acquire_snapshot(); // Own one consistent version.
const auto mode = view.find_copy("mode");
```

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
