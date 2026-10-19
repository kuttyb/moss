# key-protocol-v1 Map/Set acceptance fixtures

Team S S3.3 and S1.5 fixtures for Map and Set keys under `key-protocol-v1`.
They are written now so they can be switched on the moment the shared
implementation exists. They are **not** passing acceptance tests yet.

The scalar half of the protocol is specified in
[`docs/phase25/TEAM_S_KEY_PROTOCOL_V1_SCALARS.md`](../../../docs/phase25/TEAM_S_KEY_PROTOCOL_V1_SCALARS.md);
the Map/Set boundary is Team G's `TYPE_IR_CONTRACT.md` §4.2 and §8 and
`ABI_V10_DESIGN.md` §3.5 on `origin/phase25-generic-types`.

## Layout

Each directory is one fixture with an `EXPECTED` file:

```text
title: ...
covers: which S3.3 item (1-6) or S1.5
layout: standalone | packages
entry: app                       # packages: the package to build and run
source-free: keyprov             # optional: build this provider, delete its source
tamper: FILE | LINE-REGEX | OLD | NEW   # optional: edit the provider's .mossi
set-api: PROVISIONAL ...         # fixtures that use Set
expect: stdout | reject CODE | reject PROVISIONAL
current: what the compiler does today (see below)
defect: recorded defect ID, when current is known-failing
owner: who must land the feature
---
expected stdout (for expect: stdout)
```

`reject PROVISIONAL` means any checker diagnostic before native emission: the
G0 contracts name `KEY_PROOF_COHERENCE_MISMATCH` but no code for malformed,
impure, missing, or forged proofs.

Set uses `Set()`, `insert(k)`, `contains(k)`, and `values()` (an eager owned
snapshot, like `Map.keys()`). This spelling is **PROVISIONAL — pending Team G
G2 Set API**; no frozen Team G document defines one. Rename it in the fixtures
when the API is frozen.

## Fixtures

| fixture | covers | today |
|---|---|---|
| `01-float-keys-map`, `02-float-keys-set` | Float keys: separate signed zeros, one NaN key (Map, Set) | fail-closed |
| `03-imported-nominal-keys`, `...-source-free` | provider-owned nominal key with custom `isequal`/`hash` methods, source-backed and compiled-only | fail-closed in the checker (P25-S3-D2 interim fix) |
| `04-two-consumers-shared-map` | two consumer packages message one provider domain that owns `Map[Name, Int]` | fail-closed in the checker (P25-S3-D2 interim fix) |
| `04-two-consumers-shared-map-control` | same four-package topology with String keys | passes |
| `05-coherence-mismatch-*` | conflicting free-function proofs; a free proof overriding canonical methods (`KEY_PROOF_COHERENCE_MISMATCH`) | fail-closed in the checker (P25-S3-D2 interim fix) |
| `06a`–`06i` | malformed (wrong result/arity/operand), impure (I/O, raise, divergence), missing, and partial proofs | fail-closed in the checker (P25-S3-D2 interim fix) |
| `06j-proof-forged-interface` | compiled-only provider whose `.mossi` claims an I/O `hash` is pure | fail-closed (`SOURCE_FREE_CONTRACT_MISMATCH`) |
| `07-keys-snapshot-*` | `keys()` is an eager owned snapshot (Float, nominal; String control) | fail-closed / fail-closed / passes |
| `08-map-and-set-same-protocol` | one nominal key in a Map and a Set agrees on identity | fail-closed |
| `09a`–`09h` | S1.5 unpinned empty `Map()` (Team G Option B handoff) | see below |

S1.5 cases: `09a` (pinned by a later insert) passes today. `09b`
(never-inserted `m.get(k, d)`), `09c` (`Map().get(k, d)`), `09d`
(`fn fresh(): return Map()`), and `09e` (a `Map()` nothing pins) pass
`moss check` and fail in rustc: **known-failing (Team G Option B)**, defect
P25-S3-D3. `09e` must become `TYPE_AMBIGUOUS_OPEN_RESULT`, never a default.
`09f`/`09g` (domain state `Map()`, annotated or pinned by handlers) and `09h`
(an exported open result used by a consumer) are rejected today but are valid
under Option B.

## Running

```sh
python3 tests/phase25/key_protocol/run_fixtures.py            # verify current status
python3 tests/phase25/key_protocol/run_fixtures.py --observe  # print status, never fail
python3 tests/phase25/key_protocol/run_fixtures.py --activate # run against EXPECTED
python3 tests/phase25/key_protocol/run_fixtures.py --moss path/to/moss NAME...
```

The default mode checks that each fixture still does exactly what its
`current:` line records:

- `fail-closed CODE`: `moss check` (or `margo build`) rejects with `CODE`,
  and Fast Debug and native lowering refuse too. Reported as
  *pending activation*.
- `known-failing accepted-rustc-fails`: the checker accepts and rustc rejects
  the generated Rust. These are recorded defects, not fail-closed behaviour.
  The runner prints the owner, the first rustc error, and what Fast Debug did.
- `passes`: a control that already matches EXPECTED in all three engines.

Any difference exits 1, so a compiler change that alters one of these
behaviours is noticed and the fixture is updated or activated on purpose.

`--activate` requires `expect:` to hold: identical expected stdout in Fast
Debug, Native Debug, and Native Release (source-free fixtures run the two
native engines; ABI v10 §9 rejects a source-free provider in Fast Debug with
`FAST_DEBUG_NATIVE_DEPENDENCY`), or a checker rejection before native
emission with the stated code.

## Activation trigger

Run `--activate` and register the fixtures as a passing suite when **both**
hold:

1. Team G's G2 foundations pass (the shared Type IR with Option B open-result
   summaries, `TYPE_IR_CONTRACT.md` §4.2 and §10), and
2. the compiler-owned `MossMap<K,V>` with `KeyProof` records is available
   (`TYPE_IR_CONTRACT.md` §8, `ABI_V10_DESIGN.md` §3.5), and Set reuses it.

Until then, Team G may activate subsets: the `09*` fixtures need only Option B;
`01`, `07-keys-snapshot-float`, and the Float half of `02`/`08` need
`MossMap<K,V>` plus Team S's Float key proof. Fixtures whose spelling was
provisional (Set, `reject PROVISIONAL`) must be updated to the frozen API and
codes before activation, not loosened to match an implementation.
