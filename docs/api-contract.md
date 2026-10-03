# API reference and ownership contracts

[Documentation home](README.md) · Examples: [Tutorial](tutorial.md)

Include `<read_mostly/read_mostly_map.hpp>` and link `read_mostly::map` for the
complete map API. All public types are in `read_mostly`. Individual headers are
available when only snapshots, builders or transactions are needed.

## 1. ReadMostlyMap

Non-copyable and non-movable facade. Construction starts with an empty snapshot
at version 0. `ReadMostlyMap(SnapshotLimits limits = {}, MapOptions options = {})`
defaults to shared ownership, unlimited logical limits and disabled telemetry.

| Method | Result and contract |
|---|---|
| `acquire_snapshot()` | Owning snapshot for consistent multi-key reads. Hazard mode uses costly temporary registration. |
| `find_copy(key)` | `optional<string>`; one new acquisition, owning value copy if found |
| `contains(key)` | `bool`; one new acquisition |
| `size()` / `version()` | Size/version from each method's separate acquisition |
| `commit(transaction, expected_version)` | Ordered batch; optional expected version; `CommitResult` |
| `replace_all(span<Entry>, expected_version)` | Authoritative whole-table replacement; last duplicate wins |
| `register_reader()` | Move-only hazard registration; default backend throws `logic_error` |
| `statistics()` | Writer-locked management sample, optional gauges/counters |
| `close()` | Wait for writer, reject future writes, keep reads available; idempotent |
| `close_until(deadline)` | Best-effort timed writer-lock acquisition; `bool` |
| `drain_retired_until(deadline)` | Wait for older data versions to lose ownership; closed/tracked map required |

`Entry` is `pair<string, string>`. Replacement input is a `span<const Entry>`;
caller storage must remain valid and unmodified through the call. An empty span
publishes an empty table. Replacement's operation budget counts input pairs,
not the internal clear; duplicate pairs still count as input operations.

### CommitResult

| `status` | `version` | Published-state effect |
|---|---|---|
| `committed` | Version created by this call | Publishes the complete candidate |
| `version_conflict` | Current version at rejection | None |
| `closed` | Current version at rejection | None |
| `memory_budget_exceeded` | Current version at rejection | None; candidate was built first |

`closed` is checked before expected-version conflict. Every success increments
the version exactly once, even for an empty batch or equal contents. Rejections
do not increment. Another writer may publish before the caller next reads.

### Exceptions

- `bad_alloc`: allocation failure; recording/build/registration can allocate.
- `length_error`: operation/final-entry/final-payload candidate limit violation.
- `overflow_error`: the next version cannot be represented by `uint64_t`.
- `invalid_argument`: initial live-snapshot budget cannot admit empty v0.
- `logic_error`: wrong backend, invalid drain precondition, nested guard or moved-from guard/reader use.
- `runtime_error`: experimental backend cannot provide required lock-free atomics.
- Standard-library synchronization failures may propagate `system_error`.

Update preparation failures preserve the published table. Do not confuse an
exception with `memory_budget_exceeded`: candidate limits throw; live published
version admission returns that status. Resource usage/bookkeeping may change
without publication, such as sweeping already-expired records.

## 2. Snapshot

An owning immutable handle. Construction starts empty at v0. Copies share data;
they do not deep-copy the table. Rvalues also copy ownership: source snapshots
remain valid rather than becoming empty moved-from handles.

| Method | Meaning |
|---|---|
| `find(string_view)` | Borrowed `const string*` or null; no value copy |
| `find_copy(string_view)` | Owning `optional<string>`; may allocate |
| `contains(string_view)` | Whether the key exists |
| `size()` | Number of entries |
| `payload_bytes()` | Sum of key/value character counts, not total allocated memory |
| `version()` | Version of this handle's immutable data |

Empty strings and embedded NUL bytes are valid keys/values. Hash lookup uses
`string_view` without constructing a key. A borrowed pointer must not outlive
**all** handles owning its data. Reassigning the last handle can invalidate it.
Snapshots stay usable after a map is closed or destroyed.

## 3. UpdateTransaction

Owns operations and input strings. Records in order; the map changes only on a
successful commit. Copies are independent. Moves transfer operations; the
moved-from transaction is empty and reusable.

| Method | Recorded operation |
|---|---|
| `insert_or_assign(string key, string value)` | Add or replace a value |
| `erase(string_view key)` | Remove a key; missing key is a no-op when applied |
| `clear()` | Remove all table entries at this point in the batch |
| `size()` | Number of recorded operations, not resulting table size |

`clear()` does not clear the operation list. A failed recording allocation does
not append an operation. Reusing a transaction replays its existing operations
against the new source. Do not mutate it while another thread reads/builds it.

## 4. SnapshotBuilder and SnapshotLimits

`SnapshotBuilder(limits).build(source, transaction)` copies privately, applies
operations in order, validates the final candidate, and returns version
`source.version() + 1`. Source and transaction are unchanged. It does not publish
into a map. A stateless builder supports concurrent const builds with stable input.

| Limit | Counts | Default |
|---|---|---|
| `max_entries` | Final distinct entries | `size_t` maximum |
| `max_payload_bytes` | Final key/value characters | `size_t` maximum |
| `max_operations` | Recorded operations / replacement input pairs | `size_t` maximum |

Final-size limits do not cap intermediate candidate memory. A large assignment
followed by erase may meet final limits even though building allocated more.
Building costs average O(n + k) plus copying strings and allocations.

## 5. Options, statistics and update observers

| MapOptions field | Purpose / default |
|---|---|
| `max_live_payload_bytes` | Logical live published payload plus candidate; unlimited |
| `max_live_snapshots` | Number of live published data versions plus candidate; unlimited |
| `track_retained_snapshots` | Enable weak lifetime inventory without finite budgets; false |
| `collect_update_metrics` | Saturating write counters/timing; false |
| `observer` / `observer_context` | Optional `noexcept` callback and caller-owned context; null |
| `backend` | `shared_ownership` by default; `experimental_hazard` opt-in |

Finite live budgets enable retention tracking automatically. Zero live slots is
invalid. A payload limit of zero can still allow zero-payload snapshots.

`MapStatistics` always reports current version/payload, closed/backend flags and
whether tracking/metrics are enabled. Retention gauges are meaningful only if
`retention_tracking_enabled` is true. Write counters are collected only if
`update_metrics_enabled` is true. Read activity is not counted. See the
[statistics reference](operations.md) for field groups and timing definitions.

`UpdateObserver` is `void (*)(const UpdateEvent&, void*) noexcept`. The event
contains outcome, relevant version and elapsed update nanoseconds. Exception
outcomes are reported as well. Callbacks run after writer unlock, may be concurrent
and out of order, and must not throw, destroy the map or recursively submit writes.
Context must outlive map calls. Details are in [operations](operations.md).

## 6. HazardReader and HazardGuard (experimental)

Register once with `map.register_reader()`, then reuse `reader.acquire()`.
These types are move-only and are not default-constructible by callers.
Only one guard may be active on a reader slot; another acquisition throws rather
than replacing active protection. Separate registrations support nesting.

`HazardGuard` provides `find`, `find_copy`, `size`, `version`, and
`copy_snapshot`. Borrowed results require the guard's protection to remain alive.
`copy_snapshot()` retains owning data beyond the guard and incurs a refcount copy.
Guard moves preserve protection; moved-from guard/reader methods reject use.
Readers/guards can outlive map destruction via domain ownership.

Registration allocates and locks; guard acquisition avoids registration but can
retry indefinitely during publication. See [hazard reclamation](hazard-reclamation.md)
before choosing it for a real service.

## 7. Thread-safety and lifetime matrix

| Object/use | Safe concurrently? |
|---|---|
| Map reads with map writes | Yes, while the facade remains alive |
| Multiple map writers | Yes, serialized internally |
| Const lookup on stable snapshot handles | Yes; table data is immutable |
| Assign/destroy a handle while another thread uses that same handle object | No |
| Mutate a transaction while another thread builds/commits it | No |
| Independent hazard registrations and guards | Yes |
| Two acquisitions on one registration | One active guard only; competing acquisition rejects |
| Move/destroy a hazard object while another thread uses that same object | No |
| Map destruction while map methods are running | No; owner must join/stop callers first |

The map cannot synchronize caller-owned input buffers or callback state for you.
Compiled private storage improves modularity; version 0.1 does not promise ABI
stability. Allocator extensions would need to retain resources for every old view.
