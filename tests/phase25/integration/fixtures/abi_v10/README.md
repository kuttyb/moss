# Phase 25 ABI-v10 Reusable Integration Fixtures

This directory contains reusable fixtures, templates, and reference packages for validating the Phase 25 ABI-v10 specification and cross-crate generic type linkage.

These fixtures serve as the canonical test matrix defined in `docs/phase25/ABI_V10_DESIGN.md` §11 and `docs/phase25/TYPE_IR_CONTRACT.md`.

## Directory Layout

```text
fixtures/abi_v10/
├── templates/
│   ├── abi_v10_interface.template         # Canonical ABI-v10 .mossi schema definition
│   ├── abi_v9_interface.template          # Legacy ABI-v9 .mossi schema definition
│   ├── provider_box_v10.rs.fixture        # Rust A1 generic struct representation
│   └── consumer_box_v10.rs.fixture        # Rust consumer free-function specialization
├── scenario01_provider/                   # Scenario 1: Generic Box and open fresh/relay
├── scenario02_consumer_a/                 # Scenario 2: Consumer A specializes Box[Int]
├── scenario03_consumer_b/                 # Scenario 3: Consumer B specializes Box[String]
├── scenario04_joint_consumers/            # Scenario 4: Shared provider Rust type identity
├── scenario05_independent_freshen/        # Scenario 5: Multiple independent fresh() calls
├── scenario06_source_removal/             # Earlier single-consumer source-free smoke fixture
├── scenario07_missing_method/             # Scenario 7: Transitive missing-method error provenance
├── scenario08_effect_conflict/            # Scenario 8: Source-free ownership conflict rejected
├── scenario09_v9_rejection/               # Scenario 9: ABI-v9 artifacts rejected under v10
├── scenario10_corrupt_metadata/           # Scenario 10: Metadata tampering fails closed
└── scenario11_cache_invalidation/         # Scenario 11: Provider layout change invalidates cache
```

## Scenario Overview

| Scenario | Objective | Key Verification / Diagnostic |
|---|---|---|
| **Scenario 1** | Provider exports generic `Box` and open functions `fresh` and `relay` | Provider compiles, emits ABI-v10 `NominalTemplate` and `OpenCallableSummary` records. |
| **Scenario 2** | Consumer A specializes `Box[Int]` | Source-free provider artifact; independent consumer builds and runs with exact `42` output. |
| **Scenario 3** | Consumer B specializes `Box[String]` | Same source-free pattern with exact `hello v10` output. |
| **Scenario 4** | Shared provider-owned Rust type identity | A and B compile separately; a joint native executable links both consumer artifacts and the one provider after all provider and intermediate consumer sources are removed. |
| **Scenario 5** | Independent summary freshening | One source-free consumer calls `fresh()` at Int and String and executes with exact output. |
| **Scenario 6** | True source-free checked private-chain specialization | The harness runs `check_phase25_h3_source_free_chain.py`: canonical checked IR/dependency records, poisoned legacy body, removed provider tree, independent Int/String native consumers, and unsupported/missing-body negatives in the selected profile. The older single-consumer fixture remains as a smoke example. |
| **Scenario 7** | Transitive error provenance | Missing member call reports consumer callsite as primary and provider requirement span as related note. |
| **Scenario 8** | Ownership/effect conflict rejected before rustc | Illegal read after consume or aliasing mutation is rejected during Moss semantic checking. |
| **Scenario 9** | ABI-v9 rejection | A real v9 `.mossi` and `.rlib` are built, selected only through `MOSS_MODULE_PATH`, and rejected under v10 with `SOURCE_FREE_ABI_REBUILD_REQUIRED`. |
| **Scenario 10** | Metadata tampering fails closed | A clean source-free import passes; changed digest, schema, missing binary section, changed module ID, and trailing fake-proof text each fail with exact ABI diagnostics. The key-protocol `06j` fixture separately checks a forged proof-related method record. |
| **Scenario 11** | Semantic and layout cache invalidation | A cold consumer build is followed by a warm cache hit; the provider contract digest then changes and the affected consumer is rejected with `FIELD_COUNT_MISMATCH` at its constructor. |

## Running the Harness

The fixtures are executed and validated via `tests/phase25/integration/cross_crate_harness.py`:

```bash
# Verify fixture integrity, mutation helpers, and environment:
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/cross_crate_harness.py --self-test

# Run all scenarios:
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/cross_crate_harness.py --all

# Run specific scenario:
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/cross_crate_harness.py --scenario 1

# Select build profile:
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/cross_crate_harness.py --scenario 6 --profile release

# Test cache invalidation:
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/cross_crate_harness.py --scenario 11 --cache-test

# Emit structured JSON results:
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/cross_crate_harness.py --all --json
```
