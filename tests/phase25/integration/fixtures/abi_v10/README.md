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
├── scenario06_source_removal/             # Scenario 6: Pure source-free .mossi + .rlib build
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
| **Scenario 2** | Consumer A specializes `Box[Int]` | Consumer generates specialized free function over `provider::Box<i64>`. |
| **Scenario 3** | Consumer B specializes `Box[String]` | Consumer generates specialized free function over `provider::Box<MossString>`. |
| **Scenario 4** | Shared provider-owned Rust type identity | Consumers A and B interoperate without conflicting orphan impls or duplicate definitions. |
| **Scenario 5** | Independent summary freshening | Multiple calls to `fresh()` generate distinct type variable mappings without cross-call contamination. |
| **Scenario 6** | True source-free compilation | Provider `.moss` source files are removed; consumer compiles using only `.mossi` and `.rlib`. |
| **Scenario 7** | Transitive error provenance | Missing member call reports consumer callsite as primary and provider requirement span as related note. |
| **Scenario 8** | Ownership/effect conflict rejected before rustc | Illegal read after consume or aliasing mutation is rejected during Moss semantic checking. |
| **Scenario 9** | ABI-v9 rejection | Legacy v9 artifacts receive `SOURCE_FREE_ABI_REBUILD_REQUIRED` under v10 rules. |
| **Scenario 10** | Metadata tampering fails closed | Malformed digests, bad schema versions, truncated records, identity mismatch, and forged Float proofs fail closed. |
| **Scenario 11** | Semantic and layout cache invalidation | Changing provider layout invalidates consumer cache; unchanged provider hits warm cache. |

## Running the Harness

The fixtures are executed and validated via `tests/phase25/integration/cross_crate_harness.py`:

```bash
# Verify fixture integrity, mutation helpers, and environment:
python3 tests/phase25/integration/cross_crate_harness.py --self-test

# Run all scenarios:
python3 tests/phase25/integration/cross_crate_harness.py --all

# Run specific scenario:
python3 tests/phase25/integration/cross_crate_harness.py --scenario 1

# Select build profile:
python3 tests/phase25/integration/cross_crate_harness.py --scenario 6 --profile release

# Test cache invalidation:
python3 tests/phase25/integration/cross_crate_harness.py --scenario 11 --cache-test

# Emit structured JSON results:
python3 tests/phase25/integration/cross_crate_harness.py --all --json
```
