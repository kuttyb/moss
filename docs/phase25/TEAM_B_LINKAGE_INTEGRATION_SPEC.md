# Phase 25 — Team B Compiler Linkage and ABI-v10 Integration Specification

**Author:** Team B L6 Coordinator  
**Branch:** `phase25-g-abi-runtime`  
**Target:** Team I (Integration Owner)  
**Authorities:** `ABI_V10_DESIGN.md`, `TYPE_IR_CONTRACT.md`, `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`, `TEAM_G_ABI_V10_STAGING.md`  

---

## 1. Executive Summary

This specification defines the clean, testable compiler and linker binding points implemented by Team B for ABI-v10, the shared `moss_runtime` crate, and provider/consumer linkage. It enables Team I to perform multi-team cutover without modifying core compiler internals or re-implementing linkage logic.

All code lives in dedicated, modular headers and crates:
- `src/abi_v10_records.hpp` — Canonical typed ABI-v10 semantic records, encoders, decoders, and validators.
- `src/source_free_abi_v10.hpp` — Container framing, hashing, digest verification, and 10-step complete interface validator.
- `src/moss_runtime_linkage.hpp` — Shared runtime build/resolution, compatibility verification, cache dependency computation, and rustc flag generation.
- `runtime/moss_runtime/` — Compiler-owned shared runtime crate containing `MossMap<K, V>`, `MossSet<K>`, `MossString` (indexed-v1), and key protocol lowering.

---

## 2. Compiler Binding Points (`src/moss.cpp` & Margo)

### 2.1 Header Inclusions
Team I includes the following headers in compilation and package driver units:
```cpp
#include "source_free_abi_v10.hpp"
#include "abi_v10_records.hpp"
#include "moss_runtime_linkage.hpp"
```

### 2.2 Shared Runtime Resolution and Build
Before invoking `rustc` for any package (provider, consumer, or standalone binary), the driver resolves or compiles the canonical shared `moss_runtime` rlib:
```cpp
const auto [rlib_path, runtime_id] = moss::abi_v10::build_or_resolve_moss_runtime(
    runtime_crate_dir,  // path to "runtime/moss_runtime"
    output_dir,         // e.g. "tmp/artifacts" or margo target dir
    profile,            // "debug" (-g) or "release" (-O)
    rustc_bin           // "rustc"
);
```
- **Deterministic Rebuilding:** Rebuilds if `libmoss_runtime.rlib` does not exist or if any `.rs` file in `runtime/moss_runtime/src` has a timestamp newer than the existing rlib.
- **Identity Extraction:** Computes exact `artifact_hash` (SHA-256 of `.rlib`), Rust toolchain fingerprint (`rustc -V`), and crate version (`0.1.0`).

### 2.3 Compiler & Linker Flag Generation
When lowering Moss modules to Rust and driving `rustc`, the driver appends the canonical linkage flags:
```cpp
const std::vector<std::string> runtime_flags =
    moss::abi_v10::generate_compiler_runtime_flags(rlib_path);
// Produces:
// ["-L", "dependency=<output_dir>", "--extern", "moss_runtime=<rlib_path>"]
```
This ensures that all generated Rust modules link against the exact same shared `moss_runtime` crate, avoiding type duplication for `MossString`, `MossMap`, and `MossSet`.

### 2.4 Provider / Consumer Compatibility Verification
When a consumer module loads a compiled provider artifact, the loader verifies runtime crate identity before proceeding to rustc compilation:
```cpp
// Throws SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH if name or artifact_hash differ.
// Throws SOURCE_FREE_ABI_REBUILD_REQUIRED if crate version differs.
moss::abi_v10::verify_runtime_compatibility(provider_runtime_id, consumer_runtime_id);
```

### 2.5 Complete Interface Validation (10-Step Pipeline)
When importing a sealed `.mossi` file and its corresponding `.rlib` archive, the loader executes the complete fail-closed verification pipeline:
```cpp
const moss::abi_v10::SealedInterface sealed = moss::abi_v10::decode(mossi_content);

// Validates:
// 1. Module and package identity format
// 2. Exact byte digest match (unmodified provider contract)
// 3. Exact .rlib native-object contract marker match
// 4. Section hash integrity (binary, semantic, interface hashes)
// 5. Sealed imports closure
// 6. Canonical record ordering, framing, and schemas
// 7. Nominal template and concrete instance substitution consistency
// 8. Transitive reachability of all private semantic dependencies
// 9. KeyProof purity, coherence, and canonical callable IDs
// 10. SourceProvenance referential integrity (no dangling provenance IDs)
moss::abi_v10::validate_complete_interface(
    sealed,
    expected_provider_binary_id,
    optional_rlib_archive_bytes  // if rlib is present
);
```

### 2.6 Cache Dependency Key Computation
For compilation caching and incremental rebuild determination, compute the composite cache dependency key:
```cpp
const std::string cache_key = moss::abi_v10::compute_cache_dependency_key(
    sealed_interface,
    runtime_id,
    compiler_version,      // e.g. "moss-0.1"
    backend_fingerprint    // e.g. "rustc 1.85.0 (x86_64)"
);
```
Any layout mutation, semantic contract change, imported dependency bump, or runtime crate modification automatically invalidates downstream consumer caches.

### 2.7 Contract Change Classification
When a provider package is recompiled, compare the prior and fresh sealed interfaces:
```cpp
const moss::abi_v10::ContractChangeKind kind =
    moss::abi_v10::detect_contract_change(prior_interface, fresh_interface);

switch (kind) {
  case ContractChangeKind::None:
    // Fully cached; no consumer rebuild needed.
    break;
  case ContractChangeKind::InterfaceHashOnly:
    // Minor metadata bump; consumers can reuse binary objects.
    break;
  case ContractChangeKind::SemanticChange:
    // Re-check consumer source; downstream types/effects may have changed.
    break;
  case ContractChangeKind::BinaryChange:
    // Invalidate object artifacts; full relink required.
    break;
}
```

---

## 3. Team Dependencies & Integration Gates

### 3.1 Dependencies on Team A (Type System & Checker)
Team B consumes the authoritative type model; it does not implement a secondary type engine.
- **TypeExpr Wire Format:** Map `moss::type_ir::TypeExpr` to `moss::abi_v10::TypeExpr` during `.mossi` emission.
- **OpenCallableSummary:** Team A populates:
  - `variable_count`
  - `result` TypeExpr
  - `requirements` list (`target`, `method_name`, `access`, `parent` index, `provenance_id`)
  - `observable_effect_bits` and `observable_effect_tags`
  - `dependencies` (private callable hashes).
- **NominalTemplate:** Team A provides nominal type definitions with sorted `method_template_ids`.

### 3.2 Dependencies on Team S (Scalar & Custom Key Protocols)
- **Built-in KeyProofs:** Team B has implemented `make_builtin_key_proof` for `Int`, `Float`, `Char`, `String`, and `Bool` with exact frozen targets matching `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md` §2.
- **Runtime Relocation:** Staged implementations in `runtime/moss_runtime/src/string.rs` and `runtime/moss_runtime/src/key_protocol.rs` are ready to receive final versions from Team S during joint integration.
- **Nominal Keys (G2 Gate 10):** Team S validates `admit_member` and purity for nominal `isequal` and `hash`. Once admitted, Team B's `KeyProof` records serialize and validate their canonical callables and specializations.

---

## 4. Activation and Cutover Procedure

1. **Pre-Cutover (Staged Validation):**
   - The compiler defaults to ABI-v9 for standard workloads.
   - Setting `MOSS_STAGED_ABI=v10` activates the ABI-v10 emission, decoding, and validation path.
2. **Joint Cutover (Team I):**
   - Wire `build_or_resolve_moss_runtime` and runtime flags into the driver.
   - Switch default container version from v9 to v10.
   - Confirm Gate 10 passes with Team S nominal key admittance.
   - Invalidate legacy v9 cache artifacts with `SOURCE_FREE_ABI_REBUILD_REQUIRED`.
