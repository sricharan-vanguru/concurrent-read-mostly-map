# Step-by-step tutorial

[Documentation home](README.md) · Previous: [Getting started](getting-started.md)

Start with shared ownership, the default. The programs below each have `main()`
and are checked independently. You can copy one program at a time into the
consumer project from the getting-started guide.

## 1. Publish a batch and keep an old view

```cpp
// runnable: stable_views
#include <read_mostly/read_mostly_map.hpp>
#include <iostream>

int main() {
    read_mostly::ReadMostlyMap map;
    read_mostly::UpdateTransaction initial;
    initial.insert_or_assign("mode", "safe");
    if (map.commit(initial).status != read_mostly::CommitStatus::committed) return 1;
    const auto old = map.acquire_snapshot(); // Retain version 1.

    read_mostly::UpdateTransaction update;
    update.insert_or_assign("mode", "temporary");
    update.erase("mode");
    update.insert_or_assign("mode", "fast"); // Last operation wins.
    if (map.commit(update).status != read_mostly::CommitStatus::committed) return 1;
    const auto now = map.acquire_snapshot();
    if (old.find_copy("mode") != "safe" || now.find_copy("mode") != "fast") return 1;
    std::cout << "old=" << old.version() << ":safe new=" << now.version() << ":fast\n";
}
```

Output: `old=1:safe new=2:fast`.

| Point in the program | Current map | Retained `old` |
|---|---|---|
| Construction | Empty, v0 | Not acquired |
| First commit | `mode=safe`, v1 | Not acquired |
| Acquire `old` | `mode=safe`, v1 | `mode=safe`, v1 |
| Record second batch | Still `mode=safe`, v1 | Unchanged |
| Second commit | `mode=fast`, v2 | Still `mode=safe`, v1 |

Readers never see `temporary` or the intermediate erase. The private candidate
contains those intermediate changes, but only its final state is published.
`clear()` records another operation; it does not reset the transaction object.
Construct a new transaction if you want a fresh empty list of edits.

## 2. Use one view for related keys

Suppose a configuration updates an `enabled` flag and its `endpoint` together.
Use this fragment inside a request handler:

```cpp
const auto request = config.acquire_snapshot();
const auto enabled = request.find_copy("checkout.enabled");
const auto endpoint = request.find_copy("checkout.endpoint");
```

Both values come from the same table version. Calling `config.find_copy(...)`
twice acquires twice, so a writer between calls can produce different versions.
Consistency is guaranteed per acquired view, not across a series of unrelated
convenience calls. Do not combine `config.version()` with a later read and assume
the version labels that read.

## 3. Own a result, or borrow it carefully

`view.find_copy(key)` returns `optional<string>`: a separate string you may keep
after the snapshot is gone. It may allocate.

`view.find(key)` returns `const string*`, or null if missing. It avoids a value
copy. This fragment is safe because `view` remains alive for the entire use:

```cpp
const auto view = config.acquire_snapshot();
if (const auto* mode = view.find("mode")) {
    consume(*mode); // Application function; do not retain the borrowed pointer.
}
```

This is an intentionally unsafe fragment—do not copy it:

```cpp
const auto* dangling = config.acquire_snapshot().find("mode");
// Temporary ownership is gone here. A later write may free the pointed-to data.
```

Another handle could keep that data alive, but the pointer itself does not.
Keep a named snapshot or copy the value; never rely on the map remaining unchanged.

## 4. Reject stale edits with an expected version

Writers already serialize, even without an expected version. Version checking
adds a different feature: “apply only if nobody has updated since my decision.”

```cpp
// runnable: version_conflicts
#include <read_mostly/read_mostly_map.hpp>
#include <iostream>

int main() {
    read_mostly::ReadMostlyMap map;
    const auto decision = map.acquire_snapshot(); // v0
    read_mostly::UpdateTransaction first;
    first.insert_or_assign("owner", "alice");
    if (map.commit(first, decision.version()).status != read_mostly::CommitStatus::committed)
        return 1;

    read_mostly::UpdateTransaction stale;
    stale.insert_or_assign("owner", "bob");
    const auto result = map.commit(stale, decision.version()); // Still expects v0.
    if (result.status != read_mostly::CommitStatus::version_conflict || result.version != 1)
        return 1;
    if (map.find_copy("owner") != "alice") return 1;
    std::cout << "conflict: current version=" << result.version << '\n';
}
```

Output: `conflict: current version=1`. The stale edit does not publish or increment
the version. Reacquire, reconsider your decision, and construct an appropriate
new edit. Blindly retrying a stale full replacement can overwrite newer decisions.

## 5. Replace the entire table

Use `replace_all` for a validated, authoritative configuration file. Use a
transaction for a patch that should preserve unspecified keys. This fragment
replaces everything, and duplicate keys use the last input value:

```cpp
const std::vector<read_mostly::ReadMostlyMap::Entry> entries{
    {"mode", "safe"}, {"mode", "fast"}, {"region", "eu"}};
const auto result = config.replace_all(entries); // Final mode is fast.
```

Include `<vector>` when using this fragment. Check `result.status` before claiming
reload success. An empty span replaces the table with an empty table; a successful
empty replacement still advances the version. The span's elements must stay
valid and unmodified until the call returns.

## 6. Understand candidate limits and live-version budgets

`SnapshotLimits` bounds a single candidate's final entries, final key/value bytes,
and recorded operation count. `MapOptions` bounds logical live published data
plus the candidate. These are different limits, not a cap on total allocated heap.

```cpp
// runnable: retention_budget
#include <read_mostly/read_mostly_map.hpp>
#include <chrono>
#include <iostream>
#include <optional>

int main() {
    read_mostly::MapOptions options;
    options.max_live_snapshots = 2; // Enables tracking automatically.
    read_mostly::ReadMostlyMap map({}, options);
    read_mostly::UpdateTransaction batch;
    batch.insert_or_assign("mode", "v1");
    if (map.commit(batch).status != read_mostly::CommitStatus::committed) return 1;
    std::optional<read_mostly::Snapshot> held{map.acquire_snapshot()};
    read_mostly::UpdateTransaction next;
    next.insert_or_assign("mode", "v2");
    if (map.commit(next).status != read_mostly::CommitStatus::committed) return 1;
    if (map.commit(next).status != read_mostly::CommitStatus::memory_budget_exceeded) return 1;
    if (map.statistics().retired_snapshots != 1) return 1;
    held.reset(); // Release v1; current v2 still exists.
    if (map.commit(next).status != read_mostly::CommitStatus::committed) return 1;
    map.close();
    if (!map.drain_retired_until(std::chrono::steady_clock::now() + std::chrono::seconds(1)))
        return 1;
    std::cout << "released old view; retry committed version=" << map.version() << '\n';
}
```

Output: `released old view; retry committed version=3`. During the rejected
attempt, v1 is retained, v2 is current, and the proposed v3 would need a third
slot. Releasing v1 allows the retry. The equal-value successful retry still
increments the version. See [operations](operations.md) for a budget timeline.

## 7. Concurrent readers and a writer

Every read below checks two keys in one snapshot. The writer changes both keys
in one transaction. Scheduling may change which versions readers see, but not
the invariant `left == right`.

```cpp
// runnable: concurrent_pairs
#include <read_mostly/read_mostly_map.hpp>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    read_mostly::ReadMostlyMap map;
    read_mostly::UpdateTransaction initial;
    initial.insert_or_assign("left", "0");
    initial.insert_or_assign("right", "0");
    if (map.commit(initial).status != read_mostly::CommitStatus::committed) return 1;
    std::atomic<bool> failed{false};
    std::vector<std::jthread> readers;
    for (int worker = 0; worker < 4; ++worker) {
        readers.emplace_back([&] {
            for (int iteration = 0; iteration < 2000; ++iteration) {
                const auto view = map.acquire_snapshot();
                const auto left = view.find_copy("left");
                const auto right = view.find_copy("right");
                if (!left || left != right) failed.store(true);
            }
        });
    }
    for (int iteration = 1; iteration <= 100; ++iteration) {
        read_mostly::UpdateTransaction batch;
        const auto value = std::to_string(iteration);
        batch.insert_or_assign("left", value);
        batch.insert_or_assign("right", value);
        if (map.commit(batch).status != read_mostly::CommitStatus::committed) failed.store(true);
    }
    readers.clear(); // jthread destruction joins every worker before map destruction.
    if (failed.load() || map.version() != 101) return 1;
    std::cout << "consistent pairs; final version=101\n";
}
```

Output: `consistent pairs; final version=101`. This illustrates safe application
structure; it is not a proof of every schedule or a benchmark. The test suite
adds coordinated stress and history checks. A map must outlive every thread
that calls its methods. Closing it is not the same as joining those threads.

## 8. Try registered guards only after understanding the baseline

The hazard backend is opt-in and experimental. Register outside the hot loop,
acquire a short-lived guard for each read batch, and keep borrowed pointers
inside that scope. Required pointer/boolean atomics must be lock-free on the host.

```cpp
// runnable: hazard_guard
#include <read_mostly/read_mostly_map.hpp>
#include <iostream>

int main() {
    read_mostly::MapOptions options;
    options.backend = read_mostly::PublicationBackend::experimental_hazard;
    read_mostly::ReadMostlyMap map({}, options);
    read_mostly::UpdateTransaction batch;
    batch.insert_or_assign("mode", "fast");
    if (map.commit(batch).status != read_mostly::CommitStatus::committed) return 1;
    auto reader = map.register_reader(); // Once per independent reader.
    {
        const auto guard = reader.acquire();
        const auto* mode = guard.find("mode");
        if (!mode || *mode != "fast") return 1;
        std::cout << "guard version=" << guard.version() << " mode=" << *mode << '\n';
    } // Protection is cleared; reader can acquire again.
}
```

Output: `guard version=1 mode=fast`. One active guard per registration: use
another registration for nesting. A guard is move-only. `copy_snapshot()` gives
an owning handle when a result must outlive protection, at a refcount cost.
Do not hold guards across blocking I/O. Calling convenience `map.find_copy` on
this backend registers temporarily and is not its optimized read path.

Continue with the [API reference](api-contract.md),
[publication ordering](publication.md) or [hazard protocol](hazard-reclamation.md).
