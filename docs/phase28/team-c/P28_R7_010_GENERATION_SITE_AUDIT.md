# P28-R7-010 Team C — Generated Rust Generation-Site Audit

**Artifact base:** WIP checkpoint `0456446451e0df8dd11794eb2f37e438781dc926`
**Artifact set:** 71 Rust files captured by the reproducible oracle run
**Purpose:** independently group duplicate-operation candidates and cross-check
Team B's emitter inventory. This report does not authorize any clone.

## Method and count reconciliation

Run the frozen oracle harness into a dedicated artifact directory, then run:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase28_r7_010_clone_inventory.py \
  tmp/team-c/oracle-artifacts \
  --json-out tmp/team-c/oracle-isolated/clone-inventory.json \
  --markdown-out tmp/team-c/oracle-isolated/clone-inventory.md
```

The scanner found **2,047** candidate operations in **86** grouped source-site
fingerprints across **71** Rust files. Direct fixture artifacts account for 70
files; the remaining file is ORACLE-54's generated provider `provider.rs`.
Cargo `build.rs` is excluded. The `.clone()` reconciliation is:

Source-side span coordinates use 1-based line and **byte-counted** column
positions, matching EST source positions. The harness independently maps those
coordinates back to UTF-8 character offsets and checks the exact byte ranges.
Adversarial coverage places a multibyte `é` before `copy(...)` on the same
line, so codepoint-column assumptions cannot accidentally pass.

| Generated region | Grouped sites | `.clone()` observations | Repetition and interpretation |
|---|---:|---:|---|
| Source-marked Moss constructs | 21 | 44 | 2 artifacts per construct, except explicit `copy(v[0])` with 4 debug/optimized observations across 2 artifacts |
| `__moss_value` object materializers | 25 | 60 | Type/field-specific generated bodies, repeated 2–4 times |
| Other generated runtime/helper lines | 6 | 426 | Each lexical helper line appears in all 71 artifacts |
| **Total** | **52** | **530** | 524 direct fixture observations + 6 provider observations |

These are grouped **artifact observations**, not necessarily 52 distinct C++
emitter functions. Moss groups are keyed by generated source markers and
preceding `// Moss line` comments. Runtime groups use enclosing Rust function
and normalized source line. Debug and optimized files repeat the same source
operation. The 6 runtime/helper sites include branch-scope, active-executor,
configuration affinity, current-worker context, active executor config and
worker-spawn context lines.

The 524 direct fixture clones match the harness's current `.clone()` count. Six
more provider clones are outside `results.json`'s `clone_audit` coverage and
remain unverified. The scanner also found `.cloned()` 4, `.copied()` 6,
`.to_string()` 639, qualified `Clone::clone`/`Arc::clone`/`Rc::clone` 639,
`mem::replace` 213, `mem::take` 14 and `String::from`/`Vec::from` 2. Their
semantics vary; these are not counts of Moss value copies.

## Clone trait capabilities and 524/494 reconciliation

The scanner now reports `#[derive(Clone)]` declarations on generated Moss
`type`/`enum` regions separately from invoked clone operations. A derive
declaration creates a Rust duplication capability; it is not itself a value
copy and does not grant semantic authority. The inventory also associates
clone calls inside typed `__moss_copy_value_*` helper bodies with the helper's
raw fact-marker locator, while explicitly leaving that locator unauthenticated
until the checked-fact validator verifies it.

On the WIP checkpoint's 71-artifact set, the operator scan remains **2,047
candidate operations in 86 groups**, including **530 `.clone()` occurrences**:
524 direct fixture observations plus 6 provider observations. The separate
capability scan finds **344 Moss-region Clone derive declarations**, repeated
across artifacts, representing **26 unique Moss type/enum names**. The
single-file current ORACLE-10A artifact has five such declarations (four
standard error enums and Moss `Record` at Rust line 3402); its `Record` clone
at line 3448 is the one call inside the fact-specific helper, with a marker
whose 16-character helper prefix matches the full fact ID. The full Team C
validator independently checked that occurrence against the exact checker
query. The other six `.clone()` occurrences in that artifact are runtime
occurrences and remain unclassified.

The later post-Map.get comparison set at
`tmp/phase28-r7-010/tmp/phase28-r7-010/oracle-post-mapget-v2` contains 67 Rust
artifacts. Re-running this scanner gives **1,933 candidates in 84 groups** and
**500 `.clone()` calls**: 494 direct fixture observations plus 6 provider
observations, matching the later failure ledger's 494 direct-artifact count.
That set has 259 repeated Clone derive declarations. Its older generated
artifacts do not use fact-specific helper symbols, so the new helper-use
association finds none there; this is a scanner-format limit, not evidence
that the older `.clone()` sites are unauthorized or absent.

There are two distinct six-file scopes. Team B's complete
`tmp/team-b/vector-read-and-echo` gate directory contains four oracle artifacts
plus the two read-then-write lifetime-probe artifacts. That directory yields
162 candidates in 27 groups, 36 `.clone()` calls, and **28** derive
declarations (four standard error enums per file, plus `Node` twice and `Bag`
twice).

The six-file conformance comparison scope used for Agent 2 review is exactly:
Team B ORACLE-39 Debug/Optimized, the independent full-v2-evaluator ORACLE-39
Debug/Optimized outputs, and Team B ORACLE-42 Debug/Optimized. Those files are
copied to `tmp/team-c/team-b-44513837/derive-audit-six/` for the grouped scan.
They yield **30** derive declarations: four error enums in each of six files
(24), `Node` in four ORACLE-39 files, and `Bag` in two ORACLE-42 files. The
scanner regression pins those exact six filenames and 30 declarations. The
application loop/indexed echo code contains no detected clone operation;
`Node`/`Bag` derives expose capabilities but do not prove an invoked duplicate,
and the scanner cannot rule out unscanned or future call paths from a derive
alone.

## Moss-marked operations observed

The 21 groups (44 occurrences) point to these generated source expressions:

| Generated Moss source expression | `.clone()` observations | Finding |
|---|---:|---|
| `count_val = b |> count` (two helper/function contexts) | 4 | Unverified transfer/borrow lowering |
| `return alias` (two specializations) | 4 | Unverified alias-return lowering |
| `return item.value` | 2 | Unverified projection return |
| `return b` | 2 | Unverified return ownership |
| `again = values[0]` | 2 | Unverified indexed alias read |
| `echo (m["first"].items |> count)` | 2 | Unverified nested Map/index projection |
| `echo (message w.Increment(10))` | 2 | Unverified message result lowering |
| `echo b.elements[1]`, `echo collected[2]` | 4 | Unverified indexed collection reads |
| `echo frontier[0]`, `echo frontier[1]` | 4 | Unverified indexed reads |
| `echo read_missing()` | 2 | Unverified returned value |
| `echo shifted[0]`, `echo shifted[1]` | 4 | Unverified indexed reads |
| `echo values[9]` | 2 | Unverified indexed read |
| `first = as_int(values[0])` | 2 | Unverified nested call/index lowering |
| `match v[0].pending:` | 2 | Unverified match payload/projection lowering |
| `res = message w.Process(42)` | 2 | Unverified message result lowering |
| `var copied = copy(v[0])` | 4 | Explicit-copy lowering candidate; only this source spelling is an explicit-copy candidate, and exact checker-fact provenance is still required |

The descriptions above are source-site breadcrumbs. They do not establish
whether a call duplicates an owned Moss value, borrows, transfers, or is
runtime support. Apart from the explicit source-level copy spelling, every
Moss-marked clone remains unverified and blocks authorization pending exact
checker facts and complete sidecar links.

## Confirmed application-value duplication mechanism

Team B's independent source review identified
`src/borrowed_views.inc:140-143`: `gen_object_access` emits a
`__moss_value()` builder that calls `self.__moss_field_<name>().clone()` for
non-viewable fields while materializing an owned object from a borrowed view.
The generated inventory independently observes **25** type-specific
`__moss_value` bodies and **60** clone calls across the 71 artifacts. Team B
confirmed String and collection leaves can pass through this path.

This establishes an application-value duplication mechanism, not authorization
for any occurrence. Each generated field clone remains **unverified and
blocking** until replaced by an authorized borrow/move path or linked to exact
checker-approved copy authority. No runtime-mechanical exception applies.

## Cross-check against Team B's emitter inventory

Team B provided a tentative list of native generator candidates (line numbers
were from its moving worktree). The comparison below uses the frozen
checkpoint artifact set, before Team B's uncommitted changes:

| Team B candidate family | Checkpoint artifact evidence | Independent conclusion |
|---|---|---|
| Explicit `copy(x)` | 4 `.clone()` calls for `copy(v[0])` in debug/optimized artifacts | Site observed; authority still requires exact `TypedCopyFactV1` query/sidecar join |
| `__moss_value` borrowed-object materialization | 25 generated type-specific sites, 60 field `.clone()` calls | Mechanism confirmed by Team B source inspection; value duplication confirmed, occurrence intent unverified and blocking |
| Index, indexed projection, Map access/key/value, borrowed receiver | Source-marked indexed/nested Map expressions above; separate `.cloned()` groups for `m.keys()` and `m.values()` | Candidate operation classes occur in artifacts; scanner cannot map them to exact C++ emitter line/site without source map or marker for that emitter |
| Message result/payload, exception/recovery, domain/view transfer | Message and match/return source markers occur; no complete emitter-to-artifact bijection established | Candidate families are not independently authorized by these observations |
| Sort-key closure, ABI/error bridge, domain-handle constructor/capability, string-concatenation bridge | No matching source-site marker identifies these native generator sites in this corpus | Not observed here; absence in 71 fixtures is not proof the emitter site is absent or safe |
| Runtime scaffolding | Six same-line helper clone groups repeated in each of 71 artifacts | Repetition strongly identifies shared generated helper code; all 426 calls remain subject to reviewed runtime registry/evidence and are not automatically exempt |

Team B's inventory is useful for prioritizing source inspection. This artifact
cross-check neither adopts its classifications nor treats an emitter comment,
line number, or generated helper name as checker authority.

## Scanner limits and gate status

The scanner lexes comments, nested block comments, ordinary/raw/byte/C strings
and character literals before recognizing known spellings. It detects direct
`.clone()`, `clone_from`, `.cloned()`, `.copied()`, `.to_owned()`,
`.to_string()`, `.to_vec()`, qualified `Clone`/`Arc`/`Rc::clone`,
`mem::replace`, `mem::take`, `ptr::read`, and `String`/`Vec::from` candidates.

It does not expand macros or inspect Rust AST/MIR, trait resolution, `Copy`
loads, compiler intrinsics, arbitrary conversion/constructor APIs, or
user-defined helpers that duplicate values without these spellings. Clone
derive detection reports Moss-type capabilities but does not prove all
possible invocation reachability; only typed per-fact helper bodies receive a
separate lexical association, and their marker is not itself authority. Its
“generation-site” key is lexical output context, not a proven native emitter
function identity. Thus a clean scan would not establish absence of
duplication. **Gate result: NO-GO** while any occurrence lacks an independent
semantic classification and complete provenance. Raw JSON/Markdown inventory
is reproducible but stored under disposable `tmp/`.

## ORACLE-10A runtime-site evidence appendix

An independent follow-up mapped all six runtime `.clone()` observations in the
ORACLE-10A artifact to exact source callsites and concrete Rust fields. The
artifact is pinned by Rust SHA-256
`b51fd33939f319e1eaf1fc6b1e08f3b64be3ee06a93db598eaacd976913013bd`, source
SHA-256
`90e2f18939e3db4be156b9725599c24286a1d617f539aa43c139ef5b56765f22`, and
producer commit `14c4ad4c663c00f7eb7e756ad13c7c4dd7b7e059`. The matching
structured evidence, including source-file digests and concrete types, is
[`P28_R7_010_ORACLE10A_RUNTIME_CLONE_EVIDENCE.json`](P28_R7_010_ORACLE10A_RUNTIME_CLONE_EVIDENCE.json).

| Rust artifact line | Source generator/runtime site | Concrete Rust value | Evidence observed | Authorization |
|---:|---|---|---|---|
| 2014 | `src/executor_runtime.cpp:497` | `Option<Arc<MossGlobalExec>>` | Arc handle/reference-count clone; it does not deep-clone the executor | **Unclassified** |
| 2241 | `src/executor_runtime.cpp:724` | `Option<Vec<usize>>` | Runtime CPU-affinity indices | **Unclassified** |
| 2357 | `src/executor_runtime.cpp:840` | `Option<Vec<usize>>` | Runtime CPU-affinity indices | **Unclassified** |
| 2907 | `src/executor_runtime.cpp:1390` | `MossBranchScope { state: Arc<MossBranchScopeState> }` | Arc handle clone; state may retain queued Moss work, which this call does not deep-copy | **Unclassified** |
| 2949 | `src/executor_runtime.cpp:1432` | `Option<Arc<MossGlobalExec>>` | Arc handle/reference-count clone from runtime TLS | **Unclassified** |
| 3112 | `src/executor_runtime.cpp:1595` | `MossExecutorConfig` of numeric runtime settings | Configuration clone; fields are thread counts, affinity indices, and priority | **Unclassified** |

The artifact also includes two runtime `String::to_string()` substring
materializations: ASCII slicing at Rust line 1393 maps to
`src/char_string_ordering_runtime.cpp:115`, and Unicode slicing at Rust line
1406 maps to `:128`. They produce owned substring values and remain
unclassified. The artifact contains no raised-error `.clone()` invocation:
its seven `.clone()` calls are these six runtime sites and the one
checker-linked explicit `Record` copy. Clone derives are capabilities, not
invocations.

These mappings document concrete types and callsites only. They do not grant a
runtime-mechanical exception or semantic authorization; all six runtime sites
remain unclassified pending L7-approved provenance. The bounded regression
`tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py` pins the artifact
and source hashes, generates the fixture through the supplied compiler, checks
all seven raw clone spans and six runtime expression mappings, verifies the
sidecar result (`1` checked copy, `6` unauthorized/unclassified runtime clones),
and asserts the evidence rows remain unclassified. From the repository root,
run `python3 tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py
<L7-compiler> <canonical-ORACLE-10A-source>`. Both paths are required because
the checker fact identity in the pinned artifact is bound to its producer
source path; an older Team C compiler or a different source path is not a
comparable producer input.
