# Local smoke sample — 2026-10-03

Environment: Intel Core i7-10750H, 6 cores / 12 logical CPUs; GCC 13.3.0,
CMake 3.28.3, Release build. Phase 5 working tree based on f121412. Executed
through the managed command environment with no affinity/frequency control.
Background load was not measured. These are short functional smoke runs,
not a stable performance baseline or a basis for regression thresholds.

Command:

```bash
./build/benchmarks/benchmarks/read_mostly_benchmark --entries 256 --threads 4 --iterations 10000 --repeats 1 --write-permille 1
```

Each adapter completed 39,960 reads and 40 writes, minimum 16-byte keys,
32-byte values, batch=1, spaced writes.

| Adapter | Wall seconds | Operations/s | Sampled read p99 ns | Write p99 ns |
|---|---:|---:|---:|---:|
| Mutex | 0.0344776 | 1,160,170 | 39,082 | 32,299 |
| Shared mutex | 0.0177277 | 2,256,350 | 5,394 | 73,203 |
| Snapshot COW | 0.0259825 | 1,539,500 | 4,546 | 951,874 |

Each row has 620 read samples and only 40 write samples. Tail estimates are
unstable at these counts. The COW row illustrates full-copy write expense;
no claim of an overall winner follows from one small sample.

Snapshot profiler command:

```bash
./build/benchmarks/benchmarks/snapshot_cost_benchmark 256 64 3
```

Each instrumented build requested 514 ordinary allocations totaling 37,216
bytes. Build timings were 119,095 / 74,051 / 70,819 ns and last-owner release
timings 18,527 / 17,468 / 17,273 ns. These counts exclude transaction setup and
publication and do not measure allocator overhead or RSS.
Eight retained versions reported 144,528 logical payload bytes; after releasing
the handles, the retired-version count returned to zero.

Container/PMR evaluation decision: retain the current hash container and
allocator until longer controlled runs show which cost dominates.
