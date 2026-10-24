# Moss Phase 25.2 — L7 single-agent execution mandate

**Authority:** L7, 2026-10-23. **Branch:** `phase25-2-work` (based on accepted `main` `2420439cf1cbaa08c8cd45d0d77b436ea4fb43f0`). **Clang/mold Makefile commit:** `466801dcf22fb7c5dfbeb79ef4fa61ac82979bdf`. **Goal:** Deliver a genuinely faster incremental C++ compiler build by splitting the monolithic `src/moss.cpp` into about 10–14 cohesive translation units, with careful template/header work.

## Staffing and authority

One **L6 agent** is the sole Coordinator, implementer, and ordinary Reviewer. **Do not launch implementation subagents, teams, or L5 workers.** An **independent L6 adversarial architect** may be consulted at two frozen checkpoints: the first successful multi-TU extraction and the near-final integrated candidate (or once more if a genuine architecture blocker emerges). Do not hold every small commit for a reviewer.

This L7 decision overrides prior Phase 25.2 briefs proposing multiple workers. Continue to follow `AGENTS.md`, required repository skills/bootstrap, feedback, branch safety, focused validation and accurate review reporting. Record ordinary reviews as **self-review**, not independent signoff. Do not modify or merge `main` without L7 authorization.

## Already done

The work branch is created. The Makefile currently selects `clang++` by default (with explicit `CXX` overrides) and passes `-fuse-ld=mold` on the dev/optimized compiler link commands (overridable with `MOSS_LDFLAGS=`). The branch may still contain a monolithic compile rule. **Do not redo the compiler-selection debate.**

## Immediate action: verify installed tools and compiler

After mandatory repository startup, confirm `clang++` and `mold` resolve locally and verify the exact Make link command uses mold, not merely that mold exists. Make the existing `moss-dev` and `moss-opt` targets build from this branch and run a focused `./moss` smoke test. Fix actual toolchain or linker integration failures in scope. Keep `-std=c++17`, `-O0` development and `-O2` optimized profiles. GCC remains an explicitly selectable compatibility path, not the default. Do not run repeated compiler comparisons or extended profiling.

## G1: make multi-TU builds possible

Replace the single compile/link recipe with:

- An explicit maintainable `.cpp` source list, initially including `src/moss.cpp`, incrementally extended during extraction.
- Separate object directories for dev and optimized profiles, using `-MMD -MP` and included depfiles for exact transitive header/`.inc` dependencies.
- Independent object compilation with parallel Make scheduling and one final compiler-driver link (using mold by default).
- Correct no-op rebuilds and incremental updates after `.cpp`, `.hpp`, and `.inc` edits, without the old wildcard-all-headers invalidation.
- Safe handling of compiler/flag/linker changes: never silently reuse stale objects or an out-of-date executable when build flags change.
- Preserve all existing public make targets, executable paths, tests and examples. Do not introduce CMake or another build system.

G1 is complete when the single-source build functions through the new object/link mechanism. Move directly to G2; **do not spend time on a separate performance report**.

## G2: extract real compiler translation units

Read the actual code and follow symbol and dependency ownership, **not equal file sizes**. A tentative target is driver, parser/AST, types, checker, ownership/effects, domains, functional/collections, typed semantic API, ABI-v9, ABI-v10 checked-body/codec, native codegen, IO/executor lowering, Fast Debug, and diagnostics/IDE. Consolidate or subdivide as actual dependencies demand; 10–14 is a guideline, not a quota.

- First identify the lowest-risk coherent extraction, create the narrow necessary declarations, move implementation to a new `.cpp`, and get a working two-TU binary. Continue in modest, compileable extraction commits.
- Audit `static` functions, anonymous namespaces, file-local globals, initialization order, implicit template instantiations, `.inc` inclusion context and ODR/linkage rules *for the symbols being moved*. Prevent duplicate definitions and hidden cross-TU dependencies.
- Do not substitute broad `#include "*.cpp"`, giant shared implementation headers or unity builds: the result must be independently compilable TUs.
- Prefer declarations in narrow stable headers and out-of-line non-template implementations. Move expensive inline header bodies only where safe. Keep semantics unchanged.
- No language design changes, compiler algorithm refactors, ABI schema changes, ABI-v10 promotion or unrelated fixes.

Build, link, and run targeted behavioral tests after each bounded extraction. Preserve exact existing diagnostic contracts, Native/Fast Debug behavior, source-free ABI-v9 default and ABI-v10 opt-in, cross-crate provenance, cache invalidation and SDK behavior. If a cut proves tightly coupled, undo it or regroup without thrash. Use commit-level rollback.

At the **first working multi-TU checkpoint**, ask the independent adversarial architect to review the exact SHA for symbol ownership, ODR, cross-TU state identity and hidden implementation-header coupling. Record findings, fix genuine P0/P1 issues, and carry on without asking L7 for every cut.

## Optimize once splitting works

- **ETI:** centralize expensive *actual* template specializations and add corresponding `extern template` declarations where legal and useful. Verify suppression of redundant emission and correctness at link; do not blanket-instantiate all STL types or claim generic algorithms can be moved entirely out of headers.
- **Header discipline:** stop unrelated edits from recompiling most TUs. Extract implementation-heavy non-template code from common headers.
- **PCH:** add a Clang PCH for stable system/common headers if it materially helps; ensure compile flags, invalidation and toolchain version match. Do not hide compiler definitions in a universal PCH.
- **mold:** already the default link mode; preserve configurable fallback. No further linker bake-off is required.

Apply optimizations in bounded independently verifiable commits. Prefer a functioning 10-TU compiler over an elegant design that does not build.

## Tests, measurement and acceptance

Do not re-run exhaustive baselines or the ~17-minute full regression suite after every edit. Use focused correctness tests during extraction. At the final integrated SHA:

1. Build and link dev/optimized profiles with Clang+mold, and exercise a clean and representative incremental edit/relink cycle. Report new production measurements at the **end**, referencing the historical ~24s monolithic `-O0` edit baseline only as non-controlled context; distinguish clean, incremental, compilation and link time.
2. Show that touching a narrow implementation only rebuilds its object plus relink; touching a shared header recompiles actual dependents; no-op builds do nothing.
3. Execute `make check` (Phase 25.1 baseline: 136/136), `make examples`, plus focused ABI-v9/v10, SDK, source-free provider/consumer and hostile-artifact controls. Do not claim a pass for any unexecuted gate. Make an explicit GCC override compatibility build if practical.
4. Record `git diff --check`, test evidence, commit SHA and self-review. Invite the independent adversarial architect to challenge the final *frozen* SHA; document GO/NO-GO and any real findings.

Keep a concise `docs/phase25_2/STATUS.md` with current commit, TUs extracted, proven checks, blockers and next step. Follow existing agent-feedback and session status policies; no daily essays. At final delivery provide: exact SHA, TU list/ownership, tests, measured new build times, review verdict and residual bottlenecks.

**Stop only for a material design/semantic blocker or after delivery of the reviewed Phase 25.2 candidate. No merge to `main`. Do the work; do not substitute planning documents for code.**
