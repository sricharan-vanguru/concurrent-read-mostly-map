# Architecture

Small public headers declare concrete types; .cpp files implement storage and
logic. Initial domain is string-key/string-value configuration tables.

| Component | Responsibility | Status |
|---|---|---|
| Snapshot | Owning immutable handle and opaque hash table | Implemented |
| UpdateTransaction | Own ordered input operations | Implemented |
| SnapshotBuilder | Private copy/apply/validate; next version | Implemented |
| ReadMostlyMap | Publication/commit façade | Phase 2 |
| Writer coordinator | Serialize complete update cycle | Phase 2 |
| Owning backend | Atomic acquire/release and shared ownership | Phase 2 |
| Advanced backend | Guard protocol and retired-memory reclamation | Phase 6 |
| Observer | Optional metrics with explicit overhead | Phase 4 |

Publication and reclamation must be designed together. An atomic raw-pointer
acquire load establishes visibility but does not keep storage alive. The
advanced backend must prove reader-entry races, grace periods, and exit safety.

The owning backend will acquire-load a shared pointer and release-publish fully
built data. Readers keep ownership through lookup; writers serialize
load/copy/build/publish to avoid lost writes. Atomic shared-pointer operations
may use internal locks, so this does not establish strict reader progress.

Snapshot building gives the strong exception guarantee. Payload limits count
logical bytes; allocation and retirement budgets are separate. Hash lookup is
average O(1), worst-case O(n); full-copy updates cost O(n) plus batch operations
on average. Container/PMR customization needs measurements and explicit resource
lifetime rules.
