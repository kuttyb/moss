# Phase 24 Foundations SDK — Team F execution plan

**Baseline:** `main` at `4c07e31ea98a2ec9facb14c38cf3af8e56efe81d` (includes Phase 21 and ABI v9).  
**Integration branch:** `phase-24-foundations-integration`.  
**Scope:** `moss.core`, `moss.bytes`, `moss.collections`, `moss.algorithms`; no other SDK families or language-semantic changes.

## Live reconnaissance

`./moss agent bootstrap --json` reports `moss-0.1`, Margo package dependencies, static specialization, built-in `Vector`/`Map`/`Queue`, and source-backed Fast Debug. `moss.core` is accepted as a Margo package name; a consumer with a quoted `"moss.core"` path dependency builds and runs a qualified `core.identity` call. Moss source modules use identifiers such as `core`, `bytes`, `collections`, and `algorithms`; package names and source module names are distinct.

Moss already has eager `map`, `filter`, `reduce`, `sum`, `count`, `any`, and `all`, so this SDK should add reusable algorithms and explicit collection operations rather than wrappers with no new behavior. The public FileIO `Range` is a scoped, read-only borrowed byte view; the live `source_surface` does not advertise a general `Span[u8]` source type. Byte APIs must preserve that distinction and must not move a `Range` across an ownership boundary. Ordinary source generics are statically specialized, not runtime dispatch. All proposed exported generic helpers must be checked through a separate consumer package and source-free interface where relevant.

## Proposed package architecture for Architect challenge

Each package lives under `sdk/foundations/<name>/`, has its own `Moss.toml`, `src/<name>.moss`, tests, examples, and API README. Package IDs are `moss.<name>`, while source imports remain `import <name>` and public calls remain `<name>.function(...)`.

- **`moss.core`:** a small set of checked, deterministic scalar/index helpers useful to the other packages. Candidate operations include integer min/max/clamp and explicit index validity. Avoid mimicking language operators, duplicating parser built-ins, or silently changing integer overflow behavior.
- **`moss.bytes`:** owned byte storage based on current `Vector[Int]` unless a compiler-supported byte-specific type is found. Provide construction/validation, read and write, copying/slicing, and conversion between owned storage and scoped `Range` where legal. State byte range and index behavior precisely. Investigate whether nominal private fields can enforce the `0..255` invariant and whether imported generic `Range` adapters specialize safely.
- **`moss.collections`:** statically specialized vector searching, traversal and sequence helpers with explicit allocation/ownership contracts. Use existing eager pipelines for generic map/filter/reduce where they already satisfy the need. Probe callback specialization and source-free interface behavior before publishing callable-taking APIs.
- **`moss.algorithms`:** an initial complete sorting/searching set with documented complexity and duplicate behavior. Prefer an `O(n log n)` integer sort and binary-search/lower-bound family; generic comparator variants are conditional on checked specialization and ownership evidence. Define Float/NaN ordering only if existing semantics provide a settled total order.

Dependency direction is provisionally `core → bytes`, `core → collections`, `collections → algorithms` (arrows mean the right package may depend on the left). Cross-package test applications will import all four. No package may depend on a later layer. The Architect may simplify this DAG if dependencies add no genuine value.

## Task DAG and integration authority

1. **Coordinator reconnaissance and plan:** inspect live capabilities, existing APIs and package conventions; record a `main` baseline and a green baseline test result.
2. **Independent Architect review:** challenge API scope, package IDs, ownership, effects, generic/source-free boundaries, byte representation and performance assumptions. Resolve routine disagreements using minimal `moss check --json`, Margo builds and native/Fast Debug probes. Freeze v0.1 SDK contracts before broad implementation.
3. **Implementation A and B in parallel:** A owns only `core`/`bytes`; B owns only `collections`/`algorithms`. Each produces narrow tested commits, public API documentation, examples and package-level tests. Coordinator alone integrates their commits.
4. **Independent validation/performance:** after architecture review frees a concurrency slot, a validation agent owns adversarial cross-package fixtures, randomized/property tests and benchmark baselines on its own branch. It must try to falsify implementation claims.
5. **Integrated review and repair:** Coordinator reviews each change, runs targeted checks, integrates sequentially, reruns combined tests and benchmarks, sends the exact candidate to Architect and validation agent, then fixes confirmed findings.
6. **Delivery:** final report records APIs, exact SHAs, test matrix, measured performance, limitations, agent coordination metrics and recommendations for a larger swarm. Do not merge into `main` without project-level authorization.

## Required gates

- Each package: `moss check --json`, `margo build`, `margo test`, `margo run` example, native debug/release and source-backed Fast Debug where supported.
- Cross-package: path-dependency consumer build/test/run, generic specialization, ownership/aliasing negatives, empty and invalid-index cases, large inputs, independent randomized oracle comparison, and source-free rebuild where relevant.
- Repository: `make check`, `make examples`, strict C++17 compilation, `git diff --check`, clean integration worktree and branch ancestry from `main`.
- Performance: pin machine/toolchain/profile/input corpus; compare at least the critical byte copy/search and sorting paths with a suitable Rust baseline, reporting ratio and allocation behavior without claiming parity from intuition.

## Pilot bookkeeping

The five roles are Coordinator, independent Architect, Implementation A, Implementation B, and adversarial validation/performance. The execution environment allows four active agents including the Coordinator, so validation starts when the Architect's initial design pass frees a slot; Architect returns for integrated review. Each role has an isolated worktree under repository `tmp/phase24-foundations/`. Agent observations use role-local `examples/swarm/phase24_foundations_*/FEEDBACK.jsonl`; Coordinator deduplicates validated findings after independent work. The final report will record completed tasks, review/validation defects, rework, Git conflicts, idle time where observable, integration effort and scale-up changes.
