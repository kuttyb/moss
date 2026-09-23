# Moss current status

Updated: 2026-09-22

## Phase 15.9 — Cross-Package Static Specialization Convergence — COMPLETE

Phase 15.9 closes the native artifact-boundary specialization gap. The root
cause was projection, not checking: `Checker` created canonical static
specializations correctly, but explicit-module lowering emitted every one into
a final-root `moss-specializations` crate. A non-root provider rlib could then
emit a call to `__moss_specialize_*` before that symbol existed in its own
artifact context.

The canonical specialization identity remains compiler-owned. Native lowering
now deterministically projects it into each module artifact whose checked body
contains the concrete call. Provider concrete wrappers therefore carry the
specialization they execute in their own rlib. A consumer can still instantiate
an imported generic: `.mossi` supplies its checked generic IR and private helper
closure, while the consumer's calling artifact owns that projection and links
the provider's ordinary rlib for concrete implementation. Native debug symbols
are artifact-scoped, avoiding duplicate link symbols when the same semantic
specialization is required in independent artifacts. No source folding, dynamic
dispatch, Rust trait object, runtime dictionary, or Margo semantic behavior was
introduced.

`.mossi` now distinguishes statically dispatched exports and retains their
semantic IR even when their open parameters are trait-constrained. Exported
trait method contracts are preserved as interface metadata, so source-free
structural specialization stays checked and static. This requires native ABI
version 6, so pre-15.9 provider interfaces fail closed and must be rebuilt.
The focused regression in `tests/tooling/check_phase15_9_cross_package_specialization.py` is registered
in `tests/run.sh`; it covers same-module and cross-module calls, the historical
provider concrete-wrapper reproduction, repeated use, structural trait
specialization, Margo package build/run/test, and a consumer built after the
provider source is hidden with only `.mossi` + `.rlib` artifacts. It also checks
that provider and consumer generated Rust have the intended artifact ownership
and no runtime-dispatch machinery.

Build Planner now uses `model.identity_task` across the `graphlib` package
boundary; the former `identity_task_typed` workaround was removed. `graphlib`
(12/12) and `planner` (7/7) build, test, and run through Margo with the natural
generic helper. Strict C++17 `-Werror`, Phase 15.9 and existing module-provider,
Margo, Fast Debug, agent, examples, shell, and whitespace checks pass. A full
`tests/run.sh` rerun passes through the new Phase 15.9 coverage and all reached
compiler/tooling/Emacs suites; after the repair it again reaches only the known
unrelated Phase 10.6F `&String == String` native Rust backend failure. Phase
15.9 intentionally does not add language syntax or runtime generic dispatch.

Post-closeout corrective hardening records concrete static-call dependencies
while checking each specialization and closes those dependencies recursively
during artifact projection. Thus `outer<Int> -> inner<Int>` is emitted in the
same provider or consumer artifact for both source-backed and source-free
`.mossi` + `.rlib` use. Private helper IR is now limited to the actual
module-local transitive closure from exported static functions; unrelated
private helpers stay out of `.mossi`. The focused regression verifies all of
these facts with a source-free fixture containing only `Provider.mossi` and
`libProvider.rlib`.

## Phase 15.8 — Cross-Package Fast Debug Source Convergence — COMPLETE

Margo now resolves the package graph and supplies the exact transitive dependency
source-root universe through the internal `MOSS_FAST_DEBUG_SOURCE_ROOTS` handoff for
`margo debug` and `margo debug --trace`. Moss does not parse Margo dependency selectors,
lockfiles, or Git sources: it indexes those supplied package roots, resolves Moss modules,
checks the reachable source closure, rejects any remaining compiled-only Moss provider as
`FAST_DEBUG_NATIVE_DEPENDENCY`, then runs interpreter-only optimization and Fast Debug.
Invalid supplied roots fail closed; cross-package source module-provider collisions report
deterministic `MODULE_IMPORT_AMBIGUOUS` errors. Same-package multi-file modules remain
valid. `margo debug` accepts only its bare and `--trace` forms, clears inherited source-root
state, and never invokes `rustc` for a fully source-backed graph.

The Phase 15.8 regression is registered in `tests/run.sh`. It verifies source-backed path,
transitive, and Git package closures; cached locked Git resolution; real source-free `.mossi`
selection and rejection; native/interpreter fixture and Build Planner output parity; no-rustc
debug; package-aware trace events; distinct same-filename provenance; invalid-root and
duplicate-provider failures; CLI rejection; and standalone `moss debug` preservation. Build
Planner runs through interpreted `graphlib` source with trace events in both packages.

Completed validation: `make`; `check_phase15_8_cross_package_fast_debug.py` (9/9);
`check_agent_skills.py`; `check_agent_api.py`; existing Fast Debug, Margo,
module-provider, multifile-project, and project-workflow regressions; `make examples`;
`sh -n tests/run.sh`; and `git diff --check`. A full `tests/run.sh` rerun passed all of
the Phase 15.8 coverage, agent/project/tooling checks, 20/20 Emacs tests, and existing
Fast Debug checks, then reached the known unrelated Phase 10.6F native Rust backend
failure (`&String == String`). No Phase 15.8 test was weakened.

Post-closeout hardening aligned Fast Debug source-provider selection with native
module semantics: unused duplicate dependency modules are legal, root source retains
precedence, and ambiguity is reported only when a reachable import requires multiple
external source providers. The Git fixture explicitly initializes and selects `main`.
Focused Phase 15.8, native provider-ambiguity, Margo, Fast Debug, Build Planner,
agent, examples, shell, and whitespace validation passed. The full suite again reached
only the unchanged Phase 10.6F `&String == String` backend failure.

## Phase 15.7 — Multi-Module Formatter Semantic Convergence — COMPLETE

Closed Phase 15.7 multi-module formatter semantic convergence:
- Authoritative Project/Module Semantic Convergence:
  - Eliminated isolated semantic checking for project files in the formatter (`run_project_format`): the formatter no longer uses a standalone merged-namespace approximation. `merge_project_program` is retained inside `check_project_sources()` where it belongs for all consumers. `analyze_project_texts()` is retained for `moss edit`, which requires full codegen.
  - Factored authoritative project analysis so semantic validation is cleanly separated from optimizer and backend code generation: `check_project_sources()` performs parsing (with in-memory `source_overrides`), module/package resolution, compiled provider (`.mossi`) loading, export validation, namespace rewriting, declaration filtering, and `Checker` execution. Formatter validation stops cleanly after `Checker`, ensuring a valid Moss project never fails `moss fmt` due to an optimization or backend code-generation defect. Normal compilation flows continue through `FunctionalOptimizer` and `Generator` via `analyze_project_sources()`.
  - Project files use authoritative project/module resolution across `Application`, `Tests`, and `Benchmarks` target generation modes.
  - Selected-file formatting (`moss fmt path/to/file.moss`) resolves the containing project context and manifest via `analyze_source_context(file)`, validating the file against its full module dependencies and qualified types (e.g. `model.Graph`), while scoping writes strictly to the selected file without mutating sibling files.
  - Genuine standalone files outside any project context continue to use standalone parse/check validation (`validate_formatted_source`); isolated checking is preserved for truly standalone files.
- Validation-Transactional Formatting:
  - Formatting is validation-transactional: all proposed source is formatted in memory and validated before any writes occur; validation failures cause zero writes, leaving every target unchanged on disk.
  - Sibling files in a selected-file formatting run remain byte-for-byte untouched when validation fails or succeeds.
  - Multi-file filesystem writes in a whole-project format are not claimed to be filesystem-rollback-atomic (the OS does not guarantee atomic multi-file commit); the transactional invariant is that no write occurs if validation fails.
- Directory and CLI ergonomics:
  - Added support for passing project directories directly to `moss fmt <dir>` and `moss fmt <dir> --check`.
- Regression Coverage:
  - Hardened `tests/tooling/check_phase15_7_formatter_convergence.py` covering:
    1. Selected-file formatting and check with imported qualified types (`model.Item`).
    2. Verification that sibling modules are not touched when formatting a selected file.
    3. Whole-project formatting, idempotency, and `--check`.
    4. Whole-project and isolated selected-file transactional failure guarantees (verifying byte-for-byte zero disk writes and isolated sibling validity). The `pkg_selected_transactional` fixture is fully isolated: the sibling file is semantically valid on its own; only the selected file contains the type error, so failure cannot be attributed to the sibling.
    5. Proof that formatter semantic validation executes `Checker` stages (`effects`, `synchronization_plan`) while demonstrably bypassing `FunctionalOptimizer` and `Generator` (`rust_generation`), via `MOSS_PROFILE_COMPILER=1` opt-in developer stage timing (emits to stderr only when the env var is set; no JSON or semantic change).
    6. Clean resolution and compilation checks.
  - Registered the regression in `tests/run.sh`.
- Completed Validation:
  - `./moss fmt projects/build_planner/graphlib/src/graph.moss --check` (exits 0).
  - `./moss fmt projects/build_planner/graphlib --check` (exits 0).
  - Regressions: `check_phase15_7_formatter_convergence.py`, `check_formatter_indexing.py`, `check_swarm_003_formatter.py`.
  - Margo build/test/run for `graphlib` (12/12 tests) and `planner` (7/7 tests, deterministic run output).
  - `check_agent_skills.py`, `check_agent_api.py`, `make examples`, `sh -n tests/run.sh`, and `git diff --check`.
  - Full `tests/run.sh` suite: all compiler, formatter, agent, synchronization, Fast Debug, module/project, and 20/20 Emacs ERT checks passed. Reached only the known environment-specific LLDB/DAP real-integration handshake failure; it was not changed.

## Phase 15.6 — Multi-Package Build Planner Dogfood

Completed Phase 15.6 multi-package dogfood experiment in `projects/build_planner/`:
- `graphlib`: Validated and strengthened with 12 unit tests covering empty graph, single/duplicate/independent tasks, linear/diamond dependency chains, disconnected components, missing dependency endpoints, total/category costs, multiple ready tasks, topological constraints, and cycle detection. Documented `empty_slots() = 32` representation. All 12 tests pass under Margo.
- `planner`: Implemented multi-module source architecture across `policy.moss`, `service.moss`, and `main.moss`, consuming `graphlib` through Margo path dependency:
  - Consumes exported concrete types (`model.Task`, `model.Graph`, `model.PlanSummary`) and typed functions.
  - Implements two synchronous domains (`PlannerDomain` -> `ReporterDomain`) with static routing in `main` composition prefix, nested messaging, and imported `model.PlanSummary` payload.
  - Implements eager functional pipelines in `service.compute_cost_metrics` utilizing `filter`, `sum`, `count`, `map`, `all`, and `any`.
  - Exercises structural traits via compile-time duck typing (`policy.Scorable` satisfied by both `service.TaskItem` and `policy.Milestone`).
  - Added 7 comprehensive planner tests in `tests/test_planner.moss`. All 7 pass under Margo.
- Compiler & backend defect repairs in `src/moss.cpp`:
  1. Multi-module test projection: `module_program()` projected tests into library rlibs when `main_module.empty()`. Fixed by accepting `root_module`.
  2. Member call rewriting: In `rewrite_module_program`, calls with `!statement.b.empty()` (e.g. `costs.push(c)`) improperly prefixed receiver variable names (`service__costs`). Fixed by checking `statement.b.empty()` and correctly rewriting imported module calls.
  3. Builtin and pipeline preservation: Prevented qualification of functional stages (`filter`, `map`, `sum`, etc.) and builtins (`Vector`, `Queue`, `Map`, `Some`, `sqrt`) with module names.
  4. Double module prefix guard: Guarded token rewriting with `token.find("__") == string::npos`.
  5. Return statement type check: Added `check_expression` to `Stmt::Kind::Return` in `walk_type_environment`.
  6. Borrowed parameter return value: Emitted `.clone()` / `.__moss_value()` for borrowed object parameters in return expressions.
  7. Foreign object view access: Guarded `gen_object_access` in `Generator::generate` to skip foreign objects whose defining module is not in `rust_dependencies_`.
- Defect classifications and limitations:
  1. Cross-package / cross-module generic structural dispatch was limited in the original Phase 15.6 dogfood; Phase 15.9 now projects specializations into their calling provider/consumer artifacts, so the concrete typed-helper workaround is no longer needed.
  2. `moss fmt` reports `FORMAT_PARSE_ERROR` on qualified parameter types in multi-module files because `validate_formatted_source` type-checks files in isolation.
  3. Fast Debug cannot mix interpreted Moss with compiled Moss module dependencies (`FAST_DEBUG_NATIVE_DEPENDENCY`), requiring reachable source.
- Completed validation: `make`; `margo build`, `margo run`, and `margo test` in both `graphlib` and `planner`; `tests/tooling/check_agent_skills.py ./moss`; `tests/tooling/check_agent_api.py ./moss`; `make examples`; `sh -n tests/run.sh`; `git diff --check`. Full test suite passes to known sandbox LLDB/DAP handshake skip.
- Post-closeout cleanup (Phase 15.6 remains COMPLETE): Normalized incidental spaced indexing syntax (`Vector [Int]` -> `Vector[Int]`, `vec [i]` -> `vec[i]`) across `graphlib` (`algorithms.moss`, `graph.moss`, `model.moss`, `test_graph.moss`). Fixed root cause in `moss fmt` (`canonicalize_code_spacing` in `src/moss.cpp`) which previously defaulted to injecting space before `[` after identifiers. Added regression `tests/tooling/check_formatter_indexing.py` locking down canonical `Vector[Int]()`, `xs[i]`, and `xs[i] = x`. Full re-validation passed.

## Dogfood compiler/interpreter repair batch

Completed in the current working tree: SWARM-011 now also hoists proven-safe
Copy sibling reads for indexed writes; empty `Queue()` uses concrete field/state
context; invalid built-in `Queue(...)`/`Map(...)` arguments are rejected before
lowering; `Vector[T]()` is pure to observable-effect analysis; and native
Vector/Queue `pop()` preserves Moss's `T` contract with fail-closed empty-pop
behavior. Fast Debug now supports the current collection operations through
object fields, concrete Map state (including nested object state), and eager
`map`/`filter`/`reduce`/`sum`/`count`/`any`/`all` pipelines. It remains limited
for `for` traversal.

`projects/cache` now writes directly through `evict_idx`; `projects/rolling_window`
uses `Queue[Int]` for alerts while preserving the separately committed clear/reuse
cleanup. Both projects pass Margo test/build/run and `moss debug`.

Completed validation: `make`; focused check/native/Fast Debug regressions;
`tests/run.sh` through all new regressions and the existing suite until the known
unrelated Phase 10.6F `&String == String` native failure; bootstrap/capabilities/
schema; agent API/skill checks; `make examples`; and shell/whitespace checks.
`make check` repeated the new regression pass and reached the existing
environment-dependent LLDB/DAP handshake failure; it was not changed.

## Discovery-only follow-up — fresh-agent cache experiment

Discovery work in commit `95f5552` extends live `moss agent bootstrap --json` with
canonical-document routing, collection operation metadata, a compact
`project_surface`, test/domain-topology facts, and the composition-initializer
rule. The skills and Gentle Introduction route unfamiliar Moss-writing work
through bootstrap and the practical guide before a minimal probe; arbitrary
example search is explicitly deferred. Drift checks cover the new structured
facts and compiler probes for collection operations, test composition, and
initializer behavior.

Important live-result nuance: direct state construction and a pure ordinary
helper initializer both check successfully. The actual restriction is
side-effect-free initialization; messages, domain access, I/O, failing,
divergent, and unresolved work are rejected. No compiler, interpreter, cache,
SWARM, or example/project implementation changed.

Completed validation: `make`; bootstrap/capabilities/schema JSON; both agent
drift checks; focused compiler probes; `make examples`; `sh -n tests/run.sh`;
and `git diff --check`. `make check` passed the new agent checks and all reached
compiler/tooling/Emacs suites, then stopped at the existing environment-specific
real LLDB/DAP initial-handshake failure; it was not changed.

Routing/status cleanup validation repeated both agent checks, `make`, `make
examples`, and the shell/whitespace checks; `make check` again reached only that
same unchanged LLDB/DAP environment failure.

## Phase 15.4 — Julia → Moss Swarm: BinaryHeap

The bounded pure-Moss `Int` min-heap experiment is complete under
`examples/swarm/Julia/BinaryHeap/`. It translates Julia's hole-moving heap
algorithm with zero-based indexes and passes four project tests plus native build/run
(`1 2 5 7`). SWARM-001 through SWARM-003 are now fixed, so the checked-in source
uses `Vector |> count`, `pop`, and ordinary negative literals; its README preserves
the original observations as historical records.

The independent CPython `heapq` experiment under
`examples/swarm/Python/BinaryHeap/` is also complete. Its zero-based `sift_down` /
two-stage `sift_up` port passes four project tests plus native build/run
(`1 1 2 5 7`). SWARM-004 and SWARM-005 are now also fixed, so the checked-in
Python port uses direct indexed comparisons and helper returns. No frozen Julia
source changed.
The canonical Phase 15 swarm findings ledger is
`examples/swarm/FINDINGS.md`. Its BinaryHeap baseline recorded five findings,
three independently reproduced across the Julia and Python ports.

The isolated Agent A Python `Counter` Map experiment now passes its core project
tests after the checked-in `Map.get`, `Map.keys`, and `Map.values` work. The
README and ledger preserve its original bounded-failure observations. SWARM-006 and
SWARM-007 remain fixed; SWARM-009 remains an agent misunderstanding. The Julia
`Accumulator` experiment remains separate.

The Julia `Accumulator` experiment under `examples/swarm/Julia/Accumulator/`
historically reproduced missing-key/default lookup and Map-value traversal gaps.
Focused current Map regressions verify those APIs and close SWARM-008/SWARM-010;
the optional checked-in merge body still needs a separate source-level follow-up
before that project is again treated as a complete project validation.

Phase 15.4 follow-up repairs now verify the checked-in `Map.get`, `Map.keys`,
and `Map.values` work and reconcile SWARM-008/SWARM-010 as fixed. Current local
work also fixes sibling-field native E0502 lowering, `not` Bool inference, and
frontend enforcement of explicit `let` immutability; SWARM-011 through
SWARM-013 track those verified repairs pending the repository's next allowed
commit. Agent discovery now advertises the compact source surface and verified
Fast Debug limits. Strict C++17 `-Werror`, focused compiler/native Rust,
agent API/skill, Map, and frozen BinaryHeap project validation, plus `make examples`,
pass. `make check` reaches only the existing sandbox LLDB/DAP real-integration
handshake failure after the compiler, tooling, and Emacs suites pass; it was not
weakened. The optional Julia Accumulator merge source remains a separate
source-level follow-up, while the direct Map API regressions pass.

The current HashMap dogfooding follow-up adds `Int % Int`, Rust backend symbol
hygiene for Moss names such as `HashMap`/`VecDeque`/`Arc`, and the empty typed
`Vector[T]()` constructor; unsupported local annotations now report
`LOCAL_TYPE_ANNOTATION_UNSUPPORTED`. The HashMap collision/tombstone tests pass
(13 project tests), and SWARM-014, SWARM-015, and SWARM-017 are recorded as fixed.
The reported owned-field replacement receiver consume was reduced to a passing
control: the RHS is consumed while the receiver remains available, so no
SWARM-016 was allocated. Strict C++17 `-Werror`, focused native/FAST Debug,
agent API/skill, HashMap project, and `make examples` validation pass. `make check`
reaches the sandbox LLDB/DAP handshake failure. A direct `tests/run.sh` pass that
skips that environment gate reaches a separate pre-existing Phase 10.6F native
failure (`&String == String`); the same failure reproduces with the base HEAD
compiler and is unrelated to this HashMap repair.

## Phase 15.3 — Path-Sensitive Synchronization Placement

Closed at the Phase 15.3 implementation plus closeout commit: eligible leading
top-level conditionals use static typed continuation splitting, branch-local
deferral, rank-forced untouched cancellation, and touched full hold. Conditions
with a message, external observable effect, or unresolved observable effect fall
back to conservative full-entry acquisition. Strict C++17 `-Werror`, focused
placement/2PL/borrowed-payload/Fast-Debug/Margo checks, and `make examples` pass.
`make check` reaches only the existing sandbox LLDB/DAP initial-handshake failure
after compiler, synchronization, package/module, Fast Debug, tooling, and Emacs
checks pass; that test was not weakened. The representative branch fixture
generates 23,138 bytes of Rust.

## Phase 15.2 — Margo package/project driver (closed)

Margo is implemented by `44b5c99` and executable closure `53cf425`. The package-to-
module closure additionally proves a real source-free chain: Git `Math` emits its
`.mossi`/rlib, path `Geometry` imports it through Margo's resolved module environment,
and root `App` imports `Geometry` and runs to `42`. One reused deterministic package
environment now supplies dependency artifact roots to build/run/test/bench; the root
test and benchmark both call `Geometry`, and deliberately fail without that environment.
The offline locked-cache rebuild repeats the actual import chain after the original Git
remote is removed; the lockfile remains byte-identical. Generated consumer crates are
checked to reference provider crates rather than fold provider source. `moss` remains
authoritative for modules, `.mossi`, checking, and lowering; Margo owns only manifests,
resolution, cache, ordering, and orchestration. Registry, publishing, updating, and
workspace management remain deliberately deferred. The normal suite reaches only the
existing sandbox LLDB/DAP handshake limitation.

Closure validation: strict C++17 `-O2 -Wall -Wextra -pedantic -Werror`, the focused
hermetic Margo regression, and `make examples` pass. The full `make check` run passes
the compiler, package/module, Fast Debug, tooling, and Emacs checks before the same
environment-dependent real LLDB/DAP initial-handshake failure; it was not weakened.

Phase 15.2 resolver hardening additionally requires every requested external Moss
module identity to select exactly one canonical `.mossi` provider. Overlapping paths
to the same artifact deduplicate; unrelated duplicate exports remain legal until an
import requests that module; direct and transitive duplicate providers now produce a
deterministic `MODULE_IMPORT_AMBIGUOUS` diagnostic listing canonical interface paths.
The selected interface is carried to native lowering so its `.rlib` is paired from the
same provider directory. No package-qualified import syntax or Margo-side import
resolution was introduced.

## Phase 15 — agent onboarding hardening

The fresh-agent chain is now `AGENTS.md` → repository-local Moss skills → live
`moss agent bootstrap --json`. `AGENTS.md` tells an agent how to build/use `./moss`,
then routes package/project work to Margo and semantic/module/compiler work to Moss.
The skills and versioned bootstrap expose `Moss.toml`, `Moss.lock`, path/Git packages,
canonical `margo build|run|test|bench|clean`, and retained Moss semantic tools. Skill
contracts are checked against live bootstrap/capability/schema facts, including Margo,
semantic edits, impact/affected tests, synchronization introspection, Fast Debug, and
trace support. This is discoverability/drift protection only; no Phase 22 work or
language/package semantic change was started. Validation: strict C++17 `-Werror`,
agent Skill/bootstrap/schema regressions, focused Margo regression, and `make examples`
pass. `make check` reaches its existing sandbox LLDB/DAP initial-handshake limitation
only after the compiler, package/module, Fast Debug, tooling, and Emacs checks pass;
that integration test was not weakened.

## Phase 15.1 — borrowed synchronous message payload lowering (closed)

The main implementation is committed as `13602ce` (`Phase 15.1: add borrowed
lowering for synchronous message payloads`). This closure patch repairs `moss cost`
parsing for the current `materializes N bytes` diagnostic and adds a JSON regression.
Internal synchronous `_shared` handler contracts pass non-Copy
ordinary payloads as `&T` and object payloads as `&impl MossAccess_T`.
`MessagePayloadLowering` is the backend representation decision: `CopyValue`,
`BorrowOwned`, `BorrowView`, or `MaterializeOwned`. The checked Moss rule is
unchanged: messages are immutable value snapshots, payload WRITE/CONSUME remains
illegal, forwarding remains legal, and replies remain owned value boundaries.

Borrowed state projections reuse the existing static `MossAccess_*` / `*View`
representation. Handler2PL retains the sender guards throughout nested messages,
so a callee can read the same stable leaves without `__moss_value()` reconstruction
or a deep clone. Exported/native message bridges intentionally retain an owned
payload and lend it only to their internal shared body. Large-payload warnings are
now emitted only for a planned owned materialization boundary.

Focused checks passed with the current compiler build: strict C++17 `-O0` and
`-O2 -Wall -Wextra -pedantic -Werror`; `check_borrowed_reads.py`; new
`check_phase151_borrowed_payloads.py`; Phase 10.6F's broad compiled/interpreted
and source-free module regression; generated Rust `-D warnings`; and shell/Git
whitespace checks. The first full `make check` exposed a generated comparison of
an incoming borrowed `String` against an owned literal; lowering now dereferences
borrowed non-object payloads in value comparisons/operators, and the affected F
regression passes. The final suite reached the optional real LLDB/DAP test after
all compiler, Phase 15.1, module/project, agent, source-map, and Emacs checks
passed, but the sandboxed adapter exited during its initial handshake
(`Connection shut down by remote side while waiting for reply to initial handshake
packet`). This is the existing environment-dependent process-tracing integration,
not a Phase 15.1 assertion; it was not weakened. `make examples` passed.

The benchmark harness compared detached baseline `7661ffc` with the Phase 15.1
working tree, three repetitions at one thread and 200 iterations. Moss-backend
1 MiB internal messages changed from approximately 44.34 us to 0.364 us for
String and 408.0 us to 0.402 us for Vector, consistent with removing deep
materialization. These are small-machine evidence, not a correctness gate; raw
data is disposable under `tmp/phase151/`.

## Repository-owned Moss agent onboarding skills

Two small current-v0.1 agent skills now live at the repository-local Codex
discovery path `.agents/skills/`: `moss-language` and `moss-agent-workflow`.
Their compact hand-maintained bodies are the canonical copies; `AGENTS.md` only
routes Moss-source agents to them. `moss-language` teaches static construction,
`domainroutes`, synchronous `message`, terminating `reply`, immutable payloads,
domain-handle closure, structural traits, inferred READ/WRITE/CONSUME, supported
functional callables, modules, and common rejected patterns. It explicitly marks
`await` and `spawn` retired. `moss-agent-workflow` teaches the existing
`moss-agent-1` bootstrap/JSON-query/format/impact/affected-test/Fast-Debug-trace
loop and keeps generated Rust as an implementation artifact.

`tests/tooling/check_agent_skills.py` verifies both `SKILL.md` metadata and their
machine-readable current-language markers, rejects stale runtime terminology in the
language skill, checks that its advertised listing exactly matches a checked fixture,
then checks, lowers, compiles generated Rust with `-D warnings`, and executes that
example. The check runs at the start of `tests/run.sh`; the existing retired-syntax
gate remains unchanged. `docs/AGENT_SKILLS.md` documents the discovery convention
and explicitly reserves broader measured onboarding/repair/trace work for Phase 22.

Validation after these documentation/tooling-only changes: strict C++17
`-std=c++17 -O2 -Wall -Wextra -pedantic -Werror` rebuild, focused skill check,
`make check`, `make examples`, `sh -n tests/run.sh`, and staged/unstaged
`git diff --check` passed. No Moss semantic, backend, or Phase 10 implementation
changed. This is a low-risk dogfooding aid; Phase 22 is not complete.

## Moss v0.1 peer-review hygiene cleanup

Active examples, projects, benchmark sources, and semantic regressions now use
current static construction, declared routes, synchronous messages, and replies.
Retired source syntax remains only at `tests/negative/await_retired.moss:8` and
`tests/negative/spawn_retired.moss:7`, one migration token each. Historical docs
remain explicitly labeled; the old cycle error example was replaced with a
concrete three-instance route-cycle example.

Twelve redundant rejection-only fixtures were removed. Four useful old cases
were rewritten as current message target/handler/arity negatives and a positive
no-value call. Self-send, same-domain chaining, route-cycle and specialization
fixtures now have descriptive current names. Seven currently valid payload-copy
and reply fixtures were moved out of `negative/`; two obsolete reply-error
examples became one positive reply-by-value example. Current topology, closure,
ownership, functional, synchronization, 2PL, module, and Fast Debug coverage is
preserved. `tests/README.md` documents the current test groups.

The new source-hygiene gate runs before the suite, rejects unallowlisted tokens
including comments/nested examples, verifies both migration fixtures, and has
self-tests. The cleanup exposed a real error-path bug: unknown expression-valued
message handlers were dereferenced during signature inference. A one-line null
guard restores the existing missing-handler diagnostic; no valid-program semantics
or production backend code changed.

Validation: strict C++17 `-O2 -Wall -Wextra -pedantic -Werror`, the initial full
suite, all examples with Rust warnings denied, all four project examples in
debug/release, module/ledger Fast Debug output equivalence, test/benchmark demo,
intentional failures, source audit/self-tests and Emacs byte compilation passed.
The final full-suite rerun after descriptive fixture renames passed, including
all current A/B/B.1/C/D/D.1/E/F/F.1, ownership/functional, native/2PL,
Fast Debug, module/project, agent, source-map/objdump and 20 Emacs ERT checks.
Live LLDB/DAP tests retain their capability skip because process tracing is
unavailable. `sh -n tests/run.sh` and staged/unstaged Git whitespace checks pass.
The final audit reports exactly the two source occurrences listed above and no
active example or benchmark occurrences. No validation remains pending.
Logs are under repository `tmp/` and disposable. This is hygiene only:
no Phase 15 work, automatic release re-closure, or release/tag publication.

## Phase 10.6F.1 checked in as `f1dd43e` — awaiting review and explicit v0.1 re-closure

The provisional Phase 10 closeout (`a907336`, status `94fca2c`) was reopened for
F.1 and the static typed-lowering implementation is now checked in as `f1dd43e`.
Phase 10.6F measured release-blocking fixed overhead from runtime synchronization
metadata interpretation. F.1 now lowers the unchanged SynchronizationPlan into
typed class-owned state and direct statically emitted acquisitions. No semantic,
partition, lock-order, Fast Debug, or source-language changes are authorized.
Phase 15 must wait for F.1 review and explicit v0.1 re-closure.

Implementation and validation complete: production now emits typed class-owned RwLocks,
direct acquisitions, in-place WRITE, and borrowed state projections. Generic
runtime plans/maps/evacuation are removed. Source-free providers expose semantic
typed borrowed handler bodies; the final application generates all physical
layouts, including for a provider built without any concrete instances. Native
ABI is bumped to 5. The 10.6C analysis and Fast Debug execution are unchanged.

Final strict C++17 `-O2 -Wall -Wextra -pedantic -Werror`, `make`, complete
`make check`, and `make examples` passed. Generated Rust uses warnings denied.
This includes all A/B/B.1/C/D/D.1/E/F tests and new F.1 structure, direct typed
layout/acquisition, no-allocation assembly, independent-instance and provider
built-without-instances checks. D's concurrency harness repeats five times;
all conflict/exclusion, nested/sibling ordering, reply release, WRITE-through,
panic/poison, no-clone/original-storage/helper/method/functional and module tests
pass. Fast Debug equivalence, deterministic plans/traces, ledger, agent/tooling,
all 20 Emacs ERT tests and warning-as-error editor byte compilation pass. Live
LLDB/DAP is capability-skipped because process tracing is unavailable.
`sh -n tests/run.sh` and Git whitespace checks pass. Subsequent edits only update
documentation, test comments/report wording and the final success-message order;
they do not invalidate implementation validation. Earlier development failures
are superseded by this final full-suite run.

The durable [F.1 report](../docs/PERFORMANCE_10_6F_1.md) contains methodology,
old/new results, example generated layout/body, native ABI impact, assembly
findings, limitations, and the full validation record. Isolated eleven-sample
tight-loop medians in ns/call (typed / handwritten Rust) are: empty 0.710/0.571,
SHARED 11.782/11.652, EXCLUSIVE 11.647/11.613, two EXCLUSIVE 20.947/20.301,
mixed two-class 21.639/21.171, three-class 30.120/29.382. The multi-fold metadata
overhead is removed; the sub-nanosecond empty result describes a near-empty
benchmark loop, not service request latency.

The unchanged original threaded F harness was rerun with both compilers, seven
repetitions each. Two disjoint callers measured 10.33 Mops/sec with typed Moss
versus 3.13 with a coarse RwLock in this workload. Instrumentation still observes
four shared readers, two disjoint writers and one conflicting writer; nested
ancestor hold amplification remains intentional. No universal speedup is claimed.
At the largest existing stress size (96 leaves/192 handlers per Store), generated
Rust shrank from 7,062,780 to 728,572 bytes; optimized rustc compile/link median
fell from 37,354 to 536 ms, C++ generation from 30.01 to 16.78 ms. Stored-plan
derivation stayed about 76/75 ms. Repeated output is deterministic. Measurements
are not correctness timing gates and raw logs are disposable.

No unsafe, new language semantics, early unlock, atomics, lock elision, layout
padding, scheduler, or alternative backend was introduced. No F.1 implementation
work remains; review and explicit v0.1 re-closure are pending. Phase 15 and Phase
20 have not begun, and no release/tag has been published. The validation below
applies only to the historical F implementation.

## Historical provisional Phase 10 closeout — superseded by F.1 blocker

Phase 10.6F implementation, benchmarks, tests, and release documentation are
checked in as **`a907336`**, based on `202d9c7` (the 10.6E documentation correction).
Phases 10.6A, 10.6B, 10.6B.1, 10.6C, 10.6D, 10.6D.1, 10.6E, and 10.6F are
complete. **Next: Phase 15 — Dogfooding.** No GitHub release or tag was created.

The [v0.1 milestone](../docs/V0_1.md) records the settled structural-trait,
effect, static-topology, synchronous-message, 2PL, borrowed-READ, and value-boundary
model. The [performance report](../docs/PERFORMANCE_10_6F.md) preserves methodology,
environment, robust measurements, fixes/deferred issues, and the final terminology
audit. These documents are durable; temporary raw logs are not status authority.

Stored-plan introspection now reports leaf/class counts and compression, exact
per-handler shared/exclusive acquisition counts, read-only/self-conflicting
handlers, pair opportunities, a deterministic conflict matrix with class/leaf
witnesses, and class-sharing/splitting explanations. No effects or synchronization
partition are independently rediscovered. Optional `moss_perf` physical events
measure actual contention, wait/hold time, handler/nested time, and current call
depth using thread-local collection. Normal production instrumentation is off.
`MOSS_PROFILE_COMPILER` provides optional stage timings outside semantic JSON.

Measurement preceded optimization. Borrowing private handler route/self handles
removes redundant Arc clones without changing locks or message/reply values.
A separate concrete backend fix publishes rustc crate-name rlib aliases so
transitive local/source-free module dependencies link correctly. Native `.mossi`
ABI remains 4; compiler version is 0.1.0 and backend cache identity is refreshed.
No unsafe, layout padding, new language semantics, synchronization algorithm,
lock elision, early unlock, atomics, scheduler, or concurrency-ingress syntax was
introduced.

Measured conclusions on the recorded i3-1315U/Linux/Rust environment:

- Shared readers reached four simultaneous holders; disjoint writers reached two;
  conflicting writers remained at one. Shared-reader throughput rose from 3.51 to
  7.98 million operations/sec at one/eight callers. Disjoint has only two writable
  classes: throughput rose from 2.99 to 4.11 million operations/sec at one/two
  callers; more callers contend within those classes.
- Empty/shared/two-class wrappers measured 81/167/330 ns median. Typed Rust shared
  reads measured about 36 ns. Small-handler metadata/view overhead remains real;
  no blanket near-Rust or coarse-lock speedup claim is made.
- Ordinary String/Vector/whole-object READ remained effectively size-independent
  from 16 to 1,048,576 bytes/elements. Large explicit copies scale with payload
  size as required. Existing non-Clone/clone-counter tests remain authoritative.
- Nested child execution accounts for roughly three quarters of parent lock hold
  time in the instrumented sample; maximum nesting is three. Instrumented durations
  include observer overhead and are separate from uninstrumented throughput.
- Runtime-shaped locks occupy 40 bytes/alignment 8. Packed and 128-byte-separated
  probes both measured about 9.1 ns/op median, providing no padding justification.
- At 96 leaves/192 handlers per Store, 24 instances/12 routes/1,152 total classes,
  plan derivation was about 75 ms of 380 ms total C++ compilation. Generated Rust
  reached 7 MB; code-size/static-projection work remains a real future cost.
- Deterministic tracing grew linearly to 60,017 events / 14.6 MB for 4,002 messages
  (about 498 ms in the recorded sample). Trace slicing is useful future tooling;
  Fast Debug is not turned into an optimizing runtime or concurrency simulator.

New seed programs include a four-module ledger with types, structural predicate,
concrete helpers/factories, domains/routes, independent payload forwarding, and
state replies; an index summary; and a production functional pipeline. Existing
trait examples exercise constrained structural specialization. The clean ledger
builds/runs/debugs with equivalent results, and its model works source-free in
production. Cross-module generic/structural dispatch, qualified statement calls,
provider-field construction, multiple-main ergonomics, Fast Debug `for`/pipeline
coverage, and trace volume are explicitly recorded for Phase 15 rather than
silently treated as finished language surface.

Final validation passed on the final implementation:

- Strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build and `make`.
- Complete `make check`: retained 10.6A/B/B.1/C/D/D.1/E suites and new F diagnostics,
  deterministic output, benchmark/instrumentation structural checks, expanded
  compiled/interpreted equivalence, clean modules and source-free providers.
- Repeated 2PL concurrency, nested/sibling ordering, reply release, primitive WRITE,
  fail-closed panic/poison, non-Clone/zero-copy READ, helper/method/functional and
  module regressions. Each D concurrency harness runs five repetitions.
- `make examples`, generated Rust with warnings denied, functional/dataflow,
  project, agent/introspection, debug-map/objdump, and tooling checks.
- All 20 Emacs ERT tests and warnings-as-errors byte compilation.
- `sh -n tests/run.sh`, working-tree/staged/HEAD Git whitespace checks, and the
  repository-wide terminology/artifact audit. Live LLDB/DAP tests were
  capability-skipped because process tracing is unavailable.

Benchmark timings are never correctness gates. No generated measurement logs,
binaries, or compiled editor files are checked in; the Rust benchmark files are
hand-written source. These results supersede development runs and failures before
the final fixes. Only this checked-in-state documentation follows the implementation
commit; no code changes invalidate the recorded validation.

Phase 10.6F completes Phase 10 and establishes Moss v0.1. The synchronous-domain
architecture has now been implemented, consolidated, instrumented, and quantitatively
validated from compiler-derived synchronization planning through production
handler-level 2PL and borrowed protected reads. Fast Debug executes the same checked
domain semantics deterministically, and the repository now contains coherent v0.1
documentation, diagnostics, benchmarks, and seed programs for real-world dogfooding.
Further language refinement will be driven by Phase 15 usage rather than speculative
extension.

## Historical Phase 10.6E closeout — checked in as `7b335d1`

Phase 10.6E is checked in as `7b335d1`, atop `e70943c`. Phase 10.6D is checked
in as `9f49e64` and 10.6D.1 as `b3f8a64`. All three phases are implemented,
validated, and committed.

Documentation-only closeout correction: README now explicitly names the single
SynchronizationPlan-driven Handler2PL backend across `-O0`, `-O`, and
`-Oshared-memory`, removes stale serialized-handler/atomic-handler wording, and
describes directly interpreted synchronous domains/messages/replies. The requested
README terminology audit and `git diff --check` pass. No compiler, runtime, or
test files changed; the implementation validation recorded below is unchanged.

Production now has one domain implementation: direct synchronous calls through
plan-driven handler entries, using the unchanged safe-Rust class runtime and
borrowed views. Legacy mailbox/worker/tracker/completion paths, coarse locks,
atomic-domain lowering, batching, coalescing, clustering, selectors/options,
and await IR/analysis/serialized metadata are removed. No unsafe, synchronization
optimization, new scheduler, source syntax, or changed 10.6C algorithm was added.
The only C traversal cleanup removes unreachable retired-Await cases.

Fast Debug owns logical instances keyed by checked concrete identities, exact
specializations, state, and immutable routes. Messages execute nested frames;
reply terminates immediately. Independent payload/reply values and helper/method
WRITE-through preserve checked semantics. Deterministic domain/state traces carry
source, semantic, instance, specialization, handler, and access-path identities,
with bounded summaries and no physical synchronization or scheduling simulation.
Transitive source-module interpretation works; source-free Moss still requires
production execution. Existing interpreter `for`/functional-pipeline limits remain
explicit. No Phase 11 foreign ABI or Phase 12 supervision was introduced.

Native `.mossi` ABI is **4**; old artifacts require rebuilding. Impact snapshots
are **v2** and retain checked concrete route edges instead of await edges. Physical
synchronization/borrow metadata remains outside provider ABI.

Final validation passed after the last source change (the internal reply-slot
rename), using a strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build:

- `make`, complete `make check`, and `make examples`; generated Rust uses
  `-D warnings`. The intentional negative use-after-transfer example is excluded.
- 10.6A/B/B.1/C semantics, closure, exact specialization, and plan regressions.
- 10.6D class layout/acquisition, nested/sibling ordering, primitive WRITE-through,
  reply release, fail-closed panic/poison, source-free domains, and whole-object
  restoration. The concurrency harness repeats five times: compatible readers
  and disjoint writers overlap, conflicting writers serialize, and readers exclude
  writers. These tests also passed in the preceding full-suite development run.
- D.1 non-Clone runtime values, zero clone counters, borrow lifetime rejection,
  original String/Vector storage, whole/nested-object READs, mixed/immutable state,
  helper/method/functional captures, value boundaries, and source-free providers.
- New 10.6E compiled/interpreted equivalence, exact static routes and specializations,
  nested/sibling order, primitive/nested/helper/method state mutation, independent
  payload/reply values, terminating reply, deterministic traces, source-module
  closure, source-free interpreter rejection, and absent legacy implementation.
- Functional/dataflow, module/native ABI, project, agent/introspection, Fast Debug,
  debug-map/objdump, tooling, and all **20 Emacs ERT tests**.
- Emacs byte compilation with warnings as errors, `sh -n tests/run.sh`,
  `git diff --check`, and staged/HEAD whitespace checks.
- Optional live LLDB/DAP tests were capability-skipped because process tracing
  is unavailable. Existing objdump missing standard-library source warnings do
  not prevent mapping checks from passing.

These results supersede earlier development failures and runs invalidated by later
edits. Temporary logs live under repository `tmp/` and are not durable evidence.
The [retirement audit](../docs/LEGACY_RUNTIME_AUDIT.md) records the changed-file
inventory, deleted fixtures, removed types/backends, and repository search
classification. Remaining retired terms are migration/absence tests, explicitly
historical records, removal/future-work descriptions, or valid unrelated names,
ordinary collection/runtime utilities, and test threads. No stale active backend
reference remains in the audited implementation.

Phase 10.6E completes the migration to the synchronous-domain architecture.
Production execution now has a single compiler-derived handler-level 2PL backend
with borrowed protected reads, while retired mailbox/worker/await/commit-order and
alternate synchronization backends have been removed. Fast Debug now executes the
same checked synchronous domain semantics directly and deterministically, including
static composition, routes, messages, handler state, nested calls, and terminating
replies, without simulating physical locks or scheduling. Phase 10.6F remains for
diagnostics, layout, and quantitative performance validation.

## Historical checked-in Phase 10.6D.1 closeout — borrowed protected READs

The 10.6D baseline is checked in as `9f49e64`. Phase 10.6D.1, checked in as
`b3f8a64`, replaces
handler-entry READ snapshots with frame-bounded borrows and generated typed object
views. `MossHandlerFrame::read` returns a reference backed by retained class guards,
or directly by immutable runtime storage. `V: Clone` and physical leaf-enum Clone
derivations are removed. Storage remains entirely safe Rust, with no unsafe,
Arc/COW value substitution, or whole-state shared mutable references.

The private handler state ABI now contains recursive typed views with borrowed
READ slots and owned EXCLUSIVE slots. Unobserved slots contain only absent
metadata, never fabricated/default user values. Exclusive evacuation/restoration
is retained; normalization, plans, exact ClassSets, modes, ranks, parent guard
retention, failure behavior, and primitive WRITE-through are unchanged.

Static Rust accessor traits and reusable generic helper/method implementations
support whole/nested-object READs without reconstructing owned objects. There is
no dynamic trait dispatch or new Moss syntax. Helpers, methods, indexed reads, and
functional captures borrow existing values. Explicit message/reply boundaries
materialize independent owned values; Rust references into caller state do not
cross messages. Owned replies remain valid after frame release and later mutation.

Source-free providers use existing semantic type/effect records. A pre-existing
off-by-one read of `public_representation field` metadata is fixed; no 10.6C
algorithm changes were needed. Generated decomposition entry points move private
provider fields into physical storage without exposing new Moss field access.
Native ABI version 3 requires rebuilding older providers for the new internal
entry points; no view layout, lifetimes, or synchronization metadata enters `.mossi`.
Debug maps retain one source/semantic identity and the original native symbol,
with exact line mappings for both owned and borrowed implementations.

Final validation passed after the last compiler and regression changes:

- Strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build, followed by `make`.
- Complete `make check`: 10.6A/B/B.1/C/D and new D.1 regressions; modules and
  source-free `.mossi` providers; functional/dataflow, agent/introspection,
  project workflows, existing Fast Debug tests, and tooling.
- `make examples`, including generated Rust with warnings denied. The existing
  intentional negative use-after-transfer example remains excluded.
- The full-suite and standalone 10.6D runtime/codegen runs both passed. Each
  runs five concurrency harness repetitions, with 20 disjoint-writer,
  shared-reader, and split-object overlaps and 800 conflicting increments per
  repetition. Reader/writer exclusion, reply release, exact class layouts,
  nested/sibling ordering, whole-object restoration, primitive WRITE-through,
  panic/poison failure, and dispatch-option convergence remain passing.
- New D.1 checks compile non-Clone runtime values, require zero clones across
  entry/repeated reads/helpers/methods, and reject a borrow escaping its frame.
  Instrumented generated wrappers/helpers/methods observe the original backing
  addresses of 100,000-element String/Vector state. Whole/nested objects,
  mixed READ/WRITE, immutable state, generics, indexed reads/writes, and captured
  map/filter/reduce work without entry snapshots. Message payload and reply
  independence, deterministic Rust, and stable debug identities are verified.
- Source and source-free provider tests pass for aggregate READ helpers,
  methods, protected domain state, and consumer-local views. No physical view
  or synchronization metadata is serialized into `.mossi`.
- All 19 Emacs ERT tests, warnings-as-errors Emacs byte compilation, debug-map
  and objdump checks, `sh -n tests/run.sh`, and Git whitespace checks pass.
  Optional live LLDB/DAP checks were capability-skipped because process tracing
  is unavailable; objdump's missing Rust standard-library source warning does
  not prevent its mapping checks from passing.

These final results supersede development runs invalidated by later edits.
Logs are disposable under repository `tmp/`; no validation remains pending.
Remaining representation costs are static accessor code size, absent-slot and
descriptor metadata, and conservative explicit-boundary copies. No new 10.6E
blocker was discovered. Fast Debug domain execution and legacy deletion remain
10.6E work; neither is completed by this change.

Changed files: `src/moss.cpp`, `src/handler_runtime.hpp`,
`src/handler_lowering.inc`, new `src/borrowed_views.inc`,
`tests/phase106d1_borrowed_reads.moss`,
`tests/phase106d1_read_projection.moss`,
`tests/tooling/check_borrowed_reads.py`, `tests/tooling/check_handler_2pl.py`,
`tests/run.sh`, `docs/SYNCHRONIZATION_PLAN.md`, `docs/MODULE_ABI.md`,
`docs/SEMANTIC_CONVERGENCE.md`, and this status file.

Phase 10.6D.1 removes the remaining hidden READ snapshots from production domain
execution. Protected READ state is now accessed through borrowed views tied to
the retained synchronization-class guards, while immutable-after-publication
state is borrowed directly. Explicit Moss value boundaries remain by value, and
the Phase 10.6D handler-level 2PL, rank ordering, deadlock proof, and
conflict-serializability guarantees are unchanged.

## Historical checked-in Phase 10.6D closeout — production handler-level 2PL

Commit `9f49e64` implements production handler-level 2PL from the authoritative
`Program::synchronization_plan`. Each constructed domain owns typed leaf storage
and one Rust `RwLock` per planned class. The synchronized `_shared` wrapper
acquires exactly ClassSet in increasing class rank, selects read/write guards
from SHARED/EXCLUSIVE modes, retains them across the full body and nested messages,
restores exclusive leaves, then releases before returning the reply. Empty
ClassSets and immutable-after-publication storage acquire no class lock.

At this historical checkpoint storage was entirely safe Rust, without unsafe,
raw pointers, or runtime type erasure. Exclusive leaves moved into a private
working value under retained guards, while READ leaves used physical snapshots.
Phase 10.6D.1 supersedes those READ snapshots with borrowed views. Exclusive
restoration before unlock and fatal panic/poison/missing-reply behavior remain.

All active compilation modes, former mailbox/reference options, cluster options,
nominal specialization adapters, and exported bridges converge on synchronized
entry. Old transport/coarse/atomic/coalescing/cluster implementations remain
dormant in source for 10.6E. Ordinary primitive WRITE parameters now use writable
caller storage, including generic/method calls and projections. Parameter effects
and the 10.6C synchronization-analysis algorithms are unchanged.

Final graph/specialization/rank/partition/mode validation rejects corrupt checked
state before emission. Source-free providers receive the final application's
descriptor at construction; no physical synchronization metadata becomes `.mossi`
ABI. Native ABI version 2 requires rebuilding older providers.

Final validation passed after the last compiler changes:

- Forced C++17 `-O2 -Wall -Wextra -pedantic -Werror` build, then `make`.
- Complete `make check`: 10.6A/B/B.1/C regressions, new corrupt-plan and 10.6D
  runtime/codegen checks, module/source-free `.mossi`, functional/dataflow,
  agent/introspection, project workflows, existing Fast Debug checks, and tooling.
- `make examples`, with generated Rust warnings denied; the intentional negative
  use-after-transfer example remains excluded by the existing target.
- Standalone 10.6D suite and the full-suite invocation both passed. Each executes
  five harness repetitions, each with 20 disjoint-writer, shared-reader, and
  split-object overlaps plus 800 conflicting increments. Barriers prove overlap;
  these are correctness checks, not timing benchmarks.
- Reader/writer exclusion, zero immutable-handler acquisitions, exact class
  sharing/splitting, increasing local ranks, parent retention, nested/sibling
  ordering, blocked-caller reply release, whole-object restoration, specialization
  layouts, primitive/generic/method/projection WRITE, source-free providers,
  deterministic generation, legacy-option convergence, panic, and poison checks.
- All 19 Emacs ERT tests; warnings-as-errors Emacs byte compilation; debug-map
  and objdump checks; `sh -n tests/run.sh`; working-tree, staged, and HEAD diff
  whitespace checks. Optional live LLDB/DAP checks were capability-skipped because
  process tracing is unavailable. Objdump's missing Rust standard-library source
  warning does not prevent the source-map checks from passing.

Earlier development runs are superseded by these final results. One added
corruption-test input initially assigned the already-present mode; correcting it
required no compiler or analysis change. The complete suite passed afterward.
Logs are disposable under repository `tmp/`; no validation remains pending.

For failure-free execution, domain-local protected state is conflict-serializable
under compiler-derived handler-level 2PL. Every newly acquired lock ranks above
every currently held lock under `(domain_rank, class_rank)`, so a wait cycle would
require the impossible `L1 < L2 < ... < Ln < L1`. Completed descendants leave the
held stack, allowing later lower-ranked siblings above their ancestor. This is
not a global multi-domain transaction, total outbound-effect order, rollback,
supervision, fairness, or starvation-freedom guarantee. Parent lock retention can
amplify nested-call latency; early unlock, elision, atomics, and clustering remain
future work. See `docs/SYNCHRONIZATION_PLAN.md` for the full architecture.

Changed files: `src/moss.cpp`, `src/handler_lowering.inc`,
`src/handler_runtime.hpp`, `src/synchronization_lowering.hpp`, `Makefile`,
`tests/run.sh`, `tests/phase106d_handler_2pl.moss`, `tests/phase106d_nested.moss`,
`tests/phase106d_parameter_write.moss`, `tests/phase106d_specializations.moss`,
`tests/tooling/check_handler_2pl.py`,
`tests/tooling/check_synchronization_lowering.cpp`,
`tests/tooling/check_synchronization_plan.py`, `docs/SYNCHRONIZATION_PLAN.md`,
`docs/SEMANTIC_CONVERGENCE.md`, `docs/MODULE_ABI.md`, `docs/AGENT_API.md`, and
this status file. No 10.6C analysis algorithms or language design changed.

Phase 10.6D makes the compiler-derived SynchronizationPlan authoritative for
production domain execution. Every compiled handler now acquires exactly its
planned shared/exclusive synchronization classes in deterministic rank order,
retains them for the full handler lifetime including nested synchronous messages,
and releases them only after normal handler completion. Nested acquisitions obey
the global `(domain_rank, class_rank)` order, establishing the failure-free
deadlock-freedom and conflict-serializability guarantees of the synchronous-domain
architecture. Legacy transport/runtime machinery remains for Phase 10.6E cleanup.

## Current checkpoint — Phase 10 / Moss v0.1 complete

Checked-in 10.6C compiler closeout: `eb4d49a`, following snapshot `486eba4`.
The 10.6D closeout is checked in as `9f49e64`, atop `3d7ccd1`.

| Phase | Implementation state |
| --- | --- |
| 10.6A | Complete: synchronous, expression-valued `message` and terminating `reply`; source `await` is retired. |
| 10.6B | Complete: static composition, `domainroutes`, a closed concrete DAG, and deterministic `domain_rank`; source `spawn` is retired. |
| 10.6B.1 | Complete: closed domain-handle universe and exact concrete `DomainSpecialization` linkage. |
| 10.6C | Complete: compiler-owned `SynchronizationPlan`, including leaf effects, classes, ranks, handler sets, conflicts, and introspection. |
| 10.6D | Complete at `9f49e64`: physical class storage, production handler-level 2PL, and primitive parameter WRITE-through. |
| 10.6D.1 | Complete at `b3f8a64`, atop `9f49e64`: borrowed protected/immutable READs, recursive object views, no runtime Clone requirement, and independent message/reply values. |
| 10.6E | Complete and validated at `7b335d1`: legacy runtime retired; deterministic Fast Debug domains supported. |
| 10.6F | Complete at `a907336`: diagnostics, instrumentation, quantitative validation, measured backend fixes, and v0.1 release closure. |
| Next | Phase 15 Dogfooding; then Phase 20 Rust Interoperability, Phase 21 Error Propagation & Supervision, and Phase 22 Agent Agency Tooling. |

Current language semantics are synchronous domain calls over the closed concrete
graph. Legacy mailbox/worker adapters and alternate synchronization implementations
have been removed; production execution uses planned synchronization classes.
Neither asynchronous mailbox behavior nor total domain serialization defines
the current Moss language model. Historical checkpoint
descriptions below are implementation history, not permission to restore retired
syntax or change language semantics.

## Historical Phase 10.6C closeout and validation

The `eb4d49a` closeout completes the synchronization-plan implementation resumed
from `486eba4`. The compiler derives leaf READ/WRITE/CONSUME
observations, X*, ProtectedRead, leaf LockSets, equal-signature synchronization
classes, local class ranks, handler ClassSets, and conflict witnesses from the
closed graph's exact specializations. JSON and human dumps consume the stored
plan. That checkpoint emitted no production class-lock storage, acquisitions,
or handler-level 2PL; the current 10.6D implementation supplies them.

This closeout preserves inferred primitive-parameter WRITE effects instead of
downgrading them based on backend Copy representation. Compiled helper interfaces
carry formal leaf effects, alongside handler-state records; missing records fail
closed with a provider-rebuild diagnostic. Tests cover source/source-free effects,
exact specialized object leaves, captures, class splitting, and primitive WRITE.

At the checked-in 10.6C closeout, final validation passed after rebuilding with
C++17 `-O2 -Wall -Wextra -pedantic -Werror`: full `make check`, `make examples` with generated Rust
warnings denied, focused Python/C++ synchronization tests, all 19 Emacs ERT
tests, warnings-as-errors Emacs byte compilation, shell syntax, and diff checks.
Account generated Rust is byte-identical to B.1 in both `-O0` and
`-Oshared-memory`. Optional live LLDB/DAP tests were capability-skipped because
process tracing is unavailable; debug-map and objdump tests passed.
No validation remains pending for this closeout. Session logs live under
repository `tmp/` and are disposable; the outcome is recorded here durably.

Known pre-existing backend limitation, reproduced using commit `d451d8a`:
an exported concrete function in a module calling that same module's exported
generic can reference a missing `__moss_specialize_*` symbol in its Rust crate.
Minimal shape: exported generic `bump(value): return value`, called from exported
concrete `append(values: Vector[Int])`. This is not introduced by synchronization
planning. The source-free leaf-effect fixture uses concrete helpers to test its
intended contract; provider-native generic specialization linkage needs separate
backend work. Ordinary/generic semantic-effect tests remain enabled.

## Primitive WRITE backend correctness item — resolved in 10.6D

Moss semantic analysis permits caller-visible WRITE through ordinary primitive
function parameters when inferred by the existing parameter-effect analysis.
For example, the existing synchronization regression has `change(value: Int)`
assign `value = value + 1` and calls it with domain state `counter`; the inferred
handler write set includes `counter`. These are ordinary function parameters,
not incoming message payloads, which remain immutable snapshots.

The historical by-value Rust mismatch is resolved by generated mutable-reference
calling conventions selected from existing parameter effects. Executable 10.6D
regressions verify ordinary primitive mutation and mutation of protected domain
state through helpers, as well as methods, generics, projections, and source-free
providers. WRITE has not been weakened to READ. No parameter modifier or semantic
effect change is introduced.

## Historical documentation preflight validation (before 10.6D)

This preflight changes only `.codex/CURRENT_STATUS.md`. The 10.6C analysis and
language design remain unchanged. Documentation consistency review checked the
phase boundary against the checked-in compiler, synchronization regressions,
and `docs/SYNCHRONIZATION_PLAN.md`; historical runtime descriptions are labeled
below. `git diff --check`, `git diff --cached --check`, and `git diff HEAD --check`
passed. Status consistency checks confirmed the completed phase entries,
historical labels, deferred backend obligations, referenced files/commits, and
that only this status file differs from HEAD. No dedicated documentation lint
target is configured in the repository.

Compiler/runtime tests were not rerun for this documentation-only preflight.
The recorded 10.6C code-validation results remain applicable to the unchanged
implementation; they do not establish physical 2PL or primitive write-through
correctness. No preflight validation remains pending. Implementation obligations
remain listed under Immediate next tasks.

Follow-up status correction: ordinary READ/WRITE/CONSUME parameter effects are
inferred; no `var`, `mut`, `write`, or `inout` modifier is planned for parameter
mutation. Old await/serialized-domain assumptions are historical and superseded
by Phase 10.6A. The historical Phase 10.5 open-design blocker is superseded by
the resolved decisions in 10.6A–C. Only this status file changed; compiler code
and tests were untouched. `git diff --check` passed for this correction.

## Historical version and commit chronology

- Historical compiler version at this checkpoint: Moss v0.2 (superseded by the v0.1 milestone above).
- Static duck-typed methods and named-trait specialization are complete on top of
  `df73771` (`Complete concrete method semantics`). This checkpoint closes the initial
  static method/trait foundation without adding runtime dispatch.
- The showcase/documentation checkpoint is built on `eecac49` and demonstrates the
  stabilized static duck-typing, named-trait, collection, pipeline, and domain syntax.
  It adds no ownership, borrow, move, optimizer, or runtime-dispatch semantics.
- Phase 2 ownership/effect safety is present in the current history (`c5396c6` and
  `58772f3`). Phase 2.5 is frozen and complete on top of `d76449f`. It adds
  backend-only batching, explicit domain-lowering plans, direct lock regions,
  RwLock specialization, and whole-domain atomics without changing Moss source
  semantics. The freeze checkpoint closes the final `-O0`/atomic equivalence gap
  by defining and explicitly lowering wrapping `i64` arithmetic.
- The historical post-Phase-2.5 await-DAG correctness checkpoint made local type environments
  branch-aware, rejected divergent concrete domain references at joins, validated
  bounded await targets in all executable code, and reported per-edge source sites in
  cycle witnesses. Source `await` and passable domain references were subsequently
  retired by 10.6A/B.1; the current concrete route DAG is recorded above.
- Phase 4 functional/dataflow compilation is implemented at core checkpoint `829b1a9`.
  Pipelines now have typed, provenance-carrying compiler IR; observable callable effects
  are inferred separately from ownership; `-O0` preserves eager stage semantics; and
  `-O` fuses proven-safe chains into explicit loops without a Rust iterator runtime.
- The current Phase 4 hardening work carries exact pipeline plan IDs from checked AST
  occurrences into Rust lowering, includes initializer effects in `Reduce`, rejects
  non-Copy placeholder projections at Moss level, preserves real result-expression
  lines with source-derived semantic identities, and propagates capture mutation through
  ordinary helper calls.
- Phase 4.5 extends the authoritative Phase 4 IR with explicit materialization plans,
  exact-count/dead-map simplification, effect-safe `any`/`all` short-circuit plans,
  single-use immutable cross-binding fusion, and shared-source terminal DAGs. It adds no
  source syntax and keeps `-O0` as the complete eager reference traversal.
- Phase 4.6 adds a bounded Moss-to-Moss semantic rewrite sequence over the same IR.
  Optimized plans explicitly compose adjacent maps/filters, perform identity-proven
  predicate pushdown, eliminate safe trailing maps before `count`, propagate terminal
  facts, preserve single-use virtual bindings, and fold inert literal cardinality. It
  adds no syntax, lazy runtime, MLIR, hardware optimizer, or domain/backend policy.
- Phase 5 tooling is frozen at closeout checkpoint `92d0953` without changing the
  frozen semantic phases. The
  compiler emits a deterministic shared `.mossmap`; debug generation retains stable
  native symbols; `editors/emacs/moss-mode.el` provides editing, compilation, map-based
  navigation, `dape`/`lldb-dap` launch support, and objdump integration; optional native
  tools remain capability-gated.
- Phase 6A is frozen. It exposes those already-computed facts through the deterministic
  `moss-agent-1` JSON protocol. Bootstrap/schema/capability discovery, structured checks,
  and `inspect`, `type`, `effects`, `ownership`, `calls`, `awaits`, and `why` queries are
  read-only views over the ordinary compiler pipeline. Statement/binding queries do not
  borrow the enclosing callable's effect summary: exact effects remain null when no
  precise target summary exists, with the callable summary exposed separately as context.
- The remaining AI-native Phase 6 work is implemented and frozen. `entity-v1` durable
  semantic identities carry deterministic implementation/interface hashes; project
  snapshots power `impact` and conservative `test --affected` selection with dependency
  paths and reuse counters. `fmt` is canonical and idempotent; exact semantic rename,
  binding-expression, and call-argument edits validate and format before writing.
  Structured diagnostics expose stable `fixes` and `legal_alternatives`, while `cost`
  reports factual materialization, traversal, copy, specialization, and backend facts.
  Bootstrap advertises the complete workflow and a deterministic session-report template.
  Phase 5 `.mossmap` source identities remain build/source-layout provenance and are not
  silently redefined as durable edit identities. MCP, a daemon, package management, new
  syntax, and AI telemetry remain deferred.
- Phase 7 is frozen. It adds a conventional `moss.toml` project layer and the `moss build`,
  `moss clean`, `moss test`, and `moss bench` workflows. Debug/release builds,
  native test/benchmark harnesses, stable project/source IDs, filtered discovery,
  benchmark sampling/baselines, and all structured results reuse the ordinary
  compiler pipeline and `moss-agent-1` envelope. It adds no package resolver,
  module system, or new optimizer semantics. Native cache reuse
  includes the resolved Rust compiler, verbose backend version, profile, and flags;
  benchmark baselines use the same fingerprint for compatibility.
- Phase 10.6A establishes synchronous domain invocation in the checked compiler and
  production lowering. `message` is a blocking, expression-valued handler call;
  `reply` terminates a handler; source-level `await` is retired with a migration
  diagnostic. Incoming payloads remain immutable value snapshots, may be forwarded,
  and may be replied by value, but cannot be written or consumed. Self-send and
  same-domain handler chaining are rejected. Transitional `spawn` construction
  existed at this checkpoint and was retired in 10.6B. Legacy mailbox transport
  adapters remain backend machinery pending later removal.
- Phase 10.6B establishes static composition and concrete topology. `spawn` is retired;
  direct domain constructors are restricted to the initial `main` composition prefix,
  and `domainroutes(...)` declarations provide immutable route slots in the shared domain
  member namespace. The compiler records one `ConcreteDomainGraph`, rejects concrete
  route cycles, preserves physical source provenance, and assigns deterministic unique
  whole-program `domain_rank` values. Synchronization classes followed in 10.6C;
  physical handler-level 2PL is implemented in 10.6D.
- Phase 10.6B.1 closes the handle universe in the checker: handles only occur as
  composition bindings, declared route bindings, and message receivers. Ordinary
  parameters/payloads, replies, state, aggregates, aliases, reassignment, and route
  shadowing are rejected. Every instance links directly to its canonical
  `DomainSpecialization` record, including typed instances that reuse a layout.
  Topology JSON exposes the closed graph, source domain, specialization, concrete
  identity, ranks, and physical edge provenance. Historical handle-passing tests
  are replaced with route equivalents and explicit negative regressions.
  Module namespace qualification, constructor metadata, synchronous exported
  bridges, and legacy cluster construction consume the same checked composition.
  This checkpoint did not add synchronization classes, LockSet/ClassSet, class
  ranks, or 2PL. Analysis followed in 10.6C and physical 2PL in 10.6D.
  Validation: strict C++17 `-O2 -Werror` build, full `make check`, `make examples`
  (generated Rust `-D warnings`), focused handle-closure/module regressions,
  Emacs warnings-as-errors byte compilation and 19 ERT tests, shell syntax, and
  diff whitespace checks pass. Optional live LLDB/DAP checks are capability-skipped
  when process tracing is unavailable; debug-map and objdump checks pass.
- Phase 10.6C is complete at `eb4d49a`: `Program::synchronization_plan` owns the
  graph-relative analysis over exact specializations. It derives leaf
  READ/WRITE/CONSUME observations, X*, ProtectedRead, LockSets, equal-signature
  classes, local class ranks, ClassSets, and conflict witnesses. JSON and human
  dumps read this stored plan. Phase 10.6D adds physical class locks and
  handler-level 2PL without replacing the analysis.

## Approved semantics

- Assignment of a uniquely owned nontrivial local transfers ownership; later source use is an error.
- `message` payloads and `reply` results are explicit value-copy boundaries. Existing non-primitive locals, parameters, state, and projections may cross while the sender retains an independent value.
- Ordinary primitive values remain usable after sending. Domain handles are routing
  capabilities and cannot be sent as payloads or used as ordinary values.
- Hidden deep copies and copy-on-write are prohibited. Independent duplication is the explicit future `deepCopy()` operation.
- Ordinary function parameter READ/WRITE/CONSUME effects are inferred. Primitive
  parameter assignment may be caller-visible WRITE; 10.6D implements physical
  write-through. No `var`, `mut`, `write`, or `inout` modifier is
  planned for parameter mutation.
- Immutable sharing, arenas, `ref object` identity, and persistent `revise` versions are deferred because retention and leak behavior is unresolved.
- Moss `Int` is currently signed 64-bit two's-complement. Overflowing integer
  arithmetic wraps modulo 2^64 in every backend; Rust overflow-check settings are
  not observable Moss semantics.

## Implemented features

- A minimal project manifest names the project/version and configured source directory
  or file. Commands locate the nearest project root, use deterministic profile artifact
  paths, hide direct Rust invocation, compile with warnings denied, and reuse unchanged
  generated Rust/debug maps/native artifacts only under the same backend-toolchain
  fingerprint. `moss clean` removes build products while
  retaining saved benchmark evidence.
- Top-level `test "name":` declarations support statically checked `assert(bool)` and
  `assertEqual(actual, expected)`. Project source and recursive `tests/*.moss` discovery,
  substring filtering, per-test panic containment, stable
  `test:<relative-source>:<name>` identities, Moss source diagnostics, human summaries,
  and versioned JSON output are implemented.
- Top-level `bench "name":` declarations compile only through the release/highest-
  optimization profile. The safe generated harness black-boxes every discarded
  value-producing expression (calls, arithmetic, and functional pipelines), uses warmup,
  31 repeated samples, 1,000 invocations per sample, and median/p25/p75 reporting.
  Stable-ID JSON baselines retain compiler/profile/platform/backend-toolchain/time/
  methodology/sample data. Incompatible backend fingerprints suppress both comparison
  and optional regression-threshold enforcement; compatible comparison and filtering are
  implemented without fragile thresholds in the compiler regression suite.
- Build, test, and benchmark generation share the parser, checker, ownership/effect
  passes, functional/dataflow optimizer, backend plan, generator, `.mossmap` contract,
  profiles, artifact directories, and Phase 6A JSON envelope. Test and benchmark code is
  absent from ordinary application artifacts.
- `moss agent bootstrap|capabilities|schema --json` provides vendor-independent protocol
  discovery and workflow/safety guidance. `moss check --json` uses stable diagnostic
  categories and a uniform source/span/identity/details shape. Semantic queries accept
  exact source locations or retained semantic identities and reuse the compiler's static
  call/concrete-domain graphs, synchronization plans, ownership/effect summaries,
  and optimization explanations. A
  statement with no precise retained effect summary reports null rather than inheriting
  all effects from its enclosing callable.
- Successful project checks/builds/tests/benchmarks persist a compact semantic snapshot
  under `.moss/semantic-cache-v1/`. `moss impact <target> --json` distinguishes unchanged,
  implementation-only, and semantic-interface changes and reports direct/transitive
  dependents, affected tests/benchmarks/domains/route edges, specializations, and reuse
  counts. `moss test --affected` uses that cone during iteration and conservatively runs
  all tests when no baseline exists.
- `moss fmt` and `moss fmt --check` provide canonical two-space Moss formatting after
  parse/check validation. `moss edit` applies only exact durable semantic targets for
  initial rename/replace-expression/change-argument operations; stale or ambiguous
  targets are rejected. `moss cost <target> --json` exposes compiler-known cost facts,
  and every structured diagnostic includes `fixes` plus `legal_alternatives`.

- A representative Phase 6 agent session used bootstrap, structured checks and
  semantic queries, exact edits, formatting, impact analysis, and affected tests.
  The structured repair for a missing block colon and the dependency path reported
  by `impact` avoided guesswork and an unrelated test run; ownership intent remains
  deliberately agent-selected from compiler-provided alternatives.

- Every Rust emission has an adjacent versioned JSON `.mossmap` with absolute source and
  output paths, real one-based source spans, deterministic source/provenance identities,
  generated ranges, line mappings, generated/native symbols, and provenance. Transient
  functional IR IDs are excluded. Fused functional nodes deliberately share one
  generated range while retaining all contributing origins.
- `--debug` selects unoptimized eager lowering with planned class locking and adds stable
  function boundaries. `tools/moss-build-debug` compiles that Rust with DWARF, frame
  pointers, no stripping, and warnings denied. Concrete functions, methods, handlers,
  and main have deterministic readable exported symbol names; synchronized handler
  entries and private bodies retain stable tooling identities.
- The dependency-light Emacs `moss-mode` includes comment/string syntax, centralized
  font-lock definitions, tab-free two-space indentation, `else` dedenting, Imenu and
  defun navigation, compilation-mode check/build/run commands, read-only artifact views,
  and bidirectional Moss/generated-Rust navigation through the shared map.
- Optional debugging uses Emacs `dape` with `lldb-dap`. Pending Moss source breakpoints
  translate deterministically after LLDB creates the native target;
  `tools/moss_lldb.py` also supplies standalone `moss-map-load`, `moss-break`,
  `moss-where`, and filtered `moss-stack` commands. Ordinary variable inspection stays
  in LLDB/DWARF rather than a custom debugger runtime.
- Emacs disassembly commands resolve a concrete symbol through `.mossmap`, prefer
  `llvm-objdump`, fall back to GNU `objdump`, use `asm-mode`, and display all fused
  provenance origins. Compiler operation never depends on editor/debugger/disassembler
  availability.

- The compiler now has extracted `src/ast.hpp`, `src/constraints.hpp`,
  `src/diagnostics.hpp`, and `src/functional_ir.hpp` modules. `moss.cpp` still contains
  most parser, checker, ownership, optimizer, and Rust-generator implementation; the
  extraction is incremental rather than a completed module split.
- Type declarations retain method bodies as children of their method nodes in the
  AST, including nested control-flow blocks.
- Concrete methods are checked with receiver fields and typed parameters in scope;
  result types are inferred across return paths and lowered through the ordinary
  statement generator to executable Rust inherent methods. Same-receiver calls,
  locals, conditionals, loops, and early returns are supported.
- Concrete method parameters must have resolved static types; unresolved parameters
  are compile errors rather than defaulting to an integer backend type.
- Untyped-function operation metadata now flows through structured `Constraint`
  records; the former `generic_ops` field has been removed.
- `ConstraintKind::Method` records the receiver relationship, method name, arity,
  argument relationships, and method-result relationships. Every concrete
  duck-typed call site is checked by the same resolver used for direct concrete method
  calls, including distinct diagnostics for missing methods, arity mismatches,
  incompatible arguments, ambiguity, and incompatible results.
- Named traits are structural compile-time contracts. Conformance resolves every
  declared method against the concrete type's inherent methods and checks annotated
  parameter and result types; there is no separate trait dispatch path.
- Functions with duck-typed method requirements or trait-typed parameters are
  specialized into concrete function instances discovered during checking. Generated
  calls target those instances directly, so multiple conforming object types execute
  through ordinary inherent calls without `dyn Trait`, vtables, runtime method search,
  implicit `Any`, or source-level generic type parameters.
- The Rust emitter lowers the existing `sum` builtin for inferred `Vector`/`seq`
  element types, including the concrete result annotation needed by strict Rust.

- Indentation-aware parser for object types, domains, handlers, local `fn` functions, and both `fn main()` and compatibility `proc main()`.
- Julia-like `type Name:` blocks, inferred object fields, expression/block-bodied `fn`
  functions, typed functional pipeline expressions, and synchronous expression-valued
  `message`; source-level `await` is retired.
- Phase 4 recognizes `map`, `filter`, `reduce(initial, fn)`, `sum`, `count`, `any`, and
  `all` as functional/dataflow operations rather than opaque nested calls. Static type
  propagation verifies element, predicate, accumulator, and terminal result types before
  Rust generation, including defined empty-input behavior. A nontrivial reduce initializer
  transfers into the terminal result rather than being copied.
- Functional stages accept named functions, bound READ-only instance methods,
  `_` placeholder expressions with immutable captures, and statically resolved method
  placeholders. Untyped higher-order callable
  parameters are closed at each call site, recorded as specialization dependencies, and
  erased from generated Rust; unbounded callable identity is rejected.
- `ObservableEffects` independently records local capture reads/mutation, domain
  reads/writes, message, I/O/external effects, unresolved effects, and possible
  failure. Transitive summaries conservatively govern fusion without changing
  READ/WRITE/CONSUME ownership inference.
- The functional IR distinguishes dense compilation-local pipeline/node handles from
  source-derived semantic identities. The checked AST carries the exact plan handle for
  each expression and static specialization, so Rust emission never re-matches plans by
  expression text/type/callable. Nodes retain real function/method result lines and stage,
  concrete input/output types, callable identity, captures, ownership/effect summaries,
  logical materialization, and stable semantic provenance. It also records element
  independence, determinism, and reduction compatibility for future planners.
- `-O0` emits explicit eager stage loops and logical intermediate collections. `-O`
  fuses safe map/map, map/filter/map, and terminal-reduction chains into one explicit
  loop, preserving order and wrapping integer accumulation while eliminating intermediate
  vectors. Effectful or possibly failing stages retain eager lowering.
- Phase 4.5 separates traversal fusion from physical materialization. Transformation
  nodes record virtual/materialized state plus escape, multiple-consumer, and barrier
  reasons. Exact count uses collection length; preceding pure/non-failing/non-divergent
  maps are dead when only cardinality is observed. Safe optimized `any`/`all` stop their
  consumer at a decisive element, while eager/effectful/failing/divergent callbacks visit
  the full source.
- Callable summaries carry `may_diverge` independently of ordinary effects and failure.
  Any function/method/handler containing `while`, plus callers reached through the
  acyclic local call graph, is conservatively marked. Work-eliminating plans require
  non-divergence; invocation-preserving ordinary fusion remains legal.
- A new local binding or `let` transformation with one adjacent immutable consumer can
  remain virtual across statements. Mutable/reassigned bindings, later uses, multiple
  consumers, intervening effects/statements, source effects, and control-flow ambiguity
  retain a concrete collection.
- `FunctionalTraversalGroup` represents one stable local source with multiple independent
  terminal consumer edges. Adjacent pure/non-failing sums, counts, filters/reductions,
  and `any`/`all` can share one explicit loop; dependent results and source mutation are
  conservative barriers. Group plans and generated comments retain every consumer's
  semantic provenance.
- `FunctionalPipeline::semantic_steps` is the Phase 4.6 post-analysis Moss semantic
  computation consumed by exact-plan codegen. A composed step references every original
  node; eliminated work retains its source identity and provenance. The bounded pass
  sequence does not reconstruct types/effects or use a generic rewrite framework.
- Phase 4.6 composes adjacent safe maps and left-to-right short-circuit filters. Its only
  predicate pushdown is through trivial `map(_)`, which the existing IR proves to be
  identity. Terminal `count` removes safe trailing maps even after a cardinality-changing
  filter, and inert scalar literal counts become constants. Effects, failure,
  `may_diverge`, ownership, multiple uses, and escapes remain conservative barriers.
- Deterministic `--dump-functional-ir` and `--explain-fusion` output exposes stage types,
  effects, spans, materialization decisions, retained provenance, fusion barriers, the
  optimized semantic-stage sequence, and successful `semantic-opt` rewrites.
- Julia-like domain state bindings (`value = initializer`) with optional `value: Type`
  constraints, statically inferred handler reply types, and `=`-named object constructors.
- Primitive, object, domain-reference, `seq`, `option`, and `table` types in the implemented slice.
- Direct composition-prefix domain construction and `domainroutes(...)` implement
  the static concrete graph. Production handles share per-instance class storage;
  all active modes use the synchronized handler entry described above.
- Phase 10.6E removes legacy mailbox, worker, coarse-lock, batching, coalescing,
  clustering, and atomic-domain implementations and their backend selectors.
- `-Oshared-memory`, `-O`, and `-O0` use the same planned domain runtime;
  only functional optimization depends on optimization level. Retired cluster
  and await-error-handling options are rejected.
- `_shared` wrappers retain planned reader/writer guards around private `_body`
  implementations. Rust checks Send/Sync legality; no unsafe storage access is used.
- Ordinary integer `+`, `-`, `*`, `/`, integer `sum`, and generated state-update
  equivalents lower through explicit `i64` wrapping operations.
- Synchronous expression/statement messages, typed/inferred reply handlers, terminating
  `reply value`, and explicit normal handler completion for no-value handlers.
- Domain-owned mutable state, synchronous handler invocation, local `let`/`var`,
  control flow, `echo`, and bare `return`. Legacy total-domain serialization is
  superseded by 10.6D's exact handler-level class acquisition.
- Ownership checks for direct local assignment, existing owned cross-domain payloads, nested non-primitive projections, domain state, and non-primitive replies.
- One indentation-aware type-environment walker now serves ordinary statement checking,
  domain-reference inference and local-call discovery. It
  forks `if`/`else` environments, merges only types present on every relevant path, and
  rejects conflicting concrete types. Definite same-type branch creation is carried to
  Rust lowering through explicit join metadata.
- Static callable/effect validation remains available across handlers, methods, local
  functions, and `main`. Await-target/cycle machinery is removed; retired syntax
  is covered by migration-negative tests. The concrete route DAG and lock ranks
  provide the active domain deadlock proof.
- Direct and mutual recursion are rejected. Compiler-internal READ/WRITE/CONSUME summaries drive local call lowering, and conflicting aliases at a call site are Moss compile-time errors.
- Copyable field projections retain a read effect; moving a nontrivial field consumes its containing value. Consuming method receivers lower by value rather than as shared receiver references.
- Generated Rust compilation with warnings denied in the test suite. Active domain
  calls enter synchronized wrappers directly and complete before the following
  Moss statement.
- Executable showcases cover method-based duck typing, two concrete named-trait
  implementations, inferred Vector/Map/Queue use, an executable optimized functional
  dataflow pipeline, and a small static job-scheduler application with domains and
  synchronous messages.
- The focused functional showcases additionally cover named and placeholder stages,
  immutable captures and source reuse, every reduction terminal and empty-input identity,
  bound methods and static higher-order specialization, observable eager effect order,
  wrapping reduction arithmetic, read-only pipelines over user-defined values, and an
  synchronous domain callback that remains an eager fusion barrier.
- Four Phase 4.5 showcases cover terminal simplification, a virtual cross-binding
  intermediate, a shared terminal traversal, and multiple-consumer materialization.
- The Phase 4.6 showcase covers semantic map/filter composition, a virtual named
  intermediate, dead-map removal, known literal cardinality, and safe short circuiting.

## Partially implemented or unimplemented

- Package/dependency resolution remains unimplemented. Phase 8 added explicit
  modules/imports; legacy implicit-module project targets still use an uber-module:
  build links `src/**/*.moss`, test links src plus
  `tests/**/*.moss`, and bench links src plus `benches/**/*.moss` as one global unit.
  Assertion panics are isolated per test in one process, but process aborts and failures
  on spawned threads can still terminate that unit. Benchmark warmup/sample counts are a
  fixed conservative first methodology rather than adaptive statistical calibration.
- Phase 5 does not invent one-to-one stepping for fused code. Several Moss nodes may
  resolve to one Rust/native location, and reverse assembly navigation is currently
  symbol/provenance-oriented rather than an exact address-level UI. Generic Rust
  implementations without a concrete Moss specialization have no stable native symbol.
- Emacs, Python, LLDB/`lldb-dap`, `dape`, and objdump are optional integrations. The
  corresponding regression runs only when each capability is installed; there is no
  VS Code extension, LSP, performance model, or Rust interoperability in Phase 5.

- Ownership analysis remains conservative around arbitrary raw expressions and indirect aliases outside the statically represented projection/call slice.
- `deepCopy()` and its cost warnings are approved but not implemented; no implicit copy is inserted.
- Top-level `fn` local functions and inferred parameter effects are implemented.
  Earlier explicit parameter-mutation syntax proposals are superseded by the
  current inferred-effect rule; no such syntax is planned.
  Self-send and same-domain handler chaining are rejected; queued self-communication
  is not an active language feature.
- Reply-path completeness is checked only syntactically; fallthrough is diagnosed at runtime.
- Production synchronization consumes the stored plan at every optimization level;
  old backend synchronization heuristics are removed. 10.6D.1 removes READ
  snapshots; static-accessor code size, slot/descriptor overhead, and conservative
  explicit-boundary copies remain possible future representation optimizations.
- The closed concrete route DAG and deterministic domain ranks are complete.
  `SynchronizationPlan` supplies class ranks and handler ClassSets; 10.6D consumes
  them for production physical class locking and handler-level 2PL.
- Fast Debug executes synchronous domains and source-module closures. Functional
  pipelines and `for` remain unsupported interpreter constructs with precise errors;
  source-free Moss dependencies require production execution.
- Primitive ordinary-parameter WRITE lowering is fixed in 10.6D; the existing
  semantic effects remain authoritative.
- Functional collection ownership is intentionally conservative. A callback that would
  WRITE or CONSUME a nontrivial element, a mutable capture, or a nontrivial `filter`
  result that would require an implicit copy is rejected rather than cloned or made
  unsafe.
- Functional fusion uses correctness-first effect rules rather than profitability data.
  Phase 4.5 scope reconstruction is deliberately lexical and adjacent rather than a
  general SSA optimizer. Automatic SIMD, threading, GPU lowering, generic stage
  reordering/algebraic predicate pushdown beyond Phase 4.6's proven identity case,
  layout/storage reuse, general lambdas, runtime callable values, user effect
  annotations, initializer-free reduction, and sophisticated profitability modeling
  are not implemented.

## Known bugs and limitations

- Static specialization currently covers method-constrained and trait-typed local
  functions in the implemented expression/call slice. Associated types, trait
  inheritance, default trait methods, runtime trait objects, and source-level generic
  declarations remain intentionally unsupported.

- Rust type errors may still surface when Moss inference lacks enough source information; normal Phase 2 ownership and conflicting-call-access errors are diagnosed by Moss.
- Source `await` and `spawn` are retired; domain construction is restricted to the
  initial `main` composition prefix. Cancellation, timeouts, and failure propagation
  remain unimplemented.
- Parent class guards remain held across nested synchronous messages, intentionally
  increasing lock-hold latency and possible head-of-line blocking. Fairness is
  unspecified; early unlock and lock elision remain future work.
- Most compiler implementation remains in one C++17 translation unit with a deliberately
  compact semantic/type checker and extracted data headers.

## Historical validation by checkpoint (before Phase 10.6)

The following records describe tests at their original checkpoints. References to
spawn, await, one-way messages, passable handles, FIFO, and serialization are
historical coverage, not current source semantics or a claim that those fixtures
remain unchanged. Current validation and the historical documentation-only
preflight are recorded separately above. Legacy physical-code expectations have
been replaced with plan-driven expectations while executable behavior stays covered.

`make check` passes with Phase 4.6 on the frozen Phase 2.5 foundation. The suite compiles ordinary,
Mutex/RwLock/atomic optimized, batched, coalesced, and clustered Rust with
`rustc -D warnings`; rejects any generated
`std::sync::mpsc` use; checks local implementations for the expected synchronization
boundary; compares behavior for checkout, object isolation, ignored replies, local and
shared domain-reference messages, FIFO, and non-reentrancy; rejects invalid cluster
layouts, cycles, naked domain calls, invalid awaits, unresolved fields/state, conflicting
reply types, state annotation mismatches, and value-returning `main`; verifies source/
backend annotations; verifies `--no-await-error-handling`; and repeatedly exercises both
the lock-backed mailbox and direct state-lock paths under contention. Dedicated positive
cases cover inferred state, optional state annotations, inferred replies, and `=`
constructors; the former one-way-reply negative fixture is now a positive compatibility
case because reply presence infers request/reply capability. Dedicated method/trait
cases execute one duck-typed function and one trait-typed function with two distinct
user-defined types, assert that two concrete specializations are emitted, and reject
runtime Rust trait machinery. Negative coverage includes missing methods, wrong arity,
incompatible method arguments, missing trait methods, incompatible trait signatures,
conflicting method-result expectations, unresolved collection element types,
heterogeneous collections, and naked cross-domain calls. The showcase suite also executes
under the regression harness: static duck typing, named traits, inferred collections and
methods, the `map |> filter |> map |> sum` dataflow shape, the domain-backed mini
application, and the focused functional examples through Phase 4.6.

Await regressions additionally accept same-concrete-domain branch joins, reject
different-domain joins and one-branch-only targets, validate an await helper reached
only from `main`, retain unreachable-branch dependencies, and cover direct, transitive,
helper-hidden, and repeated-edge cycles. Cycle assertions require the correct Moss line
for every witness edge. All of these cases run alongside the unchanged Phase 2
ownership and Phase 2.5 backend suites.

Phase 2.5 regressions additionally prove same-receiver batching and sender order,
different-receiver/effect barriers, batched tracker accounting/rollback, exclusive
one-guard regions, multi-caller rejection, shared READ versus exclusive WRITE guards,
integer and boolean atomics, mixed awaited/one-way mailbox elimination, multi-field
SeqCst use, atomic request/reply copy boundaries, locking fallbacks for invariants and
external effects, fully atomic runtime removal, atomic and read-heavy contention, and
representative `-O0`/`-O` output parity.

The overflow differential additionally runs increment-at-`i64::MAX`, decrement-at-
`i64::MIN`, positive addition overflow, negative-direction subtraction overflow,
and awaited new-value replies through both `-O0` mailboxes and `DirectAtomic`.
Both generated programs are compiled with `rustc -C overflow-checks=yes -D warnings`
and must match. It also checks ordinary wrapping multiplication and division,
integer `sum`, and a concretely specialized duck-typed integer operation.

Phase 4 regressions run representative functional programs through both `-O0` and `-O`,
compile both generated Rust programs with `rustc -D warnings`, compare their exact output,
and cover named, bound-method, and placeholder `map`, `filter`, map/map, map/filter/map,
every terminal, immutable capture, statically specialized higher-order helpers, empty
input, object-method placeholders, eager callback output order, and wrapping reduction.
Generated-code checks require one explicit loop and no intermediate vector or Rust iterator chain for fusible
pipelines. Separate I/O, possible-failure, domain-state, and synchronous `message` cases must
retain eager lowering. Terminal scalars can use message/reply boundaries directly, while
transformation collections must first materialize. Deterministic IR/explanation checks
cover types, spans, materialization, provenance, and barrier reasons. Negative tests require Moss diagnostics for invalid
predicates/callables/reductions, consuming elements, mutable capture, unbounded callable
identity, pipeline domain-boundary escape, higher-order recursion, and callback alias
violations.
They additionally distinguish identical pipeline text in pure-local and domain-effect
contexts, prove that an I/O-producing `reduce` initializer is recorded on the `Reduce`
node and preserves differential evaluation order, verify real method-result provenance,
reject non-Copy `_`/field-projection maps before Rust generation, and reject captured
mutation propagated through nested ordinary helpers.
The focused functional examples are likewise differential tests: each is compiled with
both `-O0` and `-O`, compiled by `rustc -D warnings`, executed, and compared against one
expected output.

Phase 4.5 differentials cover exact and mapped count, empty/immediate/late/no-match
`any`/`all`, complete eager effect ordering, possible-failure barriers, bare and explicit
immutable cross-binding fusion (including a function result), later-use/mutable/effect
materialization barriers, multiple consumers, sum/count/filter-count and any/all/count
shared DAGs, result-dependency and source-mutation barriers, and wrapping shared sums.
Direct/transitive `while` regressions verify that mapped-count elimination and standalone,
cross-binding, and shared-DAG `any`/`all` skipping are disabled by `may_diverge`, while
ordinary invocation-preserving fusion remains enabled.
Generated-Rust checks require eliminated callbacks/loops, optimized-only short-circuit
control flow, absent virtual collections, present required collections, and one shared
source loop. Deterministic IR checks assert materialization reasons, DAG edges, stable
group identity, and combined provenance.

Phase 4.6 differentials cover adjacent map and filter composition, identity-only legal
predicate pushdown plus non-identity/effectful rejection, safe and divergent/effectful
dead-map cases, filter/map/count terminal simplification, safe and divergence-blocked
`any`/`all`, known literal cardinality, runtime-empty terminal identities, single-use
cross-binding collapse, downstream dead work, and multiple-use materialization. The
suite inspects the semantic plan and generated Rust, including composed predicate
short circuiting, eliminated callbacks/literal allocation, retained barrier callbacks,
deterministic rewrite output, and exact `-O0`/`-O` result parity.

Phase 5 regressions build the tooling fixture twice at identical paths and require
byte-identical maps; compare `-O0`, optimized, and explicit debug maps; verify deterministic
source/provenance identities, real function/method/handler lines, readable native symbols,
bidirectional exact line resolution, and many-to-one fused provenance; and compile and
run both generated programs with warnings denied. Python map/LLDB helper checks pass,
all 19 batch ERT tests pass under Emacs 30.1, and `llvm-objdump` resolves both an ordinary
debug function and an optimized fused-pipeline symbol. Live LLDB 19.1.7 and
`lldb-dap-19` regressions load the shared helper/map, resolve exact breakpoints for main,
a method, ordinary functions, and a handler, step between two exact Moss lines, map user
and runtime stack frames, inspect integer/boolean/user-type/raw String/raw Vector locals,
and terminate cleanly. Debian's LLDB build warns that Rust-specific value presentation is
limited, so collection/string inspection remains structural. Phase 5 provenance IDs are
repeatable for an unchanged source layout but are not the durable cross-edit semantic IDs
planned for Phase 6. No compiler functionality depends on optional debugger tools.

`make examples` passed, compiling every valid example (including the executable static
trait, dataflow, reduction, callable, effect-order, and object-pipeline showcases) with
`-Oshared-memory`; the intentional `use_after_transfer.moss` negative example was skipped. A strict
`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic` build and `sh -n tests/run.sh` also
passed for this checkpoint.

Phase 7 validation additionally builds the example project in debug and release modes,
checks byte-stable map/artifact reuse under one Rust toolchain, forces rebuilds for a
changed/fake Rust identity, executes and filters passing/failing native tests,
inspects Moss assertion values/locations, executes and filters release benchmarks,
validates call/arithmetic/pipeline black-box and sampling structure, saves and compares
a toolchain-identified metadata/sample baseline, suppresses incompatible baseline
threshold enforcement, checks structured project errors, and runs `moss clean`.
Phase 6A validation also proves target-local statement effects remain unknown rather
than inheriting callable-wide I/O while the enclosing, callable, and pipeline summaries
remain available. The full `make check` suite,
`make examples`, strict C++17 warnings-as-errors build, generated Rust `-D warnings`,
Phase 4/4.5/4.6 differentials, Phase 5 map/Emacs/objdump/LLDB/DAP integrations, Phase 6A
API regressions, 19 ERT tests, `sh -n tests/run.sh`, and `git diff --check` all pass.

Two local smoke samples of the contention program completed 20 baseline runs in approximately 0.20–0.25 seconds and 20 direct shared-memory runs in approximately 0.02–0.03 seconds. This is evidence that transport elimination works for the intended request/reply shape, not a general performance claim.

## Immediate next tasks

Phases 10.6A–F are complete and checked in. Phase 10 is the Moss v0.1 milestone.

1. Phase 15 — Dogfooding: write substantial Moss programs, identify real friction,
   and let actual usage determine the next refinements. Start with the ledger,
   index, pipeline, and structural-trait seeds. This closeout does not begin new
   Phase 15 feature work.
2. Track measured tiny-handler/storage/code-size costs, interpreter `for`/pipeline
   coverage, cross-module generic linkage and ergonomics, trace slicing, package
   resolution, and standard-library gaps as evidence for subsequent priorities.
3. Post-dogfooding roadmap: Phase 20 Rust Interoperability; Phase 21 Error
   Propagation & Supervision; Phase 22 Agent Agency Tooling. Scoped domain lifetimes
   and concurrency ingress remain explicitly unresolved future directions.

The 10.6C synchronization algorithms remain authoritative and unchanged apart
from removal of unreachable retired-Await traversal branches. No language-design
change is authorized by this consolidation phase.

## Open design questions requiring Kutty's decision

- Exact `deepCopy()` warning wording and fixed-size estimates.
- Future retention-safe designs for immutable sharing, arenas, and persistent versions.
- How future automatic cluster selection should balance locality, synchronous calls, and load distribution.
- Supervision, failure propagation, transactional rollback, and restart semantics remain
  unresolved and were deliberately not changed by this frontend migration.

## Historical 2026-09-11 shared-memory and clustering handoff

This record predates 10.6A/B/B.1. Its spawn, await, one-way queue, self-message,
and serialization descriptions are historical, not the current language model.

### What changed in this session

- Replaced every Rust standard channel with generated lock-backed shared-memory queues and one-shot reply cells.
- Kept the optional awaited-only `Arc<Mutex<DomainState>>` direct optimization for unclustered domains.
- Added repeatable `--cluster=A,B` backend placement. A generated cluster runtime owns one thread, one shared ingress queue, all member states, and a plain local queue.
- Generated distinct `_shared` and `_local` call implementations and selects between them statically. Cluster-member capabilities lower to zero-sized local references. Local awaits are ordinary handler calls; one-way calls retain queued semantics without synchronized operations.
- Added `--no-await-error-handling` as an explicit backend opt-in for supervision-owned failures. It removes per-await `unwrap_or_else` diagnostics and emits unchecked reply extraction; the checked path remains the default until supervision trees exist.
- Added target-specific local flushing before direct awaits to preserve FIFO when an earlier local one-way call targets the same domain.
- Prohibited cross-domain ownership transfer of bound non-primitive data, including nested projections and replies, while permitting fresh message construction and same-domain queued transfer.
- Updated the object pipeline to send a primitive snapshot across domains and added transport, clustering, contention, FIFO, and ownership regressions.

### Decisions Kutty explicitly made

- All Rust message transport must use shared memory with locks and Rust's `Send` boundary; there is no true Rust message-passing fallback.
- Moss must continue to expose and behave as message passing; the backend representation must not change the language.
- Domain-private non-primitive values cannot transfer ownership across domains.
- Configured clusters share one execution thread, and calls among their members remove synchronization through statically selected generated implementations.

### Assumptions Codex made

- `--cluster` is a compiler-side placement input that generates the requested runtime startup call, because runtime branching would conflict with static call selection and Moss must not gain placement syntax.
- Fresh values built directly as payloads are message-owned rather than transferred from a domain. Queued self-messages remain within one ownership domain.
- The initial cluster implementation is intentionally limited to one unconditional spawn per member type.

### Tests actually executed

- `make check` — passed with baseline and shared-memory generated Rust compiled under `rustc -D warnings`.

### Exact commit hash containing the backend work

`cd65d8c` — Backend predecessor before the frontend migration. The syntax-direction
checkpoint is `c130ff2`; the completed frontend checkpoint is the commit that follows.

## Historical 2026-09-05 transfer-semantics handoff (superseded domain model)

The domain-boundary descriptions and await/serialization assumptions below are
historical. Current message/reply value boundaries, immutable payloads, closed
handles, and synchronous calls are described above.

### What changed in this session

- Made approved transfer semantics authoritative in the design records.
- Renamed user-facing move terminology to transfer terminology.
- Added the earlier source checks for detached message transfers and direct domain-state transfer rejection. The later 2026-09-11 boundary rule now rejects the cross-domain transfer case.
- Updated the earlier message lowering to avoid hidden deep copies; current lowering accepts fresh payloads and rejects existing owned non-primitive bindings at cross-domain boundaries.
- Added positive and negative examples and regression tests.

### Decisions Kutty explicitly made

- Nontrivial assignment transfers ownership; hidden deep copies and COW are prohibited.
- `deepCopy()` is explicit future syntax and is not implemented.
- Detached-local cross-domain transfer was the decision at that time and is superseded by the 2026-09-11 prohibition. Same-domain queued transfer remains allowed.
- The historical explicit-parameter proposal is superseded: ordinary function
  parameter READ/WRITE/CONSUME effects are inferred, and primitive parameter
  assignment may be caller-visible WRITE. No `var`, `mut`, `write`, or `inout`
  modifier is planned for parameter mutation.
- Immutable sharing, arenas, and `revise` remain deferred due memory-retention and leak concerns.

### Assumptions Codex made

- Historical assumption, superseded by Phase 10.6A: await/reply and total-domain
  serialization were treated as the language model. Phase 10.6A establishes
  synchronous `message` and terminating `reply`. Phases 10.6B–C add the closed
  concrete graph and compiler-owned synchronization analysis; physical
  handler-level 2PL is implemented in 10.6D.
- The lightweight checker is extended only for direct, statically recognizable transfer cases in this increment.

### Tests actually executed

- `make clean && make check` — passed; generated Rust was compiled with `-D warnings`.

### Tests that could not be executed and why

- None.

### Semantic questions still unresolved

- At this historical checkpoint: `deepCopy()` warning details and ordinary
  procedure design. The parameter-syntax proposal is superseded by the current
  inferred-effect rule above.

### Exact commit hash containing the work

`0adac33` — Implement approved Moss transfer semantics.

## 2026-09-14 temporary multi-file project linkage

This checkpoint introduced the uber-module model before Phase 8 added explicit
modules/imports. Legacy implicit-module targets retain it: `moss build` compiles
every `src/**/*.moss`; `moss test` adds
`tests/**/*.moss`; and `moss bench` adds `benches/**/*.moss`. Each target is one
global compilation unit with deterministic path discovery, order-independent
declaration resolution, duplicate-symbol diagnostics, and original physical
source provenance in diagnostics, semantic queries, generated maps, tests, and
benchmarks. Native artifact fingerprints include the complete source set.

Semantic query/edit context hardening: Phase 6A queries and edits now consume the
same temporary uber-module source sets as project build/test/bench. `src` selectors
analyze all application files, test selectors use `src + tests`, and benchmark
selectors use `src + benches`; standalone files remain single-file. Cross-file
renames are transactional within the selected context, and expression/argument edits
are validated against the complete logical program before writing. Physical source
paths remain in query results and diagnostics.
## Implicit domain specialization hardening

Each declared instance of an implicit-state domain now has one monotonic concrete
specialization. Compatible repeated calls remain valid; an incompatible state,
handler-parameter, or reply constraint is rejected with both constraint lines rather
than overwriting the layout. Separate instances of the same source domain continue to
specialize independently. Historical exact-instance await identities are now
superseded by the closed concrete graph and exact specialization linkage.

Handler parameter constraints are indexed by handler and parameter slot. Multi-parameter
handlers can therefore specialize as tuples such as `Set[0] -> Int, Set[1] -> Float`; only
an incompatible constraint for the same slot is rejected.

Historical typed domain-handle parameter support is superseded by Phase 10.6B.1:
handles cannot be parameters, payloads, or reply values. Concrete composition and
declared routes retain exact instance identity; the backend nominal route wrapper
may still route to independently materialized layouts. That wrapper is implementation
plumbing, not a passable Moss value.

## Phase 8

Modules/imports/exports are supported with qualified namespace resolution.
Typed exports are materialized contracts; untyped exports are compile-time Moss
generics represented in versioned `.mossi` interfaces. Historical await-graph
identities are superseded by 10.6B/B.1's closed concrete route DAG and exact
instance identities (`module::binding`). Each explicit module emits its own generated Rust
crate/rlib in import-DAG order; consumers link dependency rlibs with `--extern`.
Generic semantic IR and private helper closure are re-instantiable without
provider source, and final-build specializations are canonical in a dedicated
Rust crate. Legacy implicit-module projects remain supported.

## Phase 4.7 — Static Iteration and `for` Loops

**Phase 4.7 — Static Iteration and For Loops: complete and frozen.**

Phase 4.7 is implemented additively on the frozen Phase 4–4.6 semantics. `for binding
in source:` has scoped bindings and participates in the existing type, effect, and
ownership walkers. `range(start, end)` is half-open; vectors use READ traversal; and
custom traversal resolves statically through the structural `Iterator` contract
(`next() -> Option[element]`). Collection mutation during an active READ traversal is
rejected. Effect analysis reuses the canonical iterator-element resolver for loop
bindings, so method and ownership/effect summaries agree with ordinary type analysis.
No runtime iterator dispatch or vtable is emitted.

## Historical Phase 2.6 — Immutable Message Payloads and Shared-Memory Copy Elision

The physical backends below are retired in 10.6E; current production message and
reply boundaries establish independent owned values. Payload immutability remains.

Incoming handler arguments are immutable READ-only value snapshots regardless of
concrete type. The normal ownership/effect checker rejects mutation,
reassignment, consumption, and moving a payload into domain state, including
through transitive helper calls. A payload may be replied by value or forwarded
with `message`; both are explicit value boundaries. `Copy` is only a
backend/property distinction and does not weaken these Moss restrictions.
Legacy mailbox adapters retain owned payload copies. Synchronous DirectMutex,
DirectRwLock, DirectAtomic, and cluster-local lowering pass nontrivial READ-only
payloads by temporary immutable reference when safe, while primitive Copy values
remain by value. This is a backend optimization preserving identical Moss
semantics.

## Historical Phase 10.1A — Fast Debug interpreter checkpoint

The domain restriction below is superseded by Phase 10.6E.

The checked Moss AST now has a direct Fast Debug execution backend for ordinary
functions. `moss run --interp` and standalone `moss test --interp` execute
literals, arithmetic, locals, assignments, conditionals, while loops,
function calls, returns, structs, fields, methods, and assertions without
invoking Rust tooling. `--trace` emits deterministic newline-delimited JSON
semantic events. Domain scheduling, synchronous messages, and replies remain
unsupported in Fast Debug; source `await` is retired. Unsupported operations
report an interpreter diagnostic rather than falling back to generated Rust.

Fast Debug project entry points now reuse the project compiler's checked source
loading. `moss debug app` (or a project directory/entry source) interprets the
transitive source-module closure for explicit-module projects, and the complete
legacy `src/**/*.moss` uber-module otherwise. All selected Moss code executes
in one interpreter; compiled Moss interfaces/native modules are rejected rather
than mixed into the run. Project closure regressions cover cross-file calls and
verify that no Rust compiler is needed.

## Historical Phase 10.5 — Semantic convergence and AI introspection

Phase 10.5 is a non-semantic tooling/documentation checkpoint. The existing
static callable specialization, first-order effect summaries, entity-v1/source
provenance, semantic query API, impact graph, and Fast Debug trace are now
documented together. Query JSON exposes the existing source identity,
specialization identity when an exact record exists, resolved call kind, and
caller edges without creating a second analysis. Fast Debug ordinary execution
trace events retain source file and semantic identity and include local reads,
returns, branches, loops, writes, and assertion failures.

At the 10.5 checkpoint, synchronization diagnostics and the Account derivation
were reserved/design-only. This is superseded by 10.6C: schema and semantic
queries now expose the stored `SynchronizationPlan`, including derived effects,
classes, ranks, handler sets, and conflicts. `docs/SYNCHRONIZATION_PLAN.md`
records the implemented Account derivation.

The Phase 10.5 “BLOCKED BY OPEN DESIGN” paragraph is historical and superseded
by the resolved decisions in Phases 10.6A–C:
10.6A/B/B.1 settled synchronous calls, self-send/chaining rejection, closed handles,
and concrete domain ranks; 10.6C implements synchronization analysis and semantic
effect metadata. Existing ordinary parameter WRITE effects remain authoritative.
Phase 10.6D implements physical handler-level 2PL and primitive write-through;
Phase 10.6E adds Fast Debug domain execution and removes the legacy runtime.

## Fast Debug parity follow-up

Fast Debug now preserves eager source traversal for `any` and `all`, evaluates
`Map.get` defaults eagerly, and returns independent Map `get`, `keys`, and
`values` results. Typed empty vectors retain their element type in Fast Debug so
`Vector[Float]()` sums to Float zero. Focused native/Fast Debug regressions cover
these semantics, Map operations through object fields, and empty Queue pop
fail-closed parity.

`tests/run.sh ./moss` reaches the known unrelated Phase 10.6F `&String == String`
native lowering failure after all focused additions pass. `make check` reaches the
known environment-specific LLDB/DAP real-integration handshake failure first.

The remaining Fast Debug closeout repairs detach a selected non-Copy `Map.get`
fallback and retain the checked map-output element type through empty pipelines.
Focused native/Fast Debug parity regressions cover missing-key fallback ownership,
empty `Int -> Float` and `Int -> Int` maps, and an empty Float filter before `sum`.

Fast Debug frames now carry the checker-compatible functional context separately from
their human-readable trace name. Pipeline metadata lookup matches canonical context,
line, and checked stage source, including static function specializations and multiple
pipeline expressions on one line.
