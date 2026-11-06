# Independent review: Root local transfer

**Candidate:** `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`  
**Scope:** Root-message transfer of a local nonprimitive using checker-authorized explicit `copy(...)`.  
**Verdict:** **NO-GO** for the stated acceptance requirements. The native path is checker-authorized and corruption-tested, but Fast Debug accepts an unsupported no-copy transfer and silently deep-copies it without checker authority.

## Evidence

Candidate worktree was clean at review start. Built isolated binaries with:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-root-local-build moss-dev moss-opt
```

The build completed successfully. Then ran:

```sh
python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/review-root-local-build/moss-dev
```

Result: `Phase 28 Root-ingress domain capture authority: 190 scenarios PASS`. The gate covers Default and EST-shadow modes, source and consumer drop/duplicate/perturb hooks, explicit-copy fixtures, native `rustc -O -D warnings` output, and Fast Debug parity for the explicit-copy fixtures. Its no-copy case checks that the checker emits no root-ingress payload fact and native generation fails closed; it does not run Fast Debug on that case.

```sh
python3 tests/tooling/check_phase27_optimizer_expression_uses.py tmp/review-root-local-build/moss-dev
```

Result: all six listed fixtures passed decision and three-engine parity.

The omitted Fast Debug probe was run independently:

```sh
tmp/review-root-local-build/moss-dev run --interp tests/phase28/r7_010/root_ingress_local_without_copy.moss
```

Result: exit status 0; stdout `accepted`. The fixture passes a local `Payload` identifier directly to `message sink.Accept(payload)` without explicit `copy(...)`.

## Authority review

The native implementation binds the Root argument to the exact `TypedCopyFact` and validates the message statement/argument occurrence, arena, source fingerprint, source and copy spans, and source/result types before passing the fact to the existing checked copy lowering. The candidate adds no message `Consume` authority and no native payload `.clone()` fallback. The explicit-copy native fixtures carry copy-fact provenance and compile with Rust warnings denied. No-copy native lowering fails closed with the missing checked-copy/consume invariant.

The interpreter path is different. In `src/interpreter_domains.inc`, `prepare_handler_call(const ExpressionTreeView&, const est::Message&, ...)` wraps every evaluated message argument in `independent(...)`. That helper recursively deep-copies structs and collection values. The explicit `copy(...)` evaluator in `src/interpreter.hpp` also calls `independent(...)`, so explicit-copy Root messages undergo an additional handler-preparation clone. Most significantly, the no-copy fixture is accepted by Fast Debug because handler preparation supplies the copy unconditionally, despite the absence of any checker copy fact. This behavior predates this candidate, but it is neither bound to nor justified by this candidate's exact `TypedCopyFact`; it is an interpreter hidden clone and violates the requested no-implicit-nonprimitive-copy / no-unapproved-clone acceptance. The new native authority does not close this interpreter path.

## Disposition

The scoped native lowering change demonstrates exact checker authority for explicit Root copies and fail-closed native corruption handling. It does **not** satisfy the cross-engine acceptance contract while Fast Debug silently clones an unsupported no-copy payload. Do not treat this checkpoint as approved for integration. Repair or otherwise formally authorize/verify the Fast Debug transfer path, then rerun the no-copy case and focused gates. This review makes no disposition on pending whole-Phase-28 suite, ownership-oracle, or runtime-clone gates; those remain separate and pending.
