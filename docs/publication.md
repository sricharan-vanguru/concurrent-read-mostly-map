# Publication and lifecycle

ReadMostlyMap is a non-copyable, non-movable compiled façade, initially empty
at version 0. acquire_snapshot returns one owning version for multi-key reads.
Separate find_copy/contains/size/version calls can observe different versions.

commit holds the writer mutex across load, status checks, private build, and
publication. replace_all clears contents, then assigns input pairs in order;
duplicate keys use the last value. Its operation budget counts input pairs,
excluding the internal clear. Caller batches/spans must remain valid and
unmodified during calls.

Each successful update, even an empty/identical batch, increments the version.
CommitResult reports committed/new version or version_conflict/closed/current
version. Closed takes precedence over conflicts. Rejections do not build.
Allocation/validation errors and version exhaustion propagate without changing
published state.

Update linearization is the release store; read linearization is the acquire
load. Rejections linearize at status checks under the writer mutex. Release
synchronizes with acquire loads that observe it, exposing initialized data.
The atomic shared-pointer load acquires ownership before old storage can be
freed. Snapshot handles retain immutable storage independently of the map.

The backend owns a Snapshot wrapper pointing to shared immutable storage.
This adds a wrapper allocation per write and an ownership copy per read;
later benchmarks will assess consolidation. Reads do not take the application
writer mutex, but atomic shared-pointer operations may lock internally.
Last-owner destruction may occur on a reader thread. No lock-free or bounded
read-latency guarantee is made.

close waits for the active writer, then rejects future writes; it is
idempotent and reads remain available. Caller threads must finish map methods
before destruction. Handles remain valid after close or destruction.
Payload limits do not bound all allocations or memory retained by old handles;
advanced budgets and reclamation remain future work.
