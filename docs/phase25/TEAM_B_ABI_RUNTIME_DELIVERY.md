# Phase 25 — Team B Delivery Report: ABI-v10 Semantic Records, Runtime Infrastructure, and Linkage

**Author:** Team B L6 Coordinator  
**Branch:** `phase25-g-abi-runtime`  
**Common Baseline:** `5b29352aae6a4b59b0c43b9264fe72fb08bbbb6c` (`origin/phase25-generic-types`)  
**Mission:** Complete ABI-v10 semantic records, shared runtime infrastructure (`moss_runtime`), provider/consumer linking, and source-free validation.  
**Authorities:** `ABI_V10_DESIGN.md`, `TYPE_IR_CONTRACT.md`, `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`, `TEAM_G_ABI_V10_STAGING.md`  

---

## 1. Executive Summary

Team B has fulfilled its Phase 25 charter under the L7 Execution Directive. All deliverables for Workstreams B1 through B5 are implemented, cleanly separated from Team A's type solver and Team S's scalar key semantics, and verified with extensive unit, adversarial, multi-crate, and integration tests.

The implementation strictly maintains `MOSS_STAGED_ABI=v10` gating so production defaults remain stable on ABI-v9 until Team I's joint cutover.

---

## 2. Delivered and Independently Verified

### 2.1 Workstream B1 — Typed ABI-v10 Semantic Records
- **Canonical Record Definitions (`src/abi_v10_records.hpp`):**
  - `ModuleRecord`: Module identity, package name/version, provider binary digest.
  - `SourceProvenance`: Provenance IDs mapped to logical source hashes, display file paths, line/column coordinates, and AST declaration IDs.
  - `MethodTemplate`: Method IDs, owning nominal template ID, parameter/result TypeExprs, parameter/receiver access modes (`READ`/`WRITE`/`CONSUME`), and provenance link.
  - `NominalTemplate`: Template ID, exported visibility, type parameter count, fields, and sorted `method_template_ids`.
  - `ConcreteInstance`: Canonical instance type key, template ID, type arguments, provider binary ID, and layout verification hash.
  - `OpenCallableSummary`: Open result type expression, requirements DAG with parent chains, observable effect bits/tags, and private semantic dependencies.
  - `KeyProof`: Key type, exact equality/hash callable IDs, specialization keys, semantic hashes, key law contract version, and purity constraints.
- **Fail-Closed Validation (`src/source_free_abi_v10.hpp`):**
  - Implemented the full 10-step fail-closed verification pipeline (`validate_complete_interface`).
  - Strict validation of canonical record ordering (must sort strictly by record ID in every section).
  - Rejection of dangling provenance references, unregistered private dependencies, malformed line ranges, and conflicting key proofs.

### 2.2 Workstream B2 — Shared `moss_runtime` Infrastructure & Deterministic Caching
- **Deterministic Content-Addressed Artifact Resolution (`src/moss_runtime_linkage.hpp`):**
  - Replaced filesystem timestamp checks with composite SHA-256 build key (`compute_runtime_build_key`) over:
    - Runtime crate identity and version
    - Exact SHA-256 content hashes of all runtime source files (`src/*.rs`, `Cargo.toml`)
    - Rust toolchain fingerprint (`rustc -vV` release, commit-hash, host)
    - Target architecture (`target_arch`)
    - Profile (`debug` vs `release`)
    - Compilation flags
  - Sidecar metadata manifest (`moss_runtime-<profile>-<short_id>.meta`) verifies `artifact_hash`.
  - Cache corruption detection: automatically detects corrupted or tampered `.rlib` files and performs safe, deterministic rebuilds.
  - Safe process execution: Replaced shell invocation with `run_command_safe` using direct `fork()`/`execvp()` and non-blocking `poll()` on stdout/stderr pipes, eliminating shell injection and pipe buffer deadlocks.
- **Shared Rust Crate (`runtime/moss_runtime/`):**
  - `MossMap<K, V>`: Bucket-indexed deterministic hash map with caller-provided static specialization functions (`insert_with`, `get_with`, `get_mut_with`, `remove_with`, `contains_key_with`, `clear`, `keys`, `values`).
  - `MossSet<K>`: Set representation sharing the Map kernel.
  - Deterministic invariants enforced: No `dyn` trait objects, no runtime comparison closures, no dynamic dispatch, no per-process `RandomState`.
  - Staged `MossString` (indexed-v1) and `key_protocol` modules ready for Team S relocation.
- **Rust Unit & Linkage Validation:**
  - Passes 4/4 Rust unit tests (`cargo test` / `rustc --test`).
  - Multi-profile compilation: Both Native Debug (`-g`) and Native Release (`-O`) verified across multi-crate linkage graphs.

### 2.3 Workstream B3 — MapKeyProof and Coherence
- **Built-in KeyProof Factory (`make_builtin_key_proof`):**
  - Generates canonical, frozen proofs for `Int`, `Float`, `Char`, `String`, and `Bool`.
  - Enforces zero observable effect bits and empty effect tags.
  - Coherence verification (`validate_key_proof_coherence`): Conflicting equality or hash proofs for the same key type are rejected with `KEY_PROOF_COHERENCE_MISMATCH`.

### 2.4 Workstream B4 — Source-Free ABI Verification (The 11 Behaviors Genuinely Executable)
Every behavior from Directive §5 is verified with genuine executable assertions and explicitly categorized by proof tier in `tests/phase25/check_phase25_b_acceptance.py`:

| # | Behavior | Proof Tier & Evidence | Verdict |
|---|---|---|---|
| 1 | Provider-owned generic record identity | Codec: `abi_v10_staging.cpp`<br>Runtime: `check_runtime.py`<br>Moss: G2 Gate 9 Box[T] -> Box[Int] | **PASS — Codec**<br>**PASS — Runtime**<br>**PASS — Moss** |
| 2 | Two independently compiled consumers using same provider | Runtime: `check_runtime.py` (`consumer_a` & `b`)<br>Moss: G2 Gate 9 (`g2consumera` & `g2consumerb`) | **PASS — Runtime**<br>**PASS — Moss** |
| 3 | Provider source removed before consumer compilation | Runtime: `check_runtime.py` (`unlink()` verified)<br>Moss: G2 Gate 9 (`shutil.rmtree` verified) | **PASS — Runtime**<br>**PASS — Moss** |
| 4 | Imported generic summaries preserving open results | Codec: `OpenCallableSummary` roundtrip<br>Moss: G2 Gate 4 (`g4_source_free`) | **PASS — Codec**<br>**PASS — Moss** |
| 5 | Imported requirement and effect provenance | Codec: `SourceProvenance` & non-weakening<br>Moss: G2 Gate 5 (related provenance notes) | **PASS — Codec**<br>**PASS — Moss** |
| 6 | Wrong package, module, type or provider artifact rejection | Codec: `validate_complete_interface`<br>Moss: G2 Gate 9 (forged `module_id` rejection) | **PASS — Codec**<br>**PASS — Moss** |
| 7 | ABI-v9/v10 incompatibility detection | Codec: `SOURCE_FREE_ABI_REBUILD_REQUIRED`<br>Moss: G2 Gate 9 (v9 header rejection) | **PASS — Codec**<br>**PASS — Moss** |
| 8 | Runtime identity mismatch | Codec: `verify_runtime_compatibility`<br>Runtime: `check_runtime.py` (forged runtime rejection) | **PASS — Codec**<br>**PASS — Runtime** |
| 9 | Metadata mutation and forged proof rejection | Codec: `abi_v10_adversarial.cpp`<br>Moss: G2 Gate 9 (bit-flip & digest mismatch) | **PASS — Codec**<br>**PASS — Moss** |
| 10 | Correct cache invalidation after layout/semantic changes | Codec: `compute_cache_dependency_key` & build key<br>Integration: G2 Gate 10 interim contract | **PASS — Codec**<br>***PENDING — Integration*** |
| 11 | Native Debug and Native Release compatibility | Runtime: `check_runtime.py` (-g and -O)<br>Moss: G2 Gate 11 (debug run & Fast Debug rejection) | **PASS — Runtime**<br>**PASS — Moss** |

Total verification checks: 22 PASS, 1 PENDING (Gate 10), 0 FAIL.

### 2.5 Workstream B5 — Compiler-Facing Linkage Interfaces
- **Dedicated Implementation Unit (`src/moss_runtime_linkage.hpp`):**
  - `build_or_resolve_moss_runtime`: Deterministic rebuild checking, artifact hashing, toolchain fingerprinting.
  - `generate_compiler_runtime_flags`: Generates `-L dependency=...` and `--extern moss_runtime=...`.
  - `verify_runtime_compatibility`: Fails closed before rustc on name, version, or artifact hash mismatch.
  - `detect_contract_change`: Classifies changes into `None`, `InterfaceHashOnly`, `SemanticChange`, `BinaryChange`.
  - `compute_cache_dependency_key`: Computes deterministic cache key over compiler, backend, native ABI, runtime rlib hash, provider contract digest, binary/semantic hashes, and imports.

---

## 3. Ready for Team I Integration

1. **Compiler Binding Points:** Documented in detail in [`TEAM_B_LINKAGE_INTEGRATION_SPEC.md`](TEAM_B_LINKAGE_INTEGRATION_SPEC.md). Team I needs only to include `moss_runtime_linkage.hpp` in the package driver.
2. **Semantic Facts Required from Team A:**
   - TypeExpr wire mapping.
   - Population of `OpenCallableSummary` and `NominalTemplate` from the authoritative type checker.
3. **Runtime Modules to Incorporate from Team S:**
   - Final relocation of Team S's scalar and string runtime implementations into `runtime/moss_runtime/src/string.rs` and `key_protocol.rs`.
4. **ABI-v10 Cutover Steps:**
   - Default container switch from v9 to v10 under Team I.
   - Activation of joint gate 10 via `MOSS_G2_ACTIVATE_JOINT=1`.

---

## 4. Not Yet Proved (Explicit Joint Dependency)

Per Directive §4, §5, and `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md` §2, §5:
- **G2 Gate 10 (Custom-key Map/Set execution):** Requires nominal key method admittance (`admit_member`) and semantic purity validation owned by Team S.
- Status is formally reported as **PENDING Joint Integration**. It is NOT reported as an independent Team B completion claim.

---

## 5. Test Commands and Evidence

All tests pass cleanly:
```bash
# 1. Acceptance suite covering all 11 behaviors
python3 tests/phase25/check_phase25_b_acceptance.py

# 2. ABI-v10 Staging Tests
c++ -std=c++17 -Wall -Wextra -Werror -pedantic tests/tooling/abi_v10_staging.cpp -o tmp/abi_v10_staging && tmp/abi_v10_staging

# 3. ABI-v10 Adversarial Mutation Tests
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/tooling/abi_v10_adversarial.cpp -o tmp/abi_v10_adversarial && tmp/abi_v10_adversarial

# 4. Runtime Multi-Profile and Multi-Crate Linkage Tests
python3 tests/phase25/runtime/check_runtime.py

# 5. G2 Conformance
python3 tests/tooling/check_phase25_conformance.py ./moss
```

---

## 6. Team B Readiness Statement

Team B certifies that:
1. No second generic type engine was introduced; Team A's type ownership is strictly preserved.
2. Team S's scalar and string semantics were not rewritten; runtime hooks and metadata are in place for joint integration.
3. ABI-v10 records adhere strictly to `ABI_V10_DESIGN.md` and `TYPE_IR_CONTRACT.md`.
4. The branch is clean, isolated, and ready for integration under Team I.
