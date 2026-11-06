# Independent review: Phase 28 R7-010 X1 authority repair

**Verdict: scoped GO for the X1 checker-to-artifact-to-native authority repair at `46ff31314cc6000e9543a7459e07a19a7119c703`. This is not a Phase 28 completion or staging GO.** The Phase 28 default and EST-Shadow suites, frozen ownership oracle, runtime clone provenance, and broader parity gates remain separate required gates.

## Reviewed state

Reviewed exact commit `46ff31314cc6000e9543a7459e07a19a7119c703` (`phase28-r7-010-x1-authority`) in a detached review worktree. Candidate worktree was clean before validation; review/build outputs were under its ignored `tmp/`. `git diff --check HEAD^..HEAD` passed.

The change transports checker-produced ReturnValue facts for handler Reply statements, including failure arms, through `.mossi` Section 2 and imported execution bodies. The export path requires one matching fact and one requirement, keyed by checked statement occurrence, EST arena/expression, and enclosing callable; it checks authoritative status and that the checker requirement resolves to that exact fact. The artifact record binds callable identity, execution-body hash, preorder ordinal, ReturnValue slot, source span/fingerprint, specialization and EST contexts, type, source binding/definition, access/action, and provenance. Consumers use exact fact resolution; no fallback authority inference was found. The X1 harness exercises deletion, duplication, action perturbation and owner/module/source/type forgery at serialization/import and native-consumer boundaries, all fail closed.

## Evidence

- Independent build: `make --jobs="$(nproc)" COMPILER_DIR=tmp/review-x1-authority-build moss-dev moss-opt` — passed.
- X1 end-to-end harness: `env -u MOSS_EST python3 tests/tooling/check_phase26_1_1_x1.py tmp/review-x1-authority-build/moss-dev tmp/review-x1-authority-x1` — passed (`ok: true`). It verified generated native app output exactly `4\n7\n9\n`, checker/import/consumer corruption cases, and trace parity. The generated library `.mossi` record for `handler:x1_library__Worker.Run` bound the `reply 9` at line 29, ordinal 0, to the handler callable, source module, and `int` type.
- ReturnValue authority checker, both modes: `env -u MOSS_EST python3 tests/tooling/check_phase28_return_none_authority.py tmp/review-x1-authority-build/moss-dev` and `MOSS_EST=shadow python3 tests/tooling/check_phase28_return_none_authority.py tmp/review-x1-authority-build/moss-dev` — both passed. These cover overload identity in Default and Shadow.
- Isolated Phase 26 C3 `-Werror`: `env -u MOSS_EST python3 tests/tooling/check_phase26_0_1_c3_parse_cache.py tmp/review-x1-authority-build/moss-dev` — passed; helper compiles with `-std=c++17 -O0 -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic`.
- Native source independence: after building the X1 app, temporarily moving the provider source and running `app/build/debug/x1_app` produced exactly `4\n7\n9\n`; source was restored. The Fast Debug/interpreter harness with provider source physically removed returns `SOURCE_FREE_PROVIDER_SOURCE_MISSING` because artifact freshness verification requires reachable provider source. Thus I do **not** claim physical-source-free Fast Debug; the harness verifies the imported execution-body/check-only path with source roots cleared while source remains available for freshness.
- No newly introduced implicit nonprimitive Moss copy was found in the reviewed change. The `worker.clone()` in generated Rust is an existing root-ingress domain-handle capture accompanied by its domain capture fact, not a Moss value copy. No new clone exception is approved by this review.

## Scope boundary

This verdict approves only the reviewed X1 authority repair. It does not waive any end-to-end Phase 28 gate or authorize merging into `phase28-work` or `main`. Runtime clone provenance must still be independently verified, and whole-phase default/Shadow suites, ownership oracle, and engine parity must pass before broader acceptance.
