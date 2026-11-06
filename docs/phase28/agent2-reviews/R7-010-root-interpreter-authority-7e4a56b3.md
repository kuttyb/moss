# Independent review: Fast Debug Root ingress authority

**Candidate:** `7e4a56b3461ffd949f89542ad50bc7ccd4557241`  
**Parent:** `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`  
**Verdict:** **Scoped GO** for this Fast Debug authority repair. This is not an end-to-end Phase 28 GO; full suites, ownership oracle, and runtime-clone gates remain separate.

## Independent validation

The candidate worktree was clean at review start. The candidate contains the requested parent and resolves to the exact SHA above. I built an isolated compiler:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-root-interpreter-build moss-dev
```

Build completed with exit status 0. Then I ran:

```sh
python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/review-root-interpreter-build/moss-dev
```

Result: `Phase 28 Root-ingress domain capture authority: 214 scenarios PASS`. This registered gate runs Default and EST-shadow cases and checks native plus Fast Debug behavior; explicit-copy, fresh-value, primitive, unsupported no-copy local, and interpreter fact corruption paths are exercised. Explicit-copy native fixtures compile with `rustc -O -D warnings`. The gate verifies interpreter drop, duplicate, and perturb hooks for Copy and fresh-transfer facts and checks Fast Debug copy accounting.

The parent’s earlier repro now fails closed in Fast Debug as required by the gate. The candidate also reports exact runtime accounting in test builds: an explicit Copy has one copy evaluation, one authorized independent value creation, and zero implicit boundary-copy calls; fresh and primitive Root arguments have zero explicit Copy, zero authorized Copy, and zero implicit boundary-copy calls. The Root argument is moved into the prepared handler call.

`git diff --check 3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a..HEAD` passed. No implementation files were modified during review.

## Authority and clone review

`resolve_root_ingress_argument_authority()` requires the active statement to be marked Root ingress and have a checked statement occurrence, the exact EST arena and message, the argument index and occurrence, a unique statement handle, exact checked handler capture identity, and a closed argument TypeRef in `main` context. An explicit `copy(...)` must resolve to `builtin:copy-v1`, have arity one, match the exact copy requirement/source occurrence/statement/arena/context, and have an authoritative fact whose result type matches the checked argument TypeRef. A fresh nonprimitive transfer must match a unique checker-issued payload requirement/fact and exact handler/type/action, be checker-proved fresh, and explicitly disallow Moss value copying. Primitive ingress is restricted to a closed direct primitive TypeRef with a recognized primitive name. Other nonprimitive arguments fail closed.

In `prepare_root_ingress_handler_call()`, the authority is resolved before evaluating each argument. The argument is evaluated once; the Root-boundary `independent()` snapshot is gone, and the value is moved into the handler call. Test-only scoped counters detect an independent value created implicitly during argument evaluation and distinguish that from the one `independent()` invoked by an authorized explicit `copy(...)`. The implementation adds no new message `Consume`, does not authorize a new clone exception, and does not infer authority in native consumers. This source path is main-source EST; the candidate status correctly notes there is no `.mossi` serialization/import boundary here.

The three adversarial hook families for Copy and fresh-transfer facts are exercised by the registered gate. I found no material defect in the reviewed Root Fast Debug authority path. The pre-existing general interpreter `independent()` helper remains for other operations, but Root message argument preparation is now scoped and checked, and the focused gate verifies no implicit Root-boundary clone for its covered cases.

## Scope boundary

This verdict approves only the frozen Fast Debug Root-ingress authority repair on the exact SHA above. The 214-scenario focused gate is not the full Default/Shadow suite, frozen ownership oracle, end-to-end engine parity, or independent runtime clone provenance audit; those remain pending and must keep whole Phase 28 status separate from this scoped GO.
