# Independent holdout revalidation — P28-R7-011

**Verdict:** VALID — the prior scoped READ/CONSUME holdout GO remains applicable to the requested combined compiler source. This does not approve overall compiler conformance, the full Phase 28 change, or clone provenance.

## Pinned inputs

- Current certified oracle SHA: `e3d0cb70647238f80a1c42cb1db56868595abf9a`
- Expectation table: 62 rows, SHA-256 `4f5f2b1849ced45e520c15fc17914cc6e284b47da709d17c5c33d8668f52045a`
- Requested compiler source SHA: `977f5fe1a40e13cc72245b38f625ef00879194a6`
- Integration worktree HEAD at validation: `a9432fe3a99ae05cc7af1cc713eb021f0a9f72f3`; its only change after 977 is `.codex/CURRENT_STATUS.md`, so compiler/test source contents are unchanged.
- `moss-dev` SHA-256: `d0cbe77c29c7b935691641e497ab0064c668484207d71c1b159b1a74f2ee8a10`
- `moss-opt` SHA-256: `82a7255c64280f79d5c09e32ecfb36fa95ba421875b1e5272d7bce61c33c0cb4`

The two frozen v2 source hashes are unchanged: READ `3113cfbde2492c45f54a52edf3011bb00a70e6bc6f324511d82ea8ec506d3500`; CONSUME `197b10e89838bfb588d8aedf5d2d1a3a39feb42e0ada399b48c6bb32865488ac`; holdout plan `596bf6b29dc930a90eb26e0b250493f8fd80bd91554f0db1fddcdc3ae1f8b28d`.

## Revalidation results

- READ scenario: checker accepted; Fast Debug, Native Debug, and Native Optimized each printed `4\n`. The original field remained readable after passing the binding to a count-only helper.
- CONSUME scenario: checker, Fast Debug, Native Debug emission, and Native Optimized emission all rejected with `OWNERSHIP_ILLEGAL_CONSUME`; no Rust artifact was emitted. Explicit `take` remains required.
- Binary hashes were identical before and after execution.

Optimized emission used **`moss-opt -O`**, followed by `rustc -O`. Checker, Fast Debug, and Native Debug used `moss-dev`.

## Exact command lines

Working directory:

```sh
/home/owner/daji/code/moss/tmp/review-p28-r7-011-oracle-ff306
```

Variables:

```sh
DEV=/home/owner/daji/code/moss/tmp/phase28-r7-011-integration/build/compiler/moss-dev
OPT=/home/owner/daji/code/moss/tmp/phase28-r7-011-integration/build/compiler/moss-opt
H=tmp/p28-r7-011-review-holdouts-v2
```

READ scenario:

```sh
"$DEV" check "$H/holdout_read.moss" --json
"$DEV" run --interp "$H/holdout_read.moss"
"$DEV" "$H/holdout_read.moss" -o "$H/reread.rs"
rustc "$H/reread.rs" -o "$H/reread_native"
"$H/reread_native"
"$OPT" -O "$H/holdout_read.moss" -o "$H/reread_opt.rs"
rustc -O "$H/reread_opt.rs" -o "$H/reread_opt_native"
"$H/reread_opt_native"
```

CONSUME scenario:

```sh
"$DEV" check "$H/holdout_consume.moss" --json
"$DEV" run --interp "$H/holdout_consume.moss"
"$DEV" "$H/holdout_consume.moss" -o "$H/consume_revalidation_native.rs"
"$OPT" -O "$H/holdout_consume.moss" -o "$H/consume_revalidation_opt.rs"
```

Each CONSUME invocation rejected with `OWNERSHIP_ILLEGAL_CONSUME`; both native emission paths emitted no Rust file, so Rust compilation was not invoked for that rejected input.
