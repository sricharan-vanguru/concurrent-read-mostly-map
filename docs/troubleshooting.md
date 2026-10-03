# Troubleshooting and common mistakes

[Documentation home](README.md)

## Does optimized replacement remove retention costs?

No. Direct replacement avoids copying the old table while constructing the
candidate, but does not destroy data still owned by readers. Current data,
retired snapshots and the new candidate can coexist. Live-budget rejection is
still possible. Input strings and hash-table storage still require allocation;
logical payload limits are not physical heap limits.

For performance investigation, compare replacement construction and retained
memory separately using [the benchmark guide](../benchmarks/README.md).

## My snapshot still shows the old value

That is expected. A snapshot is a stable view, not a live subscription. Acquire
a new snapshot when a new request should use another publication. Keeping old
handles also keeps their data alive.

## Two related lookups disagree

Check whether you called map convenience methods separately. A write may publish
between them. Acquire once and read both keys from that same snapshot. This is
different from a failed atomic batch: the library never publishes a partial batch.

## My transaction clear did not reset the operation count

`transaction.clear()` records “clear the table” as an operation. It does not
remove previously recorded operations. Create a new transaction for a fresh
batch. Operations are applied in order, including assignments after the clear.

## An update returns version_conflict

The expected version differs from current. Reacquire and reconsider the stale
decision. Writers are already serialized; this status is not a data-race error.
Do not silently retry an authoritative stale replacement against any version.

## An update returns memory_budget_exceeded

The candidate cannot coexist with all currently live published data under the
configured budgets. Allow old-current/candidate overlap, check for held old
snapshots/guards, and inspect retention gauges only when tracking is enabled.
Release obsolete views before a deliberate retry. Candidates allocate before
admission, so this is not a physical memory-cap guarantee.

If an update instead throws `length_error`, inspect `SnapshotLimits`: operations,
final entries or final key/value payload exceeded a candidate limit.

## Statistics are all zero

Current version/payload still describe current data. Retention gauges and update
counters are optional: check `retention_tracking_enabled` and
`update_metrics_enabled`. Tracking does not automatically enable write metrics.

## Drain times out, or throws logic_error

Drain requires a closed map with retention tracking. It waits only for older
published versions. A timeout leaves retained data usable; no forced deletion
occurs. Check for snapshot copies, guards and owning conversions kept in caches.
An indefinitely held retired handle can cause indefinite retention.

## close did not stop a request thread

Close rejects writes but permits reads. Application cancellation and thread
joining are your responsibility. Destroying the facade while a thread calls it
is unsafe even though independently acquired snapshots can outlive it.

## A borrowed pointer crashes after a reload

Keep the snapshot/guard alive for the entire pointer use, or use `find_copy`.
A raw lookup pointer provides no ownership. A temporary snapshot from a single
expression is not enough to protect later code. Never cache those pointers
without their owning view.

## Hazard acquisition throws, or reads are unexpectedly expensive

- Register only on an experimental-hazard map.
- One active guard per registration; use another registration for nesting.
- Do not use moved-from reader/guard objects.
- Register outside the hot loop. Map convenience reads on this backend register
  temporarily and are deliberately not its optimized path.
- Do not move/destroy a reader or guard concurrently with using that same object.
- Required pointer/boolean atomics must be lock-free; unsupported hosts reject it.

Pending retired wrappers are collected on a later write/statistics/drain call,
not by a background thread immediately after every guard exit.

## Clang cannot link __atomic_is_lock_free

Use the current CMake project and exported target. CMake probes runtime atomic
support and links the system atomic library when necessary. A manual build that
omits that dependency can fail. Installed consumers should link
`read_mostly::map`, not copy link flags from an older run.

## Clang TSan reports duplicate new/delete symbols at link time

Its runtime conflicts with the isolated allocation override test. The current
CMake setup excludes that test only in Clang TSan; it stays covered in ordinary
and ASan builds. Allocation-profile benchmarks require a separate build.
Do not force-link duplicate definitions or disable the sanitizer to call it a pass.

## A sanitizer fails before running tests

`unexpected memory mapping` is a runtime startup failure, not a passing test
and not automatically evidence of a library race. Use a compatible host/runtime
and record the failure. Clang TSan has passed locally; earlier GCC runtime
limitations are recorded in [testing](testing.md). Keep actual race/memory
diagnostics distinct from startup issues. Do not suppress findings to turn CI green.

## Benchmark comparison says INCOMPARABLE

Environment/workloads may differ, repeats may be missing, or variance may exceed
the noise threshold. Use controlled hardware, consistent affinity/build flags,
longer runs and idle cores. Do not loosen the gate simply to accept a noisy run.
Sanitized runs and hosted-runner smoke timings are not approved baselines.

## My editor shows Mermaid source rather than diagrams

View the Markdown on GitHub or enable a compatible Mermaid preview. Each diagram
has an adjacent explanation. Contributors can validate rendering using the
[documentation workflow](contributing.md).
