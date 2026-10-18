# Phase 25 Team I — Team B merge rehearsal

This is a disposable ancestry-preserving rehearsal, not the reviewed B intake
PR or an integration-target merge. It starts from S/H candidate `9a04230` and
merges pinned Team B `755f13172c37447bca981d1fefbb885ec46f3a59`.
The eventual B PR must start from the latest reviewed `phase25-integration`
target, preserve this input ancestry, and pass its own exact-head CI and L6
review.

The merge had three textual conflicts and no compiler-source conflict:

| File | Resolution |
| --- | --- |
| `.codex/CURRENT_STATUS.md` | Retain the current integration recovery log. Team B's historical closeout remains in its delivery report and branch ancestry. |
| `docs/phase25/TEAM_A_TYPED_SEMANTIC_API.md` | Retain Team A's canonical implementation contract and preserve Team B's distinct agreement as `TEAM_B_TYPED_SEMANTIC_API_AGREEMENT.md`. Update Team B review references. |
| `tests/run.sh` | Retain L7's option 1 exact `ECHO_DISPLAY_UNSUPPORTED` assertion for `unresolved_collection_type`; Team B's older fallback accepted two mutually exclusive diagnostic paths. |

The runtime, ABI record, linker helper, and Team B test changes merge without
textual conflict. This only demonstrates mergeability. Compiler binding of
Team B's typed records and shared runtime, nominal `MapKeyProof` Gate 10,
same-SHA source-free proof, and production ABI-v10 cutover remain open.

Rehearsal validation: compiler build, `check_agent_feedback.py`, Team B
debug/release runtime source-removal and identity checks, and both ABI-v10
staged/adversarial C++ suites pass. Team B's acceptance suite reports
**22 PASS, 1 PENDING, 0 FAIL**; the pending check is correctly Gate 10. Its
source-free G2 Gate 5 test was tightened to require the new exact
`METHOD_NOT_FOUND` code and original `provider.moss` related origin.
