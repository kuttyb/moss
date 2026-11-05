# P28-R7-010 — Consolidation and Conformance Reconciliation

**Status:** Isolated staging branch; no merge to `phase28-work` or `main`.
**Branch:** `phase28-r7-010-reconciliation`.

## Source ancestry

- Staging base: `dedcb1ed6e7178abc1a4419415af09c976f0d6a1` on
  `phase28-r7-008-integration`.
- R7-008 implementation: `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc`.
- Team A reviewed delivery: implementation `6abe746ae7d641c8abbc7970ec1ee2ec5b97bc56`,
  final branch tip `27454cc6ac47b3749ff8fc45161fead661e887ca`; independent
  Agent 2 GO on both the implementation and status-only final delivery.
  Integrated by merge commit `9cfff9731105353a4fa6bd9a7af317ed8c178f63`
  with parents `dedcb1ed...` and `27454cc6...`.
- Team C reviewed delivery: implementation and test registration
  `81350ff6046cff120da61a1c35f4af8c91049804`, final branch tip
  `c1c677e7c57d13d7cb8bfd7a1b1de34b2f5ba804`; independent Agent 2 GO on the
  implementation and documentation-only closeout. Integrated by merge commit
  `e34afb6c77eaa329a2a42365832893ad091fd50b` with parents `9cfff973...` and
  `c1c677e7...`.
- Team B certified oracle delivery: `66447bf8d923436b430b85b59aa3a323750988a1`;
  independent normative, holdout, and final artifact reviews were GO. Its branch
  was **not merged** because it is based on the older oracle handoff. Only
  `tests/phase28/oracle_r7_005_1/**`,
  `docs/phase28/P28_R7_005_1_NORMATIVE_REVIEW.md`, and
  `docs/phase28/P28_R7_005_1_REPORT.md` were imported from that reviewed SHA.

## Oracle integrity

- The original v1 package under `tests/phase28/oracle/**` is unchanged from
  certified handoff `46619968f64ec618f4cc2576f601b86569399f3c` (`git diff`
  empty).
- V1 expectations digest: `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431`.
- V1 runtime allowlist digest: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`.
- V2 expectations digest: `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`.
- V2 runtime allowlist digest remains the v1 digest above. The certification record
  says `CERTIFIED_FROZEN`, records the approved candidate `c808e9fab5187e25703cac42e4be15162516d39d`,
  and frozen oracle commit `df318989fb7cc6c5c9876ff6a55a7284f53195c9`.
- Team A and Team C runner gates are both retained in `tests/run.sh` at lines
  101–102. `AGENTS.md` keeps their scoped authorizations and adds the R7-010
  staging authorization; no production merge is authorized.

## Combined validation

Not yet run. The next checkpoint rebuilds the merged compiler, runs the 60-row
v2 oracle and both team harnesses against the combined implementation, then
records the combined results separately from the frozen R7-008 comparison.
