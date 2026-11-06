# Independent review: R7-010 bounded Map CollectionView authority

**Verdict: scoped GO for the bounded CollectionView checker authority repair at `342f2258b0fd66f454b66a499580abc98992be95`. This is not an end-to-end Phase 28 GO.** Full default/EST-Shadow suites, frozen ownership oracle, engine parity, and runtime clone provenance remain separate gates.

## Reviewed state and authority

Reviewed exact SHA `342f2258b0fd66f454b66a499580abc98992be95`, based on intake `993fbd54`; candidate worktree remained unchanged. The source change is limited to checker-side Map-view fact production, the focused consumer harness, its Default/Shadow registration, and a developer-experience observation.

For projected receivers such as `c.entries.keys()`, the producer walks each member projection via the exact EST receiver and closed input TypeRef, resolves the named Field on the declaring Record, and adds the field declaration identity and EST occurrence to the receiver definition identity. It also requires the root name to resolve in the current ownership environment and records its exact binding and definition identities. The Map receiver TypeRef is closed and checked as `map[K,V]`; the view result is a closed `vector[T]` matching the key or value TypeRef. For a local view, the only allowed consumer is its immediately following same-scope binding with a single one-stage `view_identifier |> count` pipeline whose checked result is a closed primitive Int. Any other use, alias, escape, intervening statement, or duplicate use suppresses authority. Consumer fact lookup checks uniqueness and compares the full requirement/fact payload; Fast Debug and native consumers fail closed if authority is missing or contradictory.

Statement occurrence IDs are allocated across the flattened body before per-statement ownership processing. The count binding occurrence is therefore reused from the same ledger rather than minted later in a separate traversal. The view fact retains that ID as the single bounded-read use and last use.

## Validation evidence

- Independent dev and optimized compiler build: `make --jobs="$(nproc)" COMPILER_DIR=tmp/review-take-map-build moss-dev moss-opt` — passed.
- Take harness: `python3 tests/phase28/verify_take_fixtures.py` — 18/18 passed, including the projected Map-field fixture; checker, Fast Debug, Native Debug and optimized Native parity reported with zero clones.
- CollectionView consumers, Default: `env -u MOSS_EST python3 tests/phase28/r7_010/team_b/check_collection_view_consumers.py tmp/review-take-map-build/moss-dev tmp/review-take-map-default` — passed for both direct and projected fixtures. Fast Debug/Native Debug/Native Optimized parity passed, and all 8 authority corruption hooks passed in both Fast Debug and native generation. Failed native generation left no Rust artifact.
- CollectionView consumers, Shadow: `MOSS_EST=shadow python3 tests/phase28/r7_010/team_b/check_collection_view_consumers.py tmp/review-take-map-build/moss-dev tmp/review-take-map-shadow` — same pass, including all 8 corruption hooks.
- X1 regression: `env -u MOSS_EST python3 tests/tooling/check_phase26_1_1_x1.py tmp/review-take-map-build/moss-dev tmp/review-take-map-x1` — passed (`ok: true`).
- ReturnValue overload authority: `env -u MOSS_EST python3 tests/tooling/check_phase28_return_none_authority.py tmp/review-take-map-build/moss-dev` and `MOSS_EST=shadow python3 tests/tooling/check_phase28_return_none_authority.py tmp/review-take-map-build/moss-dev` — both passed.
- C3 isolated Phase 26 `-Werror`: `env -u MOSS_EST python3 tests/tooling/check_phase26_0_1_c3_parse_cache.py tmp/review-take-map-build/moss-dev` — passed (`C3 negative call-parse cache replay checks passed`). The helper compiles with `g++ -std=c++17 -O0 -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic` and completed its fixture/profile checks.
- Adversarial probes under candidate `tmp/review-take-map-adversarial`: aliasing the view before `count`, and mutating the underlying projected Map between view binding and count, both failed in Fast Debug and native generation with `MOSS_INTERNAL_INVARIANT` for missing nonescaping CollectionView authority.
- Generated Rust for the projected fixture uses `((c).entries).keys().collect::<std::vec::Vec<_>>()`; the harness found no `.keys().cloned()` / `.values().cloned()` and the take oracle reported zero clones. No Moss copy authority or clone exception is introduced by this change.
- `git diff --check 993fbd54..HEAD` — passed; candidate worktree tracked state stayed clean.

## Scope boundary

This review approves only the checker CollectionView authority slice. It does not approve merging to `phase28-work` or `main`, and it does not close the whole Phase 28 suites, ownership oracle, engine parity, or clone-provenance gates.
