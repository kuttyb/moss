# Moss Phase 25 — Controlled Promotion to main (ABI-v9 Default)

**Status:** L7 release playbook; instructions only. Not authorization to move main.
**Repository:** `kuttyb/moss`
**Release source branch:** `phase25-integration`
**Pinned candidate:** `48d4d0d20d07c413f870b269d7e51bbc2c6beed1`
**Pinned pre-release main:** `fa3a340a0a6652568b97869489da1a84038b9857`
**Separate documentation-only branch:** `phase25-main-delivery-plan` (this file only; DO NOT use this branch as the code release target).
**Already-started Phase 25.1 branch:** `phase25-1-v10-readiness` (continue separately).
**Git tag names:** `phase25-pre-main` (recovery anchor) and `phase25-delivered-v9` (validated delivery).

## 0. L7 release decision

Deliver the exact reviewed Phase 25 integration implementation to `main` via a **fast-forward-only promotion**, retaining the full existing commit history. No squash, rebase, cherry-pick, synthetic merge commit, or resolution changes.

- The **default** module native ABI must remain **v9**. In absence of staged environment variables, `moss`/`margo` builds and loads v9 interfaces and uses the existing v9 source-free path.
- ABI-v10 remains **opt-in** through the existing `MOSS_STAGED_ABI=v10` flag, and must still reject incompatible v9 artifacts and unsupported checked bodies without legacy fallback.
- Phase 25.1 has a separate scope and remains outside this promotion. **Do not** merge `phase25-1-v10-readiness`, the documentation-only release-plan branch, or any unreviewed later changes.
- No generic/compiler implementation fixes are authorized in this operation. Any required code or test adjustment means **STOP**, create a separate feature branch, obtain L7 review, repin the target and repeat preflight. No fixing tests by excluding them or weakening checks.
- The previously reported `make check: 131/131, 0 failed, 0 skipped` is an important baseline, **not a substitute** for a fresh pre-delivery run on the exact pinned release SHA.
- The agent may do preflight and tests autonomously. **MANDATORY L7 STOP before pushing `main`.** It may create the pre-delivery tag only after L7 authorizes the promotion.
- Never push directly to `main` if it diverges; never use `--force`, `--force-with-lease`, `git reset --hard` or branch deletion as a routine release step.
- Use one release coordinator/integrator. An independent read-only adversarial review of the release diff and tags is welcome, but do not run competing branch writers.

## 1. Deliverables and invariants

1. An independently retrievable **annotated Git tag** `phase25-pre-main` pointing exactly to the pre-delivery main commit.
2. `origin/main` pointing to exactly the validated Phase 25 candidate commit, fast-forward only.
3. A clean verification that ABI-v9 is still default, v10 still opt-in, and both existing v9 and admitted v10 test pathways work.
4. An **annotated Git tag** `phase25-delivered-v9` pointing to the exact delivered main commit, published **only after post-push validation succeeds**.
5. A delivery report with complete old/new/tag SHAs, exact commands, pass/fail results and checked artifact byte/effect evidence.
6. `phase25-integration` remains preserved, and `phase25-1-v10-readiness` remains intact, separate and unmoved.

**Release must preserve:**
- All Phase 25 language/type-checking/compiler changes.
- ABI-v9 as the ordinary default and prior source-free SDK support.
- Strict staged-v10 per-callable admission and no `generic_stmt` execution fallback.
- Existing Fast Debug and SDK restrictions; no claims of full v10 readiness or `from_range` support.
- Existing source-free metadata protections, provider/provenance diagnostics, negative tests, runtime/cache behavior and compiler execution profiles.

## 2. Gate P0 — Immutable release inputs and clean environment

From a checkout with proper push permissions:

```bash
git fetch --prune --tags origin
git status --porcelain
git rev-parse origin/main
git rev-parse origin/phase25-integration
git rev-parse origin/phase25-1-v10-readiness
git merge-base --is-ancestor origin/main origin/phase25-integration
git rev-list --left-right --count origin/main...origin/phase25-integration
git diff --check origin/main origin/phase25-integration
```

**Require:**
- Remote main exactly `fa3a340a0a6652568b97869489da1a84038b9857`; remote integration exactly `48d4d0d20d07c413f870b269d7e51bbc2c6beed1`. These are pinned as of this plan; **if either ref changes, STOP and ask L7 to repin and reassess**, even if the change is status-only.
- `git rev-list` shows **0 behind, 447 ahead** at these pinned commits.
- Clean worktree or fresh isolated checkout, no unstaged changes. If the active user's worktree is dirty, use a separate `git worktree` and do not destroy or clean their work.
- No unexpected deleted/renamed public API or SDK assets, no unresolved conflict markers, no new ABI-default staging flag in startup scripts. Review **the full local diff**, not a truncated hosted comparison.
- Release refs `refs/tags/phase25-pre-main` and `refs/tags/phase25-delivered-v9` must not exist locally or remotely, unless they already point **exactly** to the intended commits and L7 explicitly agrees to reuse them. **Never move an existing tag.**

Review/summarize:
```bash
git log --oneline origin/main..origin/phase25-integration
git diff --stat origin/main origin/phase25-integration
git diff --name-status origin/main origin/phase25-integration
git diff --name-status --diff-filter=D origin/main origin/phase25-integration
git ls-remote origin refs/heads/main refs/heads/phase25-integration refs/tags/phase25-pre-main refs/tags/phase25-delivered-v9
```

The integration has a large history (447 commits); do not assume “fast-forwardable” means the diff is small. Provide risk-class summaries: compiler/type IR; runtime/linkage; semantic changes; Fast Debug; SDK; tests/tooling; docs; agent metadata. Flag surprising production-facing deltas for L7.

## 3. Gate P1 — Preflight on the exact candidate SHA

Check out the pinned candidate in a **dedicated clean release-validation worktree**, not the user's current main worktree:

```bash
git worktree add --detach ../moss-phase25-release-preflight 48d4d0d20d07c413f870b269d7e51bbc2c6beed1
cd ../moss-phase25-release-preflight
test "$(git rev-parse HEAD)" = "48d4d0d20d07c413f870b269d7e51bbc2c6beed1"
git status --porcelain
```

Clear all ambient opt-ins for **default-v9** checks; let individual staging tests enable them explicitly. The following are a minimum, not permission to skip discovered failures:

```bash
env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT make check
env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT make examples
env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT python3 tests/phase25/integration/sdk_compatibility_smoke.py --smoke
env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT python3 sdk/advanced-data-acceptance/check_source_free.py
env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT python3 sdk/encoding/tests/check_source_free.py
python3 tests/tooling/check_phase25_mixed_provider_admission.py
python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py
python3 tests/tooling/check_phase25_d4_collection_source_free.py
```

Tests must run against the exact commit and freshly built compilers/artifacts, not stale build caches. The smoke runner may report documented, justified engine limitations; report them separately, not as unexpected failures. If a suite requires setup, run its documented prerequisites and report the steps; do not skip silently.

**ABI-mode sanity check (mandatory, independently verified):**
- In an environment **without** `MOSS_STAGED_ABI` or other `MOSS_PHASE25_*` enabling variables, build a small actual provider and inspect the emitted `.mossi` header: `native_abi 9`. Compile/run its source-free consumer using its matching v9 artifacts.
- In **separate** build/artifact/cache directories with `MOSS_STAGED_ABI=v10`, build a supported checked-body provider/consumer; inspect `native_abi 10`. Remove provider source, poison/remove selected `generic_stmt`, and prove native execution + checked-body projection; verify unsupported selected generic callables reject with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`.
- Prove a v9 artifact is not silently loaded by v10 and vice versa; require the established `SOURCE_FREE_ABI_REBUILD_REQUIRED` behavior. No accidental cache reuse across modes.
- Show the flag is **opt-in only**: a fresh shell without it rebuilds v9, not v10.
- No claim that v10 `bytes.from_range` works; it remains a Phase 25.1 task.

**P1 exit:** `make check` reports **131/131 PASS, zero failed, zero skipped** for the pinned integration tree; `make examples`, the required SDK v9 source-free checks, and focused staged-v10 checks are green with documented expected negatives. Build warnings may be reported; do not globally suppress or hide newly failing tests. If any regression or unknown result appears, STOP without promotion.

## 4. Gate P2 — Independent delivery review and L7 GO/NO-GO

Produce a preflight report **before mutating main or creating any release tags**:

- Exact `origin/main`, `origin/phase25-integration`, and candidate SHA; ahead/behind status.
- Full diff summary (including production file deletes/renames), unusual changes and any risk approvals.
- Preflight commands, machine environment/profile, outcomes/log locations, totals/expected negatives, any warnings/exceptions.
- Explicit **default ABI-v9** artifact evidence and opt-in ABI-v10 artifact evidence, negative mode-mismatch test, SDK v9 source-free compatibility.
- Proof `phase25-1-v10-readiness` is separate and not part of this promotion.
- Proposed pre- and post-delivery tags and the exact commits they will reference.
- Confirmation that no code, test or documentation was modified during verification.
- Explicit `GO/NO-GO` recommendation.

**STOP and request affirmative L7 authorization to promote exactly the pinned candidate to main.** Do not interpret this playbook or earlier enthusiasm as authorization to push the remote main ref.

## 5. Gate P3 — Pre-delivery recovery tag, then atomic fast-forward push

**Run only after L7 GO**, with exclusive coordination and no other main writers.

Fetch/recheck exact remote refs **immediately** before every mutation:

```bash
git fetch --prune --tags origin
git ls-remote origin refs/heads/main refs/heads/phase25-integration
git merge-base --is-ancestor origin/main origin/phase25-integration
```

Require remote heads still equal pinned SHAs. If changed: **STOP**. If `main` branch rules prohibit the ref update, stop and request a protected-branch/PR mechanism that **preserves the exact commit graph**, without bypassing policy.

Create and publish an **annotated** recovery tag BEFORE updating `main`:

```bash
git tag -a phase25-pre-main fa3a340a0a6652568b97869489da1a84038b9857 \
  -m "Last main commit before Moss Phase 25 promotion (ABI-v9 default)"
git push origin refs/tags/phase25-pre-main
git rev-list -n 1 phase25-pre-main
git ls-remote origin refs/tags/phase25-pre-main 'refs/tags/phase25-pre-main^{}'
```

For annotated tags, compare the remote **peeled** `^{}` commit to the intended old main SHA; the annotated tag-object SHA is different from its target commit. Fail if the remote tag is absent, ambiguous or incorrectly pointed.

Only with the recovery tag verified, push the exact reviewed candidate commit into `main` **as a normal fast-forward**:

```bash
git push origin 48d4d0d20d07c413f870b269d7e51bbc2c6beed1:refs/heads/main
git ls-remote origin refs/heads/main
```

The remote head MUST now be exactly `48d4d0d20d07c413f870b269d7e51bbc2c6beed1`. No `--force`, no synthetic merge, no rebased replacements, no default-ABI toggles. If the push rejects because main changed, stop for L7; do not retry with a force variant.

## 6. Gate P4 — Post-push validation on the real main ref

Fetch and verify that `origin/main` exactly equals the target SHA. Validate from a fresh detached worktree at the **remote main ref** (not merely the old preflight worktree). Re-run:

1. `env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT make check` — expect **131/131, 0 failed, 0 skipped**.
2. `env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT make examples`.
3. `env -u MOSS_STAGED_ABI -u MOSS_PHASE25_EMPTY_VECTOR_EMIT python3 tests/phase25/integration/sdk_compatibility_smoke.py --smoke`.
4. Default-v9 compiled-only provider/consumer and artifact header sanity check (native ABI 9), including v9 advanced-data and encoding source-free checks.
5. Focused staged-v10 source-free corpus, SDK collection execution, mixed-provider fail-closed admission, ABI mismatch diagnostics and correct cache isolation.

Compare exact output and ABI-mode behavior with preflight. Log all commands and the final `main` SHA. No functional changes during validation.

**If post-push regression fails:** Stop. Mark main delivered candidate as **unverified**, do **not** create or publish `phase25-delivered-v9`, and immediately notify L7 with reproduction and the `phase25-pre-main` recovery tag. Do not automatically reset main or roll back by force.

## 7. Gate P5 — Publish the Phase 25 delivery tag

Only after P4 PASS (not merely after a successful `git push`), publish the immutable annotated delivery tag:

```bash
git tag -a phase25-delivered-v9 48d4d0d20d07c413f870b269d7e51bbc2c6beed1 \
  -m "Moss Phase 25 delivered to main; ABI-v9 production default, ABI-v10 opt-in"
git push origin refs/tags/phase25-delivered-v9
git rev-list -n 1 phase25-delivered-v9
git ls-remote origin refs/heads/main refs/tags/phase25-delivered-v9 'refs/tags/phase25-delivered-v9^{}'
```

Require:
- `main` and peeled `phase25-delivered-v9` both point to the exact same committed release SHA.
- Peeled `phase25-pre-main` points to the exact old main SHA.
- No branch was force-pushed; `phase25-integration` preserved; Phase 25.1 branch preserved; default still v9.

No need to create a separate GitHub Release or change the language/ABI version number. A permanent remote annotated tag suffices to identify the delivered version. A GitHub Release description can be added later if useful; do not repoint tags.

## 8. Recovery and rollback rules

### Inspect or rebuild old main at any time (safe)

```bash
git fetch --tags origin
git show --no-patch phase25-pre-main
git show --no-patch phase25-delivered-v9
git worktree add --detach ../moss-pre-phase25 phase25-pre-main
# Work/rebuild/reproduce in the detached historical tree. No changes to main.
```

To develop a recovery patch without moving main:
```bash
git switch -c recovery/phase25-from-old-main phase25-pre-main
```
This creates a new branch from the old code; publishing it does **not** revert `main`.

### If `main` must be reverted

Prefer fix-forward where practical. A large 447-commit fast-forward cannot be safely "undone" by `git revert -m 1` (there is no merge commit). A batch revert of all commits is not automatically safe either.

**Emergency ref restore is L7-controlled only:** coordinate and pause writers/builds, check branch rules and downstream consumers, communicate the ABI/cache rollback implications, record the exact current main SHA, and only then explicitly authorize a bounded restoration of remote main to `phase25-pre-main`. Such restoration rewrites main history and may require a narrowly scoped force-with-lease or administrative protected-branch process. Never perform it as an autonomous agent action, and never force without an exact expected-old SHA, exclusive coordination and L7 GO.

Recovery remains possible because the old main is permanently reachable via `phase25-pre-main`. The delivery tag remains intact for forensics, even after any authorized rollback.

### Avoid false “rollback” claims

Checking out an old tag recreates the historical *working tree*; it does not alter remote main or revert existing compiled v10/v9 artifacts in external caches. A production rollback plan must rebuild/redeploy relevant libraries under the ABI appropriate to the restored compiler and invalidate incompatible package caches.

## 9. Final morning/delivery report

Send L7 one concise report:

```text
Phase 25 main delivery: PASS / BLOCKED / FAIL
main before: <full SHA>
main delivered: <full SHA or unchanged>
integration source: <full SHA>
pre-main tag: phase25-pre-main -> <peeled full SHA>
delivery tag: phase25-delivered-v9 -> <peeled full SHA, or not created>
main movement: fast-forward only? yes/no
default: v9 (native_abi 9 in fresh artifact)
v10 opt-in: MOSS_STAGED_ABI=v10, positive/negative tests
make check: X/Y, 0 failed, 0 skipped
make examples: PASS/FAIL
v9 source-free SDK/advanced-data/encoding: PASS/FAIL
v10 checked-body SDK/mixed-provider/corpus: PASS/FAIL
ABI mismatch/cache isolation: PASS/FAIL
phase25.1 branch unaffected: yes/no
unexpected changes or limits:
git working-tree status:
logs/evidence paths:
remaining Phase 25.1 items: from_range, relay/size, SDK parity inventory and other audited cases
```

Do not claim delivery complete until **both** remote tags are verified, `main` has the pinned target SHA, the default ABI-v9 contract is observed and post-push checks pass.

## 10. Runbook for the executing Codex L6

Work with **one release/integration writer** and optional read-only reviewer; do not launch compiler implementation agents. The coordinator can complete P0 and P1 and present P2 results autonomously. **STOP at P2 pending explicit L7 approval to move main.** If approved, execute P3–P5 and report. Do not alter this playbook on `phase25-integration` or `main` as part of the release; the pinned playbook resides on a separate documentation-only branch. Future Phase 25.1 work continues independently from its already-pushed branch.

**Success:** `main` is exactly the validated Phase 25 implementation, has ABI-v9 production behavior with ABI-v10 staged opt-in, and two durable annotated tags identify the versions immediately before and after delivery.
