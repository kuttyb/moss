# Phase 25 ABI-v10 and Cross-Crate Integration Harness

**Status:** Ready for G2/G3 Compiler Integration
**Workstream:** Workstream I-B (ABI-v10 & Cross-Crate Harness)
**Branch:** `phase25-prep-ib`
**Canonical Contracts:**
- `docs/phase25/ABI_V10_DESIGN.md`
- `docs/phase25/TYPE_IR_CONTRACT.md`
- `docs/phase25/TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`

---

## 1. Overview and Purpose

Workstream I-B establishes the automated integration harness and reusable fixtures for the Phase 25 ABI-v10 transition and A1 cross-crate linkage.

In Phase 25, Moss transitions from legacy ABI-v9 (`moss-module-interface-v2`, `native_abi 9`) to ABI-v10 (`moss-module-interface-v3`, `schema_version 10`, `native_abi 10`). Under ABI-v10:
1. Providers export generic nominal templates (`NominalTemplate`), open-result function summaries (`OpenCallableSummary`), exact specialization records (`Specialization`), and canonical key proofs (`MapKeyProof`).
2. Providers own the single Rust generic struct definition (e.g. `pub struct Box<F0>`) in their emitted `.rlib` artifact (A1 cross-crate linkage model).
3. Consumers specialize provider nominals (e.g. `Box[Int]`, `Box[String]`) by generating free functions without orphan trait implementations or runtime vtables.
4. Source-free consumers compile against provider `.mossi` and `.rlib` artifacts even when provider `.moss` source files have been completely removed.
5. Incompatible, legacy (v9), corrupted, or tampered artifacts fail closed with precise diagnostic codes before any generated code reaches Rust compilation.

This harness provides an objective, automated, non-mocked test execution engine for validating all 11 proof matrix obligations defined in `docs/phase25/ABI_V10_DESIGN.md` §11.

---

## 2. The 11 Integration Scenarios

The harness executes 11 distinct scenarios mapping directly to the Phase 25 ABI-v10 proof matrix:

| Scenario | Title | Description & Invariant | Expected Status Under Baseline Compiler |
|---|---|---|---|
| **1** | Provider exports generic Box | Provider exports generic `Box` and open functions `fresh(x)` and `relay(b)`. Verifies emission of `NominalTemplate` and `OpenCallableSummary` in `.mossi`. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` |
| **2** | Consumer A specializes `Box[Int]` | Consumer A specializes provider `Box` with `Int` using provider-owned generic struct. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` |
| **3** | Consumer B specializes `Box[String]` | Consumer B specializes provider `Box` with `String`. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` |
| **4** | Shared provider Rust type identity | Consumers A and B exchange `Box` values across crates, verifying that both bind to the exact same provider Rust type constructor in `libbox_provider.rlib`. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` |
| **5** | Independent summary freshening | Multiple calls to `fresh()` allocate fresh type variables independently without cross-call contamination. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` |
| **6** | True source-free compilation | Provider is compiled to `.mossi` and `.rlib`; provider `.moss` source files are deleted; consumer compiles and runs cleanly via `MOSS_MODULE_PATH`. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` |
| **7** | Transitive missing-method error | Unspecializable transitive member requirement retains consumer callsite and provider declaration provenance in the diagnostic chain. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` |
| **8** | Source-free ownership conflict rejection | Consumer attempting illegal read-after-consume is rejected at the Moss checking phase before Rust code generation. | `PASS` (rejected during Moss check) |
| **9** | ABI-v9 artifact rejection | Legacy v9 artifacts (`moss-module-interface-v2`, `native_abi 9`) must be rejected with `SOURCE_FREE_ABI_REBUILD_REQUIRED`. | `BLOCKED: Team G G2 ABI-v10 source-free compiler support` (baseline accepts v9) |
| **10** | Metadata corruption fails closed | Tests 5 corruption modes: altered digest, corrupted schema version, missing required records, provider identity mismatch, and forged Float-key proofs. All fail closed. | `PASS` |
| **11** | Layout and cache invalidation | Changing provider layout (e.g. adding struct fields) marks consumer cache dirty and forces recompilation, whereas clean runs hit warm cache. | `PASS` |

---

## 3. Harness Architecture

The harness runner is located at `tests/phase25/integration/cross_crate_harness.py`.

### 3.1 Key Design Principles

1. **Never Mock Outcomes:** All tests run actual `moss`, `margo`, and `rustc` binaries via subprocess execution. No outcomes, exit codes, or ASTs are faked.
2. **Explicit Dependency Block Tracking:** When a scenario fails due to an unimplemented compiler feature (e.g. generic nominal type inference in Team G G2), the harness inspects the structured diagnostic or compiler failure and explicitly classifies it as `BLOCKED: Team G G2 ABI-v10 source-free compiler support (<details>)` rather than an unexplained test failure.
3. **Isolated Sandbox Workspaces:** Every scenario execution runs in an isolated temporary directory within `<workspace>/tmp/` to avoid polluting the git worktree or leaving untracked files.
4. **True Source Removal:** For Scenario 6 (source-free verification), provider source files are physically removed (`shutil.rmtree(provider_dir / "src")`), proving that consumer compilation relies exclusively on `.mossi` semantic interface records and `.rlib` binary objects.
5. **Mutation Helpers:** Five dedicated helpers programmatically mutate `.mossi` interfaces to test fail-closed validation:
   - `tamper_digest`: Modifies provider contract digest.
   - `corrupt_schema_version`: Replaces schema/native ABI version with invalid values.
   - `remove_required_records`: Drops mandatory contract sections.
   - `alter_provider_identity`: Mutates `module_id` to induce provider conflict.
   - `corrupt_float_key_proof`: Injects malformed Float-key proofs with invalid effects.

---

## 4. Usage Guide

### 4.1 Running Self-Test
The self-test validates toolchain availability (`moss`, `margo`, `rustc`), verifies fixture integrity, runs all Scenario 10 mutation helpers, checks cache hashing, and compiles/links a standalone Rust A1 cross-crate fixture:
```bash
python3 tests/phase25/integration/cross_crate_harness.py --self-test
```

### 4.2 Running All Scenarios
To execute all 11 scenarios against the repository compiler:
```bash
python3 tests/phase25/integration/cross_crate_harness.py --all
```

### 4.3 Running a Single Scenario
```bash
python3 tests/phase25/integration/cross_crate_harness.py --scenario 1
python3 tests/phase25/integration/cross_crate_harness.py --scenario 11
```

### 4.4 Build Profiles
The harness supports both Debug and Release profiles:
```bash
# Debug profile (default)
python3 tests/phase25/integration/cross_crate_harness.py --scenario 11 --profile debug

# Release profile (-C opt-level=3, margo --release)
python3 tests/phase25/integration/cross_crate_harness.py --scenario 11 --profile release
```

### 4.5 Cache Invalidation Test
```bash
python3 tests/phase25/integration/cross_crate_harness.py --cache-test
```

### 4.6 JSON Output for Automation
For CI/CD and automation pipelines:
```bash
python3 tests/phase25/integration/cross_crate_harness.py --all --json
```

---

## 5. Transition to Team G G2 Delivery

When Team G completes Workstream G2:
1. `box_provider` (Scenario 1) will compile with `margo build` and emit an ABI-v10 `.mossi` file with `schema_version 10` and `native_abi 10`.
2. Scenarios 1–6 will immediately transition from `BLOCKED` to `PASS`.
3. Scenario 7 will verify transitive provenance notes in `METHOD_NOT_FOUND`.
4. Scenario 9 will verify that ABI-v9 artifacts produce `SOURCE_FREE_ABI_REBUILD_REQUIRED`.
5. The harness requires zero modification to accept the landed compiler changes.
