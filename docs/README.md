# Documentation guide

This library solves two different problems together: **seeing a complete
version** and **keeping that version alive while reading it**. Start with the
concepts, use the API, and then explore what makes it safe.

## Choose your path

| Your goal | Read in this order |
|---|---|
| Learn from the beginning | [Concepts](concepts.md) → [Getting started](getting-started.md) → [Tutorial](tutorial.md) |
| Integrate the library | [Getting started](getting-started.md) → [API reference](api-contract.md) → [Integration](integration.md) |
| Understand correctness | [Architecture](architecture.md) → [Publication](publication.md) → [Hazard reclamation](hazard-reclamation.md) |
| Operate a service | [Operations](operations.md) → [Troubleshooting](troubleshooting.md) → [Release gates](release-checklist.md) |
| Measure or contribute | [Benchmarks](../benchmarks/README.md) → [Testing](testing.md) → [Fuzzing](fuzzing.md) → [Contributing](contributing.md) |

## What each guide answers

- [Concepts](concepts.md): What do snapshot, transaction, publication, registry,
  telemetry and retention mean? How do they relate?
- [Getting started](getting-started.md): What should I build, run and see?
- [Tutorial](tutorial.md): What happens after each line? How do I handle conflicts,
  read several keys, and use budgets safely?
- [API reference](api-contract.md): Which methods exist, what do they return,
  what can throw, and what may be used concurrently?
- [Architecture](architecture.md): Which files own which responsibilities?
- [Publication](publication.md): Where does an update take effect? Why are
  memory ordering and lifetime protection both needed?
- [Operations](operations.md): Why is memory retained? What do counters measure?
  What exactly do close and drain do?
- [Hazard reclamation](hazard-reclamation.md): How do guards prevent deletion?
- [Integration](integration.md): How do I package/link the library and separate domain policy?
- [Troubleshooting](troubleshooting.md): What does an unexpected status or build error mean?
- [Contributing](contributing.md): How do I verify code, documentation and diagrams?

[Compatibility](compatibility.md), [release audit](release-audit.md),
[release checklist](release-checklist.md) and [testing evidence](testing.md) are
engineering records. Read them before calling this production-ready.

## Reading examples and diagrams

Complete programs are marked `// runnable: name` and include `main()`.
The documentation checker compiles and runs them against the library.
Other C++ blocks are described as fragments or pseudocode.

Mermaid diagrams render on GitHub. If your editor shows the source instead,
read the explanation below the diagram or enable a Mermaid preview.
Arrows are labeled steps or relationships, not hardware memory barriers unless
explicitly stated. Dotted arrows indicate optional tracking or a later action.

Commands assume the repository root unless stated otherwise. Examples use
explicit checks rather than `assert`, so checks remain active in Release builds.
All successful updates—including empty batches—advance the version.

The tutorial now includes a complete bulk-replacement example. The architecture
and API guides distinguish direct replacement from copy-on-write patches;
the benchmark guide explains hot-key workloads and replacement allocation costs.
