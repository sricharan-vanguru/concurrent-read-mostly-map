# Benchmark methodology

Build without sanitizers:

```bash
cmake --preset benchmarks
cmake --build --preset benchmarks
ctest --preset benchmarks
./build/benchmarks/benchmarks/read_mostly_benchmark
./build/benchmarks/benchmarks/snapshot_cost_benchmark
```

## Throughput and latency

Four compiled adapters run the same fixed-size lookup/update workload:
mutex + mutable unordered_map, shared_mutex + mutable unordered_map, and
ReadMostlyMap, and the experimental hazard backend with one pre-registered
reader per worker. All read batches hold one lock, snapshot, or guard for their entire
batch. The COW adapter includes transaction construction and publication in
write timing; baseline writes mutate in place and provide no snapshot versions.
These are practical architecture comparisons, not identical internal operations.

Options:

| Option | Meaning | Default |
|---|---|---|
| --threads | Mixed-workload worker count | 4 |
| --entries | Initial table size | 1024 |
| --iterations | Per-worker read-batch/write iterations | 10000 |
| --key-bytes | Minimum generated key length, padded after unique ID | 16 |
| --value-bytes | Value length | 32 |
| --batch | Lookups per read iteration, one shared lock/handle | 1 |
| --write-permille | Writes per 1000 iterations | 0 |
| --burst | 0: spaced writes; 1: contiguous writes each 1000-cycle | 0 |
| --repeats | Independent tables/runs; adapter order rotates | 3 |
| --hot-permille | Fraction of iteration positions selecting read-batch starts in the first 1% of keys (minimum one key) | 0 |

0, 1, and 10 permille correspond to read-only, 99.9/0.1, and 99/1 workloads
for batch=1 and complete 1000-iteration cycles. Batch>1 changes the fraction
of individual lookups vs writes; CSV records actual counts. Partial cycles
can have a different ratio. All workers follow the same deterministic write
schedule; simultaneous writer contention is intentional. Generated keys have
uniform cyclic access, not a realistic skewed production distribution.

`--hot-permille 900` enables deterministic hot-key traffic. Writes remain uniform;
read starts target the hot subset at selected iteration positions. The actual
fraction among reads depends on the write schedule, and batches can leave that
subset. This is not random Zipf traffic or a production trace. CSV includes
`hot_permille`; regression comparisons separate different settings (older files
without this column are interpreted as zero).

Table setup and 256 warm-up reads are outside timing. Worker input generation
and readiness happen before the starting barrier. Wall time includes barrier
release, scheduling, and thread joins. Each write is timed; every 64th
iteration samples read-batch latency. CSV includes sample counts, p50/p95/p99
using sorted floor-index quantiles, throughput, and an escaping checksum.
Empty latency populations report zero. Read latency is for the whole batch,
not a single lookup. Clock calls, sampling, and bookkeeping add overhead.
Regular sampling can alias periodic traffic; tiny populations cannot support
strong tail-latency claims. There is no coordinated-omission correction.

Example sweeps (run separately, never concurrently):

```bash
for workers in 1 2 4 8; do
  for writes in 0 1 10; do
    ./build/benchmarks/benchmarks/read_mostly_benchmark --threads "$workers" --write-permille "$writes" --iterations 100000 --repeats 5
  done
done
./build/benchmarks/benchmarks/read_mostly_benchmark --batch 16 --write-permille 1
./build/benchmarks/benchmarks/read_mostly_benchmark --burst 1 --write-permille 10
./build/benchmarks/benchmarks/read_mostly_benchmark --entries 4096 --key-bytes 64 --value-bytes 256 --write-permille 1
```

Record date, commit/dirty state, OS, CPU model/topology, compiler, optimization,
CPU affinity, frequency policy, and background load alongside CSV output.
The executable prints compiler and available hardware threads; use lscpu and
taskset -pc $$ on Linux for model/affinity. Pin consistent cores with taskset
when making repeatable comparisons, and inspect each repeated row rather than
reporting the best run. Current smoke runs do not establish performance gates.

## Snapshot construction and retained memory

snapshot_cost_benchmark optionally accepts three positional parameters:
entries, value_bytes, repetitions. It separately counts ordinary new/new[]
requests during builder copy/apply/validation and measures final-owner
destruction by replacing the candidate handle with an existing empty snapshot.
Inputs and reporting stay outside counted intervals. Allocation instrumentation
changes timing; do not compare these timings directly with throughput results.

Requested allocation bytes exclude allocator overhead, aligned allocation,
stack memory, RSS, transaction preparation, and publication-wrapper allocation.
These are allocation requests, not peak live-memory measurements.
A second experiment holds eight retired handles and prints logical retained
payload/version counts, then checks that releasing them clears the backlog.
Logical payload and requested allocation bytes are different measures.

## Decisions

The snapshot-cost executable also compares `direct` replacement construction
against the previous `copy_clear` transaction path, rotating order each repeat.
Both use identical source/input. Transaction preparation and destruction are
outside the measured build interval; no concurrent publication is measured here.
Allocation requests exclude allocator overhead and are not RSS measurements.
This diagnostic provides evidence, not an approved hardware performance baseline.

Read the replacement rows as follows:

| Column | Meaning |
|---|---|
| `replacement_mode` | `direct`: empty candidate; `copy_clear`: copy old data, clear, then assign |
| `iteration` | Repetition index; the two modes alternate execution order |
| `build_ns` | Candidate construction and validation duration, not complete reload latency |
| `allocations` | Ordinary `new`/`new[]` allocation request count during construction |
| `requested_bytes` | Sum of requested bytes, not live memory, allocator overhead or RSS |

The output contains separate CSV sections with different headers; it is not an
input to the throughput regression comparator. Compare rows within the same
section and retain all repetitions. See [testing evidence](../docs/testing.md)
for the initial local observation and its limitations.

```sh
./build/benchmarks/benchmarks/snapshot_cost_benchmark 4096 128 31
./build/benchmarks/benchmarks/read_mostly_benchmark --entries 4096 --hot-permille 900 --write-permille 1 --iterations 100000 --repeats 5
```

Use the same hot-key setting in baseline and candidate captures. The gate rejects
different settings instead of interpreting a workload change as a speedup.
This workload does not yet measure dedicated reload writers, long-held views
under concurrent reload or reclamation-induced reader tail latency; those are
follow-up experiments, not implemented guarantees.

Use results to identify ownership contention, batching benefit, and O(n)
write-copy cost before evaluating sorted storage, PMR, or advanced reclamation.
No container/allocator change is selected from smoke measurements.
Hazard comparisons now use the same workload and include writer collection
costs. Registered guard setup is outside timing; ephemeral-registration
convenience APIs are not used for timed hazard reads.

## Controlled regression gate

`tools/capture_benchmark.py` pins the child workload and saves CSV plus CPU,
OS, compiler, declared build flags, affinity and frequency-governor metadata.
Use at least five repeats, long runs, identical binaries/build configurations
apart from the intended change, idle dedicated cores, and stable thermal state.
The tool does not change the system governor or prove that the machine is idle.
Build flags must be verified against the build command, not invented.

```sh
python3 tools/capture_benchmark.py --executable build/benchmarks/benchmarks/read_mostly_benchmark \
  --output build/perf-base --cpus 0,1,2,3 --build-flags='-O3 -DNDEBUG' \
  -- --iterations 100000 --repeats 7 --write-permille 1
# Rebuild the candidate, then capture into a new directory with identical controls.
python3 tools/compare_benchmarks.py \
  --baseline build/perf-base/benchmark.csv --candidate build/perf-next/benchmark.csv \
  --baseline-env build/perf-base/environment.json \
  --candidate-env build/perf-next/environment.json
```

Choose CPUs allowed on your host. The comparison requires matching environment
fields/workloads, unique repeats, finite positive throughput and sufficient
samples. A coefficient of variation above 10% yields `INCOMPARABLE` (exit 2).
Stable median throughput drops above the configurable 5% tolerance fail (exit
1); otherwise exit 0. Synthetic unit tests check these decisions. This is a
practical noise gate, not statistical significance testing or a latency gate.
Tune thresholds only using controlled baseline variability; hosted nightly
artifacts do not establish an approved baseline. Store baseline/candidate
revisions, raw CSV and environment files together for review.
