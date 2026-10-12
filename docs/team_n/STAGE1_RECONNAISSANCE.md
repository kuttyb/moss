# Moss Phase 24 Team N — Stage 1: Reconnaissance Report

**Team:** Team N (Numerical Computing)  
**Role:** L6 Coordinator  
**Branch:** `phase24-numerics`  
**Starting HEAD:** `3999e12b635be2f6d5f74dcbc1cf72bb50b74ffc` (`origin/phase24-integration`)  

---

## 1. Baseline Verification

1. **Integration Baseline:**
   - Switched to new branch `phase24-numerics` from `origin/phase24-integration` at commit `3999e12b635be2f6d5f74dcbc1cf72bb50b74ffc`.
   - Executed existing acceptance suites:
     - `sdk/foundations/acceptance/run_all.py`: PASS (11 sort cases, 8 byte cases, native O0, native release, Fast Debug, source-free ABI-v9).
     - `sdk/text-encoding-acceptance/run_all.py`: PASS (UTF-8 encode/decode, slice, search, compiled-only ABI-v9).
     - `packages/integration`: 2/2 PASS via `margo test`.
   - Verified language contract: `moss-0.1` via `./moss agent bootstrap --json`.

2. **Repository Hygiene & Tools:**
   - `python3 tests/tooling/check_agent_skills.py` PASS.
   - `python3 tests/tooling/check_agent_feedback.py` PASS.

---

## 2. Compiler & Language Discovery Findings

1. **Flat Row-Major Dense Representation:**
   - `export type Matrix: rows: Int, cols: Int, data: Vector[Float]` provides $O(1)$ indexing and contiguous memory locality.
   - Size calculation: `n = rows * cols`.

2. **Rust Backend Lowering E0502 Borrow Conflict:**
   - Writing `m.data[r * m.cols + c] = val` causes Rust E0502: `cannot borrow *m as immutable because it is also borrowed as mutable`.
   - Root cause: The LHS mutably borrows `m.data` while the index expression simultaneously reads `m.cols` from `*m`.
   - Solution: Mandatory rule for numerical implementation agents: always compute the index into a local variable first:
     ```moss
     idx = r * m.cols + c
     m.data[idx] = val
     ```

3. **Module Header Requirement:**
   - Every library source file must begin with `module <pkg_name>` on line 1 for exports and ABI `.mossi` symbol table generation to function correctly in Margo package consumers.

4. **Vector Length Operation:**
   - In Moss v0.1, collection size is obtained via `v |> count` or `v.length()`, not `.len()`.

5. **Cross-Package Package Dependency Keys:**
   - In `Moss.toml`, `[dependencies]` keys must match the imported module name (e.g. `linalg = { path = "../linalg" }`).

---

## 3. Package Layout Plan

- `packages/linalg`: Dense linear algebra (`moss.linalg`), depends on `math = { path = "../math" }`.
- `packages/sparse`: Sparse linear algebra (`moss.sparse`), depends on `linalg = { path = "../linalg" }`.
- `packages/statistics`: Descriptive and streaming statistics (`moss.statistics`), depends on `math = { path = "../math" }`.
- `packages/numerics_integration`: Cross-package acceptance and showcase application, depends on `linalg`, `sparse`, `statistics`, and `math`.
