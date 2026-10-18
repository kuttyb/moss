# Team G intake reconciliation

Scratch base: H/T intake `5cfdc1630d67506146e4adcc52628e508ffdc7c5`.
Pinned G delivery: `2938d257bf10bbb11bd221fa14237ddcd1e0702b`.
Common H/G ancestor: `5b29352aae6a4b59b0c43b9264fe72fb08bbbb6c`.
Normal merge preserves G history; target integration is still gated by review.

The merge produced 28 conflicting hunks across compiler, Type IR, Type IR tests, regression driver and the unresolved-contract document. They were reconciled individually:

- Keep H/T CallableId registration, inferred-effect fixed-point flag, bound declaration initialization/consistency, prebound field annotation identities and strict interned nominal contracts. G's older branch lacks these later additions.
- Keep H/T term-driven compatibility: its solver instantiates bare nominal templates and open collections, covering G's older string-based compatibility paths without weakening typed authority.
- Keep H/T syntax-only annotation validation. Known names bind later through the declaration context; replacing it with early symbol resolution would regress H's annotation order behavior.
- Preserve H/T strict unresolved nominal equality, nonclosed internal markers, trait registrations, bound field annotations and all additional authority regression cases.
- Preserve the D5 L7 specialization-identity resolution, all existing H suites and enum Map-key rejection.
- Retain G's stronger unresolved-collection assertion: the live diagnostic on the exact H base is `[TYPE_AMBIGUOUS_OPEN_RESULT] echo requires a concrete result type for 'values'`.

After reconciliation, `git diff --cached HEAD -- src` is empty: G's compiler, stable nominal identity, visibility and Type IR behavior is already included or superseded by H. Original G ancestry is still integrated explicitly rather than claiming it was an ancestor. The remaining input diff contains G history/status, the GA3 review, and the stronger diagnostic assertion. No G code is replaced by a whole-file ours/theirs operation.

Local optimized compiler build, live moss-0.1 bootstrap, structured Type IR, G-A3 nominal identity and record field visibility suites pass. G2 conformance reports 10/11 gates passing with Gate 10 PENDING joint custom-key integration (its runner exits zero); this is not complete G2 or Phase 25 acceptance. Feedback and whitespace validators pass. Before opening a draft PR, merge the reviewed integration target into this branch, reconcile any new overlap, and let all required automated gates run at its latest head.
