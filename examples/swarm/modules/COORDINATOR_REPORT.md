# Phase 15.12D — Module, Package, Visibility & Public-API Boundary Torture
## Coordinator Synthesis Report

**Date:** 2026-09-23  
**Status:** COMPLETE (Dogfood discovery and validation phase; compiler implementation fixes pending)  
**Artifact Location:** `examples/swarm/modules/`  

---

## Executive Summary

Phase 15.12D evaluated Moss v0.1's module, package, visibility, and compilation-unit boundary semantics across six substantial, independent workloads. Prior dogfood phases focused on language-internal features (expressions, control flow, domains, collections, static polymorphism). Phase 15.12D specifically evaluated the boundary abstraction promise:

> **Can a Moss programmer put an abstraction boundary around otherwise-valid code without changing its semantics or needing compiler-internal knowledge?**

**Answer: No, not yet.** In current Moss v0.1, introducing a module or package boundary frequently alters program semantics, breaks type inference, or causes compiler crashes on code that is completely valid in a single file. Across six independent workstreams, agents uncovered **8 boundary-specific defects** and **1 general backend hygiene defect** (allocated as **SWARM-056 through SWARM-064**), while independently reproducing **6 existing findings** (**SWARM-033, SWARM-034, SWARM-037, SWARM-045, SWARM-046, SWARM-053**).

Despite these compiler boundary obstacles, five of the six workstreams successfully adapted their architectures with well-characterized workarounds, achieving **109 passing unit tests** natively, full executable execution, and bit-for-bit Fast Debug parity on supported subsets. The sixth workload (`algo_chain/routekit`) remains blocked by an internal compiler synchronization invariant crash (SWARM-046).

---

## 1. Workload Architectures & Final Status

The six dogfood workstreams implemented distinct architectural patterns:

| Workstream | Intended Architecture | Packages | Modules | Native Build | Native Tests | Native Run | Fast Debug | Checker | Formatter | Final Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`algo_chain`** | Reusable algorithms (`heapkit`) consumed by routing/scheduling application (`routekit`) via Margo path dependency | 2 | 9 | `heapkit`: PASS<br>`routekit`: FAIL (SWARM-046) | 8 / 8 pass (`heapkit`) | `routekit`: FAIL | `heapkit`: PASS<br>`routekit`: FAIL (`break`) | `heapkit`: PASS<br>`routekit`: FAIL | `heapkit`: PASS<br>`routekit`: PASS | `heapkit` GREEN;<br>`routekit` BLOCKED |
| **`calc_interpreter`** | Multi-module calculator / AST interpreter in a single package (`ast`, `calc`, `env`, `errors`, `eval`, `lexer`, `parser`, `node`, `report`, `token`, `main`) | 1 | 11 | PASS | 32 / 32 pass | PASS | PASS (100% parity) | PASS | FAIL (`lexer.moss:46` SWARM-033) | GREEN (tests, run, debug);<br>Fmt blocked |
| **`data_pipeline`** | Data validation provider (`records`) consumed by analytical summary application (`analytics`) via path dependency | 2 | 6 | PASS | 16 / 16 pass (9 records, 7 analytics) | PASS | PASS (100% parity) | PASS (with `MOSS_MODULE_PATH`) | PASS (with `MOSS_MODULE_PATH`) | GREEN (with workarounds) |
| **`domain_services`** | Three-tier architecture: common records (`svc-common`), synchronous actors (`svc-services`), coordinator application (`svc-app`) | 3 | 7 | PASS | 15 / 15 pass (3 common, 2 services, 10 app) | PASS | PASS (100% parity) | PASS (with `MOSS_MODULE_PATH`) | PASS (with `MOSS_MODULE_PATH`) | GREEN (with workarounds) |
| **`lib_and_app`** | 2D vector geometry library (`geolib`) consumed by scene composition application (`sceneapp`) via path dependency | 2 | 8 | PASS | 14 / 14 pass (9 geolib, 5 sceneapp) | PASS | PASS (100% logical parity) | PASS (with `MOSS_MODULE_PATH`) | PASS (with `MOSS_MODULE_PATH`) | GREEN (with workarounds) |
| **`text_toolkit`** | Reusable text formatting/statistics library (`toolkit`) consumed by order shop (`app`), plus Fast Debug probe (`probe_debug`) | 2 | 6 | PASS | 24 / 24 pass (21 toolkit, 3 app) | PASS | `probe_debug`: PASS<br>`app`: FAIL (`for` limit) | PASS (with `MOSS_MODULE_PATH`) | PASS (with `MOSS_MODULE_PATH`) | GREEN (with workarounds) |

**Totals:**
- **Packages compiled & verified:** 12
- **Source modules:** 47
- **Unit tests:** **109 passed, 0 failed** across all green suites.
- **Completed executable runs:** 5 applications running to completion with deterministic output.

---

## 2. Parity Across Tooling Layers

### Native Compilation vs Fast Debug (Interpreter)
- **Source-closure parity:** Fast Debug (`margo debug`) interprets the full reachable Moss source tree provided by Margo. When source is available, Fast Debug executes methods on imported types that natively fail due to `.mossi` interface omissions (SWARM-056).
- **Parity divergences:**
  - `for` traversal: Fast Debug rejects `for` loops with documented interpreter limitation `Fast Debug does not support for iteration yet`. Workloads targeting Fast Debug parity rewrote loops to `while`.
  - Boolean expressions: Expressions such as `a > 0 and b >= 0` succeed natively but fail in Fast Debug with `interpreter error: unsupported expression '0 and b'` (SWARM-037).
  - Floating-point string formatting: Minor representation difference in Float printing (e.g. `27.351757499999998` in native Rust vs `27.3517575` in Fast Debug), noted in `lib_and_app`.

### Margo vs Standalone Moss Tooling
- **Package Graph Resolution:** `margo build`, `margo test`, `margo run`, and `margo debug` automatically resolve path dependencies and pass compiled artifact roots (`build/debug/`) and source directories to compiler subcommands.
- **Standalone Tool Friction:** Standalone `moss check <source>`, `moss fmt --check .`, and semantic queries (`moss inspect`, `moss type`) on downstream packages fail with `MOSS_COMPILE_ERROR: imported module '<name>' was not found in source or compiled interfaces` unless the user manually supplies `MOSS_MODULE_PATH=<dep>/build/debug`. This is intended tooling separation (Margo owns manifests, Moss owns compiler semantics), but requires explicit agent awareness when invoking standalone Moss tools on package consumers.

---

## 3. Module & Package Boundary Defects (SWARM-056 – SWARM-063)

The central strength of the Phase 15.12D swarm is its extensive collection of **paired single-vs-split reproducers**. The following findings demonstrate failures that occur solely because an abstraction boundary was introduced:

### 1. SWARM-056 — Methods of Exported Types Omitted from Compiled `.mossi` Metadata
- **Observation:** In a single package across multiple modules, methods on exported types (`b.area()`) resolve and execute. In Fast Debug, methods resolve because ASTs are loaded. But across a compiled package boundary (`margo build` via path dependency), calling any method on an imported type fails with `MOSS_COMPILE_ERROR: no matching method '<pkg>__<Type>.<method>' for supplied arguments`.
- **Root Cause:** In `build/debug/<module>.mossi`, `export type <Name> nominal` emits only public field representations; member method definitions are completely omitted from the interface metadata. Downstream consumers importing via `.mossi` see a methodless struct.
- **Paired Proof:** `data_pipeline/repro/dep_type_methods` (`one_package` passes; `two_packages` fails). Also reproduced in `lib_and_app/repro/method_across_package`, `text_toolkit/repro/method_call_downstream`, `algo_chain`, and `domain_services`.
- **Workaround:** Provider packages must export free-function wrappers (`export fn box_area(b: Box) -> Int: return b.area()`).

### 2. SWARM-057 — `for i in range(...)` Fails Type Inference Inside Explicit Modules
- **Observation:** `for i in range(start, end):` is accepted by the checker and formatter in non-module files. As soon as the file declares `module <name>`, the checker fails with `TYPE_INFERENCE_FAILED: cannot infer the static iterator source type`.
- **Root Cause:** Module AST rewriting alters range expression lowering or type propagation in module scopes.
- **Paired Proof:** `data_pipeline/repro/range_in_module` (`single` passes; `split` fails). Confirmed independently in `lib_and_app`, `text_toolkit`, `algo_chain`, and `domain_services`.
- **Workaround:** Rewrite `range` loops to explicit `while` counter loops.

### 3. SWARM-058 — Sibling Method Calls in Exported Types Mis-mangle as Module Functions
- **Observation:** Inside an exported type's method, calling a sibling method unqualified (`return a() + 1`) resolves to `self.a()` in a single-file program (per SWARM-025/028). In an explicit module, `rewrite_module_program` rewrites the call to a top-level module function `mod__a()`, failing with `UNKNOWN_SYMBOL_OR_TYPE: unknown local function 'mod__a'` or `TYPE_INFERENCE_FAILED`.
- **Root Cause:** Module symbol qualification rewrite runs before or without method context checks, treating all unqualified calls in methods as module-level function calls.
- **Paired Proof:** `data_pipeline/repro/sibling_method` (`single` passes; `split` fails). Also confirmed in `lib_and_app`, `text_toolkit`, `calc_interpreter`, and `algo_chain`.
- **Workaround:** Inline sibling method bodies or extract shared logic to private module-level helper functions.

### 4. SWARM-059 — Multi-File Project Diagnostics Misattribute Error Source File
- **Observation:** In a multi-file package, type/syntax errors occurring in secondary modules (e.g. line 9 in `beta.moss`) are reported with the correct line and column, but with `source_file` set to the project root (`main.moss`) or the first alphabetical module (`ast.moss`, `alpha.moss`). This frequently reports line numbers that exceed the total line count of the attributed file.
- **Root Cause:** Whole-project AST merging in `check_project_sources()` or formatter validation assigns the root/first source URL to diagnostics emitted from merged token spans.
- **Paired Proof:** `domain_services/repro/diag_file` (`split` reports `alpha.moss:9` for line 9 in `beta.moss`). Also observed in `calc_interpreter`, `data_pipeline`, and `lib_and_app`.

### 5. SWARM-060 — `moss edit rename` Fails on Module-Qualified Entities (`EDIT_TARGET_AMBIGUOUS`)
- **Observation:** Querying `moss inspect util__double` returns durable identity `entity-v1:function:util__double`. Executing `moss edit rename entity-v1:function:util__double new_name` fails with `EDIT_TARGET_AMBIGUOUS: rename could not map every semantic reference to one exact token`.
- **Root Cause:** The rename tool searches for exact tokens matching the mangled identity or fails to match the declaration token (`double`) and qualified call site tokens (`util.double`).
- **Paired Proof:** `domain_services/repro/edit_rename` (`single` passes; `split` fails). Confirmed in `text_toolkit`, `data_pipeline`, and `lib_and_app`.

### 6. SWARM-061 — Test Binary Compilation Fails When Tests Import Internal Modules Omitted from `main.moss`
- **Observation:** When test files (`tests/*_test.moss`) import an internal module (`import util`) that is not imported in `src/main.moss`, `margo test` fails with rustc `error[E0425]: cannot find function util__... in this scope`.
- **Root Cause:** Project tests are compiled into `app.rs` (the root crate), but lowering emits Rust module dependencies (`use moss_<mod>::*;`) only for modules imported by `main.moss`.
- **Paired Proof:** `calc_interpreter/repro/test_imports_not_in_main`.
- **Workaround:** Developers must place unused `import` statements in `main.moss` for all modules used anywhere in tests.

### 7. SWARM-062 — Specialized Generic Functions Calling Transitive Modules Fail Native Lowering (`E0425`)
- **Observation:** When consumer module `A` calls generic function `f` exported from `B`, and `B.f` calls a function from module `C`, monomorphization projects the specialized body of `f` into `A.rs`. Because `A.moss` does not import `C`, `A.rs` does not include `use moss_C::*;`, failing rustc with `error[E0425]: cannot find function C__... in this scope`.
- **Paired Proof:** `algo_chain/repro/generic_transitive_import` (`single` passes; `split` fails). Required workarounds in `routekit` (`rank.moss`, `spanning.moss`).
- **Workaround:** Consumer module must explicitly import the transitive module (`import leaf # generic body needs it linked`).

### 8. SWARM-063 — Transitive Struct Fields Leak Unimported Rust Trait Requirements Across Modules (`E0405`)
- **Observation:** When module `A` uses `B.Outer`, which contains a field of type `C.Inner`, the backend generates view trait implementations in `A.rs` constrained by `MossAccess_C__Inner`. If `A.moss` does not import `C`, native compilation fails with `error[E0405]: cannot find trait MossAccess_C__Inner in this scope`.
- **Paired Proof:** `lib_and_app/repro/transitive_import_closure` (`single` passes; `split` fails).
- **Workaround:** Consumer module must explicitly import the transitive module (`import inner`).

---

## 4. General Defects Encountered at Boundaries

The following findings represent general Moss defects rather than boundary-only issues, but were surfaced prominently when code was partitioned into modules:

1. **SWARM-045 — Exported Struct Fields Lower Without `pub` Modifier (Rust E0616 / E0451):**
   Independently confirmed by all six workstreams. Struct fields lowered to Rust structs lack `pub`, preventing direct field access and literal construction from other modules. Workaround: accessor methods and factory functions.
2. **SWARM-046 — Module Export Effect Analysis Invariant Crash:**
   Independently confirmed across 5 workstreams (`algo_chain`, `data_pipeline`, `lib_and_app`, `calc_interpreter`, `domain_services`). The internal synchronization invariant crash (`unresolved concrete method effect target` at `src/moss.cpp:4658` or `unresolved callable argument`) is triggered not just by trait-annotated parameters, but by any non-primitive compound expression in an exported function (e.g. `2.0 * (w + h)`, `p.get_x() + p.get_y()`, or `f(a) + f(b)`).
3. **SWARM-033 — Negative Return / Reply Inference:**
   `return -1` in `calc_interpreter/src/lexer.moss:46` and `lib_and_app/repro/fmt_return_paren` fails formatter semantic validation.
4. **SWARM-034 — Tail `push` Return Inference:**
   A no-value function ending in `vec.push(item)` fails return inference; requires trailing bare `return`. Reproduced in `lib_and_app`, `text_toolkit`, `algo_chain`.
5. **SWARM-053 — String Concatenation Lowering (`String + literal`):**
   `s = s + " extra"` fails in rustc with `mismatched types: expected &str, found String`. Reproduced in `lib_and_app`, `data_pipeline`, `domain_services`.
6. **SWARM-064 — Rust Reserved Keyword in Struct Field (`box: Int`):**
   Using `box` as a field name compiles in `moss check` and Fast Debug, but leaks unescaped `box: i64` to rustc, failing with 12 errors. Non-boundary general backend hygiene issue.

---

## 5. Intended Language Rules vs Friction

During the swarm, agents encountered several intentional Moss language constraints that required structural discipline:

1. **Filter Requires Deep Copy for Objects:** `items |> filter(predicate)` over a `Vector` of non-Copy structs is rejected with `MOSS_COMPILE_ERROR: filter over nontrivial element type ... cannot produce a new collection without an explicit deep copy`. This is an intended ownership invariant (filter cannot synthesize new owned collections from borrowed elements). Workaround: map to primitives or iterate with `while`.
2. **Strict Value Boundaries Across Domains:** Passing domain handles as values or payloads across messages is strictly forbidden. Domains must be statically wired via `domainroutes(...)` in `main`.
3. **No Ordinary Recursion:** Algorithms must be written iteratively (e.g. DSU path compression, Dijkstra, AST evaluation).

---

## 6. What Worked Exceptionally Well

1. **Margo Package Orchestration:** Dependency resolution (`path = "..."`), build ordering, test runner execution, and cache management worked smoothly across all multi-package graphs.
2. **Paired Reproducer Discipline:** The presence of `single/` vs `split/` test cases provided undeniable evidence isolating boundary defects from general language limitations.
3. **Fast Debug Trace Generation:** `margo debug --trace` emitted rich structured JSON traces attributing events to exact physical files across package boundaries (over 1,200 events traced in `lib_and_app`).
4. **Dogfood Resilience:** Five out of six workstreams successfully adapted their designs to produce robust, complete dogfood projects with 109 green unit tests.

---

## 7. Core Verdict

> **Can a Moss programmer put an abstraction boundary around otherwise-valid code without changing its semantics or needing compiler-internal knowledge?**

**Verdict: No.** At present, placing code behind a module or package boundary exposes significant compiler-internal projection artifacts:
1. Moving code into a package strips all methods from exported types (`.mossi` omission).
2. Moving a `range` loop into a module causes type inference failure.
3. Calling a sibling method within a module type mis-mangles as a module function.
4. Splitting code into multiple files breaks diagnostic source-file attribution.
5. Exporting functions with compound expressions triggers internal compiler synchronization invariant crashes.
6. Using internal modules in tests requires artificial dummy imports in `main.moss`.
7. Using generic exports or composite types leaks unimported transitive module requirements into consumer Rust crates.

Closing Phase 15.12D establishes the exact boundary defect surface. With these 9 new and 6 amended SWARM findings meticulously cataloged with minimal reproducers, the compiler team has an actionable blueprint for the upcoming hardening phase.
