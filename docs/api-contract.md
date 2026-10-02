# Phase 1 API and ownership contract

Include read_mostly/snapshot_builder.hpp and link read_mostly::map with C++20.
Public types live in namespace read_mostly; implementations are compiled.

Snapshot starts empty at version 0. Copies share immutable owned storage.
Rvalues also copy ownership so source handles stay valid. find returns a
borrowed pointer or null; that pointer must not outlive all handles to its
storage. Reassigning the last handle invalidates borrowed pointers. find_copy
returns optional<string> and can allocate. Empty keys/values and embedded zero
bytes are supported. Lookup uses string_view without constructing a key.

UpdateTransaction owns input strings and applies operations in order. Assign
replaces existing values, absent-key erase is a no-op, and clear removes all
entries at that point in the batch. Copies are independent; moved-from
transactions are empty and reusable. Failed recording does not append an
operation; allocation errors propagate.

SnapshotBuilder privately copies source storage, applies operations, validates
the final candidate, and returns it with source.version()+1. Empty batches also
advance the version. Source and batch stay unchanged on success or failure.
Allocation failures propagate bad_alloc; limit violations throw length_error;
version exhaustion throws overflow_error. Limits count operations, final
entries, and final key/value bytes. They do not bound total heap memory or
intermediate candidate size. Build costs O(n + operations) on average plus
string copying and allocation.

Independent handle copies and concurrent const lookups are safe. The same
handle cannot be assigned concurrently with lookup. A transaction cannot be
mutated while a builder reads it. A builder supports concurrent const builds.
ReadMostlyMap implements concurrent publication; see publication.md.

Opaque private storage reduces compilation dependencies. Binary ABI stability
is not promised at version 0.1. Future allocator extensions must retain their
resource for the entire snapshot lifetime, including old read handles.
