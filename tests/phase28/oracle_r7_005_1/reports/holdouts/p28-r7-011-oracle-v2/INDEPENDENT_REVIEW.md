# Independent normative review: P28-R7-011 ORACLE-15

**Verdict:** GO for the normative ORACLE-15/15B/15C expectations and reviewer-controlled holdout integrity, scoped to the oracle candidate below. This verdict does not imply compiler conformance or clone provenance approval.

## Pinned candidate and compiler

- Oracle candidate: `ff306c27f58738bd7eb9c79b00eb165cead01209`
- Expectation table: 62 rows; SHA-256 `ce0581abcecd1b476f9f552433375493b1ed0cd9a58c61236b959c2d453fbfec`
- Certification status at reviewed candidate: `DRAFT_PENDING_L7_APPROVAL`; reviewer and holdout fields are pending. The prior 60-row v2 certification is archived separately.
- Combined compiler source: `83e42eece65663b3de46e09a14227f9e17633513`
- `moss-dev` SHA-256: `1e83d1874b9cd22ce714ffca0bdfb824342a2fbddcd2ba94e9f73119894ad362`
- `moss-opt` SHA-256: `e715f529537fbbbb67ad1fc31b5713ec0e03a5c8ce1b9da3b58363f57d58e303`

## Normative decision

Proposal §3 says access effects follow a definition's uses; the table under §10 rejects `b = state.frontier; archive(b)` only if `archive` consumes. Proposal §1 and §5 also preserve plain fields and reserve extraction for `take(place)`. Therefore:

- ORACLE-15, whose `archive` returns its Vector parameter and whose result is retained, is the consuming case and must reject.
- ORACLE-15B's `pass`-body `archive` does not consume `b`; the later read of `s.frontier` is valid and must produce `3`.
- ORACLE-15C uses the field-derived binding only for `count`; it is READ-only, must accept, and must produce `3`.

The prior ORACLE-15 fixture used a `pass` body while expecting REJECT, so it was a fixture defect. The revised fixture and two separated acceptance fixtures express the proposal's conditional rule. The `3` expectations match the harness's output representation (no trailing line feed); this is representation only, not a semantic adjustment.

## Reviewer-controlled holdouts

Holdout sources and expected outcomes were frozen before execution in `tmp/p28-r7-011-review-holdouts-v2/holdout_plan.md` (SHA-256 `596bf6b29dc930a90eb26e0b250493f8fd80bd91554f0db1fddcdc3ae1f8b28d`).

- READ holdout (`holdout_read.moss`, SHA-256 `3113cfbde2492c45f54a52edf3011bb00a70e6bc6f324511d82ea8ec506d3500`): a Vector field binding is passed to a count-only helper and the original field is read afterward. Checker accepted; Fast Debug, Native Debug, and Native Optimized each printed `4`.
- CONSUME holdout (`holdout_consume.moss`, SHA-256 `197b10e89838bfb588d8aedf5d2d1a3a39feb42e0ada399b48c6bb32865488ac`): a Vector field binding is passed to a function returning that parameter. Checker, Fast Debug, Native Debug, and Native Optimized all rejected with `OWNERSHIP_ILLEGAL_CONSUME`; no Rust artifact was emitted.

The first draft holdout set used the Moss identifier `crate`, which emitted invalid Rust because it is reserved. That set was abandoned before complete execution and was not evidence. The v2 plan documents this and changes only the local identifier; v2 was frozen before rerunning compiler commands.

## Combined oracle comparison cross-check

I independently inspected the full machine result at the path recorded in the oracle ledger. The artifact SHA-256 is `ffea7b638f2becc592f7709d9f3ff147cc3ceff9433284675cb1b19b6acbc2ea`. It contains 62 rows: 19 overall PASS, 43 overall FAIL; all 62 `semantic_status=PASS`; 43 clone provenance FAIL, 19 NOT_APPLICABLE; zero blocked on missing/invalid provenance. Summed harness `unclassified` and `unauthorized` counters are both 264. ORACLE-15 is PASS; ORACLE-15B/15C have checker, Fast Debug, Native Debug, and Native Optimized PASS, but each remains a provenance failure. The durable ledger reports these same values and explicitly distinguishes this implementation comparison from oracle certification.

## Remaining certification actions

The normative expectations and holdout integrity are GO. The oracle owner must now record this reviewer identity/report/hash, set the holdout result to PASS, update the exact candidate and frozen-SHA fields using the reviewed expectation digest, run the certification validation, and push the certified oracle commit. No clone exceptions are approved by this review.

## Exact holdout commands

All commands below were invoked with the detached review checkout as the working directory:

`/home/owner/daji/code/moss/tmp/review-p28-r7-011-oracle-ff306`

Compiler paths:

```sh
DEV=/home/owner/daji/code/moss/tmp/phase28-r7-011-integration/build/compiler/moss-dev
OPT=/home/owner/daji/code/moss/tmp/phase28-r7-011-integration/build/compiler/moss-opt
H=tmp/p28-r7-011-review-holdouts-v2
```

The READ holdout used these commands. The final `rustc -O` binary is the Native Optimized path; compiler emission for it used **`moss-opt -O`**, not `moss-dev -O`.

```sh
"$DEV" check "$H/holdout_read.moss" --json
"$DEV" run --interp "$H/holdout_read.moss"
"$DEV" "$H/holdout_read.moss" -o "$H/read.rs"
rustc "$H/read.rs" -o "$H/read_native"
"$H/read_native"
"$OPT" -O "$H/holdout_read.moss" -o "$H/read_opt.rs"
rustc -O "$H/read_opt.rs" -o "$H/read_opt_native"
"$H/read_opt_native"
```

Observed outputs: Fast Debug `4\n`; Native Debug `4\n`; Native Optimized `4\n`.

The CONSUME holdout commands were:

```sh
"$DEV" check "$H/holdout_consume.moss" --json
"$DEV" run --interp "$H/holdout_consume.moss"
"$DEV" "$H/holdout_consume.moss" -o "$H/consume_native.rs"
"$OPT" -O "$H/holdout_consume.moss" -o "$H/consume_optimized.rs"
```

Each command rejected with `OWNERSHIP_ILLEGAL_CONSUME`; the two emission paths produced no Rust artifact. `rustc` was therefore correctly not invoked for this rejected source. Optimized compiler emission used **`moss-opt -O`**, not `moss-dev -O`.
