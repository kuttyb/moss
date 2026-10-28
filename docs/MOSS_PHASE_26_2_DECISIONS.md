# Moss: Phase 26.2 — decisions and delivery

Status: **delivered to main Oct 28 2026.** Merge commit `ff8d8f4e` (tag `phase26-2-m1`); delivery record `d2b1620e`. Open follow-up: R1/R2 coverage regressions (§6). Base before merge: `main` at `e97751e`. Work branch: `phase26-2-work` (head `eea053f6`). Companion to `MOSS_LAZY_EXPRESSION_TREE.md`.

## 1. Scope as kicked off

1. Parallel builds: Margo builds independent packages concurrently (serial today).
2. How rustc is invoked: direct `rustc` vs Cargo; one crate per package.
3. Native rebuild hole: `margo build`/`test`/`run` recompile every package's Rust each time; extend G1-style skipping to native artifacts.
4. Retire source-free code paths deferred from 26.1 (dead-code removal).

## 2. Decisions

**D1. Parallel build is a language feature.** Principled, not performance-gated. No timing study; compiler-perf is a no-regression guard only. The laptop package-perf numbers (toy fixtures, 2–6 functions) are not evidence and are not to be cited.

**D2. Generic type identity across crates is a non-issue.** Settled by inspection, not design:
- Moss v0.1 has no user-defined generic types (`docs/LANGUAGE_SYNTAX.md` §"generic user-defined types"). The only generic types that cross packages are built-in collections, which lower to Rust std types whose nominal identity comes from std.
- Generic *functions* (untyped parameters) are specialized and projected into the artifact containing the concrete call. That duplicates code, not types — Rust's own model.
- Concrete provider types are defined once and referenced by consumers. Verified on `numerics_integration` → {`linalg`, `sparse`}: a single `linalg__Matrix` struct in `linalg.rs`; both consumers carry `extern crate moss_linalg; use moss_linalg::*;`. Consistent with `docs/MODULE_ABI.md`: "Provider source is never folded into the consumer."
- Nothing from ABI v10 (scenario 4, shared generic identity) is pulled forward.
- Consumers emit a local `linalg__MatrixView` adapter; harmless while consumer-local (backlog).

**D3. One crate per package already exists.** Item 2 is only "who drives rustc over the existing crates."

**D4. Cargo drives package builds.** When Margo builds a Moss package, Cargo is the driver (Margo generates manifests from `Moss.toml`). Direct `rustc` is reserved for file-based compiles and introduced only when a need shows up. The real axis is graph ownership; Cargo brings pipelining, the jobserver and incremental compilation.

**D5. Item 3 is largely absorbed by D4.** Cargo fingerprinting skips unchanged rlibs; Margo's obligation is byte-stable generated `.rs`. G1 remains the check path; Cargo owns the native path.

**D6. Parallelism: process per package, `-j` cap.** Not one long-lived process (reentrancy work never promised). Persistent workers are a later option.

**D7. ABI v10: ignore and mask.** No deletion. v10-only failures ignored; `checked_body_ir` retained and resurrectable.

**D8. `moss build` never triggers a package build; `margo build` does.** Margo drives `moss`, never the reverse. `moss build` and `moss build --check` on a package fail with `MOSS_PACKAGE_BUILD_REQUIRES_MARGO` (`690c7fa`).

## 3. What was delivered

- **Ready-queue scheduler** (`d419305`): a package starts when its own dependencies finish, within `-j`; a failure cancels only its dependents; tested by event order.
- **One Cargo workspace per graph:** `moss` per package (ready queue) generates Rust/`.mossi`, then one `cargo build --workspace --jobs N` over `build/.margo-cargo/<profile>`. One target dir, no lock contention, provider crates built once, global `-j` cap. Native starts after all Moss passes finish (accepted).
- **Native skip:** an unchanged graph skips native builds (`check_margo.py`).
- **Margo is the only package build driver** (D8).
- **Item 4 closed as a no-op:** no F1-only code; loader/verifier serve source-backed providers; v10 machinery retained per D7.
- **Inference convergence** (see §4): W2 updated, verify-mode corpus gate `check_phase26_inference_verify_modes`, `inference_rounds_cap_reached` counter.

Merge-commit gates (`ff8d8f4e`): `make check` 150/151, sole failure the Emacs xref parallel flake (`moss-mode-cross-package-xref-source-location-known-regression`). compiler-perf vs `c66fb76c`, 77 inputs: aggregate median −94.6%, no slowdown beyond noise.

## 4. Inference convergence finding

- `34082a83` (committed **directly to main** Oct 26, message noting `make check` still owed) stops cross-family inference rounds at the first round leaving `inferred_checker_signature()` unchanged; cap stays 3. This is why W2 went from 5 to 3 rounds and main was red after 26.1.
- Classification: stale expectation, not a defect. Audit (`docs/PHASE_26_2_INFERENCE_SIGNATURE_AUDIT.md`): all 16 written semantic members were already in the signature, so the early exit was sound as merged.
- The audit also added `parse_cache_entries_` and the signature dependency worklist state. These are **conservative additions, not defect fixes**: the parse cache is a memo (cannot change semantic output if pure), the worklist is scheduling state (checked by `MOSS_VERIFY_SIGNATURE_WORKLIST`). Side effect: inner convergence loops use the same signature, so they changed too; output identical on all 77 gate inputs.
- Correct audit criterion: a missing item is a defect only if written, read by a later round, **and** able to change that round's semantic output.
- Design note: the cap of 3 guards an unproven termination property. Moss likely has the bound (no recursive types, no user generics, statically closed) for a monotone-framework argument; the principled end state is one worklist over all three families, removing rounds. Not scheduled.

## 5. Behavior changes from 26.1 + 26.2 (intended, break old usage)

- `moss build` / `moss build --check` on a package rejected; use `margo build`.
- `moss check` fails on a missing or stale dependency `.mossi` (26.1).
- `moss check` reads only `.mossi`, so it accepts a swapped provider rlib.
- Margo overwrites tampered output when provider source is present.
- v9 contract marker now includes the package source hash (`bb4b3f7`; `validate_provider_source_freshness` defined in F1 `e78b6a22`, called from `verify_source_free_provider_binding` in `bb4b3f7`): pre-existing artifacts need a rebuild.
- `-D warnings` on generated crates: a rustc upgrade can fail builds.
- Cargo is required for native package builds.

## 6. Open follow-up: coverage regressions from 26.1/26.1.1

Not in the delivered merge. Assigned to `phase26-2-work`, then a new merge with gates on the new merge commit.

- **R1.** Fast Debug: the assertion that a supplied, nonexistent source root fails with `PROJECT_SOURCE_NOT_FOUND` was deleted from `check_phase15_8_cross_package_fast_debug.py` with no replacement. Behavior may now be "silently ignored." Verify; fix the code if needed; restore the assertion.
- **R2.** Tamper tests in `check_phase21_abi_v9_external_cache.py` and `check_phase21_imported_effects.py` lost their specific error codes (now `not ok` or generic `MOSS_COMPILE_ERROR`). Restore exact codes; add an external-provider (no source, on `MOSS_MODULE_PATH`) tamper case that `margo build` must reject.

## 7. Process rules (added Oct 28)

- **No direct commits to main without the full gates.** Root cause of W2 reaching main red.
- **Merge gates run on the merge commit itself**, not on a parent.
- compiler-perf comparisons should name the baseline; prefer the previous main head so a change is attributable.

## 8. Working rules (unchanged)

- No PRs; agents coordinate among themselves; review off the remote branch.
- All gates run on the work machine.
- Principled changes need no timing study; compiler-perf guards against slowdowns only.
- ABI v10 supported only as far as `make check` tests it.

## 9. Backlog (not 26.2)

- Ownership analysis accepts code Rust's borrow checker rejects (E0502/E0596).
- Native `moss test` doesn't emit test helpers, test types or test rlibs.
- Implicit-module identity has two keys.
- Consumer-local view adapters (`*View`) must never cross a crate boundary.
- Emacs xref fixture flaky under parallel `make check` only; the fixture's name says "known-regression", so check what regression it tracks.
- Confirm parse-cache purity: every input a cached parse result depends on is in its key.
- When rounds become one worklist, shrink the convergence signature to semantic state only.
- Rename/split `MOSS_MARGO_CARGO_DRIVER` (means both "Cargo owns native output" and "caller is Margo").
- Retire `check_phase25_r2_wp2_member` (binary v10 interfaces) if v10 is ever dropped.
- Parked: lazy expression tree (with the Graph DSL); HM inference (2.0); in-process parallel checking; persistent compiler workers.

## 10. Literature pointers

- Type vs. code instantiation across compilation units: C++ template instantiation / COMDAT; rustc per-crate monomorphization and `-Zshare-generics`; Swift cross-module generics.
- Build orchestration: Bazel/Buck action graphs; *Build systems à la carte* (Mokhov, Mitchell, Peyton Jones); Cargo pipelined compilation and the GNU jobserver; Bazel persistent workers.
- Fixed-point inference: monotone frameworks, Kildall dataflow, chaotic iteration; widening (abstract interpretation).
