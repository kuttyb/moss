# Independent review: declared-None method ReturnValue authority

**Verdict: scoped GO for the declared `None` method ReturnValue EST authority repair at `9a6731276c9dd9705b9034c071a81f5b4cb3ae95`. Overall Phase 28 remains NO-GO.** This does not approve integration into `phase28-work` or `main`, and does not close full-suite, ownership-oracle, engine-parity, or clone-provenance acceptance.

## Reviewed state and authority path

Reviewed exact frozen branch/worktree `phase28-r7-010-native-proof-edge-return-authority` at `9a6731276c9dd9705b9034c071a81f5b4cb3ae95`. The candidate tracked state remained clean; independent compiler outputs were under `tmp/review-proof-edge-build/`. `git diff --check HEAD^..HEAD` passed.

The new checker producer runs only for an unshadowed `None` return whose type came from the declared callable contract, when the existing overloaded-method context path does not apply. It requires the exact current method and object to agree with `method:<owner>.<name>`, verifies that pointer/name/owner select exactly one declaration, rejects specialization, and requires a closed declared `option[T]` TypeRef structurally equal to the already checked ReturnValue expression TypeRef. It binds the fact to the exact ReturnValue handle/EST occurrence and records the declared TypeRef under `functional_method_context(object, method)`. A contradictory existing closed TypeRef fails immediately; after recording, missing, open, or structurally different retained facts fail closed. The fact carries that exact EST context to the consumers.

The drop and wrong-TypeRef hooks are selected for `Counter.next`; both cause checker failure. A wrong-owner-context hook rewrites the fact context after materialization; native and Fast Debug consumers reject it because no occurrence type exists under that context and this local method cannot use the imported-owner fallback. The native generator and Fast Debug remain fact consumers; no consumer inference was added. The change does not modify copy/transfer action selection or authorize any clone.

## Independent validation

Independent build:

`make --jobs="$(nproc)" COMPILER_DIR=tmp/review-proof-edge-build moss-dev moss-opt` — passed (existing compiler warnings only).

Focused gates, run from the detached review worktree at the same SHA using the isolated compiler above:

- Phase 21 source-free O1/O8 proof-edge gate, Default and Shadow:
  `env -u MOSS_EST python3 tests/tooling/check_phase21_source_free_proof_edges.py /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev` and the same command with `MOSS_EST=shadow` — both passed (`Phase 21 source-free O1/O8 proof edges passed.`). The harness keeps provider source available for R2 freshness validation; this report does not claim physical deletion of that source.
- ReturnValue checker/native authority, Default and Shadow:
  `env -u MOSS_EST python3 tests/tooling/check_phase28_return_none_authority.py /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev` and the same command with `MOSS_EST=shadow` — both passed. This covers exact overload Int/String identities, shadowed `None`, checker drop/type corruption, native `empty\n`, and native wrong-context failure.
- X1 v9 and v10, each Default and Shadow:
  `env -u MOSS_EST python3 tests/tooling/check_phase26_1_1_x1.py /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev tmp/review-proof-edge-x1-v9` and `MOSS_EST=shadow python3 tests/tooling/check_phase26_1_1_x1.py /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev tmp/review-proof-edge-x1-v9-shadow`; `env -u MOSS_EST python3 tests/tooling/check_phase26_1_1_x1_v10.py /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev` and the same v10 command with `MOSS_EST=shadow` — all four runs passed. v9 reports `ok: true` across handler/method artifact and consumer corruption coverage; v10 confirms Section 2/generic Section 1, native output, and its corruption gates.
- Phase 27 `.mossi` EST Default and Shadow:
  `env -u MOSS_EST python3 tests/tooling/check_phase27_est_mossi.py /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev` and `MOSS_EST=shadow` with the same script/compiler — both passed; each reported generic=1 and Section2=2 imported dumps.
- Isolated Phase 26 C3 `-Werror`:
  `env -u MOSS_EST python3 tests/tooling/check_phase26_0_1_c3_parse_cache.py /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev` — passed (`C3 negative call-parse cache replay checks passed`). Its standalone helper compiled with `-std=c++17 -O0 -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic` and completed fixture/profile checks.
- Additional Fast Debug wrong-context probe:
  `MOSS_TEST_PERTURB_DECLARED_NONE_METHOD_RETURN_EST_CONTEXT=Counter.next /home/owner/daji/code/moss/tmp/phase28-r7-010-native-proof-edge-return-authority/tmp/review-proof-edge-build/moss-dev run --interp tests/phase28/r7_010/team_a/return_none_fact.moss` — exited nonzero with `MOSS_INTERNAL_INVARIANT return-value EST type fact missing outside exact imported owner`.

The Default and Shadow ReturnValue scripts have a shared fixed scratch directory (`tmp/phase28-return-none-native`) and must not run concurrently in one worktree. An initial concurrent attempt raced while one run removed that directory, causing Default to fail with `FileNotFoundError`; sequential Default and Shadow reruns both passed. This was a harness scratch-path race, not a candidate failure.

## Scope boundary

This review approves only the local declared-`None` method EST fact materialization and fail-closed consumer path. No implicit nonprimitive copy or runtime clone was introduced or approved. Whole Phase 28 acceptance remains NO-GO until its separate suites, frozen ownership oracle, engine parity, and clone-provenance reviews complete.
