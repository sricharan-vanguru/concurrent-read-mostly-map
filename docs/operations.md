# Budgets, telemetry, and management operations

ReadMostlyMap accepts SnapshotLimits plus MapOptions. Defaults disable
retention tracking, counters, timing, and callbacks. Normal reads have no
telemetry counters or registry accesses.
For the experimental hazard backend, guard registration and reclamation have
separate costs documented in hazard-reclamation.md.

```cpp
read_mostly::MapOptions options;
options.max_live_payload_bytes = 4 * 1024 * 1024;
options.max_live_snapshots = 64;
options.collect_update_metrics = true;
read_mostly::ReadMostlyMap config({}, options);
auto status = config.statistics();
```

## Writer admission

Finite budgets automatically enable weak tracking of actual snapshot storage,
not publication wrappers. Snapshot copies count until their last owner exits.
Expired records are swept on admissions, statistics calls, and drains.

The budget includes all live published versions plus the candidate, including
the old current snapshot owned during publication. Provision at least two
payloads/slots for ordinary replacement. Snapshot-count limits also bound
zero-payload versions. Zero slots is invalid.

Exceeding a budget returns memory_budget_exceeded/current version and preserves
the map. Writers may retry after obsolete handles are released. There is no
automatic wait/retry; closed and conflict checks take precedence.

Payload counts key/value characters, not all allocations. Candidate construction,
transaction strings, hash nodes, capacities, weak control blocks, registry
capacity, and allocator metadata are outside this budget. Candidates are built
before admission. This is a logical limit, not a hard heap cap; no portable
total-allocation estimate is exposed. Concurrent reader release can make sampled
gauges/admission conservative.

## Statistics and observers

statistics locks the writer mutex and is a control-plane operation. Optional
saturating counters cover attempts, commits, conflicts, closed/budget rejections,
exceptions, total/max update time. Timings include writer-lock wait and exclude
callback execution. Mutex-acquisition failures are not counted as attempts.
Reads never increment these counters.

Retention gauges require retention_tracking_enabled. They report live/retired
counts, payload, oldest retired version, and its age since publication (not
time since retirement). Weak records do not retain data but can retain control
blocks until a sweep.

UpdateObserver is a noexcept function pointer plus caller-owned context.
It receives a result/exception event after unlocking and may inspect snapshots
or statistics. Callbacks can run concurrently and out of version order. They
must be thread-safe and non-blocking, must not destroy the map or recursively
submit writes, and their context must outlive map calls. Throwing from the
noexcept callback terminates the process.

## Close and drain

close_until polls writer try_lock until a steady-clock deadline, sleeping at
most 1ms between attempts. It does
not interrupt commits. False leaves state unchanged; true closes idempotently.
Reads remain available.

drain_retired_until requires a closed map and retention tracking. It waits for
older versions to lose strong ownership, excluding current-version handles.
Indefinitely held old handles cause false at the deadline; they stay valid.
Management-thread polling uses at most 1ms intervals, with no read-side polling.
Deadlines are best-effort wait bounds, not hard real-time guarantees.
False does not reopen the map. Join all map-method callers before destruction.
