# Phase 25 Acceptance Gates & Verification Ledger

**Status:** Canonical Machine-Checkable Acceptance Gates Specification
**Authority:** Workstream I-A Lead Engineer (Phase 25 Acceptance Gates)
**Baseline Commit:** `d714d17c1b1ca34b7765f4853f6d53ccb1df9daf`
**Governing Contracts:**
- [`TYPE_IR_CONTRACT.md`](TYPE_IR_CONTRACT.md) (§10 Conformance Gates)
- [`ABI_V10_DESIGN.md`](ABI_V10_DESIGN.md) (§11 Required Proof Matrix)
- [`TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`](TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md)
- [`TEAM_S_DESIGN_AND_ACCEPTANCE.md`](TEAM_S_DESIGN_AND_ACCEPTANCE.md)
- [`TEAM_S_KEY_PROTOCOL_V1_SCALARS.md`](TEAM_S_KEY_PROTOCOL_V1_SCALARS.md)
- [`G0_JOINT_ARCHITECTURE_PROOFS.md`](G0_JOINT_ARCHITECTURE_PROOFS.md)

---

## 1. Executive Summary

Phase 25 establishes a unified, machine-checkable acceptance ledger covering all frozen requirements across Team G (Generic Types & Type IR), Team S (Semantics & Engines), and Team I (Integration & Whole-System Verification).

The acceptance ledger tracks **62 individual gates** across **7 key functional areas**:

| Area Code | Functional Area | Contract Authority | Gates Count | Owning Teams | Current Status |
|---|---|---|---:|---|---|
| **TYPEIR** | Type IR Conformance | `TYPE_IR_CONTRACT.md` §10 | 11 | Team G, Joint | BLOCKED |
| **ABIV10** | ABI-v10 Proof Matrix | `ABI_V10_DESIGN.md` §11 | 11 | Team G, Joint | NOT IMPLEMENTED / BLOCKED |
| **G1** | Parity & Correctness Backlog | `TEAM_S_DESIGN_AND_ACCEPTANCE.md` §2 | 15 | Team S, Team G, Joint | 1 FAIL, 14 BLOCKED |
| **G3** | Ordering, Equality, Hashing & Key Protocol | `TEAM_S_KEY_PROTOCOL_V1_SCALARS.md` & `TYPE_IR_CONTRACT.md` §8 | 8 | Team S, Joint | BLOCKED |
| **G4** | String, Char, Unicode & Indexed Layout | `TEAM_S_CHAR_STRING_MIGRATION.md` & `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md` §3 | 6 | Team S, Joint | BLOCKED |
| **G5** | Compiler/Runtime Integration & SDK Matrix | Phase 25 Integration Charter | 6 | Team G, Team S, Team I, Joint | NOT IMPLEMENTED / BLOCKED |
| **G6** | Adversarial Falsification & Final Sign-Off | Phase 25 Verification Charter | 5 | Team S, Team I, Joint | BLOCKED |
| **TOTAL** | **All Acceptance Areas** | | **62** | **Team G, Team S, Team I, Joint** | **1 FAIL, 51 BLOCKED, 10 NOT IMPLEMENTED** |

---

## 2. Integrity Rules and Status Definitions

To ensure strict engineering integrity, every gate is classified using four truthful, machine-checkable statuses:

1. **`PASS`**: The requirement has a permanent, fully merged production implementation in the active tree that satisfies all specified engine configurations (Fast Debug, Native Debug, Native Release, package builds).
   > [!IMPORTANT]
   > **Anti-Forging Rule:** A pending fixture, static Rust feasibility test, or staged-only flag does **NEVER** count as proof of a production implementation. A gate cannot be marked `PASS` until full production integration is verified.

2. **`FAIL`**: The feature is active or callable in the current tree, but fails the stated test contract or observable semantic oracle (e.g. `GATE-G1-01` where 64-bit integers are truncated to double in Fast Debug).

3. **`BLOCKED`**: The implementation exists on an unmerged feature branch (`origin/phase25-generic-types` or `origin/phase25-semantics-engines`), or is waiting on a named upstream prerequisite (e.g. `moss_runtime` crate creation, joint ABI-v10 cutover, or G2 foundations). Every `BLOCKED` gate records an exact named blocking dependency.

4. **`NOT IMPLEMENTED`**: No compiler or runtime implementation has been written yet for this contract requirement on any branch.

---

## 3. Machine-Checkable Ledger Matrix

The machine-readable source of truth is stored in [`tests/phase25/integration/acceptance_matrix.json`](file:///home/owner/daji/code/moss-integration/tests/phase25/integration/acceptance_matrix.json) and executed via [`tests/phase25/integration/run_acceptance.py`](file:///home/owner/daji/code/moss-integration/tests/phase25/integration/run_acceptance.py).

### 3.1 Type IR Conformance (GATE-TYPEIR-01..11)

| Gate ID | Requirement Text | Owner | Test Path | Configurations | Status | Blocking Dependency / Evidence |
|---|---|---|---|---|---|---|
| `GATE-TYPEIR-01` | independent `Box[Int]` and `Box[String]`, plus nominal inequality for two declarations with the same layout | Team G | `tests/phase25/g0/conformance/nominal_instance_coexistence.moss` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: `check_phase25_conformance.py` at `a50fa30`) |
| `GATE-TYPEIR-02` | nested nominal constructors in collection literals and zero-iteration loops | Team G | `tests/phase25/g0/conformance/nested_nominal_constructor.moss` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: `nested_nominal_constructor.moss` at `a50fa30`) |
| `GATE-TYPEIR-03` | one binding rejects conflicting instances while separate constructor/call results remain independent | Team G | `tests/phase25/g0/conformance/nominal_instance_assignment_conflict.moss` | Moss Checker, Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: `nominal_instance_assignment_conflict.moss` at `a50fa30`) |
| `GATE-TYPEIR-04` | `fresh`/`relay`/multi-hop source-free summaries freshen per call and a never-pinned result reports precise ambiguity | Team G | `tests/phase25/g0/conformance/open_result_ambiguous.moss` | Moss Checker, Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: `TYPE_AMBIGUOUS_OPEN_RESULT` at `a50fa30`) |
| `GATE-TYPEIR-05` | a transitive missing method reports the full outer-call-to-provider requirement chain | Team G | `tests/phase25/g0/conformance/provider_module.moss` | Moss Checker | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: outermost call site reporting at `a50fa30`) |
| `GATE-TYPEIR-06` | known-open nominal field access succeeds and unconstrained receiver field access fails | Team G | `tests/phase25/g0/conformance/unconstrained_receiver_field_negative.moss` | Moss Checker, Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: `UNTYPED_FIELD_ACCESS` at `a50fa30`) |
| `GATE-TYPEIR-07` | generic ownership overlap/use-after-consume failures occur before Rust emission | Team G | `tests/phase25/g0/conformance/private_field_visibility_negative.moss` | Moss Checker, Moss Ownership | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: substituted effects checking at `a50fa30`) |
| `GATE-TYPEIR-08` | recursive layouts and changing-key specialization cycles terminate diagnostically | Team G | `tests/tooling/check_phase25_recursive_layout.py` | Moss Checker | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: `check_phase25_recursive_layout.py` at `97b7d42`) |
| `GATE-TYPEIR-09` | two consumers name one provider-owned Rust record identity and compatible layout | Team G | `tests/phase25/g0/rust/consumer_a.rs` | Margo Package Build, Native Debug, Native Release | **BLOCKED** | `moss_runtime crate & Margo multi-package generic linking` (evidence: prototype in `check_rust_feasibility.py`) |
| `GATE-TYPEIR-10` | compiled-only user keys call custom Moss equality/hash without an orphan impl | Joint | `tests/phase25/key_protocol/03-imported-nominal-keys-source-free/app/src/main.moss` | Margo Package Build, Native Debug | **BLOCKED** | `moss_runtime::map & KeyProofId integration` (evidence: prototype in `orphan_negative.rs`) |
| `GATE-TYPEIR-11` | native and Fast Debug agree where source is present; source-free Fast Debug fails during preflight with its established capability diagnostic | Team G | `tests/phase25/g0/conformance/local_pin_positive.moss` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-generic-types: Type IR & Option B closure` (evidence: `local_pin_positive.moss` at `a50fa30`) |

---

### 3.2 ABI-v10 Proof Matrix (GATE-ABIV10-01..11)

| Gate ID | Requirement Text | Owner | Test Path | Configurations | Status | Blocking Dependency / Evidence |
|---|---|---|---|---|---|---|
| `GATE-ABIV10-01` | one source-free provider exporting `Box`, `fresh`, and `relay` | Team G | `tests/phase25/abi_v10/01_source_free_provider/` | Margo Package Build, Native Release, `.mossi` / `.rlib` | **NOT IMPLEMENTED** | `ABI-v10 container emission and schema_version 10 bump` |
| `GATE-ABIV10-02` | two independent consumer crates using `Box[Int]` and `Box[String]` and sharing the provider Rust type identity | Team G | `tests/phase25/abi_v10/02_two_consumers_box/` | Margo Package Build, Native Debug, Native Release | **NOT IMPLEMENTED** | `moss_runtime crate & ABI-v10 generic import` |
| `GATE-ABIV10-03` | a source-removed multi-hop open-result consumer with separate fresh calls | Team G | `tests/phase25/abi_v10/03_multihop_source_removed/` | Margo Package Build, Native Debug | **NOT IMPLEMENTED** | `ABI-v10 open-result summary serialization` |
| `GATE-ABIV10-04` | an ambiguity negative and conflicting-use negative with full provider provenance | Team G | `tests/phase25/abi_v10/04_provenance_negatives/` | Moss Checker, Margo Package Build | **NOT IMPLEMENTED** | `ABI-v10 provenance DAG serialization` |
| `GATE-ABIV10-05` | method exact-instance annotations and known-open field constraints | Team G | `tests/phase25/abi_v10/05_exact_instance_constraints/` | Moss Checker, Native Debug | **NOT IMPLEMENTED** | `ABI-v10 template/summary constraints` |
| `GATE-ABIV10-06` | transitive missing-method diagnostic chains | Team G | `tests/phase25/abi_v10/06_transitive_missing_method/` | Moss Checker | **NOT IMPLEMENTED** | `ABI-v10 requirement chain reporting` |
| `GATE-ABIV10-07` | ownership/effect negatives before rustc | Team G | `tests/phase25/abi_v10/07_ownership_effect_negatives/` | Moss Checker, Moss Ownership, Moss Effects | **NOT IMPLEMENTED** | `ABI-v10 effect and access mode serialization` |
| `GATE-ABIV10-08` | compiled-only custom nominal Map key proof with custom Moss equality/hash, the same policy-free `MossMap<K,V>` value crossing two consumers, and no orphan impl | Joint | `tests/phase25/key_protocol/04-two-consumers-shared-map/app/src/main.moss` | Margo Package Build, Native Debug, Native Release | **BLOCKED** | `moss_runtime::map & ABI-v10 MapKeyProof serialization` |
| `GATE-ABIV10-09` | corrupt type key, summary, provenance edge, semantic hash, binary hash, provider marker, weakened key effects, mismatched key-callable hash, malformed or forged Float-key proofs, and same-layout proof-substitution negatives | Joint | `tests/phase25/key_protocol/05-coherence-mismatch-free-proofs/app/src/main.moss` | Moss Checker, Margo Package Build | **BLOCKED** | `ABI-v10 integrity hashing and proof signature verification` |
| `GATE-ABIV10-10` | v9 rebuild diagnostics and cache invalidation | Team G | `tests/phase25/abi_v10/10_v9_rebuild_invalidation/` | Margo Package Build, `SOURCE_FREE_ABI_REBUILD_REQUIRED` | **NOT IMPLEMENTED** | `ABI-v10 version switch and v9 migration guard` |
| `GATE-ABIV10-11` | source-backed native/Fast Debug agreement plus source-free Fast Debug preflight | Team G | `tests/phase25/abi_v10/11_fast_debug_preflight/` | Fast Debug, Native Debug, `FAST_DEBUG_NATIVE_DEPENDENCY` | **NOT IMPLEMENTED** | `Fast Debug preflight integration for ABI-v10 source-free dependencies` |

---

### 3.3 G1 Semantics Parity & Defect Backlog (GATE-G1-01..15)

| Gate ID | Requirement Text | Owner | Test Path | Configurations | Status | Blocking Dependency / Evidence |
|---|---|---|---|---|---|---|
| `GATE-G1-01` | Defect #1: Fast Debug 64-bit integer precision loss: compare int64_t directly when both operands are Int, eliminating double truncation above 2^53 | Team S | `tests/phase25/ledger/run_ledger.py::#1 Int ordering above 2^53` | Fast Debug, Native Debug | **FAIL** | `origin/phase25-semantics-engines: Defect #1 fix in interpreter.hpp` (fails live on baseline `./moss`) |
| `GATE-G1-02` | Defect #2: Ordering comparison (`<`, `<=`, `>`, `>=`) on user nominal types rejected at checker rather than leaking to rustc compile error | Team S | `tests/phase25/ledger/run_ledger.py::#2 ordering on user types rejected` | Moss Checker | **BLOCKED** | `origin/phase25-semantics-engines: S1 checker rejection for unsupported types` |
| `GATE-G1-03` | Defect #3: Reassignment with different type to existing binding rejected at checker to preserve single concrete type per binding | Team S | `tests/phase25/ledger/run_ledger.py::#3 one type per binding` | Moss Checker | **BLOCKED** | `origin/phase25-semantics-engines: binding tracking fix` |
| `GATE-G1-04` | Defect #4: Parameter-based indexed assignment borrow conflict (`m.data[r * m.cols + c] = v`) lowered by lifting index expression to temporary | Team S | `tests/phase25/ledger/run_ledger.py::#4 parameter-based indexed assignment` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: index lifting in src/moss.cpp` |
| `GATE-G1-05` | Defect #5: Repeated discard lowering (`_ = f()`) emits valid Rust `let _ = ...` without `let mut _` syntax error, and discards cleanly in Fast Debug | Team S | `tests/phase25/ledger/run_ledger.py::#5 repeated discard` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: discard lowering in src/moss.cpp` |
| `GATE-G1-06` | Defect #6: Assert diagnostic format string escaping: braces `{` and `}` in condition strings escaped in generated Rust `format!` expressions | Team S | `tests/phase25/ledger/run_ledger.py::#6 assert text with braces` | Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: assert format escaping in src/moss.cpp` |
| `GATE-G1-07` | Defect #7: Fast Debug `Float.to_string()` matches native Rust `f64` Display formatting identically across all float values | Team S | `tests/phase25/ledger/run_ledger.py::#7 Float.to_string` | Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-semantics-engines: Float display in src/interpreter.hpp` |
| `GATE-G1-08` | Defect #8: Fast Debug `for` loop traversal supported for `range`, `Vector`, and `Queue` iteration | Team S | `tests/phase25/ledger/run_ledger.py::#8 for traversal` | Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-semantics-engines: for loop in src/interpreter.hpp` |
| `GATE-G1-09` | Defect #9: Transitive missing method or missing `less` requirement reports complete outer-call-to-provider provenance chain | Team G | `tests/phase25/g0/conformance/transitive_requirement_chain.moss` | Moss Checker | **BLOCKED** | `origin/phase25-generic-types: requirement chain provenance` |
| `GATE-G1-10` | Defect #10: Cross-package constructor field labels must not resolve as imported same-name functions in namespace rewriter | Team S | `tests/phase25/ledger/run_ledger.py::#10 cross-package constructor label` | Margo Package Build, Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-semantics-engines: named_argument_label in src/moss.cpp` |
| `GATE-G1-11` | Defect #11: Nested nominal constructors in collection literals and zero-iteration loops infer and lower cleanly | Team G | `tests/phase25/g0/conformance/nested_nominal_constructor.moss` | Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-generic-types: Type IR nominal instance inference` |
| `GATE-G1-12` | Defect #12: Member dispatch on open receivers (`var v = []; v.push(1)`) admitted, unified, and lowered across Fast Debug and native | Joint | `tests/phase25/ledger/run_ledger.py::#12 open receiver (joint Team G G2)` | Moss Checker, Fast Debug, Native Debug | **BLOCKED** | `Joint G2/G3 MemberDispatchRequirement / admit_member integration` |
| `GATE-G1-13` | Defect #12d: Local collection-member call statements (`var v = Vector[Int](); v.push(4)`) dispatch and execute correctly | Team S | `tests/phase25/ledger/run_ledger.py::#12d local collection member call` | Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-semantics-engines: statement dispatch in src/moss.cpp` |
| `GATE-G1-14` | Defect #14: Raising function calls in benchmark bodies lower with Phase 21 outcome handling in `margo bench` | Team S | `tests/phase25/ledger/run_ledger.py::#14 raising call in bench` | Margo Benchmark Runner | **BLOCKED** | `origin/phase25-semantics-engines: bench codegen in src/moss.cpp` |
| `GATE-G1-15` | Defect #15: Local field member call (`h.data.push(7)`) on Holder struct dispatches and mutates as permitted code | Team S | `tests/phase25/ledger/run_ledger.py::#15 local field member call` | Fast Debug, Native Debug | **BLOCKED** | `origin/phase25-semantics-engines: local field dispatch in src/moss.cpp` |

---

### 3.4 G3 Ordering, Equality, Hashing & Key Protocol (GATE-G3-01..08)

| Gate ID | Requirement Text | Owner | Test Path | Configurations | Status | Blocking Dependency / Evidence |
|---|---|---|---|---|---|---|
| `GATE-G3-01` | Scalar key protocol v1: `less`, `isequal`, and `hash` lowered for Int, Char, String, Bool across engines with identical unseeded 64-bit hashes | Team S | `tests/tooling/check_phase25_s3_key_protocol.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: key-protocol-v1 scalar lowering` |
| `GATE-G3-02` | Float Julia-like semantics: `isequal(-0.0, +0.0)` is false, `hash(-0.0) != hash(+0.0)`, `isequal(NaN, NaN)` is true for all bit patterns, and `less` total ordering partitions `finite < NaN` | Team S | `tests/tooling/check_phase25_g3_ordering.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: moss_float_isequal/hash/less lowering` |
| `GATE-G3-03` | Ordering laws: mathematical verification of irreflexivity, asymmetry, transitivity, totality for `less` across Int, Float, Char, String, Bool | Team S | `tests/tooling/check_phase25_l7_repairs.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: law test suites` |
| `GATE-G3-04` | Equality and hash consistency laws: reflexivity, symmetry, transitivity for `isequal`, and `isequal(a, b) implies hash(a) == hash(b)` across all scalar types | Team S | `tests/tooling/check_phase25_s3_key_protocol.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: equality and hash laws` |
| `GATE-G3-05` | Purity enforcement: sort key selectors and key protocol callables strictly pure (`SORT_KEY_IMPURE` on mutation, I/O, domain messages, raises, panics, divergence, unresolved effects) | Team S | `tests/tooling/check_phase25_s3_key_protocol.py` | Moss Checker, Moss Effects | **BLOCKED** | `origin/phase25-semantics-engines: observable-effect purity checker` |
| `GATE-G3-06` | Map/Set with custom nominal keys: structural `isequal` and `hash` methods without orphan impls, transparent lowering in provider crate | Joint | `tests/phase25/key_protocol/03-imported-nominal-keys/app/src/main.moss` | Margo Package Build, Native Debug | **BLOCKED** | `moss_runtime::map & KeyProofId integration` |
| `GATE-G3-07` | Canonical Key-Proof coherence: single canonical `KeyProofId` across multiple consumer packages; conflicting key proofs fail closed with `KEY_PROOF_COHERENCE_MISMATCH` | Joint | `tests/phase25/key_protocol/05-coherence-mismatch-free-proofs/app/src/main.moss` | Moss Checker, Margo Package Build | **BLOCKED** | `moss_runtime::map & KeyProof coherence checker` |
| `GATE-G3-08` | Eager `keys()` snapshot stability: `keys()` returns owned `Vector[K]` snapshot detached from subsequent map mutations in $O(N)$ time and allocation | Joint | `tests/phase25/key_protocol/07-keys-snapshot-nominal/case.moss` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `moss_runtime::map keys() implementation` |

---

### 3.5 G4 String, Char, Unicode & Indexed Layout (GATE-G4-01..06)

| Gate ID | Requirement Text | Owner | Test Path | Configurations | Status | Blocking Dependency / Evidence |
|---|---|---|---|---|---|---|
| `GATE-G4-01` | Char as distinct 32-bit Unicode scalar: no transparent `Char == String` coercion, `char_at()` returns Char, `chars()` returns `Vector[Char]` | Team S | `tests/tooling/check_phase25_s2_char_string.py` | Moss Checker, Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: Char scalar implementation` |
| `GATE-G4-02` | Validated `Char(Int)` constructor: rejects surrogates (0xD800..0xDFFF), negative values, values > 0x10FFFF, and rejects 2^32+65 without u32 truncation across all engines | Team S | `tests/tooling/check_phase25_s2_char_string.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: __moss_char_from_codepoint lowering` |
| `GATE-G4-03` | Unicode character escapes and literals: 14 escape forms agree across Fast Debug, native debug, and native release; malformed escapes rejected with `INVALID_ESCAPE` | Team S | `tests/tooling/check_phase25_l7_repairs.py` | Moss Checker, Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: Char literal lexer and escape parser` |
| `GATE-G4-04` | MossStr indexed-v1 layout & invariants: `MossString { bytes, codepoints, offsets }` with stride 64, u32 offsets, ASCII optimization (offsets empty when `codepoints == bytes.len()`), 4 GiB bound | Team S | `tests/tooling/check_phase25_indexed_string.py` | Native Debug (`MOSS_STAGED_STRING_REPR=indexed-v1`), Native Release | **BLOCKED** | `origin/phase25-semantics-engines: MossString runtime implementation` |
| `GATE-G4-05` | String traversal & slicing performance: $O(1)$ bounded `char_at` (max 63 steps from sparse offset), linear traversal of non-ASCII text, avoiding quadratic regression | Team S | `tests/benchmarks/phase25_unicode_traversal.py` | Native Release (plain vs indexed-v1) | **BLOCKED** | `origin/phase25-semantics-engines: benchmark suite & indexed String` |
| `GATE-G4-06` | Cross-crate String/Char provider-consumer contract: provider exports string/char functions, multiple consumers exchange `MossString` via shared `moss_runtime` without type mismatch | Joint | `tests/phase25/cross_crate/string_char_exchange/` | Margo Package Build, Native Debug, Native Release | **BLOCKED** | `moss_runtime crate & ABI-v10 cutover` |

---

### 3.6 G5 Compiler/Runtime Integration & SDK Matrix (GATE-G5-01..06)

| Gate ID | Requirement Text | Owner | Test Path | Configurations | Status | Blocking Dependency / Evidence |
|---|---|---|---|---|---|---|
| `GATE-G5-01` | Single compiler-owned `moss_runtime` crate: shared by all provider and consumer crates, providing `moss_runtime::string`, `moss_runtime::key_protocol`, and `moss_runtime::map` | Team G | `tests/phase25/integration/test_moss_runtime_linking.py` | Margo Package Build, rlib verification | **NOT IMPLEMENTED** | `moss_runtime crate construction and Margo build integration` |
| `GATE-G5-02` | Unified Type IR and member dispatch admission (`admit_member` / `MemberDispatchRequirement` / ledger #12): end-to-end open receiver collection calls (`var v = []; v.push(1)`) passing in Fast Debug, native debug, and native release | Joint | `tests/phase25/integration/test_joint_ledger_12.moss` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `Team G G2 Type IR + Team S admit_member integration` |
| `GATE-G5-03` | Full SDK package compatibility matrix: all standard SDK packages (`sdk/path`, `sdk/tables`, `sdk/text-encoding`, etc.) compile cleanly under ABI-v10 with new Char and String APIs | Team I | `tests/phase25/integration/test_sdk_matrix.py` | Margo Package Build across all SDK packages | **BLOCKED** | `ABI-v10 cutover & default indexed String representation` |
| `GATE-G5-04` | Key protocol runtime activation: `tests/phase25/key_protocol/run_fixtures.py --activate` passing all 30 fixtures across Map and Set with scalar, nominal, Float, and source-free keys | Team S | `tests/phase25/key_protocol/run_fixtures.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `moss_runtime::map and KeyProof record wiring` |
| `GATE-G5-05` | End-to-end benchmark suite: String/Unicode traversal benchmarks, Map/Set key lookup/insert/iteration benchmarks, and Fast Debug execution matching expected performance scaling | Joint | `tests/benchmarks/phase25_benchmarks.py` | Native Release, Margo Bench | **BLOCKED** | `moss_runtime and Map/Set production implementation` |
| `GATE-G5-06` | Differential CI parity: all test suites pass in all three supported execution engines (Fast Debug interpreter, Native Debug rustc, Native Release rustc) with identical observable outputs | Team I | `tests/phase25/integration/test_differential_ci.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `Integrated compiler combining G2, G3, G4` |

---

### 3.7 G6 Adversarial Falsification & Final Sign-Off (GATE-G6-01..05)

| Gate ID | Requirement Text | Owner | Test Path | Configurations | Status | Blocking Dependency / Evidence |
|---|---|---|---|---|---|---|
| `GATE-G6-01` | Adversarial Fast Debug parity suite: all 324 adversarial cases passing across Fast Debug and native without unexpected panics or divergent semantics | Team S | `tests/tooling/check_phase25_s4_fast_debug_adversarial.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: S4 adversarial fixes` |
| `GATE-G6-02` | Corrupt and forged ABI / KeyProof rejection: malformed or forged ABI-v10 containers, mismatched digests, forged key proofs, weakened key effects, and v9 artifacts fail closed with stable diagnostic codes | Joint | `tests/phase25/adversarial/test_forged_abi_and_proofs.py` | Moss Checker, Margo Package Build | **BLOCKED** | `ABI-v10 integrity verification and seal validation` |
| `GATE-G6-03` | 64-bit integer and floating-point edge-case audit: overflow, underflow, wrapping, NaN bit payloads, signed zeros, subnormals, and boundaries verified across all execution engines | Team S | `tests/tooling/check_phase25_sc_safe_arithmetic.py` | Fast Debug, Native Debug, Native Release | **BLOCKED** | `origin/phase25-semantics-engines: numeric literals and safe arithmetic suites` |
| `GATE-G6-04` | Memory safety and zero orphan-rule violations: Rust compilation of generated code produces zero E0117, zero borrow-check conflicts, and zero unsafe memory leaks | Team I | `tests/phase25/integration/test_rust_hygiene.py` | `rustc --edition=2021`, clippy / borrow-check audit | **BLOCKED** | `Unified native lowering and moss_runtime integration` |
| `GATE-G6-05` | Final independent Whole-System Architect Sign-off: independent review by L6/L7 confirming all contracts, law suites, and gates satisfied without residual blockers | Joint | `docs/phase25/FINAL_ARCHITECT_REVIEW.md` | Full clean tree `make check` & `margo test` | **BLOCKED** | `All prior gates PASS (GATE-TYPEIR-01..11, GATE-ABIV10-01..11, GATE-G1-01..15, GATE-G3-01..08, GATE-G4-01..06, GATE-G5-01..06, GATE-G6-01..04)` |

---

## 4. Acceptance Runner Usage Guide

The runner is located at `tests/phase25/integration/run_acceptance.py`. It is invoked with standard Python 3.

```bash
# 1. Harness self-test (validates matrix schema, gate count, rules, invariants; exits 0 on main/prep):
python3 tests/phase25/integration/run_acceptance.py --self-test

# 2. Print formatted markdown summary:
python3 tests/phase25/integration/run_acceptance.py --summary

# 3. Output machine-readable JSON acceptance report:
python3 tests/phase25/integration/run_acceptance.py --json

# 4. Inspect a single gate:
python3 tests/phase25/integration/run_acceptance.py --gate GATE-G1-01

# 5. Strict release gate verification (exits non-zero if any gate is not PASS):
python3 tests/phase25/integration/run_acceptance.py --strict
```
