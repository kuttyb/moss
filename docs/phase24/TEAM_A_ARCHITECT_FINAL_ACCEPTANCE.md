# Phase 24 Team A Advanced Data — Architect final acceptance

**Role:** Independent L6 Architect
**Verdict:** **NO-GO**
**Exact reviewed implementation SHA:** `cdcadb26b9b46d332f5f577886c359d44ce839c0`
**Reviewed branch/worktree:** `phase24-advanced-data-architect-final` in `tmp/worktrees/team-a-architect-final`
**Language contract:** `moss-0.1`
**Date:** 2026-10-12

## Scope and method

This was an independent, package-level review of `moss.ndarray`,
`moss.datastructures`, `moss.tables`, `moss.binary`, `moss.cbor`, and
`sdk/advanced-data-acceptance`. The review covered public contracts,
forgeable-record invariants, typed errors, mutation atomicity, resource limits,
complexity claims, mandatory frozen scope, test evidence, source-free ABI-v9,
and RFC 8949 / `cbor2` interoperability. No implementation, test, acceptance
gate, `AGENTS.md`, or feedback file was changed.

Startup and provenance evidence:

- `git rev-parse HEAD` returned exactly
  `cdcadb26b9b46d332f5f577886c359d44ce839c0`.
- `./moss agent bootstrap --json` succeeded with
  `result.language_version = moss-0.1` and compiler `0.1.0`.
- The frozen contract predates the implementations and states that GO requires
  every applicable gate in
  `docs/phase24/TEAM_A_ADVANCED_DATA_ARCHITECTURE.md` §10.

## Fresh execution evidence

| Evidence | Result |
| --- | --- |
| `python3 sdk/advanced-data-acceptance/run_all.py` | PASS: seven public cross-package scenarios in Native Debug, Native Release, and Fast Debug; 12 fixed RFC vectors; compiled-only ABI-v9 gate. |
| `UV_CACHE_DIR="$PWD/tmp/uv-cache" TMPDIR="$PWD/tmp" /home/owner/.local/bin/uv run --no-project --with cbor2==5.7.1 python sdk/advanced-data-acceptance/run_all.py --live-cbor2` | PASS with exact `cbor2 5.7.1`: 12 `cbor2 -> Moss` and 12 `Moss -> cbor2` comparisons. |
| `(cd sdk/ndarray && ../../margo test --json)` and `--release` | PASS 21/21 in each native profile. |
| `(cd sdk/datastructures && ../../margo test --json)` and `--release` | PASS 23/23 in each native profile. |
| `(cd sdk/tables && ../../margo test --json)` and `--release` | PASS 24/24 in each native profile. |
| `(cd sdk/binary && ../../margo test --json)` and `--release` | PASS 9/9 in each native profile. |
| `(cd sdk/cbor && ../../margo test --json)` and `--release` | PASS 24/24 in each native profile. |

The package arithmetic is **101 unique tests** (`21 + 23 + 24 + 9 + 24`),
executed once in Native Debug and once in Native Release. The cross-package
Fast Debug run is additional execution evidence, not a multiplication of the
unique-test count.

The tracked source-free method was inspected and freshly executed. It serially
builds 14 explicitly named provider packages, copies exactly one `.mossi` and
one `.rlib` per provider, verifies copy digests and `native_abi 9`, rejects any
provider `.moss`, then checks, builds, and executes a consumer using only the
copied provider artifacts. It passed for all five Team A modules.

A disposable Architect probe under `tmp/team-a-architect-abi-probe` used the
same source-free provider set and recovered typed failures (including payload
fields) from all five Team A modules. `moss check`, native build, and execution
passed with `typed ABI-v9 failures ok`; failed deque and binary mutations also
retained their original state. This confirms that the observed tracked-fixture
gap below is acceptance coverage debt, not an ABI implementation failure.

## Defect attribution

### Net-new confirmed defects: 4

#### A-ARCH-FINAL-001 — frozen `moss.datastructures` contract is not delivered (release blocker)

The frozen architecture defines a closed `DataValue = Int | Float | Bool |
String` value surface for the stable priority queue, bounded deque, and ordered
maps, and defines both Int and String ordered sets/maps as **insertion ordered,
not key sorted** (`TEAM_A_ADVANCED_DATA_ARCHITECTURE.md` lines 130–163).

The delivered public package instead exposes Int-only queue/deque/map values,
ascending `IntSortedSet` / `IntSortedMap`, and insertion ordering only for the
String variants (`sdk/datastructures/README.md` lines 1–7 and 89–198;
`sdk/datastructures/src/datastructures.moss` lines 20–52 and 648–972). The
23/23 passing package suite explicitly tests ascending Int behavior, confirming
that this is an intentional replacement contract rather than an incidental
failure.

This finding does **not** demand unsupported source generics or runtime
dispatch. The frozen contract already chose a concrete closed value model.
Delivery must either implement that supported frozen surface or obtain a
separately approved contract revision; the final implementation cannot be
accepted against the current freeze.

#### A-ARCH-FINAL-002 — mandatory independent ndarray/table oracles are absent (release blocker)

Frozen gate 4 requires independent ndarray and table semantic oracles in
addition to fixed CBOR vectors and two-way external CBOR interoperability.
The tracked acceptance package contains the fixed CBOR fixture validator and
`cbor2_oracle.py`, but no ndarray or table oracle artifact or runner stage.
Hard-coded package assertions and the cross-package app are valuable tests,
but they are not independent semantic oracles. The CBOR half of this gate is
strong and passed; the ndarray/table half is absent.

#### A-ARCH-FINAL-003 — mandatory Team efficiency closeout is absent (acceptance blocker)

Frozen gate 9 requires
`docs/phase24/agent-patterns/team-a-advanced-data.md` following
`docs/AGENT_TEAM_EFFICIENCY.md`. That file does not exist at the reviewed SHA.
The required record must preserve unknowns rather than reconstruct unsupported
counts. This is docs/process scope and does not require implementation changes.

#### A-ARCH-FINAL-004 — tracked ABI-v9 consumer does not exercise exported typed errors (acceptance blocker)

Frozen gate 2 requires compiled-only ABI-v9 consumers covering exported closed
enums/records and inferred mutating effects. The tracked consumer exercises
records, successful operations, and mutators across all five modules, but its
only recovery is an unbound catch-all (`fixtures/source_free_consumer.moss`
lines 11–55). It therefore does not demonstrate typed variant/payload recovery
from the tracked source-free gate. The fresh disposable probe proved the ABI
functionality works, so the minimal closeout is to make this evidence durable
in the tracked source-free acceptance fixture; no ABI change is indicated.

### Independently verified earlier defects: 0

No prior Team A product-defect ledger or earlier Architect verdict exists at
the reviewed SHA. The 15 existing Team A `agent-feedback` observations describe
compiler/tooling experiences and documented workarounds; they are not prior
Team A package defects and are not re-attributed here.

### Duplicates: 0

The four findings above have distinct contract or acceptance targets, and no
earlier product-defect record was found to duplicate them.

### False positives closed: 1

The tracked catch-all-only ABI fixture initially left open whether source-free
typed error metadata itself was broken. The independent five-module typed
recovery probe passed, closing that implementation concern. Only the durable
acceptance-coverage omission remains as A-ARCH-FINAL-004.

## Supported limitations preserved

The following are honest, supported limitations and are **not** NO-GO defects:

- CBOR Float64 remains capability-gated pending exact bit-preserving Float/binary64
  conversion; Float16/Float32, tags, indefinite lengths, bignums, non-Int/text
  map keys, and other simple values remain outside the documented subset.
- CBOR text remains strict owned UTF-8 bytes/scalars; no unsupported native
  String adapter is required.
- Table String ordering is unsupported except through explicit categorical
  order. Present Float CSV export and minimum-Int CSV formatting remain typed
  unsupported operations because the required exact formatter surface is not
  available in all three execution modes.
- Public forgeable records require full validation, so documented end-to-end
  costs can exceed the underlying heap, union-find, or lookup algorithm. The
  package READMEs disclose this rather than claiming unsupported complexity.
- No zero-copy, packed-byte layout, GPU/device execution, FlatBuffers, or Arrow
  IPC support is claimed. Source-free providers are native-only; Fast Debug
  correctly requires reachable source.
- The documented direct-import workaround for transitive raised-error native
  linkage and the `table_row_count` naming workaround are preserved compiler
  limitations, not Team A semantic defects.

## Verdict and delivery provenance

**NO-GO** for exact implementation SHA
`cdcadb26b9b46d332f5f577886c359d44ce839c0` because A-ARCH-FINAL-001 through
A-ARCH-FINAL-004 leave mandatory frozen scope or acceptance evidence
unfulfilled. No repairs were made during this independent review.

Delivery may add this Architect report and later **docs/status-only** commits
without invalidating the implementation review, provided a diff from
`cdcadb26b9b46d332f5f577886c359d44ce839c0` confirms that no implementation,
tests, acceptance gates, benchmarks, ABI artifacts/contracts, or semantics
changed. Any such change requires review of a new exact implementation SHA.
