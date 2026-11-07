# Team C scoped review: Root-ingress primitive argument TypeRef

## Verdict

**Scoped PASS** for the ORACLE-45 nested primitive Root-ingress exact-TypeRef repair at implementation SHA `dce8e519238e89bce228f4db90e18c782d1f4def` (`phase28-r7-010-root-ingress-primitive-type`). Fresh Default and EST-shadow registered Root-ingress authority gates each passed **474 scenarios**. No full Phase 28 suite or whole-phase GO is claimed.

This review grants no clone exception or Moss-copy authority.

## Exact source and artifacts

- Candidate: `dce8e519238e89bce228f4db90e18c782d1f4def`.
- Candidate worktree: `tmp/phase28-r7-010-root-ingress-primitive-type`.
- Parent: `HEAD^` of the candidate branch.
- Fresh build directory: `tmp/teamc-dce8e519/build/compiler`.
- `moss-dev` SHA-256: `9c1bb95b827d3b5ea98160e6f3d80287ee612c32f5940d372c6a661a435d1511`.
- `moss-opt` SHA-256: `b521506148d7475262a909ed35f28eed83cc504243c867773e408a6dc847c562`.
- Reviewed source SHA-256 values:
  - `src/semantic_checker.cpp`: `2a011290ced57177664910d4fb8cc7a84c8b35ca55b2bf59852d94ab02755e1e`
  - `src/typed_semantic_api.hpp`: `764bb4d0a534ec819a076ca98b839c138ef4f5befb0b4de189dcd4b31f76a31b`
  - `src/interpreter_domains.inc`: `7f35587659b23ad929018808443b0da0f34b04301c374a488f8dbeaf8ab8effb`
  - `tests/tooling/check_phase28_root_ingress_capture_authority.py`: `c5571327be90b08e3d5b6908a870b0014022593356246e35816a2d77d28fcf1b`
- Candidate tracked worktree was clean before/after verification. Source, tests, frozen inputs, allowlists, and staging were not edited.

## Commands and results

From the candidate worktree:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/teamc-dce8e519/build/compiler \
  tmp/teamc-dce8e519/build/compiler/moss-dev \
  tmp/teamc-dce8e519/build/compiler/moss-opt

env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py \
  tmp/teamc-dce8e519/build/compiler/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py \
  tmp/teamc-dce8e519/build/compiler/moss-dev
```

Both modes reported:

```text
Phase 28 Root-ingress domain capture authority: 474 scenarios PASS
```

The registered gate includes checker-side drop, duplicate, and ten-field perturb cases for the new argument-TypeRef record; Fast Debug-side requirement/fact drop, duplicate, and ten-field perturb cases; Native and Fast Debug execution; nested message forms; and the Root-ingress, payload, explicit-copy and primitive controls in Default and Shadow. The ORACLE-45 case emits and compiles Native code at `-O0` and `-O`, then checks Fast Debug parity.

Additional direct trace probe, run once in each mode:

```sh
env -u MOSS_EST MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS=1 \
  tmp/teamc-dce8e519/build/compiler/moss-dev run --interp \
  tests/phase28/oracle/fixtures/oracle_45_handler_primitive_rebind_local.moss
MOSS_EST=shadow MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS=1 \
  tmp/teamc-dce8e519/build/compiler/moss-dev run --interp \
  tests/phase28/oracle/fixtures/oracle_45_handler_primitive_rebind_local.moss
```

Each printed `11` and:

```text
ROOT_INGRESS_INTERPRETER_COPY_COUNTS argument=0 authority=primitive explicit_copy_evaluations=0 authorized_independent_calls=0 implicit_independent_calls=0
```

## Authority and scope review

- The checker attaches a closed `TypeRef` derived from the already selected handler parameter to each exact Root-ingress message argument. The fact is keyed by statement occurrence, arena, owner expression, message expression, argument index and expression, source fingerprint, selected handler identity, specialization context, and EST fact context.
- The normal checked message edge establishes the selected handler and validates its argument contract. If an EST type is already present for the exact argument/context, the producer rejects a structural contradiction rather than replacing it.
- The typed API matches requirement and fact on exact occurrence identity, compares the complete fact payload, and requires exactly one authoritative closed fact. Fast Debug additionally matches owner/message/argument identity, source fingerprint, selected handler and context; missing, duplicate, malformed, or perturbed facts raise an internal invariant error.
- Fast Debug now classifies primitive handling from this checker-issued type instead of relying on the nested argument node having an ordinary root-expression type fact. ORACLE-45’s nested handler call returns `11` in Fast Debug and Native at both optimization levels.
- The type record explicitly carries no ownership or copy authority. Native generator and serialization/`.mossi` code are unchanged in this commit. Existing explicit-copy and fresh-payload paths remain separately fact-gated. The trace confirms zero explicit copy evaluations and zero `independent()` calls for ORACLE-45.
- This is limited to closed argument types for checked Root ingress. If the checker cannot produce a closed type, it emits no authoritative type record; Fast Debug remains fail-closed. The scoped tests do not establish generic/open-parameter behavior.

## Review limits

The independent run did not run `make check`, `make check-shadow`, the frozen Phase 28 oracle, or full-suite matrices. The focused gate uses `rustc -Awarnings` for its nested ORACLE-45 Native fixture and reports successful compilation/execution; it is not evidence for a separate isolated `-Werror` gate. Build output included existing `src/type_ir.hpp` missing-field-initializer warnings; both fresh compiler binaries linked successfully.
