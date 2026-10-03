# Release readiness (not a release approval)

- [x] Installed package and external consumer exercised locally in Phase 6.
- [x] Domain integration examples and deterministic randomized model tests added.
- [x] Baseline ownership, publication ordering, failure atomicity, budgets and
  shutdown contracts documented; hazard backend clearly experimental.
- [x] Nightly repeated tests and benchmark artifact workflow configured.
- [ ] Verify remote CI/nightly results on both compilers and compatible TSan hosts.
- [ ] Independently review hazard protection, address reuse, slot registration,
  exception boundaries, and final-domain destruction on a reader thread.
- [ ] Add coverage-guided fuzzing and archive minimized failures.
- [ ] Choose license and document stable API/deprecation policy.
- [ ] Establish dedicated-hardware benchmark baselines and noise-tolerant gates.
- [ ] Audit platform coverage, dependencies and distribution contents; choose
  release version/tag only after review. No release tag is created by this phase.

Nightly tests repeat each group 30 times and preserve CTest logs on failure.
The randomized integration test compares 32 fixed-seed operation streams across
both backends (8,000 steps total), including batches, clear, erase, replacement,
duplicate replacement keys, conflicts, old-view immutability and close rejection.
It is property-style testing, not a formal proof or coverage-guided fuzzer.
Concurrent reader/writer stress remains in the map/hazard/history test groups.

Hosted-runner benchmark artifacts include revision and CPU metadata for manual
comparison. They are not performance regression gates: shared runners, CPU
frequency and scheduling noise preclude reliable fixed percentage thresholds.
Collect controlled measurements before setting thresholds. Scheduling a
workflow is not evidence that it has executed successfully.
