# P28-R7-005.1 — Normative Review and Certification Record

**Status:** Normative approval GO on `c808e9fab5187e25703cac42e4be15162516d39d`; successor v2 is `CERTIFIED_FROZEN`. The final implementation comparison and certification evidence are recorded in `tests/phase28/oracle_r7_005_1/reports/`.

## Frozen parent preservation

The original certified oracle remains under `tests/phase28/oracle/` and is not edited by this successor. Verified parent pins:

- `expectations.json`: `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431`
- `runtime_callsite_allowlist.json`: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`
- Parent certified/frozen SHA: `544506774211fbb5b9d4d3e8116525b389faa3dd`; handoff: `46619968f64ec618f4cc2576f601b86569399f3c`.

The successor is `tests/phase28/oracle_r7_005_1/`, has 60 expectation rows, and completed its normative review and recertification gates. Its exact expectation digest and frozen content SHA are recorded in `certification.json`.

Normative source pins used for this review are the approved P28-R7-005 language addendum at `46619968f64ec618f4cc2576f601b86569399f3c:docs/phase28/P28_R7_005_LANGUAGE_ADDENDUM_DRAFT.md`, and the consolidated Phase 28 LOL proposal at `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc:docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md`. The latter is design authority for proposed ownership rules. These expectations define the oracle contract and are not claims about current compiler conformance. The R7-008 integration evidence is pinned at `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc:docs/phase28/P28_R7_008_INTEGRATION_REPORT.md`.

## Approved mapping corrections

The successor preserves each parent expected semantic verdict and diagnostic primary while adding only the approved corrections grounded in the R7-008 integration findings:

| Fixture | Candidate correction | Normative basis / evidence |
|---|---|---|
| ORACLE-03 | Anchor the rejected source line to `consume_resource(v[0])`; the prior `push|head[0]` expression matches only the comment and not the executable fixture. | The fixture at `oracle_03_collection_element_consume_rejected.moss:10` consumes an indexed element; proposal §5.3 and §12 require this rejection. The R7-008 report §6D explicitly identifies the stale line regex. |
| ORACLE-28 | Accept `OWNERSHIP_SOURCE_BORROWED` alongside the existing primary `OWNERSHIP_USE_AFTER_REBIND`. | The certified failure ledger records the former at the rebind while the old value remains borrowed; proposal §3.2 requires rejecting rebind while a borrower remains live. |
| ORACLE-30 | Accept `OWNERSHIP_INCOMPATIBLE_JOIN` alongside `OWNERSHIP_MIXED_JOIN`. | R7-008 §6D reports the implementation's incompatible-join diagnostic for the forbidden borrowed/owned live join; proposal §4 rejects an impossible promotion. The frozen baseline result itself was unexpected acceptance, so this mapping must not be treated as proof that behavior is repaired. |
| ORACLE-34 | Accept `MATCH_READ_BORROW_ACTIVE` alongside `OWNERSHIP_CONFLICTING_ACCESS`. | The certified ledger reports the match-borrow-specific rejection; proposal §6.1 prohibits overwriting the scrutinee while its payload is live. |
| ORACLE-35 | Accept `BORROWED_ENUM_PAYLOAD_ACCESS` alongside `OWNERSHIP_ILLEGAL_CONSUME`. | The certified ledger reports the borrowed-payload rejection; proposal §6.1 requires ordinary match to borrow and forbids extracting an owned payload. |
| ORACLE-10B | Keep only `CLONE_ALIAS_UNSUPPORTED`; require the clone-specific message and executable source line `r = v[0].clone()`. Retain the direct assignment probe from certified v1. | The approved addendum reserves explicit duplication for `copy(x)`. R7-008 report §6B lists ORACLE-10B as passing, and a focused check of the exact v1 source at R7-008 SHA `e9d3f844` emits `CLONE_ALIAS_UNSUPPORTED` with `source-level .clone() is unsupported; use copy(...)` at the clone assignment. Generic type/open-result errors and comment-only matches therefore do not satisfy this mapping. |

The five R7-008 §6D candidates plus the tightened ORACLE-10B message/source anchor form the six requested diagnostic-mapping corrections. The 10B fixture retains the frozen v1 source expression.

## Added fixture expectations

- **ORACLE-56:** primitive Vector element use through a consuming scalar helper, repeated index read, and read-only iteration preserve the Vector. Expected output: `1\n1\n3`. This follows the scalar Copy behavior and R7-009 contract; paired nonprimitive collection-element consume rejection remains in parent ORACLE-03.
- **ORACLE-57:** `Map.get(key)` returns a writable borrow; mutation is visible through a second lookup. Expected output: `9\n9`. This is explicit in the frozen P28-R7-005 language addendum's Map.get table.
- **ORACLE-58:** one generic callable specialized for two distinct record types returns each exact input; expected output `1\n2`. This extends the existing specialization isolation evidence with an executable oracle fixture.
- **ORACLE-59:** a for-loop binding and a one-branch local shadow a sibling function name; the callable remains resolvable outside the local scope. Expected output: `101\n10\n10\n10`. This uses the established Phase 15 shadowing acceptance shape, narrowed to a standalone source fixture.

## Normative review decision

The independent reviewer approved the revised ORACLE-10B fixture/mapping, the five other mapping corrections, the four fixture verdicts/outputs, and v1 digest preservation at exact candidate SHA `c808e9fab5187e25703cac42e4be15162516d39d`. Reviewer-controlled holdouts were then run with expectations fixed before execution. No compiler outcome was used to revise expected behavior.

## Review history

- Initial normative review of `53efceae9ebdf8fd38cef8264af963c17e0c0d0` returned
  **NO-GO** only on the ORACLE-10B generic type-diagnostic alternative. The
  reviewer approved ORACLE-03/28/30/34/35 and fixtures 56–59. The generic
  alternative was removed and the fixture temporarily gained a concrete
  `Record` consumer. No implementation comparison had run at that checkpoint.
- Re-review of `13a3cebec067fb65bf65e6d5d6da3a2bc9540644` confirmed that the
  mapping, fixture, negative unit test, earlier accepted candidates, fixture
  expectations, and v1 digest pins were sound, but returned **NO-GO** because
  this table still described the removed generic alternative. The stale row is
  now corrected. Final normative re-review returned GO on
  `0d4aff6ebdccc2810d7aaf15038a5329f0d7a1b4`.
- Subsequent comparison against the frozen R7-008 compiler showed the
  contextual ORACLE-10B probe reports generic `TYPE_MISMATCH`; a focused check
  of the exact certified v1 source reports `CLONE_ALIAS_UNSUPPORTED` at the
  clone assignment. The successor now restores the v1 source fixture without
  changing its expectation or narrow mapping. At that checkpoint, the source
  restoration still needed normative re-review before implementation comparison;
  the subsequent GO and completed comparison are recorded in the next entry.
- After the oracle-spec GO on `f74210891202926055ccaa3ce8c077e61ec144cc`,
  ORACLE-10B's mapping was tightened further: it now requires the exact
  clone-specific diagnostic message and the executable clone assignment line.
  Its fixture and expected verdict remain byte-identical to certified v1. This
  completes the sixth mapping correction requested. Independent normative
  review of the exact `c808e9fab5187e25703cac42e4be15162516d39d` package returned
  **GO**. The full v2 comparison was then rerun against frozen R7-008
  `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc`; see the durable results and
  failure ledger for mismatch-preserving outcomes.

## Holdout and final successor review

Independent reviewer-controlled holdouts were fixed before execution and received **oracle-spec GO / integrity PASS**. The two failed execution cases are recorded as R7-008 implementation findings in `reports/holdouts/FINAL_REPORT.md`; their expected results were retained. The exact full comparison was rerun after the final mapping change and is recorded in `reports/P28_R7_005_1_R7_008_RESULTS.json` and `P28_R7_005_1_R7_008_FAILURE_LEDGER.md`.

The v2 expectations and corrected mapping are now marked `CERTIFIED_FROZEN`. Certification pins the v2 expectation document digest and the unchanged v1 parent digests; it does not claim that R7-008 conforms. Final artifact review is tracked against the exact delivery SHA; it does not alter the frozen oracle inputs.
