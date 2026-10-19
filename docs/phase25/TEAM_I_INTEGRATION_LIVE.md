# Team I live integration record

## S draft core CI regression — 2026-10-19

Exact S PR #11 workflow `39194794707` has examples and harness green, but
core failed early in `check_phase21_float_ieee.py`: the direct explicit-module
Rust output is compiled by plain `rustc -D warnings`, where S's
`#[cfg(moss_process_root)]` causes unexpected-cfg errors and suppresses main
(E0601). This is a real regression at PR head `99aebff`, so the PR remains
draft/unmerged. The sole L4 is repairing generation context and adding
coverage for both direct Rust and source-free rlib paths. Post-G target CI
is 3/4 green with core in progress; G1-08 awaits L7 contract direction.

## G1-08 Queue source-surface conflict — 2026-10-19

L4's minimal direct `for` over Queue fails at Moss checking with
`MOSS_COMPILE_ERROR` line 5, expecting Vector, range or Iterator. Frozen
G1-08/initial architect review require Queue traversal, whereas current
canonical language/syntax/Fast Debug docs prohibit it (and the guide itself
has an earlier contradictory sentence). Bootstrap `moss-0.1` lists Queue
construction/push/pop without iteration. Root asked L7 which contract wins;
G1-08 remains red and L4 continues independent gates. This does not block
draft S CI or prior post-G target checks.

## S draft intake exact-head checkpoint — 2026-10-19

The sole L4 pushed clean S head `99aebffe4ef818abb921372504e4ec3160a3cd07`
and opened draft [PR #11](https://github.com/kuttyb/moss/pull/11) against
post-G target `64d22251d7090d6bbe36369cc80a5f95f7890ce7`. Its normal
S refresh `9aaf056` retains pinned S history and resolves the one
`tests/run.sh` overlap to L7's approved unresolved-echo diagnostic; root's
final commit carries the final-CI v10 selector, current status, and H2/G
L6 review records. Exact-head PR workflow `39194794707` is queued. Forced
build/bootstrap, S1–S4, S-A/B/C, Type IR, Team A, G3/G4/L7, indexed String,
feedback and diff checks pass locally; default-v9 G2 is 10/11 with joint
Gate 10 pending. Post-G target workflow `39193878297` is 3/4 green with
core running. S review/merge waits for both four-job green results. Staging
`cc8b80a` remains 44/62 same-HEAD live PASS; no main/release claim.

## G target integration checkpoint — 2026-10-19

Reviewed G PR #10 passed four exact-head jobs, was marked ready, and merged
normally at target `64d22251d7090d6bbe36369cc80a5f95f7890ce7`.
Parents are prior H2 target `56d68ce` and reviewed G head `6859041`;
normal ancestry and whitespace checks pass. Post-G target workflow
`39193878297` is in progress and must pass before S target merge. L4 is
refreshing pretested S onto that exact target; root will add the final
workflow v10 selector and current status before S draft PR. Staged G4 is
44/62 live PASS with span and scaling gaps open. No main merge or final
release claim.

## G exact-head L6 review and G4 bounded staged proof — 2026-10-19

Draft G PR #10 head `6859041` passed its four required exact-head jobs in
`39187310886`, and prior H2 target run `39186996122` passed its four jobs.
Combined L6 review approved the bounded ancestry/diagnostic intake; verdict
is `TEAM_I_L6_REVIEW_G.md`. PR is still draft/unmerged at this checkpoint.

Separately, L4 pushed clean staging `a93aa802d1f5b8c472c6997b0cb8fbc08d77e296`.
Strict v10 same-HEAD acceptance ran 13 complete suites and reports 44 PASS,
0 FAIL, 15 BLOCKED, 3 NOT IMPLEMENTED; exit 1. G4-04 now proves shared and
inline MossString layout, sparse offsets, ASCII metadata and bounded
4 GiB arithmetic after a pre-mutation guard repair. G4-06 executes two
distinct source-free String/Char consumers in debug/release against sealed
provider artifacts and rejects runtime mismatch before link. G4-05 scaling
and the staged Vector[Tag] diagnostic span remain open. No final release or
main merge.

## G3/G4 bounded same-HEAD proof checkpoint — 2026-10-19

L4 pushed staged `66a57c5`; strict v10 rebuilt that clean HEAD and ran
11 complete suites. It reports 42 PASS, 0 FAIL, 17 BLOCKED, 3 NOT
IMPLEMENTED, with strict exit 1. G3-01–04 now have exact scalar hash,
Float Map/Set, five-type three-engine law and NaN-payload evidence; G4-01–03
have focused Char/constructor/escape evidence. G3-05/08, G4-04–06 and
G1-07/08 remain open on their frozen breadth. Staged full S1+S2 still finds
the Vector[Tag] Map-key line-0 diagnostic; default-v9 full S1+S2 passes.
The worker is fixing the span and proving the actual shared String runtime.
No target/main merge or release claim; G draft and prior target core CI run.

The runtime audit then found that the u32/4 GiB bound is checked only when
recording a sparse offset; all-ASCII growth could bypass it. G4-04 remains
open while L4 adds a pre-mutation length guard and a bounded regression.

## G1 same-HEAD live acceptance checkpoint — 2026-10-19

L4 pushed staging `7dfe201`: strict v10 acceptance rebuilt the checkout and
passed G2 11/11, E3 Debug/Release 11/11, key 30/30, B 23/23, and mapped
13 of 15 G1 gates to complete named suite output. The aggregate is 35 PASS,
0 FAIL, 24 BLOCKED, 3 NOT IMPLEMENTED; strict exits 1. G1-07 awaits Float
display breadth and G1-08 Queue traversal. The static matrix remains
unchanged. G3/G4 audit uncovered a staged-v10 invalid `Vector[Tag]` Map-key
diagnostic with line 0; S2 isolation is only evidence collection, and full
S1+S2 must pass before final acceptance. G draft/target core CI jobs remain
in progress. No target/main merge or release claim.

## Final-workflow ABI selector checkpoint — 2026-10-19

The coordination branch now supplies `MOSS_STAGED_ABI=v10` to the frozen
final-release CI job, matching the strict same-HEAD runner's explicit
requirement. Carry this root-owned workflow change through a reviewed
target-based intake before triggering `[phase25-final]`. This is a readiness
fix, not a release result; G PR #10 and post-H2 target core jobs still run.

## Same-HEAD live acceptance checkpoint — 2026-10-19

L4 pushed `a766dfa`. Strict mode now rebuilds this checkout's compiler and
executes named gate cases against one clean, unchanged HEAD. G2 11/11, E3
Debug/Release 11/11, key 30/30 and Team B 23/23 yield 22 genuinely mapped
PASS gates; 40 remain unmet and strict exits 1. The static matrix was not
flipped. Self-test rejects partial suite output, dirty HEAD and unmapped
sign-off claims. This is a staged evidence mechanism and partial result,
awaiting further gate proofs and target integration.

## Strict acceptance ledger audit — 2026-10-19

Strict runner on staged `d5cb5e3` remains red at 0 PASS/1 FAIL/51 BLOCKED/
10 NOT IMPLEMENTED. It currently reports a static matrix without executing
tests; twenty listed test paths are absent. L4 is mapping distinct 62-gate
requirements to real same-HEAD cases, leaving uncovered gates non-PASS. E3
11/11 and Team B/G2/key staging evidence are useful inputs, not a substitute
for the full integrated release or final independent review.

## Staged E3 debug/release cross-crate checkpoint — 2026-10-19

L4 pushed `d5cb5e3`. The real ABI-v10 cross-crate harness now runs all 11
scenarios in both Debug and Release with 0 blocked/failed. It uses checkout-
local Moss/Margo, removes provider source for the source-free consumer cases,
executes exact native outputs, validates H3 checked private-chain IR with
legacy statements stripped, and asserts structured provenance/effect errors.
Migration builds/selects a real v9 artifact; metadata tampering exercises
five non-no-op mutations; cache testing proves cold miss/warm reuse and a
changed provider digest with exact layout rejection. Self-test/fault
injection, feedback, syntax and diff checks pass. This is staged evidence;
integrated exact-head CI/L6 review and strict 62/62 remain open. G PR #10
and post-H2 target core CI are still running.

## Team B staged Gate 10 executable checkpoint — 2026-10-19

L4 pushed `9f600fa`. The Team B acceptance runner now proves provider-owned
nominal KeyProof transport into two independently built source-free native
consumers (`7` each) and rejects a same-layout key identity conflict at exact
line 22. Its existing codec/cache check remains intact. The runner reports
23 PASS/0 PENDING/0 FAIL; activated G2 is 11/11 and activated Map/Set fixtures
30/30. This is staged, non-target evidence; strict integrated release remains
open. H2 target and G PR CI run, while S awaits refresh after G.

## S pre-intake checkpoint — 2026-10-19

L4 pushed fresh H2-target-based S reconciliation `c5640fd`, retaining pinned
S ancestry. Team A's missing-member/provenance assertion now reflects the
specific S/G2 diagnostic and original provider source, with a separate
incompatible-method-argument negative. A source-removed Range/FileIO backend
failure was fixed by gating explicit-module `main` to process-root builds;
native validation runs the generic and Range/FileIO consumers. Nine stale key
fixture `current:` lines were corrected without changing source or `expect:`.
Optimized S/Team A/H2/Type IR, default key 30/30, feedback and diff checks
pass. S awaits a refresh onto the post-G reviewed target before its draft PR.
G PR #10 and post-H2 target CI remain in progress. L4 returned serially to
staged B Gate 10 acceptance-harness work.

## G draft intake and S preparation — 2026-10-19

The sole L4 pushed target-based G head `6859041` with pinned G ancestry and
no compiler-source delta versus H2 target `56d68ce`. Local optimized,
Type IR, G-A3, visibility, H2/G2 declaration authority, feedback and exact
diagnostic checks pass. Draft [PR #10](https://github.com/kuttyb/moss/pull/10)
has exact-head CI `39187310886` running; post-H2 target CI `39186996122`
also runs. G review/merge waits for both required green results. L4 is now
preparing a fresh S reconciliation branch serially; no target/main direct
push or release claim.

## H2 target merge and H3 guard checkpoint — 2026-10-19

H2 draft PR #9 passed four exact-head jobs, received combined L6 approval,
was marked ready and merged normally to `phase25-integration` at `56d68ce`.
Reviewed head `9d7d354` and prior target `f63e03b` are its two parents.
Post-merge target CI `39186996122` is queued; no further target intake until
it passes. No `main` merge. Separately, L4 pushed H3 staging `4a57515` to
bind guard lifting to an actually emitted, validated private checked closure;
the unrecorded-root negative rejects, source-removed Int/String consumers
still run, and activated G2 11/11 plus Map/Set 30/30 pass. H3 is not yet
target-integrated or accepted for strict release.

## H2 exact-head L6 review — 2026-10-19

PR #9 at `9d7d354` passed four exact-head CI jobs in `39180625856`; the
post-H/T target run `39180139206` passed its four jobs. Combined L6 review
approved the bounded H2 repair after inspecting 36 negative declaration
boundary checks and the seven-file target diff. The verdict and remaining
scope are in `TEAM_I_L6_REVIEW_H2.md`. At this checkpoint PR #9 is still
draft and unmerged; H1/H3 and final acceptance remain open.

## Gate 10 staged-run correction — 2026-10-19

The preceding 10/11 G2 result omitted the required joint-activation switch
and therefore ran legacy interim Gate 10 assertions. Re-running the **same
`9621fca` staging head** with `MOSS_G2_ACTIVATE_JOINT=1` passes all 11/11,
including Gate 10. The suspected regression is withdrawn. The H3 export
guard still needs its exact emitted-closure review fix before target intake.

## H3 nested private-chain staging checkpoint — 2026-10-19

L4 pushed `9621fca`. Source-removed Int/String consumers execute from one
provider's public `fresh` and two private helper Summary/CheckedBody pairs;
all six legacy generic statement lines are stripped before re-sealing and
execution. A missing private body fails pre-link, and unsupported three-
statement H3 remains guarded. Compiler, focused ABI/typed/Team A/G2 authority
and both key fixture ledgers 30/30 pass. The broader staged G2 conformance
runner is **10/11**, with Gate 10 `identity_conflict` and
`compiled_only_custom_key` failures; L4 is fixing this regression before
intake. Combined L6 also requested exact emission/validation linkage for
lifting the H3 guard. H3 is not yet accepted or integrated. H/T target
post-merge CI is green; H2 PR core CI remains pending.

## H3 direct checked-IR importer checkpoint — 2026-10-19

L4 pushed `b4c0450` and `cfc5bfa`. A bounded structural decoder validates
canonical H3 Let/Return bodies and their TypeRefs/targets; source-free import
attaches sealed records and projects validated IR to the checker/native path
before inference. The direct provider's re-sealed interface has all legacy
`generic_stmt` lines removed, yet two physically source-removed consumers
still build and run. Optimized compiler, typed API, focused decoder/ABI,
H3 default-v9 baseline and both key fixture modes 30/30 pass. The nested
private-helper chain is still guarded in staged v10, so H3 is not accepted.

## H3 bounded direct transport checkpoint — 2026-10-19

L4 pushed staging `c5446b5`. Live compiler emission now includes versioned
nominal, summary, checked-body and provenance records for a direct
two-statement constructor; two independent consumers execute after provider
source removal. Compiler, focused ABI/typed suites, H3 default-v9 baseline,
both 30/30 key fixture modes and feedback pass. Whole-payload UTF-8 checking
was removed for byte-framed binary records, and open summary ordinals are
validated for density. Consumers still execute legacy generic statements, so
H3 remains open; the nested private-helper chain still fails closed in
staged-v10 until checked-body import/execution and dependency closure work.

## H3 versioned record substrate — 2026-10-19

L4 pushed `50bf7ba` and `ffa945d`: recognized ABI-v10 v1 records fail closed
on malformed decoding, and a private checked-body record binds exact rich-IR
bytes, body hash and callable/nominal dependency hashes to local summaries and
templates. Focused adversarial, checked-body IR, compiler and feedback checks
pass; default-v9 H3 baseline still executes two consumers. The staged-v10
guard remains active until structural IR decoding and live emission/import
produce real source-removed two-consumer execution. No H3 or target acceptance
is claimed.

## H3 callable hash-contract checkpoint — 2026-10-19

L4 pushed staging `b4bf765`. The ABI-v10 record validator now requires
versioned checked-body hashes and matches local callable dependency hashes
across public and private summaries. Focused positive and tampering tests,
ABI suites, compiler build, H3 default-v9 baseline, both key ledgers 30/30
and feedback pass. L6 is checking method-template coverage and duplicate
private-record handling. This is record-validation groundwork: real H3
versioned emission/import and source-removed two-consumer execution remain
unfinished, and the staged guard stays active. Target H/T and H2 PR CI each
have three green jobs with core regression still in progress.

## Canonical nominal ABI codec checkpoint — 2026-10-19

L4 pushed `029eef9` and L6-review correction `e5db01c` on the staging
branch. Staged ABI-v10 records now preserve frozen `nominal-template-v1`
declaration IDs; only contract/representation/instance hashes use SHA-256.
The first patch accidentally skipped concrete-instance validation. Combined
L6 review caught it, and the follow-up dispatches template and instance
records separately with tampered-instance and unknown-versioned-record
negatives. ABI staging/adversarial, checked-body IR, nominal identity and
both key ledgers pass. H3 record emission/import remains open; no target/main
acceptance claim.

## H3 checked rich-IR checkpoint — 2026-10-19

L4 pushed staged branch `d269319`. H3's two-statement local-result chain now
has checked rich `Let`/`Return` IR, an exact private call edge, and a canonical
nominal-constructor node with field/type validation. Focused hash, dependency
and forged-target tests pass, along with compiler build, typed API, staged G2
11/11, default-v9 two-consumer H3 baseline, feedback and diff checks. The
guard still rejects staged-v10 source-free export until canonical ABI-v10
dependency/body records and importer validation exist. H3 remains open.

## H3 typed-summary checkpoint — 2026-10-19

L4 pushed staged branch `71991a0`. Straight-line local-result flow now
produces complete open summaries for `fresh` and its two private helpers,
carrying `Box[value: bound var 0]` and exact transitive call edges. Typed API,
Team A, staged G2 11/11, default-v9 H3 two-consumer guard, feedback and diff
checks pass. The staged-v10 guard still rejects Scenario 6 because rich checked
body IR and canonical private dependency/provenance records are unfinished.
No H3 acceptance or target/main release claim.

## H3 fail-closed checkpoint — 2026-10-19

L4 pushed staged branch `466eb7a`. A one-provider, source-removed Int/String
two-consumer reduction ran via the old generic text IR, but all three typed
summaries were `missing_typed_facts` and the interface had no private semantic
dependency/provenance records. Staged v10 now rejects the unsupported
multi-statement helper chain with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`
before interface/link; default v9 still executes both consumers. Focused
compiler/typed API/Team A/G2 11/11/key 30/30 checks pass. Cross-crate
Scenario 6 is now intentionally red (10/11 staged scenarios); H3 remains
open for genuine checked IR and exact dependency records. L4 continues that
work; no target or main release claim.

## H2 draft intake — 2026-10-19

L4 pushed target-based H2 branch `phase25-i-h2-intake` at `9d7d354`, direct
parent target `f63e03b`. Its patch matches staged H2 `0574de4` exactly by
stable patch ID. Local boundary, typed API, Team A, feedback and diff checks
pass. Draft [PR #9](https://github.com/kuttyb/moss/pull/9) has been opened;
automated exact-head CI and combined L6 review remain pending. The worker is
back on H3. H/T target push CI `39180139206` has harness and examples green,
with semantic/core still running. No further target/main merge.

## H1 callable/effects checkpoint — 2026-10-19

L4 pushed `phase25-i-map-staging` at `c4b0729`. Inspect exposes a typed
Callable requirement and concrete pure/raising exact-ID specializations close
their distinct checked effects. The generic summary remains unavailable while
callback effects are unresolved. Staged-v10 provider export and source-free
import fail before link with a structured effect diagnostic and provider
provenance; default v9 remains valid. Typed API, Team A, G1, staged G2 11/11,
both key fixture ledgers 30/30, feedback and diff checks pass. H3 and strict
release acceptance remain open. L4 now prepares target-based H2 intake.
Post-H/T target CI run `39180139206` is in progress at `f63e03b`.

## H/T baseline target merge — 2026-10-19

PR #5 merged normally into `phase25-integration` at
`f63e03b18ac07c66a0f21594220dab7a7512d512`, with prior target `fbd2f72`
and exact reviewed head `9ddf9a4` as parents. Four PR jobs were green before
combined L6 approval; the target's post-merge CI remains pending. H1–H3 and
all later Phase 25 intake/release gates remain open. No main merge.

## H/T exact-head review checkpoint — 2026-10-19

Draft PR #5 at `9ddf9a4` passed all four required jobs in workflow
`39174097677`. The combined Coordinator/L6 reviewer approved **H/T baseline
intake only** after checking pinned ancestry, merge-specific resolutions and
target-range whitespace. The exact review and open H1–H3 scope are in
`TEAM_I_L6_REVIEW_H_T.md`. The final release job was skipped as designed for
this PR. The target has not yet been merged at this checkpoint; no main merge.

## H1 structured index checkpoint — 2026-10-19

L4 pushed `phase25-i-map-staging` at `e395a10`. Live generic indexing now
exports structured receiver/index/result/access/origin relations, with a
two-index guard against collapsing distinct requirements. Missing generic
members report `METHOD_NOT_FOUND` in source-backed and source-free cases;
present members with incompatible arguments retain `TYPE_MISMATCH`. Typed
semantic API, Team A, staged G2 11/11, default and activated key fixtures
30/30, feedback and diff checks pass. Callable argument/result relations and
H3 remain open. H/T draft #5 still awaits its final exact-head core CI job;
no target/main integration or L6 verdict is claimed.

## Activated key-surface checkpoint — 2026-10-18

L4 pushed `phase25-i-map-staging` at `9e7c729`. Opt-in ABI-v10 Map/Set
fixtures pass 30/30 in Fast Debug, native debug and native release, with G2
11/11. The default-v9 30-case ledger now matches observed outcomes; its seven
updated `current:` entries were reproduced on the clean pre-Option-B compiler
`dd7f97f`, without changing fixture source or `expect:`. Negative probes still
reject a later contradictory Map key pin and an unresolved `echo []`. This is
staging evidence only: H1/H3, target-based PR checks, strict 62-gate release
and final independent review remain open. L4 retains exclusive compiler
ownership for H1 then H3. H/T draft #5 still has three green exact-head CI
jobs and core regressions running; no target/main merge has occurred.

## Staged Float Map checkpoint — 2026-10-18

L4 pushed `phase25-i-map-staging` at `dd7f97f`. Opt-in ABI-v10 Float Map uses
shared `MossMap<f64,V>` and Team S scalar equality/hash with the same proof ID
as Float Set. Signed-zero/NaN and eager-key-snapshot fixtures pass Fast Debug,
native debug and release; source-free proof transport and staged G2 11/11 also
pass. Default v9 still rejects Float Map. Activated key fixtures are **27/30**;
only Option B 09b/09c/09g remain. L4 is working those separately. H/T core
CI remains the last prerequisite to its combined L6 review.

## Activated key-fixture checkpoint — 2026-10-18

L4 pushed staged branch `phase25-i-map-staging` through runner fix `bd1547e`
and proof-negative fix `69b042b`. The activated 30-case Map/Set suite now
links shared runtime native artifacts and passes 25/30. True two-consumer
sealed-proof conflict still proves `KEY_PROOF_COHERENCE_MISMATCH`. The legacy
free-only 05 fixture now expects `KEY_PROOF_INVALID`, because its provider
declares no canonical methods; a local free override of valid provider methods
remains a coherence error. Both pass, with the frozen-method rationale in the
fixture docs. Remaining failures: Float Map 01/07 and Option B empty-Map
09b/09c/09g. L4 is working Float Map; root continues exact-head H/T CI gating.

## Staged canonical key-proof checkpoint — 2026-10-18

L4 pushed `phase25-i-map-staging` at `7d9057e`. Staged ABI-v10 nominal
KeyProof records now cross source-free provider/consumer interfaces with exact
method/effect/semantic-hash checks, one proof ID in Map/Set operation identity
and the native build fingerprint. Two independent consumers link and execute
after provider source removal; resealed semantic-hash tampering rejects with
`KEY_PROOF_INVALID`, and valid conflicting proof identities reject with
`KEY_PROOF_COHERENCE_MISMATCH`. Activated G2 remains 11/11. The existing
explicit-module runtime and Phase 20 FileIO probes pass after a process-root
linkage repair. The older activated key-protocol fixture runner still needs
its staged runtime extern and is correctly reported not green; L4 is repairing
that harness next. This branch is a candidate, not target integration or
production ABI-v10 acceptance.

## Staged Set checkpoint — 2026-10-18

Sole L4 pushed `phase25-i-map-staging` at `dd44d5c`. Opt-in ABI-v10 Set now
implements the L7-frozen four-operation source API on shared `MossSet`, with
native and Fast Debug parity for nominal Map+Set fixture 08, Float Set fixture
02, and an Int/String/Char/Bool probe. Default v9 continues to reject Set.
Missing, impure and divergent nominal key proofs fail at Moss checking with
`KEY_PROOF_INVALID`; proof validation now occurs after main-body discovery.
Full PR/repository checks and serialized canonical proof coherence remain
open. L4 retains exclusive compiler/ABI edits for that proof slice; root owns
Git/CI coordination. No staging branch has been integrated into the target.

H/T draft #5 exact head is `9ddf9a4`. Its refreshed workflow `39174097677`
has semantic, examples and harness jobs green, with core regressions running.
Combined L6 review and target integration wait for that last exact-head job.

## Staged Map Gate 10 and current lease — 2026-10-18

`phase25-i-map-staging` checkpoint `4165e75` extends staged shared-runtime
checkpoint `641a577`. Its opt-in ABI-v10 nominal Map lowering passes a clean
provider build and all 11 activated G2 conformance gates, including the two
source-free Gate 10 consumers and identity-conflict negative. It remains an
unreviewed, non-target-based staging branch; a passing conformance runner does
not establish the complete serialized MapKeyProof, all 62 strict acceptance
gates, Set, or production cutover. Feedback validator and whitespace checks
pass. See `.codex/CURRENT_STATUS.md` for precise recovery state.

The single active L4 worker owns compiler edits in that worktree while root
handles combined L6 coordination/review and Git/CI; do not start another
compiler writer. L4 is implementing the L7-frozen four-method Set source API.
Final independent adversarial L6 review is deferred until implementation is
complete, with L4 idle during that review. H/T draft #5 advanced to `9ddf9a4`
after trimming an inherited Team T closeout trailing blank line;
target-range `git diff --check` now passes. The earlier three-green/one-running
CI result applies only to old head `bf1df10`; all four checks must rerun on
`9ddf9a4` before L6 review. No target or main merge has occurred.

## L7 Set source-surface decision — 2026-10-18

L7 selected the recommended Phase 25 option: freeze the existing provisional
Set API as `Set()`, `insert(k)`, `contains(k)`, and `values()`. Team I must deliver
Set in this phase using the same canonical key proof and shared runtime kernel
as Map. This resolves the source-surface question raised after a live
`moss-0.1` bootstrap and a minimal `Set[Int]` probe showed no implemented Set
surface. The accepted spelling is the one already recorded as provisional in
`docs/phase25/TEAM_S_KEY_PROTOCOL_V1_SCALARS.md` and
`tests/phase25/key_protocol/README.md`; implementation and acceptance remain
pending. No Set deferral or second key protocol is authorized.

## L7 diagnostic decision

L7 selected option 1 for the proven `echo []` conflict:
`ECHO_DISPLAY_UNSUPPORTED` applies to unresolved collections at echo boundaries;
other unresolved values retain `TYPE_AMBIGUOUS_OPEN_RESULT`. Sole L4 is applying
the narrow contract and conformance update in S scratch.

## G intake scratch recovery

Sole L4 pushed `76420353eb2c83b56a90051f2865b00d3e530547`
on `phase25-i-g-intake`. The normal merge preserves exact G ancestry. Deliberate
resolution of 28 hunks produced no compiler/header diff from H because H already
contains or supersedes G's changes. Local Type IR, nominal identity, visibility,
and conformance checks pass except the intentionally pending custom-key Gate 10
(10/11). No PR, review, or integration is claimed. Refresh from the reviewed H
target before draft submission; see `docs/phase25/G_INTAKE_RECONCILIATION.md`
on the G branch.

## L7 Team I staffing override — 2026-10-18

For this integration task, use exactly two active agents: one combined L6
Coordinator/Reviewer and one L4 implementation worker. Work is serial through
that sole worker, preserving exclusive compiler ownership. The independent L6
adversarial reviewer runs once at the end, after implementation is complete;
idle the L4 worker during that review to keep at most two agents active.
This L7 instruction supersedes the default L5 headcount and intermediate
adversarial checkpoints below. Keep draft PRs, automated isolated regression
checks, exact-SHA L6 review, normal integration commits, and no main merge.
Minimize repeated chat, duplicate CI polling and redundant review work.

ABI worker stopped at L7 instruction. Sole L4 inherits its frozen work serially:
strict PR #6 `ccf2ca7c6628882617ad5f830fce19b463bcce23`, source-free PR #8
`9f1ca499f57f935d34b9462c252f12043cdc677c`. Historical two-L5 concurrency
below describes earlier observed work, not current staffing authorization.


L7 authorized execution on 2026-10-18. Target `phase25-integration`; initial
SHA `fbd2f729caf1a96c3ffe184068e88ba6b09288cc`. No main merge or ABI production
cutover. Coordinator also serves as L6 PR Reviewer; independent milestone
adversarial review remains separate.

## Intake and dependency DAG

Verified after `git fetch origin`: H/T `2d457245061c75f3a4652a8851134322f1fa8445`,
G `2938d257bf10bbb11bd221fa14237ddcd1e0702b`,
S `53d04fbf48c007d912a47836959c488bcbb6c450`,
B `755f13172c37447bca981d1fefbb885ec46f3a59` match the execution plan.
H/T intake first, followed by distinct G, S, B deliveries on fresh branches
from the reviewed target. Historical prep rehearsal is not current evidence.
H1/H2/H3 and all release gates remain open until executable integrated proof.

## File leases

| Owner | Exclusive write scope | Branch/worktree |
| --- | --- | --- |
| L5 compiler | `src/moss.cpp`, `src/ast.hpp`, `src/type_ir.hpp`; compiler intake and associated tests | `phase25-i-h-intake`, `tmp/phase25-i-compiler` |
| L5 ABI/harness | ABI headers, runtime, `tests/phase25/integration/**`; initially audit and independently justified harness fixes | `phase25-i-abi-preparation`, `tmp/phase25-i-abi` |
| L6 Coordinator | root status, organizational guidance, this record, integration/review provenance | root checkout |

Whole-history H intake includes ABI files; runtime/harness worker must avoid
editing those files until their baseline interface is handed off. No concurrent
compiler writers. Each worker owns its feedback workstream. Two actual L5
agents instantiated; no optional third worker.

## Evidence and gates

Startup bootstrap returned `moss-0.1`. GitHub connector repository access
confirmed; `gh` CLI absent, connector supports draft PR and workflow inspection.
AGENTS already explicitly permits authorized normal push on the fetched branch;
no prohibiting rule remains. Push must preserve published history.

No PR review, integration, regression PASS or final acceptance is claimed yet.
Required CI runs four isolated groups; semantic gates missing before intake are
blocked, never green. Final strict 62-gate release is distinct from PR checks.

## Initial live findings

- H/T merge reports no compiler conflict; documentation conflicts are current
  status and three architecture documents. These still require deliberate
  reconciliation and regression evidence before integration.
- ABI worker identified the strict CLI bypassing `validate_gate_evidence`; a
  bounded harness integrity repair is in progress. Cross-crate source-removal
  coverage requires additional production-path work; existing status is not E3.

## Contract interpretation

The execution plan assigns typed ABI payloads, runtime linkage, Gate 10 and
source-free specialization to Team I. Historical D3/D4 ownership questions do
not authorize deferral and do not require a new language-design decision.
ABI-v10 production default cutover awaits L7 final acceptance; staging must
exercise the actual v10 emitter/importer/linker with source removal. Fail-closed
rejection is required for negative cases but is not positive H3/E3 evidence.

## Open integration issues

| ID | First evidence / owner | Acceptance consequence |
| --- | --- | --- |
| I-001 | L5 ABI audit: strict CLI bypasses evidence validator | Repair + CLI fault regression; no narrative-only strict acceptance |
| I-002 | L5 ABI audit: cross-crate scenarios 2–5 remain source-backed, 6 lacks execution, 7 checks name substring | Replace with real source-removal, link/run and exact provenance evidence before E3 |
| H1–H3 | Inherited L7 execution-plan findings | Team I owns structured requirements, stale AST projection and true generic source-free proof |

These are open implementation/validation defects, not architectural escalation.

## Inherited H2 live reproduction

Coordinator verified against H/T intake header before any authority fix:
construct each `Param`, `Method`, `TraitMethod`, `Handler`, `Function`; bind
`Type::primitive("int")` via its `bind_type`, `bind_return_type` or
`bind_reply_type`; erase the string projection (`clear()`, optional `reset()`
or assign empty string); call `assert_consistent()` and count `logic_error`.
Built with `g++ -std=c++17 -O0 -I tmp/phase25-i-compiler/src`.
Observed `rejected erased projections: 0/5`, exit 1 (expected 5/5).
Source cause: assertions guard on nonempty/present projection before comparing
against bound TypeRef. This verifies inherited L7 H2; no new discovery credit.
Focused regression and semantic/ABI boundary enforcement remain required.

## Submission ledger

| Submission | Exact implementation SHA | State | Review |
| --- | --- | --- | --- |
| [H/T intake PR #5](https://github.com/kuttyb/moss/pull/5) | `5cfdc1630d67506146e4adcc52628e508ffdc7c5` | Draft; automated workflow 39165091036 running all four isolated jobs | Not requested; CI pending |

Worker-local optimized build/bootstrap and G1/G2/G3 checks passed on H intake;
these do not substitute for PR checks. Coordinator confirmed four separate CI
jobs in progress. No target merge yet.

## Recovery checkpoint policy

L7 requested periodic crash/token recovery on 2026-10-18. Coordinator commits
and pushes `.codex/CURRENT_STATUS.md` at meaningful boundaries (PR submission,
review, integration or new validated blocker). Worker branches are committed and
pushed independently; target remains unchanged until required CI/L6 approval.
PR #6 latest recovery head is `ccf2ca7c6628882617ad5f830fce19b463bcce23`
(feedback-only follow-up to `afcf693`); exact-head CI is pending.

## Source-free harness recovery handoff

[PR #8](https://github.com/kuttyb/moss/pull/8), exact head
`ac317c65caecac0d9916d58318106fc13225939e`, makes source removal and
consumer execution real; baseline release is **0 PASS / 1 BLOCKED / 10 FAIL**,
exit 1. It is not integrated/reviewed. ABI worker now owns
`phase25-i-cross-crate` in its existing isolated worktree. Frozen strict #6 remains
`ccf2ca7c6628882617ad5f830fce19b463bcce23`. Its current semantic CI failure is
the accurately blocked missing pre-H G1 suite. H #5 has three green checks,
core still running at checkpoint. Coordinator [PR #7](https://github.com/kuttyb/moss/pull/7)
contains recovery/policy plus full-history CI checkout change; awaiting H target.

## H2 scratch recovery

Sole L4 pushed `0574de41d3c13d6af0448a80ad3708ce7a91e402` on
`phase25-i-h2-erased-projections` (`tmp/phase25-i-h2`). Local full compiler
build/bootstrap and G1/G2/G3 pass; direct regression rejects all 36 empty/reset
boundary cases. No PR/review/integration yet. H #5 remains unchanged while
core CI runs. After H integration, refresh H2 from approved target and open
its draft PR; all new automated gates still required. Actual source details
and next steps are committed in `docs/phase25/H2_DECLARATION_PROJECTIONS.md`.
