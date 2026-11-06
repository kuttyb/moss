# Independent review: take Fast Debug indexed-read classification

**Candidate:** `ca88a6c3242aa0344e69d81e7a644b93cb4742b7`  
**Base:** `58e80e8d89e74dcf4923a2cb9eb1325b55f88b80`  
**Scoped verdict:** **GO** for the checker classification repair covering the two projected Vector selector reads in take fixtures. This does not approve whole Phase 28.

## Independent validation

The frozen worktree was clean at review start and HEAD matched the exact candidate. I built isolated development and optimized compilers:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-take-classification/build moss-dev moss-opt
```

The build succeeded. The take harness uses the worktree `moss`; `moss-dev` copied the isolated compiler there for the runs. The pre-existing ignored `moss` binary was saved under review tmp and restored after validation.

```sh
python3 tests/phase28/verify_take_fixtures.py
env MOSS_EST=shadow python3 tests/phase28/verify_take_fixtures.py
```

Both passed 18/18. The two previously failing fixtures, `take_specialized_effects` and `take_multi_selector`, passed checker, Fast Debug, Native Debug, and optimized Native parity. The harness reported zero generated clones and exercised the new 13 checker/Fast Debug fact/requirement corruption probes on each affected fixture.

```sh
python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-take-classification/build/moss-dev
env MOSS_EST=shadow python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-take-classification/build/moss-dev
python3 tests/tooling/check_phase28_copy_index_authority.py tmp/review-take-classification/build/moss-dev
```

Both imported-generic modes passed with source-backed and source-free behavior; indexed-copy authority passed its 8-case Default/Shadow matrix. `python3 tests/tooling/check_agent_feedback.py` and `git diff --check 58e80e8d..ca88a6c3` passed. The focused checks do not establish full-suite or oracle results.

## Authority review

The checker’s condition walk now reduces an Index receiver through only Parenthesized, Member, and nested Index nodes until it reaches a checked Identifier. It extracts the exact receiver source using the Index base ExprId span and obtains the closed Vector TypeRef from the checker’s ownership/type environment. It records the exact Index, receiver, and selector ExprIds, indexed source span, root binding and definition identities, closed receiver/selector/result TypeRefs, statement occurrence and Condition slot, specialization identity/context, EST fact context, and source provenance. The result type is taken from the checked Vector element TypeRef. Primitive results retain the checker-selected scalar disposition; nonprimitive results require bounded nonescaping Read authority with copying disabled. No runtime type inference supplies a missing checker answer.

The new shapes remain bounded: facts are produced only in If/While conditions, only for a Vector receiver with closed receiver/selector/result types, and only when its projection chain reduces to a checked identifier. Unresolved types, non-Vector receivers, or a projection chain rooted in an unsupported expression do not receive authority. Existing consumers remain fail-closed.

Fast Debug classification uses the checker fact and exact EST occurrence/context/specialization rather than deciding based on the runtime value. The interpreter validates the runtime place corresponds to that checked receiver/selector, then borrows or reads the element according to the fact’s disposition. It adds no implicit nonprimitive copy; all take fixtures retain zero-clone evidence. Missing, duplicated, or perturbed fact/requirement state fails closed in the registered probes.

## Scope boundary

This review approves only the exact projected Vector selector classification repair and its covered Fast Debug/Native behavior. It does not approve unreviewed projections, full Default/Shadow suites, the frozen ownership oracle, whole-engine parity, or independent runtime clone provenance. Whole Phase 28 remains separately gated.

