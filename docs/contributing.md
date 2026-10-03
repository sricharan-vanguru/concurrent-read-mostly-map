# Contributor and documentation workflow

[Documentation home](README.md)

## Scope changes carefully

Keep core string-table mechanisms separate from domain examples. Preserve input
lifetime, failure-atomicity, publication and reclamation contracts. Changing an
atomic order or deleting an ownership layer needs a written argument, not only
a throughput improvement. The hazard backend remains experimental until its
independent review and promotion requirements are met.

Use small public declarations and compiled implementations where practical.
Do not add callbacks, allocators or background threads without specifying who
owns their resources and how shutdown works. See [architecture](architecture.md).

## Verify before submitting

From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --preset benchmarks
cmake --build --preset benchmarks
ctest --preset benchmarks
git diff --check
find include src tests examples benchmarks -type f \( -name '*.hpp' -o -name '*.cpp' \) -exec clang-format --dry-run --Werror {} +
```

Run separate ASan/UBSan and compatible-host TSan builds for lifetime/concurrency
changes. `ctest` with Python available also compiles and runs annotated
documentation programs. The full compiler/sanitizer matrix belongs in CI.
Keep actual test failures distinct from sanitizer startup issues.

For library/package changes, install and rebuild the external consumer:

```sh
cmake --preset install
cmake --build --preset install
cmake -S tests/package_consumer -B build/consumer -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/build/install-prefix"
cmake --build build/consumer
./build/consumer/consumer
cpack --config build/install/CPackConfig.cmake -B build/packages
cpack --config build/install/CPackSourceConfig.cmake -B build/packages
python3 tools/check_distribution.py \
  --source build/packages/ConcurrentReadMostlyMap-0.1.0-Source.tar.gz \
  --binary build/packages/ConcurrentReadMostlyMap-0.1.0-Linux.tar.gz
```

The archive names above match current version 0.1.0 and the default Linux static
build. Update checks alongside any future package-version change.

## Keep examples and navigation executable

Complete Markdown programs start with `// runnable: unique_name` and contain
`main()`. Return nonzero on a failed invariant. Do not rely on `assert`, which
disappears under `NDEBUG`. Clearly label partial or intentionally unsafe fragments.
Include expected output and explain which values may depend on scheduling.

Offline documentation checks need no external service:

```sh
python3 tools/check_docs.py
python3 tools/check_docs.py --compile-examples \
  --library build/debug/libread_mostly_map.a --include include
```

For a Clang build whose library needs the atomic runtime, add `--atomic`. When
linking a sanitized library manually, also pass the matching option, for example
`--compile-option=-fsanitize=address,undefined`. CTest supplies these automatically.
The current helper compiles GNU/Clang command-line examples; it does not validate
MSVC compilation. It checks local links and fences, not external link availability
or every GitHub heading-anchor rule.

## Diagram maintenance

Use small Mermaid flowcharts or sequences for a real relationship that prose
alone obscures. Match source-code components and edges; do not invent architecture.
Use simple quoted labels, stable camelCase IDs, and one scenario per diagram.
Put a plain-language explanation immediately afterward so the page remains useful
without a renderer. Keep beginner diagrams separate from detailed protocol proof.

Render verification uses a local Mermaid browser bundle and an isolated
headless Chrome profile, not an external rendering service. Download the public
renderer once (this fetches a dependency; it does not upload diagram contents):

```sh
mkdir -p build/doc-diagrams
curl --fail --location https://cdn.jsdelivr.net/npm/mermaid@10.9.3/dist/mermaid.min.js \
  --output build/doc-diagrams/mermaid.min.js
python3 tools/check_docs.py --render-diagrams \
  --mermaid-js build/doc-diagrams/mermaid.min.js
```

Only dependency download needs network access. Rendering disables host resolution
and uses local files, storing SVG/PNG previews in ignored `build/doc-diagrams/`.
The helper uses `--no-sandbox` for container compatibility: use it only for trusted
repository Markdown and a trusted local renderer. It does not access your existing
browser profile. Pass `--browser` if Chrome is elsewhere. Inspect the previews for
clipped labels/crossing edges; rendering success alone does not prove correctness.
The diagrams remain editable source in Markdown; generated previews are not
committed and no separate FigJam file is required.

## Reporting findings and releases

For a defect, include revision, compiler/build mode, reproduction command, input
and actual output. Fuzz reports should include original/minimized artifacts;
follow [fuzzing](fuzzing.md). Performance claims should include workload, repeated
CSV, environment/affinity and variation; follow [benchmarks](../benchmarks/README.md).

No fabricated failures, backdated commits or misleading speedups. Include MIT
notices in distribution. A release requires the remaining
[release gates](release-checklist.md), not merely a passing smoke test.
