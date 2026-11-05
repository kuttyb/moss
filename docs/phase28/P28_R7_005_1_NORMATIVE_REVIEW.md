# P28-R7-005.1 — Normative Review Package (Draft)

**Status:** Awaiting independent normative approval. No implementation comparison has run.

## Frozen parent preservation

The original certified oracle remains under `tests/phase28/oracle/` and is not edited by this successor. Verified parent pins:

- `expectations.json`: `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431`
- `runtime_callsite_allowlist.json`: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`
- Parent certified/frozen SHA: `544506774211fbb5b9d4d3e8116525b389faa3dd`; handoff: `46619968f64ec618f4cc2576f601b86569399f3c`.

The successor is `tests/phase28/oracle_r7_005_1/`, has 60 expectation rows, and stays `DRAFT_PENDING_L7_APPROVAL` until its review and recertification gates complete.

Normative source pins used for this review are the approved P28-R7-005 language addendum at `46619968f64ec618f4cc2576f601b86569399f3c:docs/phase28/P28_R7_005_LANGUAGE_ADDENDUM_DRAFT.md`, and the consolidated Phase 28 LOL proposal at `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc:docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md`. The latter is design authority for proposed ownership rules; this successor treats runtime/output expectations as proposals awaiting review rather than implementation claims. The R7-008 integration evidence is pinned at `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc:docs/phase28/P28_R7_008_INTEGRATION_REPORT.md`.

## Mapping candidates for independent approval

The successor preserves each parent expected semantic verdict and diagnostic primary while adding only candidate corrections grounded in the R7-008 integration findings:

| Fixture | Candidate correction | Normative basis / evidence |
|---|---|---|
| ORACLE-03 | Anchor the rejected source line to `consume_resource(v[0])`; the prior `push|head[0]` expression matches only the comment and not the executable fixture. | The fixture at `oracle_03_collection_element_consume_rejected.moss:10` consumes an indexed element; proposal §5.3 and §12 require this rejection. The R7-008 report §6D explicitly identifies the stale line regex. |
| ORACLE-28 | Accept `OWNERSHIP_SOURCE_BORROWED` alongside the existing primary `OWNERSHIP_USE_AFTER_REBIND`. | The certified failure ledger records the former at the rebind while the old value remains borrowed; proposal §3.2 requires rejecting rebind while a borrower remains live. |
| ORACLE-30 | Accept `OWNERSHIP_INCOMPATIBLE_JOIN` alongside `OWNERSHIP_MIXED_JOIN`. | R7-008 §6D reports the implementation's incompatible-join diagnostic for the forbidden borrowed/owned live join; proposal §4 rejects an impossible promotion. The frozen baseline result itself was unexpected acceptance, so this mapping must not be treated as proof that behavior is repaired. |
| ORACLE-34 | Accept `MATCH_READ_BORROW_ACTIVE` alongside `OWNERSHIP_CONFLICTING_ACCESS`. | The certified ledger reports the match-borrow-specific rejection; proposal §6.1 prohibits overwriting the scrutinee while its payload is live. |
| ORACLE-35 | Accept `BORROWED_ENUM_PAYLOAD_ACCESS` alongside `OWNERSHIP_ILLEGAL_CONSUME`. | The certified ledger reports the borrowed-payload rejection; proposal §6.1 requires ordinary match to borrow and forbids extracting an owned payload. |
| ORACLE-10B | Keep only `CLONE_ALIAS_UNSUPPORTED`; constrain `v[0].clone()` through `observe(record: Record)` so the result type has a concrete consumer. The v2 harness explicitly rejects `TYPE_AMBIGUOUS_OPEN_RESULT` at the clone source line. | The approved P28-R7-005 addendum reserves explicit duplication for `copy(x)`. The certified baseline reported generic open-result ambiguity for a bare assignment, while R7-008 §6B records the fixture later passing. The first normative review rejected accepting that generic code; this successor changes the probe context and retains only the clone-specific diagnostic. |

The five R7-008 §6D candidates and ORACLE-10B form the six candidate mapping repairs requested. The 10B change was revised after independent NO-GO: the generic alternative was removed and the source probe now constrains the clone result through a concrete consumer.

## Added fixture expectations (candidate)

- **ORACLE-56:** primitive Vector element use through a consuming scalar helper, repeated index read, and read-only iteration preserve the Vector. Expected output: `1\n1\n3`. This follows the scalar Copy behavior and R7-009 contract; paired nonprimitive collection-element consume rejection remains in parent ORACLE-03.
- **ORACLE-57:** `Map.get(key)` returns a writable borrow; mutation is visible through a second lookup. Expected output: `9\n9`. This is explicit in the frozen P28-R7-005 language addendum's Map.get table.
- **ORACLE-58:** one generic callable specialized for two distinct record types returns each exact input; expected output `1\n2`. This extends the existing specialization isolation evidence with an executable oracle fixture.
- **ORACLE-59:** a for-loop binding and a one-branch local shadow a sibling function name; the callable remains resolvable outside the local scope. Expected output: `101\n10\n10\n10`. This uses the established Phase 15 shadowing acceptance shape, narrowed to a standalone source fixture.

## Review required before implementation comparison

Please re-review the revised ORACLE-10B fixture/mapping along with the five accepted mapping candidates and four fixture verdicts/outputs. The original v1 oracle and its digest pins must remain unchanged. Reviewer-controlled holdouts should be selected only after this normative package is approved; no compiler outcome may be used to revise expected behavior.

## Review history

- Initial normative review of `53efceae9ebdf8fd38cef8264af963c17e0c0d0` returned
  **NO-GO** only on the ORACLE-10B generic type-diagnostic alternative. The
  reviewer approved ORACLE-03/28/30/34/35 and fixtures 56–59. The generic
  alternative was removed and the fixture was changed to provide a concrete
  `Record` consumer. No implementation comparison was run. Re-review is
  pending on the corrected successor SHA.
- Re-review of `13a3cebec067fb65bf65e6d5d6da3a2bc9540644` confirmed that the
  mapping, fixture, negative unit test, earlier accepted candidates, fixture
  expectations, and v1 digest pins were sound, but returned **NO-GO** because
  this table still described the removed generic alternative. The stale row is
  now corrected. No compiler comparison was run; final normative re-review is
  pending on the updated SHA.
