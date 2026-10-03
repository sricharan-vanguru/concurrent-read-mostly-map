# Architecture

Small public headers declare concrete types; .cpp files implement storage and
logic. Initial domain is string-key/string-value configuration tables.

| Component | Responsibility | Status |
|---|---|---|
| Snapshot | Owning immutable handle and opaque hash table | Implemented |
| UpdateTransaction | Own ordered input operations | Implemented |
| SnapshotBuilder | Private copy/apply/validate; next version | Implemented |
| ReadMostlyMap | Publication/commit façade | Implemented |
| Writer coordinator | Serialize complete update cycle | Implemented |
| Owning backend | Atomic acquire/release and shared ownership | Implemented |
| Advanced backend | Hazard guards and retired-wrapper collection | Experimental implementation |
| Retention registry | Weak tracking and writer admission | Implemented |
| Observer | Optional write metrics and non-throwing callbacks | Implemented |

Publication and reclamation must be designed together. An atomic raw-pointer
acquire load establishes visibility but does not keep storage alive. The
advanced backend's ordering/lifetime argument is documented in hazard-reclamation.md;
independent review and additional runtime validation remain pending.

The owning backend acquire-loads a shared pointer and release-publishes fully
built data. Readers keep ownership through lookup; writers serialize
load/copy/build/publish to avoid lost writes. Atomic shared-pointer operations
may use internal locks, so this does not establish strict reader progress.

Snapshot building gives the strong exception guarantee. Payload limits count
logical bytes; live-version admission and tracking are management concerns. Hash lookup is
average O(1), worst-case O(n); full-copy updates cost O(n) plus batch operations
on average. Container/PMR customization needs measurements and explicit resource
lifetime rules.
