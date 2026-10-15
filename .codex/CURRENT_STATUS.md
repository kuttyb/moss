# Moss current status

## Phase 25 Team S independent G0 boundary review — 2026-10-14

The independent Team S L6 Architect reviewed integrated G0 commit
`6f109124a580c6a76a4050b4c222484a658ee4e1` and recorded a **NO-GO for L7 G0
authorization** in `docs/phase25/TEAM_S_G0_ARCHITECT_REVIEW.md`. The checked-in
prototype proves a consumer-local Rust key adapter and the orphan-rule negative,
but it does not prove either proposed production representation. The review
recommends freezing a policy-free compiler-owned `MossMap<K,V>`, rejects
`KeySlot<K,Ops>` as frozen without a compiled-only generic-key proof, requires an
exact open-receiver member-dispatch record for ledger #12, and requires explicit
Map/Set law/effect records plus Phase 25 Float-key prohibition. Bootstrap,
focused Rust feasibility, expected baseline local-member failure, feedback
validation, and whitespace validation completed; no compiler code changed and
no push was performed.

## Phase 25 Team G G0 A1/ABI bounded feasibility — 2026-10-14

On `phase25-g0-feasibility` from pinned baseline
`fa3a340a0a6652568b97869489da1a84038b9857`, the bounded G0 prototype and
conformance design are complete without compiler/shared-IR edits. One
provider-owned generic Rust record with private fields is consumed by two
independent specialization crates through provider-generated accessor-shaped
functions. The final Architect proof adds a compiler-owned, policy-free shared
`MossMap<K,V>` runtime representation with neutral accessors. Consumer-owned
specialized free operations call provider `isequal`/`hash` functions directly.
One provider-created Map crosses both independent consumer rlibs and passes
custom static equality/hash insertion, lookup, overwrite, eager owned keys,
deletion, length, and missing-key checks without an Ops type parameter, stored
callback, dynamic dispatch, or foreign key impl; the direct foreign `Hash`
control fails with rustc E0117 as required. The earlier `MossMap<K,V,Ops>`
alternative is explicitly superseded. The report recommends provider-owned
opaque records and the policy-free shared runtime Map representation, with
explicit A1/Map escalation triggers.

`docs/phase25/TEAM_G_G0_FEASIBILITY.md` defines the proposed canonical
template/instance keys, ABI-v10 logical records, Option B freshening/closure
rules, visibility alternatives, and source/source-free conformance plan.
Forward fixtures are under `tests/phase25/g0/`; they are not wired into the
baseline test runner because current ABI-v9 Moss correctly lacks Option B.
Focused Rust feasibility and the existing ABI-v9 migration gate pass. Current
open-result probes fail at `fresh` with `TYPE_INFERENCE_FAILED`, and the local
pin probe reaches Team S's known local-member dispatch error. Major shared-IR
work remains blocked pending Team S, both Architects, and signed L7 G0 approval.

## Phase 24 Aggregate SDK Integration Complete (Teams A & R Integrated) — 2026-10-13

Team I completed the aggregate integration of all approved Phase 24 SDK deliveries into `phase24-integration`, incorporating Team A (`phase24-advanced-data`) and Team R (`phase24-data-utils`) alongside all previously integrated components:

1. **Merge Execution & Provenance:**
   - **Target branch:** `phase24-integration` (starting HEAD: `fa9b05e7877138a888936c413bf4583fd448a7a4`).
   - **Team A Merge:** Merged `origin/phase24-advanced-data` (`7f30436990f730b8556592b7d5b54e6bac9ccc18`) via sequential `--no-ff` commit `35deb9c`.
   - **Team R Merge:** Merged `origin/phase24-data-utils` (`9794122db5dc2aade42cc259879dffb16200f54b`) via sequential `--no-ff` commit `624abff`.
   - Normal three-way sequential non-fast-forward merges performed; zero squashing, cherry-picking, rebasing, resetting, or whole-file replacement. Zero merges into `main`. All newer Team I work, ABI contracts, and compiler fixes preserved intact.

2. **Mandatory Documentation & Provenance Corrections:**
   - **Team A Test Count Arithmetic (P24-A-001):** Reconciled Team A's reported 103 unique tests and 309 executions to the actual verified count of **111 unique tests**, **111 per mode**, and **333 total executions** across Native Debug, Native Release, and Fast Debug (`moss.ndarray`: 21, `moss.datastructures`: 23, `moss.tables`: 24, `moss.binary`: 9, `moss.cbor`: 34). Reconciled in `docs/phase24/TEAM_A_ADVANCED_DATA_DELIVERY.md`, `docs/phase24/agent-patterns/team-a-advanced-data.md`, and aggregate status reports without rewriting historical review text.
   - **Team R Architect Implementation SHA Erratum (P24-R-001):** Confirmed the actual Git implementation commit implementing the NFA split-origin correction and runtime-scaling verification is `4068976a9a4d1bb046b69fbc35708d8b51f831e4`, an exact ancestor of delivery HEAD `9794122d` (intervening commits are documentation/status-only). Recorded formal Section 7 erratum in `docs/team_r/ARCHITECT_FINAL_ACCEPTANCE.md` and `docs/phase24/agent-patterns/team-r-data-utils.md` (`8fdd93e`) without altering the Architect's authored text or verdict.

3. **Focused & Combined Matrix Validation:**
   - **Team A Packages:** 111/111 passed in Native Debug, Native Release, and Fast Debug (333 total executions); `sdk/advanced-data-acceptance/run_all.py` passed (12 RFC 8949 vectors, seeded ndarray/table semantic oracle in 3 modes, 7 cross-package scenarios in 3 modes, 14 source-free ABI-v9 providers).
   - **Team R Packages:** 36/36 passed in Native Debug, Native Release, and Fast Debug (108 total executions: hash 8, compress 8, regex 18, data_utils_integration 2); `tests/tooling/check_phase24_data_utils_oracles.py` passed (hash differential, compress bidirectional roundtrips, regex NFA differential & live linear scaling experiment, source-free ABI-v9).
   - **Compiler & Language Gates:** `check_phase24_bitwise_operations.py`, `check_phase24_same_module_type_resolution.py`, `check_phase24_imported_generic_expressions.py`, and `check_phase24_adversarial_architect.py` all passed (0 regressions).
   - **Full Combined SDK Matrix:** Foundations acceptance (`--full`), Text & Encoding acceptance (`--full` & release benchmarks), Math/Random/Time (37 unit tests & repair verification), Numerics (30 unit tests, falsification oracles, ABI-v9 consumer), Host Services (7 path/fs suites), Structured Data (`run_all.py` & 103 differential tests) all passed.
   - **Repository Quality Gates:** `TMPDIR="$PWD/tmp" make check` passed (54/54 executor tests, all Moss v0.1 tests); `make examples` passed (32/32 examples); `python3 tests/tooling/check_agent_feedback.py` passed; `git diff --check` passed cleanly.

4. **Aggregate SDK State:**
   - **State:** **GREEN**. All 19 packages across 8 delivered areas (Foundations, Text & Encoding, Math/Random/Time, Numerics, Host Services, Structured Data, Advanced Data, Data Utilities) are fully integrated, verified, and operational on `phase24-integration`.

5. **Independent Whole-System Architect Sign-off:**
   - Evaluated aggregate implementation on `phase24-integration`.
   - Issued attributable **UNCONDITIONAL GO FOR PHASE 24 SDK INTEGRATED MILESTONE** in `docs/phase24/WHOLE_SYSTEM_ARCHITECT_ACCEPTANCE.md`.
   - Published Final Aggregate Integration Report in `docs/phase24/AGGREGATE_INTEGRATION_REPORT.md`.


## Phase 24 Team R Data Utils SDK Final Corrective Pass Complete — 2026-10-13

Team R (`moss.hash`, `moss.compress`, `moss.regex`, `moss.data_utils_integration`) completed final corrective pass repairs, adversarial testing, live linear runtime-scaling verification, and independent Architect sign-off on `phase24-data-utils`:
1. **Implementation Baseline & Exact Reviewed SHA:**
   - Compiler R0a/R0b accepted baseline (`43757790ef3af52204c90bbd9dabcefc4470e965` on `phase24-integration`).
   - Exact implementation SHA: `4068976a9a4d1bb046b69fbc35708d8b51f831e4` (confirmed by Team I; Team R's authored report transcribed `4068976264f3312c5bfad3e680a6b12fcf45ff3b`).
2. **Corrective Pass Remediation:**
   - **NFA Origin Propagation Symmetry (Task 1):** In `packages/regex/src/main.moss` (`run_nfa_search_from`), epsilon-closure processing for `KIND_SPLIT` now symmetrically propagates earlier origins through `out2` when `s_orig < state_origins[o2]`. Cyclic termination is mathematically proven (cycles cannot decrease origin, terminating immediately). Preserves POSIX leftmost-longest match semantics. Added 5 adversarial regression test suites (18 tests total in regex).
   - **Live Linear Runtime-Scaling Experiment (Task 2):** Replaced combined-time probe with release-mode live experiment measuring precompiled Regex objects across 1K, 2K, 4K, 8K, 16K nonmatching characters against adversarial patterns `(a+)+b`, `(a|aa)*b`, and `a*b`. Across 5 repeated measurement series, median timings demonstrate exact ~2.0x doubling ratios per 2x length increase (bounded in $[1.19\times, 2.26\times]$), proving linear $O(N)$ execution time and complete ReDoS immunity. Characterized state-count complexity costs ($O(M \cdot N)$).
   - **L7 Corrective Review Closeout:** All 4 prior findings (L7-R-01 dangling quantifiers, L7-R-02 reversed character ranges, L7-R-03 malformed alternation, L7-R-04 destructive SHA-256 state mutation) verified permanently resolved.
3. **Validation & Quality Gates:**
   - In-Tree Unit Tests: 36 unique test cases across 4 packages (8 hash, 8 compress, 18 regex, 2 integration) with 108 total test executions across Native Debug, Fast Debug, and Native Release all passed (100% parity).
   - Differential Oracles: 25 differential tests, live scaling series, and source-free ABI-v9 checks in `tests/tooling/check_phase24_data_utils_oracles.py` passed against Python `zlib`, `hashlib`, `re`, and reference implementations.
   - Benchmarks: Native release benchmarks recorded for all packages (`margo bench`), with Phase 21 `RegexError` variants handled.
   - Repository Quality Gates: `TMPDIR="$PWD/tmp" make check` passed (54/54 executor tests; all Moss v0.1 tests passed); `make examples` built all 32 showcase programs; `python3 tests/tooling/check_agent_feedback.py` passed; `git diff --check` passed cleanly.
4. **Independent Architect Verdict:**
   - Independent L6 Architect issued attributable **UNCONDITIONAL GO FOR TEAM I INTEGRATION** in `docs/team_r/ARCHITECT_FINAL_ACCEPTANCE.md` §6 for exact implementation SHA `4068976a9a4d1bb046b69fbc35708d8b51f831e4` (with erratum noted for the transcribed SHA).
5. **Handoff Status:**
   - Ready for Team I integration into `phase24-integration`. Branch remains strictly isolated on `phase24-data-utils`; zero commits merged into `main`.

## Phase 24 Team A L7 CBOR corrections accepted — 2026-10-13

Branch `phase24-advanced-data` preserves all five delivered SDK packages and
adds the two L7-requested CBOR corrections. Implementation commit
`debf99f068735630ca28a7d5ee4fc343832ac094` makes linked edge ownership agree
with each child's public `parents[]` entry and rejects attachment of the
designated root through typed `CborError.InvalidRoot` before mutation.
Independent adversarial commits `a6ecc79` and `76baf87` add eight forged-record
tests, including a true ownership-consistent parent cycle; the verifier found
no net-new production defect. Commit `58340e6` adds direct corrective typed
error recovery to the compiled-only ABI-v9 consumer.

The five package suites pass 111 unique tests in Native Debug, Native
Release, and Fast Debug (333 executions; corrected by Team I from Team A's
reported 103/309 arithmetic discrepancy: ndarray 21, datastructures 23, tables 24,
binary 9, cbor 34 = 111). Normal and pinned `cbor2 5.7.1`
live-oracle acceptance pass: seven cross-package scenarios in all three modes,
the seeded ndarray/table oracle in all three modes, 12 RFC vectors, 14 exact
source-free ABI-v9 provider pairs, and 12 comparisons in each CBOR oracle
direction. `TMPDIR="$PWD/tmp" make check` passes with 54/54 Executor tests,
61/61 editor tests, and `all Moss v0.1 tests passed`; `make examples` passes.
L7-A-001 and L7-A-002 are confirmed L7-origin findings and are fixed with
passing regressions, bringing the historical resolved-defect ledger from seven
to nine. A fresh independent Architect issued **GO** for exact frozen candidate
`142b75009c229a318f46b52670f3d7b8aaca848b`, independently rerunning the
two primary reproducers, the 34-test CBOR suite in all three modes, source-free
ABI-v9 recovery, 12 RFC vectors, and 24 pinned `cbor2 5.7.1` comparisons.
The attributable report is preserved in
`docs/phase24/TEAM_A_ARCHITECT_L7_CORRECTIVE_ACCEPTANCE.md`; report-only state
is `cd0c5ae26549cd2c9e65f7df6285cebc79006c34`. No merge into
`phase24-integration` or `main` has occurred.

## Phase 24 Team A advanced-data accepted for Team I handoff — 2026-10-13

Branch `phase24-advanced-data` delivers `moss.ndarray`,
`moss.datastructures`, `moss.tables`, `moss.binary`, and `moss.cbor` from
required baseline `5394e93ceedccf21e577556325e598ff4d8c4d9c`. The initial
implementation `cdcadb26b9b46d332f5f577886c359d44ce839c0` received an
independent Architect **NO-GO**, preserved in
`docs/phase24/TEAM_A_ARCHITECT_FINAL_ACCEPTANCE.md`, for four contract and
acceptance gaps.

Repair implementation `3ea1092359cb82b537bbf33595b3d75836df8e79`
aligns the over-broad initial data-structure freeze with the mission-authorized
concrete Int and String-key typed baseline, adds a seeded independent Python
ndarray/table semantic oracle in Native Debug/Release/Fast Debug, and extends
the exact 14-provider compiled-only ABI-v9 consumer with typed variant/payload
recovery and failed-mutation atomicity for all five Team A modules. The required
Team efficiency closeout and delivery report preserve the original NO-GO and
all seven attributable Coordinator/Architect defects. A fresh independent
Architect issued **GO** for exact candidate
`c80cf8ac51a9fa264b271b5a56c89ea2068ff4b7`, carrying repair implementation
`3ea1092359cb82b537bbf33595b3d75836df8e79`, after independently verifying all
four earlier findings fixed with zero net-new defects. The GO report is the
only change in delivery state `85eb047bc44bd83cd51ec48edcd601bd257effe1`.

Repaired full acceptance passes: the semantic oracle in all three modes; seven
public cross-package scenarios in all three modes; 12 fixed RFC 8949 vectors;
typed source-free ABI-v9 execution for all five modules; and 12 pinned
`cbor2 5.7.1` to Moss plus 12 Moss to `cbor2` comparisons. The unchanged
package implementations passed 101 unique tests in each of Native Debug,
Native Release, and Fast Debug, plus two ndarray examples in all three modes.
On the exact GO candidate, `TMPDIR="$PWD/tmp" make check` passed with 54/54
Executor tests, 61/61 editor tests, and `all Moss v0.1 tests passed`;
`make examples`, feedback validation, and whitespace checks pass. No merge into
`phase24-integration` or `main` has occurred. The authorized
`phase24-advanced-data` feature branch is published to its configured `origin`;
local and remote refs were verified equal before session closeout.

## Phase 24 Team A A3 ndarray examples complete — 2026-10-12

Branch `phase24-advanced-data-a3-examples` adds two public-API-only runnable
examples under `sdk/ndarray/examples`: 3D batched trailing-axis normalization
with broadcast affine operations, and a 4D broadcast/slice/axis-permutation/
reshape/transpose pipeline with `linalg.Matrix` round-trip. Focused workflow
tests cover the same compositions, and the package README documents Debug,
Release, and Fast Debug commands plus device-agnostic semantics with no GPU or
accelerator claim.

Validation on the completed source passes: `../../margo test`,
`../../margo --release test`, and `../../margo --interp test` each pass 21/21
tests from `sdk/ndarray`; both examples pass `margo run`, `margo --release run`,
and `margo debug`; structured `moss check --json` and dependency-aware
`moss fmt --check --json` pass for the tests and examples; developer-feedback
validation and `git diff --check` pass. No implementation work remains for the
A3 scope.

## Phase 24 Team A A8 typed CSV adapters — 2026-10-12

Implementation is complete on branch `phase24-advanced-data-a8-csv-adapters`
from baseline `7773a3006faf6b247fe808de54f6a8ec01ea95ba`. `moss.tables` now exports
explicit `CsvField`/`CsvSchema` contracts, validation, `csv_import`,
`csv_export`, and closed `CsvAdapterError` failures through a same-module
`csv_adapter.moss` source file. Margo's build metadata confirms both
`tables.moss` and `csv_adapter.moss` are included in the emitted `tables`
module. Import validates headers and widths, strictly parses all four table
kinds, preserves row order, and uses a documented backslash escape to separate
present values from the caller-chosen missing token. Export validates exact
table/schema correspondence and delegates quoting to public `moss.csv`.

All 24 package tests pass after formatting in Native Debug, Native Release, and
Fast Debug. Canonical formatter checks report `would_change: false` for the new
source and expanded tests; `python3 tests/tooling/check_agent_feedback.py` and
`git diff --check` pass. The public `moss.fmt` API has no exact Float formatter
with three-mode parity, and `fmt.format_int` cannot represent minimum Int, so
present cells in those cases raise `CsvAdapterError.UnsupportedFormatting`;
Float import and missing-only/zero-row Float export remain supported. No JSON
adapter is claimed because nested arrays/objects, absent keys versus null, and
original numeric lexemes cannot be preserved by the flat Table model without a
separate explicit contract. No Team D package, compiler, or shared test
infrastructure was modified.

## Phase 24 Team A bounded FlatBuffers/Arrow feasibility — 2026-10-12

The bounded A9–A11 assessment is complete on branch
`phase24-advanced-data-buffer-feasibility`. The new
`docs/phase24/TEAM_A_FLATBUFFERS_ARROW_FEASIBILITY.md` records that current
`moss.binary` supports correct owned bytewise construction and concrete
integer access, while fixed-schema FlatBuffers and a narrow signed-integer/Bool
Arrow IPC proof of concept are feasible only with explicit copies and limits.
General schema generation, exact Float fields, native String adapters, packed
and aligned byte storage, source-visible borrowed views, upper-half `UInt64`,
zero-copy FlatBuffers, and production Arrow IPC remain blocked capabilities.
No FlatBuffers/Arrow implementation, schema generator, compiler/runtime change,
or public API was added.

Validation completed at baseline
`0d5784241a43caba87e8f024d4b10e20b4a996ea`: live bootstrap confirmed
`moss-0.1`; `moss.binary` passed 9/9 tests in Native Debug, Native Release, and
Fast Debug; current Float-bit and String-byte probes reproduced the already
documented capability-request boundary; ownership queries confirmed the
reader/writer access modes; and `git diff --check` passed before closeout.
No new developer-experience observation was warranted, so no feedback record
was added. There is no unfinished implementation work in this bounded leaf
scope.

## Phase 24 Team A A11 reciprocal CBOR decoder ready — 2026-10-12

Branch `phase24-advanced-data-a11-cbor-decoder`, based on
`b9c6ea4049530abd503c97c0d32c444efc45c3a7`, now extends `sdk/cbor` with an
atomic iterative RFC 8949 decoder. It supports exact Moss-Int major 0/1 values,
definite byte/text strings with strict UTF-8, arrays, maps, null, and Bool. The
explicit container stack preserves linked-edge ownership for interleaved nested
structures, rejects detached/discarded children on successful returns, and
enforces exact one-item input, depth, combined item, payload, input, and
deterministic arena-allocation limits with checked cursor/length arithmetic.

Malformed/truncated input, trailing data, reserved additional information,
indefinite lengths, tags, unsupported simple values, invalid/duplicate map
keys, and out-of-range integers return typed `CborError` variants. Float16 and
Float32 return `UnsupportedFloat`; Float64 remains explicitly capability-gated
because Moss has no checked bit-preserving binary64 conversion. Tags,
indefinite forms, bignums, non-Int/text keys, other simple values, and all
floats remain outside the supported subset.

Independent fixed RFC vectors, malformed and negative contracts, all 1/2/4/8
additional-argument widths, nested interleaving, cumulative limits, integer
boundaries, and semantic encode/decode round trips are covered. The final
candidate passes 24/24 tests in Native Debug, Native Release, and Fast Debug;
canonical formatting; `python3 tests/tooling/check_agent_feedback.py`; and
`git diff --check`. A native test-lowering helper omission encountered during
development is recorded under
`agent-feedback/phase24/team-a-advanced-data/cbor-decoder/` with the multi-file
same-module workaround. No push was performed.

## Phase 24 Team I consolidated checkpoint: Team D corrective delivery merged — 2026-10-13

Team I integrated Team D's corrective delivery from `origin/phase24-data`
(tip `db5f6663e94f3711db3b55a611d13f1508f97368`, reviewed implementation
`6ef17f1c2edc1022f00f77a35bec051b1b9cdab3` with independent Architect GO)
via merge commit `8b54293` into `phase24-integration`.

P24-D-003 is resolved: the singly-linked tape AST child indexing eliminates
descendant interleaving bugs; `[[1],2]` parses and serializes to `[[1],2]`,
`{"a":{"x":1},"b":2}` preserves both lookups, nested sibling containers preserve
child ordering, compact and pretty serialization round-trip, escaped control
characters round-trip while unescaped control characters are rejected, and
`fmt.format_int(INT64_MIN)` formats `-9223372036854775808`. All 16/16 unit tests
pass in native and Fast Debug. The differential test suite passes 103/103 cases
against Python stdlib via Fast Debug.

Full combined-tree regression passed cleanly:
- Compiler: R0a bitwise, R0b same-module type resolution, imported generic expressions, and Foundations adversarial gates pass.
- Foundations: full acceptance in O0, release, Fast Debug, and source-free ABI-v9.
- Text & Encoding: 12/12 Text, 5/5 Encoding, 3/3 cross-package, 66-valid/15-invalid Python UTF-8 corpus, and release benchmark validator pass.
- Numerics: Math (18/18), Random (8/8), Time (11/11), Integration (2/2), Linear Algebra (9/9), Sparse (9/9), Statistics (9/9), Numerics Integration (3/3), differential oracles, ABI-v9, and repair verifications pass.
- Host: Path differential/source-free, Filesystem native/negative/source-free/adversarial, and Host I/O feasibility boundary pass.
- Repository: `TMPDIR="$PWD/tmp" make check` (all Moss v0.1 tests, 54/54 Executor runtime tests), `make examples` (all 32 showcase programs), `check_agent_feedback.py`, and `git diff --check` pass.

All delivered SDK packages are GREEN. The whole Phase 24 release remains AMBER /
provisional pending Team R delivery (`phase24-data-utils`) and the final whole-system
Architect review. No merge into `main` occurred.

## Phase 24 Team H Host Services — bounded-read closeout complete

The reviewed implementation candidate is
`db41c09d3086629bddc37c5aeabb585161dfdd53`; the independent Team H L6
Architect issued **GO** for Team I integration in
`docs/phase24/TEAM_H_HOST_ARCHITECT_FINAL_REVIEW.md` (report commit
`32d6c2e18dcf849e78f6288c4d41c8168afb060a`). The report-only tip is now
published on `origin/phase24-host` as `c3fc90a1b946136a5f72b30a327e954b6839c2fa`.

The branch is complete for its honest source-only scope: `moss.path` and the
bounded regular-file `moss.fs` subset are delivered; this closeout adds
`fs.read_limited(path, max_bytes)` with tag-only `InvalidLimit` and
`LimitExceeded` errors, unique repository-local fixture directories, and the
Architect's path/FileIO edge regressions as a permanent gate. `moss.io` remains
a feasibility report because cross-package FileIO capability ABI is blocked;
metadata/mutation, environment, and process APIs remain primitive-gated. Full
repository checks and examples passed. The focused Architect confirmation is
**GO** for exact implementation SHA `4d04481fceb5c258670c566ed27de879fa0cddfe`
in `docs/phase24/TEAM_H_HOST_ARCHITECT_CLOSEOUT_CONFIRMATION.md` (report commit
`638c67cf1f3cf845c4252cbe0d8d61aca6b578c4`). Team I owns any subsequent
integration.

## Phase 24 Compiler R0a/R0b Integration & Team R Baseline — 2026-10-12

Team I completed compiler integration for:
- **Workstream R0b:** Same-module type resolution (`phase24-compiler-type-resolution`, `16d07cf8703c47e72b19b598608395d94960eb18`).
- **Workstream R0a:** Integer bitwise & shift operators (`phase24-compiler-bitwise`, `8a24f422b69766c8db83f7035b9caff6452416b7`).
- **Combined compiler gate:** Successfully integrated into `phase24-integration` at commit `43757790ef3af52204c90bbd9dabcefc4470e965`. All 54 Phase 20 executor tests, discriminating precedence tests, ABI-v9 checks, `TMPDIR="$PWD/tmp" make check`, and `make examples` passed.

## Phase 24 Team R Feasibility & Initial Architectural Review complete — 2026-10-12

Team R (`moss.regex`, `moss.compress`, `moss.hash`) launched on feature branch `phase24-data-utils`:
1. **Initial Feasibility Assessment:**
   - Identified and verified core Moss v0.1 physical constraints: string code-point indexing, absence of ordinary recursion, `Vector[Int]` byte representations ($0 \le b \le 255$).
   - Transitioning from arithmetic XOR emulation to newly integrated native 64-bit bitwise operators (`&`, `|`, `^`, `~`, `<<`, `>>`).
2. **Independent L6 Architect Review:**
   - Independent L6 Architect issued Conditional Approval in `docs/team_r/ARCHITECT_REVIEW.md`.
   - Amending architecture freeze with native bitwise operators, corrected reference vectors, and strict LZ4 block framing distinctions.
3. **Branch Status:**
   - Compiler R0a/R0b baseline merged into `phase24-data-utils`. Full implementation underway.

## Phase 24 Team I consolidated checkpoint — 2026-10-12

The accepted Team D structured-data, Team T performance, Team N numerical, and
Team H path/filesystem deliveries are merged on `phase24-integration`. Team H
source is `ca78d4a74aa6d0aa86da0bdaa610d6b405fa0c52`, with Architect-reviewed
implementation `4d04481fceb5c258670c566ed27de879fa0cddfe`.

R0b same-module type resolution (`16d07cf`) and R0a integer bitwise/shift
operators (`8a24f42`) were already merged through `293a6d8` and `8e0624a`;
their focused regressions pass after rebuilding `./moss`. Team H delivers
`moss.path` and bounded regular-file `moss.fs`; `moss.io`, `moss.env`, and
`moss.process` remain explicitly deferred pending host-runtime primitives.

The nested JSON probe is still required before status can be finalized. Keep
P24-D-003 RED if semantic child/key/lookup checks reproduce the known
non-contiguous descendant bug; retain P24-D-001 and P24-D-002 as open Team D
limitations. Team R's `96b9413` remains a feasibility checkpoint, and no
accepted Team A or corrective Team D delivery is available yet. No merge into
`main` occurred.

The probe was run against the integrated tree and reproduced P24-D-003: parsing
`[[1],2]` produced `[[1],1]`, while `{"a":{"x":1},"b":2}` lost the `b`
lookup and ended with an interpreter vector-bounds error. Team D owns the
correction and must provide semantic regressions plus an exact-SHA Architect GO.
Team H acceptance passed path differential/source-free checks, fs native
debug/release/Fast Debug/negative/source-free/adversarial checks, and the host
I/O feasibility boundary probes. The combined R0 type-resolution and bitwise
regressions pass after rebuilding `./moss`.

## Phase 24 Team H Host Services — implementation candidate

The `phase24-host` branch is based on `2845390534272e330a0ecc7c9e970d1815eb0588`
and now contains the Team H architecture freeze plus the path/fs and I/O
feasibility handoffs. `moss.path` and the bounded regular-file subset of
`moss.fs` are implemented. The `moss.io` workstream preserves native probes
and a permanent report instead of publishing an unsupported cross-package
FileIO API. `moss.env`, `moss.process`, filesystem metadata/mutation, and
general byte writes remain explicitly deferred pending host-runtime primitives.

Integrated package, source-free, negative, feedback, full repository, and
example validation passed on the current candidate. The independent Team H
Architect must issue the final exact-SHA verdict before the feature branch is
published for Team I; no merge to `phase24-integration` or `main` is allowed
from this worktree.

## Phase 24 Team H I/O-services feasibility — 2026-10-12

The `phase24-host-io` worktree is based exactly on Team H host base
`2845390534272e330a0ecc7c9e970d1815eb0588`. Native probes confirm fixed
4096-byte FileIO reads, borrowed Range checksums, bounded 65536-byte EOF
collection, explicit-offset Range writes, and an imported provider that keeps
FileIO internal behind a path/value API. A permanent negative provider proves
that passing FileIO itself across packages fails with `BUILD_BACKEND_ERROR` /
Rust E0308 because consumer `moss_fileio::FileIO` and provider
`moss_hostcap::FileIO` are distinct types. Generic unbounded Range writes are
correctly rejected with `FILEIO_UNBOUNDED_REQUEST`.

Environment and process probes return `UNKNOWN_SYMBOL_OR_TYPE`; FileIO remains
unsupported in Fast Debug. No fake `moss.io`, `moss.env`, or `moss.process`
package was published. The feasibility report proposes the smallest canonical
FileIO ABI and direct non-shell environment/process runtime contracts. Run
`python3 sdk/host/io/check_feasibility.py` for the complete positive/negative
gate. Source-free FileIO-capability validation and meaningful I/O benchmarks
remain blocked pending the runtime/ABI decisions.

## Phase 24 Team I final merge checkpoint — 2026-10-12

Team I merged the final source tips `phase24-math`
(`dd24df3bc653cd9d5fa93af9f5f25a9d9c3ff9bf`) and `phase24-text`
(`4a9b9be12fd230dcbd0b176d539937c29248d2f4`) into `phase24-integration` using
normal provenance-preserving merge commits `a2864a55414fd3f21619a500fcda4c37f91c4467`
and `ee4973f5556b3cd07204e877157e35fd49ad1874`. The final integrated
implementation is `ee4973f5556b3cd07204e877157e35fd49ad1874`. Foundations and
repository-wide agent-feedback infrastructure remain intact. Conflicts were
limited to historical status/review ledgers; source and test changes merged
cleanly, and the combined imported-expression plus mixed Float/Int gates pass.

Validation passed the full Team T acceptance, Foundations acceptance, all Team M
package suites, focused shared-compiler and source-free ABI-v9 gates,
`TMPDIR="$PWD/tmp" make check`, `make examples`, and native package/integration
execution. Team M's portable oracle passed functionally, but its tracked-harness
mutation prevents treating it as a reproducibly clean gate. Team T and Team M
benchmark targets execute, while provider-optimization, workload-equivalence,
constant-folding, and result-observability defects keep comparisons unvalidated.

The checkpoint remains **AMBER**. Team-specific correctness and methodology
follow-ups are assigned in `docs/phase24/INTEGRATION_ISSUES.md`; exact commands
and results are in `docs/phase24/INTEGRATION_STATUS.md`. Team I's feedback is
under `agent-feedback/phase24/team-i-integration/`. Per coordinator direction,
the independent whole-system Architect review is reserved for the final Phase
24 acceptance milestone. No merge into `main` occurred.

## Phase 24 Team T Text & Encoding complete — 2026-10-12

`phase24-text` was forked from feedback-enabled integration SHA
`610f2d632b53541cc31153174e37e1b1640d6f01`. It delivers `moss.text`, the
strict pure `moss.encoding` UTF-8 codec, external/source-free acceptance,
native debug/release/Fast Debug validation, a deterministic Python oracle,
benchmarks, documentation, and seven workstream feedback trails.

The independent Team T L6 Architect issued **GO** for exact implementation
SHA `1c01a899819d495347fd99e05500bb50e50e7da1`. Its earlier review correctly
issued NO-GO for `a2f0cd7` because the documented Rust baseline was ignored and
absent from Git; `1c01a89` tracks and reproduces that source. Fresh adversarial
UTF-8/text cases, all three execution modes, compiled-only ABI-v9, semantic
ownership/effect queries, the full Python corpus, and benchmarks passed.

Coordinator gates pass: the full Team T acceptance runner; feedback
validation; `TMPDIR="$PWD/tmp" make check` (all Moss v0.1
tests, 54/54 Executor tests); `make examples`; Margo and Rust benchmarks; and
`git diff --check`. Layer B String interoperability is explicitly deferred
because Moss v0.1 exposes no supported String/UTF-8 byte conversion or scalar
String constructor. Team I alone may integrate this branch; no merge to `main`
is authorized.

## Phase 24 repository-wide agent feedback bootstrap — 2026-10-12

Phase A infrastructure is complete on `phase24-agent-feedback-bootstrap`.
The reviewed implementation commit is
`0a99ee9fb3797404d8d84cc429313df8fd6818dd`, based directly on
`e563401a4938b76bcfce1a9c727c55b155fdc652`. It defines the repository-wide
developer-experience policy, versioned schema, dependency-free JSONL validator,
valid examples, summary template, Team T guidance, onboarding hooks, and test
workflow integration. `AGENTS.md` and `moss-agent-workflow` require ongoing
ordinary-development capture while preserving the separate SWARM experimental
protocol; work qualifying for both cross-links evidence rather than duplicating
issue stores. The AGENTS Git boundary now positively authorizes remote pushes
only on explicit user direction, replacing its earlier no-push wording.

The independent Architect issued **GO** for exact candidate `0a99ee9` after
fresh positive, critical/negative, malformed, duplicate, path, rogue-filename,
empty-repository, and Draft 2020-12 schema checks. It also verified that existing
SWARM policy, schemas, and validators are unchanged. Focused feedback, skill,
SWARM, shell, and whitespace checks pass. On the same candidate,
`TMPDIR="$PWD/tmp" make check` exited 0 with 54/54 executor tests and
`all Moss v0.1 tests passed`; `make examples` built every positive example.

The bootstrap branch is published at `b647586b463758c0dc7cc43db4f1e6280d274ef5`.
After a fresh fetch confirmed that Team I's backplane still pointed to `e563401`,
the authorized feedback-only ordinary merge was created and pushed as
`d62010e960f0db9b798a5b1ac37a9197d07b633d`. Post-merge feedback, skill,
SWARM, ancestry, and whitespace validation passed. Team I retains routine
integration ownership. Team T must fork `phase24-text` from the current remote
integration tip containing this merge and record that exact fork point.

## Phase 24 Foundations SDK independent Architect final adversarial GO; L7 merge gate — 2026-10-12

Exact reviewed implementation: `23d625ba09d20490b8c5e98beddcd246b2bac416`
on `phase24-core`. Following L7's conditional GO, the independent Architect L6
executed an explicitly adversarial, falsification-oriented campaign covering:
1. Advanced imported generic compositions (multi-level indexing, nested field projections,
   arithmetic, boolean/comparisons, nested generics, and combined indexing/projections)
   across native debug, native release, Fast Debug, and compiled-only ABI-v9 providers.
2. Ownership, mutability, and synchronization boundaries (rejection of immutable inputs
   to mutating algorithms with `IMMUTABLE_LOCAL_MUTATION`, domain-state exclusive 2PL locking
   with `EXCLUSIVE` mode in compiler's synchronization plan, invalid byte value safety).
3. Algorithm correctness across degenerate boundaries (empty collections, single elements,
   duplicate keys, and extreme signed Int64 boundaries `INT64_MIN` and `INT64_MAX`).
4. Controlled benchmark verification distinguishing algorithm implementation cost (heapsort
   is within 8.5-9% of Rust's own heapsort, validated byte copy is within 3.3% of Rust)
   from allocation overhead and pdqsort heuristics.

All adversarial falsification tests passed with 0 failures, and the permanent gate
`check_phase24_adversarial_architect.py` is integrated into `tests/run.sh`.
The full acceptance test suite (`python3 sdk/foundations/acceptance/run_all.py --full`),
all four package unit test suites (15/15), repository `TMPDIR="$PWD/tmp" make check`
(54/54 executor tests; `all Moss v0.1 tests passed`), `make examples` (all 32 programs),
strict C++17 compilation (0 warnings, 0 errors), and `git diff --check` pass cleanly.
The separately attributable **GO for Phase 24 Foundations SDK** is recorded in
`docs/PHASE_24_FOUNDATIONS_ARCHITECT_FINAL_ACCEPTANCE.md`.

**Engineering L6: GO. Independent Architect L6: GO at `23d625b`. L7:
HOLD merge pending explicit approval.** No merge into `main` has occurred.

## Phase 24 Foundations SDK independent Architect GO; L7 merge gate — 2026-10-11

Exact reviewed implementation: `bcebfaa27e57c8bfbc43ff124d9c0e5a953a9926`
on `phase-24-foundations-integration`. The independent Architect L6 retested
its earlier indexed vector literal counterexample (`[collections.index_of(...)][0]`,
`[1, 2][0]`, and `collections.reversed_copy_int(...)`) in native debug/release
and Fast Debug, as well as the earlier field-projection generic call repair
(`probe.make(7).value`). Both P1 counterexamples are verified resolved in
source-backed and compiled-only ABI-v9 provider contexts.
The separately attributable **GO for the approved Phase 24 Foundations scope**
is in `docs/PHASE_24_FOUNDATIONS_ARCHITECT_FINAL.md`. The earlier `20ccbcf`
NO-GO remains valid for that older SHA.

Four independent Margo packages (`moss.core`, `moss.bytes`, `moss.collections`,
`moss.algorithms`) under `sdk/foundations/` are implemented, documented, and tested.
The full acceptance test suite (`python3 sdk/foundations/acceptance/run_all.py --full`)
passed 44 generated sort/search cases and 8 byte cases across native O0, native release,
Fast Debug, and source-removed ABI-v9 providers. Smoke acceptance is integrated into
`tests/run.sh`. Full repository `TMPDIR="$PWD/tmp" make check` exited 0 (`all Moss v0.1 tests passed`),
`make examples` built all 32 showcase programs, strict C++17 compilation
(`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -fsyntax-only src/moss.cpp`)
passed with zero warnings/errors, and `git diff --check` passed cleanly.

**Engineering L6: GO. Independent Architect L6: GO at `bcebfaa`. L7:
HOLD merge pending explicit approval.** No merge into `main` has occurred.
## Phase 24 Team M: L7 Adversarial Repairs & Dual GO Verification (`2eee39a`) — 2026-10-12

Exact reviewed candidate commit: `2eee39ae2780389303846bd7029243d039e1c467` on branch `phase24-math`.
Baseline evaluated: `129a81fa54a4f3d9b7f6d76bb376a9f85c3d9256` (historical NO-GO).
Independent Adversarial Numerical Reviewer and L6 Architect conducted comprehensive re-evaluation following implementation repairs across `moss.math`, `moss.random`, and `moss.time`.

**Verification Results:**
1. Unit & Integration Tests: 37/37 passed (0 failed) across 4 packages (`math` 18/18, `random` 7/7, `time` 10/10, `integration` 2/2).
2. Independent Adversarial Oracles: 100% PASS (Zero falsifications) via `python3 tests/oracles/verify_repairs.py`.
3. Correctness Repairs Verified:
   - `moss.math`: `atan(x)` range reduction fixed across $(-\infty, \infty)$ including $x = 1.0, 1.5, 2.0, 3.0, -2.0, \pm\infty$ (max error $< 1$ ULP); `exp(x)` subnormals preserved down to $-745$ (e.g. $x=-710 \to 4.476 \times 10^{-309}$) and finite results up to $709.78$ without intermediate $2^{1024}$ overflow ($x=709.5 \to 1.355 \times 10^{308}$) with split $2^k$ scaling and split $\ln 2$; $\text{hypot}(\pm\infty, \text{NaN}) = +\infty$.
   - `moss.random`: Int64 signed overflow eliminated; mathematically proven uniform multi-draw sampler over $[0, AM-1]$ with zero rejection on $w \pmod A$ and rejection limit $L = AM - (AM \pmod R)$ eliminating modulo bias; Phase 21 typed error contracts (`export enum RandomError { InvalidRange, EmptyCollection }`) enforced.
   - `moss.time`: Saturating arithmetic across all operations clamping at `INT64_MAX`/`INT64_MIN`; `duration_abs(INT64_MIN)` clamps to `INT64_MAX`; Phase 21 typed error contracts (`export enum TimeError { DivisionByZero, InvalidDate }`) enforced (`duration_div(d, 0)` raises `TimeError.DivisionByZero`); complete Gregorian calendar validation rejects April 31 and Feb 29 on 1900 with `TimeError.InvalidDate`; Euclidean division for pre-1970 Unix timestamps. Real clock reading (`.now()`) explicitly documented as deferred.
4. Full Gates: `TMPDIR="$PWD/tmp" make check` PASS (all Moss v0.1 tests and 54/54 Phase 20 executor tests pass); `make examples` PASS (32/32 examples pass); `./margo bench` recorded live (sub-nanosecond to microsecond latencies).
5. Separately Attributable Verdicts:
   - Independent Adversarial Numerical Reviewer: **GO FOR MERGE REVIEW** (Zero falsifications).
   - L6 Architect: **GO FOR MERGE REVIEW** (Full architectural, contract, and correctness compliance).
   - Merge Gate: Branch remains strictly on `phase24-math`; **zero commits merged into `main`**.

## Phase 24 Team M: Independent Architect L6 Review — NO-GO — 2026-10-12

Exact reviewed candidate commit: `129a81fa54a4f3d9b7f6d76bb376a9f85c3d9256` on `phase24-math`.
Independent Architect L6 completed evaluation and rendered an attributable **NO-GO** in `docs/team_m/ARCHITECT_REVIEW.md`.

While Team M achieved clean execution of 23 existing unit tests, verified compiler lowering in `src/moss.cpp` (full `TMPDIR="$PWD/tmp" make check` passed), and unblocked environment operations, deep architectural and numerical falsification revealed 7 critical blockers:
1. `moss.math`: `atan(x)` range reduction for $|x| > 1.0$ is inverted, returning $0.4636$ instead of $1.1071$ at $x=2.0$ (error > 58%), cascading into `asin` ($0.99 \to 1.7123 > \pi/2$), `acos` ($0.99 \to -0.1415 < 0$), and `atan2`.
2. `moss.math`: `exp(x)` truncates to $0.0$ at $x < -708.3964$, missing IEEE-754 subnormals down to $-744.44$, and overflows to $\infty$ at $709.78$ due to unsplit $2^{1024}$ float overflow, failing to return finite results up to $709.7827$. Missing split $2^k$ scaling and split $\ln(2)$.
3. `moss.random`: Violates Phase 21 error semantics (missing `export enum RandomError { InvalidRange, EmptyCollection }`), silently returning $0$ on empty choice and $min$ on inverted ranges.
4. `moss.random`: Two-draw sampler outcome constant `total_outcomes = (m_half + 1) * m` mismatches generated domain $[0, A \cdot M - 1]$, failing rejection sampling and injecting modulo bias into large integer ranges.
5. `moss.time`: Violates Phase 21 error semantics (missing `export enum TimeError { DivisionByZero, InvalidDate }`), silently returning $0$ on duration division by zero.
6. `moss.time`: Core duration and timestamp arithmetic (`duration_add`, `sub`, `scale`, `instant_add_duration`, `timestamp_diff`) does NOT saturate, silently wrapping around $2^{63}-1$ to negative values under signed two's complement integer arithmetic.
7. `moss.time`: Incomplete Gregorian calendar validation allows April 31 and Feb 29 on century non-leap year 1900, which silently mutate on roundtrip (April 31 $\to$ May 1, Feb 29 1900 $\to$ March 1).

**Independent Architect L6 Verdict: NO-GO FOR MERGE INTO `main`.** Remediation roadmap defined in `docs/team_m/ARCHITECT_REVIEW.md`.

## Phase 24 Team M: Post-L7 Review Repairs & Live Validation Verified — 2026-10-12

Following L7 NO-GO merge gate on HEAD `4abed3451dfd8a475ed6641bcf6bf0cb7b6f6c1c` on `phase24-math`,
Team M resolved all requested repairs and unblocked live execution in the workspace:
- `moss.math`: Fixed `abs(-0.0)` sign clearing, IEEE-754 signed zero in `min`/`max`,
  `pow(NaN, 0.0) == 1.0`, `atan(1.0)` range reduction ($\pi/4$ exact), and large
  magnitude hyperbolic clamping (`tanh(1000) == 1.0`). Normalized unary variable
  negations to `0.0 - var` and added exported `is_finite`/`is_nan` aliases.
- `moss.random`: Repaired `randint(0, 4294967086)` rejection sampling boundary (preventing
  infinite loops), added multi-draw 64-bit integer range synthesis, guarded seed
  initialization against intermediate integer overflow, and defined empty collection fallbacks.
- `moss.time`: Added duration division-by-zero protection, overflow clamping on large durations,
  Euclidean negative Unix timestamp handling, extended ISO 8601 year formatting (`+YYYYY`),
  and explicitly marked real-clock `.now()` functionality as DEFERRED pending L7 intrinsic approval.
- `moss.integration`: Verified cross-package workflow, qualified return type `-> time.Duration`,
  and executed `margo run` successfully.
- Compiler lowering bugfix: Repaired `src/moss.cpp` Rust code generator where mixed float/int
  operations with non-literal integer variables (e.g. `z * 1.0`) were emitted without `as f64`,
  causing rustc E0277.
- Documentation: Updated `STAGE4_VALIDATION.md`, `STAGE5_BENCHMARKS.md`, and `FINAL_DELIVERY_REPORT.md`
  with live measured execution metrics and test results.

**Live Validation Results:**
- `packages/math`: 9/9 unit tests PASS via `margo test`.
- `packages/random`: 5/5 unit tests PASS via `margo test`.
- `packages/time`: 7/7 unit tests PASS via `margo test`.
- `packages/integration`: 2/2 integration tests PASS via `margo test`; executable runs via `margo run`.
- Native benchmarks: 11 benchmark targets executed cleanly via `margo bench` (1-38 ns for math,
  552-2933 ns for random, 2-872 ns for time).
- Status: **VALIDATED BY LIVE RUN — READY FOR L7 MERGE REVIEW**. No merge into `main` has occurred.

## Phase 21 independent Architect GO; L7 merge gate — 2026-10-11

Exact reviewed implementation: `19cf731ae08291500e7488c8a03df123aac4f9aa`
on `phase-21-integration`. The independent Architect L6 retested its earlier
nested Root expression counterexample in checker, native O0/O3, Fast Debug
and a consumer rebuilt after removing provider source. It also challenged
no-trailer and nested-handler safety, ABI-v9 binding, O1/O4/O8 and bounded
early-placement gates. Its separately attributable **GO for the approved
Phase 21 scope** is in `docs/PHASE_21_ARCHITECT_V9_RETEST_19CF731.md`. The
earlier `96eef9e` NO-GO remains valid for that older SHA.

Engineering's `make check`, `make examples`, strict GCC/Clang C++17, four-pair
Float matrix, source-free and focused Root gates pass on the reviewed
implementation source. The Architect separately found that whole-vector
`echo [1]` checks but fails generated Rust with E0277 on both `96eef9e` and
`19cf731`; it classified this pre-existing general compiler defect outside
the Phase 21 Root/ABI acceptance contract. Track for checker/native output
hardening. The supported vector-element Root case passes.

**Engineering L6: GO. Independent Architect L6: GO at `19cf731`. L7:
HOLD merge pending explicit approval.** No merge into `main` has occurred.

## Phase 21 independent Architect NO-GO and engineering repair — 2026-10-11

Independent Architect L6 reviewed exact pushed `96eef9e` and recorded an
attributable **NO-GO** in `docs/PHASE_21_ARCHITECT_V9_REVIEW.md`. It found
that an exhaustive Root `on_fail` was falsely rejected when `message` appeared
inside a binary, vector or ordinary call expression in `main`, including a
source-free provider case. The Architect independently reran 30+ ABI-v9,
effect, ownership, lock, FileIO, Float and original A–F gates; those passed
within the report's stated limits.

Engineering repaired the recursive Root-ingress effect context at source
`3a964dc9aa22bfc7d85cccaa57515c532ee839b2` and added
`check_phase21_root_expression_ingress.py`. The valid forms now check and
execute in native O0/O3 and Fast Debug; a consumer rebuilt after provider
source removal executes, while a nested Root without `on_fail` still fails
`ROOT_UNHANDLED_RAISE`. The isolated repair worktree passed full `make check`,
`make examples` (32), strict GCC 13.3/Clang 18.1 C++17, and the four-pair
Float matrix. The integration tree also passed full `make check` and
`make examples`. Exact-SHA Architect retest is next.
Engineering recommends GO for independent retest; **main merge remains HOLD**.

## Phase 21 original A–F final acceptance handoff — 2026-10-11

L7 supplied a single-text bundle described as a byte-for-byte transcription
of the original A–F README and six `.moss` programs. Those files were imported
unchanged into `tests/tooling/fixtures/p21_open_at_7775049/`; the actual ZIP
was unavailable for independent byte comparison. The earlier original
`p21_review_repros.zip` is still unavailable, and its cases retain
reconstructed provenance. Source hashes, command forms, exact exits and output
are in `docs/PHASE_21_ORIGINAL_AF_RESULTS.md`.

On compiler implementation `c1ef3ebb13d7c74c3ed86a45d45e536d527161da`
(pre-evidence worktree HEAD `c59ede0`), all six supplied original cases met
their README expectations: A rejects unhandled `E.A`; B and F print exact
canonical Float text in O0/O3/Fast Debug; C checks with only `E.A` escaping
and prints `outer A`; D/E check, report exact typed effects and print `-1`
through Root `on_fail` in native O0/O3. The unchanged D/E Fast Debug runs
produce the documented Phase 20 FileIO unsupported diagnostic. The original
D/E legacy `FileError` declarations needed no ABI-v9 migration variant.

The permanent original-source gate and final `make check` rerun both pass;
`make examples` also passes on the combined tree. Engineering recommends GO for
independent Architect acceptance. The strict GCC/Clang and four-pair Float
results below apply to the same compiler source and were run before these
original files entered the regression corpus.
At this earlier checkpoint the Architect had not voted on the ABI-v9 source;
the later NO-GO at `96eef9e` is recorded at the top of this status file.
The historical `5941d98` NO-GO remains attributable to that earlier SHA.
No merge into `main`.
The final acceptance evidence is committed and pushed to
`origin/phase-21-integration` following the user's explicit push request.
L7 must arrange independent Architect review of the exact candidate before
any merge vote.

## Previous Phase 21 pre-original acceptance checkpoint (2026-10-11)

Exact validated compiler/runtime/test source:
`c1ef3ebb13d7c74c3ed86a45d45e536d527161da` on
`phase-21-integration`. Later release-packet commits add unchanged original
fixtures, a regression gate and documents without changing compiler source.
The independent engineering proof reviewer ran 13 focused O1/O4/O8 and
bounded early-release gates on this source; all passed under the documented
accepted/fail-closed contract. Reconstructed external A–F cases, ABI-v9
artifact binding and migration, source-free methods/warnings, compiler-defined
standard errors, Root totality and checked pipelines are integrated.

`TMPDIR="$PWD/tmp" make check` exited 0 with `all Moss v0.1 tests passed`.
`make examples` passed 32 positive builds. Strict GCC 13.3.0 and Clang 18.1.3
C++17 `-O2 -Wall -Wextra -Werror -pedantic` builds passed. The strict Phase 20
review passed 36 bug, 30 guard and two pre-existing probes; eight design
proposals remain INFO. All four GCC/Clang × Rust 1.97.0/1.98.1 Float runs
matched native O0/O3 and Fast Debug byte-for-byte against the pinned corpora.
Actual source-removed providers, warm-cache and recomputed-hash falsification,
concurrent Executor/Root/Branch, and FileIO fault/cleanup gates passed within
`make check`. Refetched `origin/main` remains
`d15e968778fa1cff3cd5085911c505592af54a30`, an integration ancestor.
The external reproduction ZIPs were unavailable; L7 approved reconstructed
permanent cases, and that provenance remains explicit in the peer register.

**Engineering L6: GO for independent Architect acceptance.** The last
independent Architect L6 vote is historical NO-GO at `5941d98`, before the
approved ABI-v9 and A–F repairs; it is not a vote on this source. **L7: NO-GO
for merging into `main`** until independent review and explicit authorization.
Full evidence and limits are in `docs/PHASE_21_FINAL_REVIEW.md`,
`docs/PHASE_21_PEER_REVIEW_DISPOSITION.md`, and
`docs/PHASE_21_FINAL_PROOF_AUDIT.md`. Do not merge into `main`.


## Phase 21 autonomous completion checkpoint — 2026-10-11

Integration `phase-21-integration` is pushed through implementation/test SHA
`5439a2578cc529ffc18eb14a6450c9c820bd78d9` and remains **NO-GO for
`main`**. The approved ABI v9 provider-artifact binding, imported method
effects, recovery-warning provenance, compiler-defined standard errors and
cross-crate standard-error WRITE bridge are integrated. On this combined
compiler, source-free ABI execution, recomputed-hash/warm-cache falsification,
warning parity, standard-error namespace/WRITE order/Root recovery, pinned
Float IEEE plus reconstructed B/F expression corpus, reconstructed D/E chunk
pipeline native O0/O3, peer effects, and actual O9 Branch error gates pass.
Those are focused results, not a final full-suite verdict.

The external A–F ZIP and original peer ZIP are unavailable in this workspace;
L7 explicitly approved reconstruction. Reconstructed A and C are now permanent
red gates in `check_phase21_addendum_a_c.py`: A loses the raise from a trailing
local method expression; C falsely widens grouped bare re-raise effects to an
unreachable variant. Their isolated repairs are in progress. An independent
pipeline reviewer found that a raising placeholder mapper inside lexical
`try` checks but emits an invalid cross-closure recovery break; its test/fix
is in progress. The D/E fix is not closed until that gate passes. The source-free
WRITE argument-order defect was repaired at `60cd8ec` and its native O0/O3,
Fast Debug and removed-source gates pass on the combined tree.

The latest complete `make check`, `make examples`, strict GCC/Clang C++17,
strict Phase 20 review and GCC/Clang × Rust 1.97/1.98 Float matrix still apply
only to older implementation SHAs. Rerun all after the A/C and pipeline fixes,
then update `docs/PHASE_21_PEER_REVIEW_DISPOSITION.md`,
`docs/PHASE_21_FINAL_PROOF_AUDIT.md` and `docs/PHASE_21_FINAL_REVIEW.md` with
the exact code SHA and results. Independent Architect L6 acceptance is pending
L7 coordination. Do not merge into `main`.

## Phase 21 ABI v9 remediation in progress — 2026-10-11

The integration branch now contains the ABI-v9 artifact digest/rlib marker
checkpoint, compiler-defined standard errors, expression-point exceptional
ownership, and the failure-arm capture-type planner repair. The forged
failure-pattern metadata gate, source-free ABI/imported-effects gates,
standard-error namespace gate, absolute Root boundary gate, expression-edge
gate, recover-reinitialization gate, and leading-condition gate pass on the
combined compiler through `ed9ff0c` (the later proof-document commit has no
compiler edit). This remains an engineering checkpoint: imported source-free
method WRITE effects and warning provenance are being repaired in the ABI
worktree; standard-error transport through imported methods/messages is being
repaired in the standard-error worktree. The numeric package gate exposed a
stale ABI-v8 `.mossi` in its reusable scratch build directory that the v9
marker generator incorrectly treated as fresh. The FileIO adversarial gate
also needs its exported user `FileError` namesake fixture migrated to the
canonical `std.FileError` recovery selector. Neither is waived. Full `make
check` and final toolchain validation are pending after these repairs.

After that checkpoint, `838aae6` integrated native standard-error bridges
and the FileIO fixture migration. The adversarial lock/FileIO gate and new
source-free O1/O8 iterator/capture gate pass. A further independent ABI-v9
audit proved that a warm consumer build can return its cached executable
without revalidating a changed external `.mossi` or missing rlib; the
permanent negative gate is `check_phase21_source_free_cache_binding.py` and
the ABI worktree is repairing the cache path. The standard namespace
source-free handler reply gate is also red: an imported handler's synthetic
`reply 0` conflicts with the provider contract's `std__ParseError` reply.
These are known live failures, not accepted limitations. The current `make
check` run stopped at the now-migrated FileIO fixture; rerun it after ABI
repairs.

L7 has authorized ABI v9, compiler-recognized standard errors, absolute Root
typed-error totality, the O2 wording correction, and general O1/O4/O8 and
early-release proof closeout. The current `phase-21-integration` branch is an
engineering checkpoint, **not a release candidate**. Independent worktrees
are implementing the ABI, constructing standard-error/Root conformance gates,
and auditing exceptional control flow and ownership. The integration branch
now includes the approved O2 correction and failed-create durability caveat.

The leading-condition typed-raise and recover-after-reinitialization
counterexamples are repaired in `184a3b1`. Their focused native O0/O3/Fast
Debug gates pass; on_fail capture, recovery semantics and synchronization
plan gates also pass. `c5d7e08` closes an O1 hole by rejecting implicit
typed-raising custom iterator `iter()`/`next()` calls at Moss checking with
`RAISING_ITERATOR_UNSUPPORTED`, including generic specialization, until
checked iterator lowering is implemented. It also includes implicit method
observable summaries, including panic. The bounded early-placement and O8
protected-state capture gates pass. These are targeted results; the combined
full suite has not yet been rerun. The standard-error namespace gate remains
red at `UNKNOWN_ERROR_TYPE` for declaration-free `ParseError`, while the
absolute Root boundary gate passes its current native/Fast Debug/source-free
matrix. ABI v9 implementation and independent proof review remain active.
The proof audit has since added a red expression-originated O4 counterexample:
`consume_after(fail(), data)` in a `try` raises before `data` is evaluated,
yet statement-level ownership snapshots currently mark `data` consumed at
`recover`. The reverse argument order correctly rejects. The enabled
`check_phase21_expression_ownership_edges.py` gate preserves both cases;
evaluation-order exceptional-edge lowering is under repair.
Earlier `make check` and Float matrix results below apply only to the prior
implementation SHA and do not validate the eventual ABI v9 tree.

## Phase 21 peer-review engineering validation — 2026-10-11

Integration branch `phase-21-integration` is reconciled with `origin/main`
`d15e968778fa1cff3cd5085911c505592af54a30`. The frozen implementation
SHA is `5941d98c824f31550f520891235c8b7ac96f3992`; later changes are
report-only. It narrows declared `FileError` presence validation after a
full-suite counterexample on `263ad67`: legacy Phase 20 FileIO sources without
a typed enum incorrectly received `UNKNOWN_ERROR_TYPE`. Declared malformed
and ambiguous contracts still fail closed, and typed recovery selectors still
require a declaration.

`TMPDIR="$PWD/tmp" make check` now passes (`all Moss v0.1 tests passed`),
including strict Phase 20 review (36 bug, 30 guard, two pre-existing probes;
eight design INFO), native/Fast Debug, source-removed providers, concurrent
Executor/Branch and FileIO fault-injection gates. `make examples`, strict
GCC and Clang C++17, and the GCC/Clang × Rust 1.97.0/1.98.1 Float matrix pass
on this implementation. The 270-value corpus SHA-256 is
`91553f7482c35cfef6c108c429c7bdb54fba4cc8d4959c4d0a7bd3faf1ca28ef`.

The independent release-gate audit on immediately preceding `263ad67`
passed strict Phase 20, examples, actual source-removed provider execution,
imported function/generic effects, Root FileIO fault injection, Executor,
Branch, Range fail-closed controls, and native/Fast Debug focused gates. Both
that audit and the current reproducer confirm that forged ABI-v8 `.mossi`
metadata builds an executable that aborts. The current source-free
imported-helper warning repro still fails with no warning. The Architect L6
independently retested exact implementation `5941d98` and issued **NO-GO**;
its findings and per-issue dispositions are in
`docs/PHASE_21_ARCHITECT_REVIEW.md`. O1/O4/O8 and §42 early-release general
arguments remain incomplete. Engineering and L7 also remain NO-GO. No merge
into `main` is authorized.

## Phase 21 peer review EXT-12/15 isolated remediation — 2026-10-11

The `phase-21-peer-major-minor` branch corrects observable effects for IEEE
Float division while keeping integer division panicking. The focused Float
gate passes native unoptimized, native optimized, and Fast Debug output and
asserts Float and Int effect summaries. The checker now rejects malformed
declared `FileError`, `ParseError`, and `ConversionError` shapes when their
built-in operations are used, and reports a missing `FileError` contract at
Moss checking. The new contract gate passes valid native typed recovery,
numeric Fast Debug recovery, missing and malformed negative diagnostics, and
an unrelated user `FileError` enum. Fast Debug FileIO execution remains
unsupported by the existing interpreter.
An additional cross-module gate preserves an unrelated provider-owned
`FileError.Full(code: Int)` while a consumer uses its own six-variant FileIO
contract. This guards against accidentally treating every imported enum with
the same short name as the consumer's built-in error type.
The package reproducer also exposed two pre-existing native identity defects:
multiple qualified FileError names can leave generated Rust without a
FileError alias (E0425), and a module-qualified specific recovery selector
does not match the plain FileError identity raised by FileIO. The isolated
follow-up fails both cases at Moss checking with
`FILEIO_ERROR_IDENTITY_UNSUPPORTED`; it does not conflate nominal identities
or change ABI v8. When the consumer has no local FileError, a unique imported
declaration must still have the six approved tag-only variants. Full
cross-module FileIO recovery and implicit built-in enum availability remain
design decisions, not accepted fixes.

At the `8d8a9cc` checkpoint, `moss check` accepted `FileError.IO(code: Int)`
with FileIO use, then rustc failed E0533. The isolated branch rejects it with
`BUILTIN_ERROR_TYPE_SHAPE`. The strict Phase 20 review on the isolated parent
baseline still has eight Root-totality migration failures; all are
`ROOT_UNHANDLED_RAISE`, with no new built-in error contract failures. Full
integration gates and independent architectural acceptance remain with L7.

## Phase 21 peer review EXT-05 engineering checkpoint — 2026-10-11

The isolated `phase-21-peer-imported-effects` branch preserves checked `.mossi`
observable effects for source-free ordinary and generic functions. A permanent
gate tests variant-precise raises, panic summaries, native source-free execution,
Root recovery, Moss rejection of unhandled raises, and rehashed missing effect
metadata. Source-backed raising object methods now compile through their
`MossAccess_*` trait defaults and pass native/Fast Debug parity. Source-free
object methods remain unsupported under frozen ABI v8 because `object_method`
records contain no observable-effect summary; such calls fail at Moss checking
with `IMPORTED_METHOD_EFFECTS_UNAVAILABLE`, including mapper callbacks. This is
an interim safety restriction, not semantic acceptance. An approved `.mossi`
contract revision is required for full EXT-05 closure; no ABI record was changed.

Completed local validation: `check_phase21_imported_effects.py`,
`check_phase21_source_free_abi.py`, `check_borrowed_reads.py`, strict C++17
syntax compilation, and `git diff --check` all passed after the last compiler
edit. Full integration `make check` and `make examples` have not run on this
isolated branch; L7 must run them after cherry-picking the fix.

## Phase 21 final independent review candidate — 2026-10-11

The Phase 21 implementation checkpoint `8d8a9cc6f842e11701dbedc56e3d2d2d3da9852a`
was reconciled with current `main`/`origin/main`
`d15e968778fa1cff3cd5085911c505592af54a30` by integration merge
`7eb192b`. All four independent review areas completed. Their defect fixes
and adversarial regressions are integrated through implementation commit
`c89f5644785dad60c108f80609c8692364a8c190`, without changing the frozen
Phase 21 ABI v8. The full O1–O9 matrix, implementation locations, findings,
test evidence, assumptions, and approval gates are in
`docs/PHASE_21_FINAL_REVIEW.md`.

After all implementation edits, `TMPDIR="$PWD/tmp" make check` passed
(`all Moss v0.1 tests passed`, including strict Phase 20 review: 36 bug and
30 guard probes passed), `make examples` passed, and strict C++17
warnings-as-errors compilation passed. Source-free provider execution with
Moss source removed and native/Fast Debug parity passed the tested cases.
Reviewer 3's concurrent O9 case passed 64 native runs; Reviewer 2's lock
gate covered 45 concurrent failure and 45 NotFound Roots. Documentation
edits after those results do not invalidate compiler/runtime validation.

Recommendation is **NO-GO for merging into `main`**. A forged `.mossi` that
swaps failure-arm pattern identities while recomputing its existing hashes
still passes check/build and aborts at runtime; fixing cross-artifact
authenticity needs an approved ABI/contract decision. The required extra
Rust/C++ Float toolchain combination was unavailable locally. The general
early-release proof and exhaustive O4/O8 invariant arguments remain open.
Concrete cross-crate typed `Range` now fails closed at the Moss call site,
pending a broader Phase 20 native ABI repair. No waiver is assumed. Stop at
the human approval gate; do not merge into `main`.

## Phase 21 independent locking/FileIO review — 2026-10-11

Reviewer 2 audited the compiler's recovery/lock plan in `src/moss.cpp`
(`specialized_handler_effects`, `specialized_handler_failure_effects`,
`derive_path_placement`, `build_synchronization_plan`), physical wrapper
acquisition/release and Root failure dispatch in `src/handler_lowering.inc`,
and FileIO abandonment/claim release in `src/fileio_runtime.hpp`. A concrete
native defect was found: Phase 21 checked FileIO operations still lowered to
Phase 20 panicking calls, so injected `FileSync` inside an `on_fail` `try`
aborted instead of reaching `recover`. The repair lowers open, read (including
batch and expression forms), write, sync, and close through checked runtime
calls with typed propagation in Phase 21 result contexts. A source-free
provider whose only FileIO appears in an `on_fail` arm now emits the FileIO
runtime and all intermediate raised variants needed by that arm.

The new native adversarial gate in `tests/tooling/check_phase21_lock_fileio_adversarial.py`
checks separate attempt/failure class sets, old-guard release before new
acquisition, inode-claim absence at the failure acquisition, injected partial
write, abandoned-close, read, trailer-sync and expression-sync failures,
nested re-raise, 45 concurrent failure Roots, 45 concurrent NotFound Roots,
and provider execution after removal of its Moss source. This is behavioral
evidence, not a general proof. Existing Phase 21 Root cleanup, nested handler,
source-free ABI, recovery and Branch tests and strict Phase 20 FileIO suites
also pass. On the final reviewer branch, repository-local `TMPDIR make check`
ended with `all Moss v0.1 tests passed`; `make examples`, strict C++17
warnings-as-errors syntax compilation, and `git diff --check` passed.

O1/O2/O3/O5/O6/O7 have direct implementation and regression evidence from
this review, subject to the effect walk, tagged-return and runtime ownership
invariants documented in the Phase 21 specification. The distinct §16
`2PL/early-release semantics` row remains **INCOMPLETE**: bounded Phase 15.3
untouched-guard cancellation is visible in implementation, but the design
explicitly leaves a general mid-execution early-release proof open. Reviewer
2 did not claim O9's complete Branch quiescence invariant; that is Reviewer
3's area. The release candidate still needs the coordinator's integrated
validation and human approval before any `main` merge.
## Phase 21 independent source-free/numeric review — 2026-10-11

Reviewer 4's isolated branch `phase-21-review-sourcefree-numeric` was cut from
integration `82dd956`. It found two distinct issues. First, a source-free
provider with two tag-only raised variants accepted a `.mossi` file with its
two `on_fail` callable symbols swapped, linked successfully, and aborted at
runtime. The loader now checks the existing ABI-v8 concrete and semantic
section hashes plus the canonical `on_fail` symbol derived from module,
domain, handler, and arm index. This is a validation-only repair; the ABI-v8
layout and emitted records are unchanged. The focused source-free gate passes
with both unadjusted and recomputed-hash symbol-swap regressions.

The stronger falsification case remains **unresolved**: swapping only the two
failure-arm pattern identities, recomputing all existing hashes, and retaining
canonical symbols passes `moss --check` and native build but aborts at runtime
(SIGABRT, exit 134). Existing hashes verify file consistency, not that the
`.mossi` contract matches the provider rlib. A general repair needs a
provider-artifact-bound contract or provider-owned dispatch, which changes
the frozen cross-artifact agreement and requires architectural review. Do not
claim arbitrary falsified metadata rejection or Phase 21 release readiness
from the narrow validation repair.

Second, the Phase 20 typed `Range` cross-crate fixture is caused by each
generated module crate defining a separate nominal FileIO `Range`. Correct
compilation requires a shared native FileIO ABI or an equivalent owner-safe
cross-crate transport. Pending that broader Phase 20 repair, direct calls
passing `Range` or `RangeBatch` into concrete provider-owned functions of
another explicit module fail at the Moss call site with
`FILEIO_CROSS_CRATE_ABI`. The source-backed and source-free typed fixture
both reject before rustc E0308; a same-module Range helper still builds and
prints 65. Generic/static functions specialize in the consumer crate and
remain valid: independent source-backed and source-free `Range.length()`
helpers both build and print 1. This is an interim diagnostic, not a working
cross-crate concrete Range ABI.

Independent numeric probes on rustc 1.98.1 and GCC 13.3.0 passed: 2,048
finite binary64 patterns matched a Python shortest-decimal oracle in native
debug, native optimized, and Fast Debug; 2,070 `parse_float` and 535
`parse_int`/`to_int` inputs matched the same three engines. No second Rust or
C++ toolchain is installed locally. The spec's wider formatter portability
pin remains unverified; CI should run this corpus on at least two Rust
toolchain versions and both GCC and Clang C++17 compiler builds. The tested
local combination was rustc 1.98.1 (LLVM 22.1.8) and GCC 13.3.0 on
x86_64-unknown-linux-gnu; no Clang or second rustc/GCC was available. CI
should build Moss with GCC and Clang, then run the pinned Float and numeric
oracles with the current pinned rustc plus an independently pinned prior or
next supported rustc release for each C++ build. Every job should require
native debug, native optimized, and Fast Debug output equality.

Final isolated-branch validation after the narrowed diagnostic: `make check`
PASS (`all Moss v0.1 tests passed`, including strict Phase 20 review:
36 bug and 30 guard probes passed); `make examples` PASS; strict C++17
`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -fsyntax-only src/moss.cpp`
PASS; `git diff --check` PASS. Focused source-free ABI, borrowed-view, Range
ABI, and strict Phase 20 gates also passed after the follow-up. Source-free
provider execution passed in both the ABI and generic Range gates. These
results do not validate the unresolved recomputed-hash pattern swap or the
untested toolchain matrix. Historical checkpoints below describe the earlier
integration state.

## Phase 21 A–D integration review — 2026-10-10

Agents A, B, C, and D have completed their assigned work. Their reviewed
changes are integrated on `phase-21-integration` through `5d60b94`. A's
source-site capture graph, provider-owned failure frame, and lexical recovery
fixes; B's FileIO failure cleanup and guarded flag handling; C's ordered
Branch outcomes, quiescence and panic witnesses, and source-free panic fact
checks; and D's Float oracle and language parity checks are present together.
The focused A/B/C/D gates pass on this combined tree.

The O1–O9 implementation review found passing evidence for entry-planned
normal/recovery locks, separate `Run$fail` lock planning, source-site capture
proofs, guard release before failure dispatch, Root FileIO cleanup ordering,
nested typed-raise propagation, rejected unsafe captures, and ordered Branch
termination with no live read-borrow tokens. The final combined `make check`
passed with repository-local `TMPDIR` (`all Moss v0.1 tests passed`), as did
`make examples`, strict C++17 warnings-as-errors syntax compilation, and
`git diff --check`. Earlier individual-agent validation below remains useful
history; this section records the final combined validation.

Phase 21 is ready for human review of this integration branch, not merged to
`main`. The design's wider Float formatter pin across other Rust/C++
toolchains remains unverified because only Rust 1.98.1 and GCC 13.3 are
installed here. The separate Phase 20 source-free cross-crate typed `Range`
ABI gap has a tracked reproducer under
`tests/tooling/fixtures/phase20_typed_range_cross_crate/`: Moss checking
passes but native compilation fails with rustc E0308. Resolve that gap as
a Phase 20 follow-up or explicitly disposition it at the review gate.

## Phase 21.A owned failure capture closeout — 2026-10-10

The checker now proves handler-local `on_fail` captures over each selected
escaping exceptional source edge, including definite initialization, stable
binding identity, unconsumed ownership, and rejection of FileIO and borrowed
enum payload capabilities. Its ABI-v8 graph records distinct source sites,
recovery scopes, and exceptional edges instead of merging sites by error tag.
Provider body results carry an opaque provider-owned frame; Root wrappers
extract it only after body completion and dispatch it after guard release and
fresh failure-lock acquisition. Source-free `.mossi` plus provider rlib builds
execute non-Copy captures. A value first bound inside a `try` is captured at
its inner raise edge before its Rust lexical scope ends.

Variant-specific `recover` lowering retains checker-proved owned locals in
scoped `Option<T>` slots across consumed and surviving edges. Native
regressions cover consuming matches and helpers, String method READs,
reassignment within recovery and after `try`, post-try use, and a combined
recovery-slot/on_fail capture. Tracked negatives cover pre-initialization,
consumption, FileIO, and borrowed enum payloads. A message-result binding
also has a positive capture regression.

Completed validation on the final tree: strict C++17 warning-free syntax
compilation; focused Phase 21 capture, recovery, source-free ABI and native
gates; full `make check` (`all Moss v0.1 tests passed`); `make examples`; and
`git diff --check`. This closes Agent A's implementation work for integration
review. Final Phase 21 acceptance, including O1–O9 combined review and the
wider Float formatter toolchain pin, remains an integration decision.
## Phase 21.D independent Float corpus and source-free view review — 2026-10-10

The Float gate now checks 270 deterministic finite binary64 bit patterns
against a Python 3.12 shortest-round-trip oracle, including signed zero,
subnormal/normal boundaries, both notation thresholds, and maximum finite.
The reference output has a fixed SHA-256 digest, every text value round-trips
to its original bits, and default/optimized native Rust plus Fast Debug must
match it. This is independent evidence for the pinned Ryū and C++ `to_chars`
implementations on this host; it is not the spec's wider multi-toolchain pin.
Only Rust 1.98.1 and GCC 13.3 are installed; no alternate Rust or C++
toolchain was installed or tested.

The approved finite `parse_float` forms are now explicit in the language
skill, consistent with the practical guide and Phase 21 §13.3. Focused numeric
parsing/conversion, Float, borrowed-view, and skill checks pass. The direct
typed `Range` and `RangeBatch` WRITE tests emit
`FILEIO_VIEW_PARAMETER_WRITE`; source-free generic specialization reaches the
provider `.mossi` and fails closed with `FILEIO_PINNED_OWNERSHIP`. The latter
proves rejection across the interface, not the more specific WRITE diagnostic.

Full `make check` with an absolute repository-local `TMPDIR` passed after
these edits (`all Moss v0.1 tests passed`). Phase 21 acceptance remains dependent
on A's capture frame, the final O1–O9 integration review, and the unavailable
wider Float toolchain pin.

## Phase 21.C final O9 review and adjacent ABI finding — 2026-10-10

The bounded-K chunk lowering still joins each complete Branch window before
reading an outcome slot, commits indexed Value/Raised/EOF outcomes in logical
order, and publishes no later window after a terminal event. A new `moss_perf`
counter in the production FileIO read-borrow type confirms zero live Branch
read tokens at the terminal Root boundaries in the injected read-fault native
test. A generated-code mutation panics immediately after its first Branch
publication and requires process `SIGABRT` with no typed recovery output;
the Root fail-closed guard does not convert a panic into a typed raise.

A source-free exported generic mapper whose provider `.mossi` says
`may_panic=1` correctly uses the sequential chunk path and runs natively.
Removing that effect fact from the interface causes a structured rejection,
so an unknown provider panic fact cannot silently authorize speculation.
These checks supplement the existing ordered fold/mapper/EOF native matrix.
The Phase 21.C O9 paths reviewed here are complete, subject to final
all-phase integration review.

An adjacent Phase 20 cross-crate Range ABI gap remains open. A minimal
source-free provider with `export fn first(chunk: Range) -> Int` and a consumer
calling it on a normal `FileIO.read` result passes `./moss check --json` with
no diagnostics, but `./moss build` fails rustc E0308: it expects
`moss_mapper::Range` and receives consumer `moss_fileio::Range`. A durable
source reproducer is in
`tests/tooling/fixtures/phase20_typed_range_cross_crate/`; the observed
checker result and rustc error are recorded there. It does not involve Branch
lowering and is not repaired by this C
closeout; resolving the shared Range ABI or rejecting that invalid native
path is a separate Phase 20 follow-up and an integration acceptance risk.

Validation after the edits: the focused C gate, full `make check` (`all Moss
v0.1 tests passed`), `make examples`, strict C++17 warnings-as-errors syntax
compilation, and `git diff --check` passed. The typed-Range reproducer is a
separate failing native probe and does not pass the Phase 20 ABI acceptance
check; no Phase 21 acceptance or merge to `main` is claimed here.

## Phase 21.D documentation wording review — 2026-10-10

The active v0.1 syntax and roadmap pages now describe implemented numeric
parsing, checked conversion, Phase 20 support, and Phase 21's current name and
remaining gates. The consolidated Phase 21 specification now marks the old
Fast Debug Float division mismatch, Float golden migration, borrowed-view
WRITE hole, and fusion-only chunk eligibility as historical implementation
motivations. Its normative Float spelling and tie rules remain unchanged.
The shared ABI document and README already distinguish available source-free
typed failure arms from the unmaterialized handler-local capture frame.
The wider cross-toolchain Float formatter pin remains unverified in this
workspace; final Phase 21 acceptance still depends on A's capture frame and
the O1–O9 integration review.

## Phase 21.A lexical native recovery checkpoint — 2026-10-10

Native `try` lowering now uses a labeled Rust block. Explicit `raise`, fallible
calls, numeric conversion, Root ingress, and chunk-pipeline errors route to
that block's tagged outcome; nested unmatched errors route to the outer block
or callable. This removes the generated attempt closure and preserves Moss
`return` from inside a `try`. A new native regression checks both the early
return and the handled error return in a plain value-returning function.
Cross-crate error propagation retains Rust's `Into` conversion. The generated
callable fallback is diverging only on the checker's unreachable normal path
when source has exhaustive explicit returns.

Completed validation after these edits: focused recovery checker/native,
numeric cross-module parsing, chunk FileIO raise, and handler Root tests;
full `make check` (`all Moss v0.1 tests passed`); `make examples`; strict
C++17 `-Wall -Wextra -Werror -pedantic` syntax compilation; and
`git diff --check`.

The legal variant-specific ownership probe still passes `moss check` and
fails native rustc E0382. Removing the closure avoids eager capture, but Rust
still sees a possible move of `item` on `Failure.First` when
`Failure.Second` recovery reads it. The next lowering step must carry the
owned value along the exceptional edge on which it survives, rather than
assuming Rust can infer ownership from the error variant. The handler-local
`on_fail` capture frame, source-free nonempty capture execution, final O1–O9
review, and wider Float toolchain pin remain open. Phase 21 is not accepted or
merged to `main`.

## Phase 21.A exceptional recovery ownership checkpoint — 2026-10-10

The ownership checker previously entered each `recover` arm with the state
from before its `try`. A non-Copy enum consumed before an explicit raise, or
by a fallible consuming call, was accepted when the recovery arm read the
consumed binding; native Rust then failed with E0382. The checker now records
ownership snapshots at direct raising statements, carries unmatched snapshots
through nested `try`/`recover`, and joins moved binding facts only for the
error alternatives selected by each recovery arm. It records a fallible
statement after its expression ownership effects and before a successful
assignment initializes or reinitializes the target. Regression coverage
includes explicit and call-originated raises, nested propagation, a
variant-specific checker-positive path, and reinitialization in `recover`
with a passing native executable.

Strict C++17 warning-free syntax compilation, the focused recovery gate,
full `make check` (`all Moss v0.1 tests passed`), `make examples`, and
`git diff --check` pass after the compiler edit. A separate legal
variant-specific path still passes `moss check` but fails native rustc E0382:
the current `try` closure captures a non-Copy outer binding by move before
control flow selects an error. Its source fixture is retained by the focused
checker gate; native closure lowering needs a path-sensitive ownership handoff.
The handler-local `on_fail` capture frame and final O1–O9 review also remain
open. No Phase 21 acceptance or merge to `main` is claimed.

## Phase 21.A capture-frame investigation and documentation checkpoint — 2026-10-10

The concrete `on_fail` local probe in `tmp/phase21_on_fail_capture_probe.moss`
still fails `moss check --json` with `UNKNOWN_SYMBOL_OR_TYPE` on the
handler-local `attempt`. The failure-arm type/ownership environments in
`check_domains` and `phase21_validate_failure_arms` contain only domain
members, routes, handler parameters, and pattern bindings. The current
provider body returns `Result<_, __MossRaised>`; its Root wrapper receives no
owned-local frame. The `.mossi` capture schema validates ownership/type facts
but source-free loading rejects every nonempty capture list. The existing
two-crate Rust fixture demonstrates an opaque frame crossing a source-free
provider boundary, but generated Moss code does not yet implement that fixture's
capture behavior.

The next A implementation must first derive each arm's free handler-local
bindings and intersect definite availability, ownership, and binding identity
across **escaping** exceptional paths selected by that arm. This includes
raises inside expressions, nested `try`/`recover`, and local moves; a
normal-path scope join is insufficient. Then materialize the provider-owned
frame at those paths, carrying values through the provider body result so its
lexical frame and Root-local FileIO end before the fresh failure lock plan.
The application Root wrapper must pass the opaque frame to the exported arm;
nested wrappers must discard or propagate errors without running the arm.
Source-free tests need positive owned captures and negative pre-initialization,
consumed, capability, and view cases. Do not accept a sidecar that snapshots
only at statement entry: a raising operation can mutate an owned value before
returning an error.

The Phase 21 design, handoff, shared ABI, README roadmap, and v0.1 language
design now distinguish the implemented checkpoints from the unmaterialized
capture contract. The Part I Example 10 and Part II §21 cross-enum snippets now
use dynamic input and a bounded write/sync path. A standalone probe with the
required enum/reporter declarations passes `moss check --json`; `moss effects`
reports both `ParseError.Invalid` and `FileError.Full` among the handler's
raise alternatives. The Float golden gate now compares default and
`-C opt-level=3` native builds with Fast Debug; its focused run passes after
the follow-up edit. The follow-up `git diff --check` also passes.
Only Rust 1.98.1 and GCC 13.3 are installed in this workspace; the wider
cross-toolchain Float-format pin remains unverified. Phase 21 is still open.

## Phase 21.B FileIO API audit and 21.C iterator-boundary follow-up — 2026-10-10

The final B source/runtime audit enumerated open and reopen, metadata,
descriptor flag setup, parent-directory open, read, write, file and parent
sync, and explicit close sites against the six `FileError` variants and the
operation-specific inferred raise sets. `fstatfs` remains diagnostic only.
Known closed-resource use is a checker error; dynamic closed-resource use
remains a fatal programmer error. The Rust runtime's `is_open()` is internal,
and Moss source rejects it with `FILEIO_INVALID_OPERATION`; the Phase 21
specification now records that API disposition. B's audited failure mapping
and source API are complete, while the all-phase O6 capture-frame proof still
depends on A.

The final C offset review found that the public checked `MossChunks` iterator
could request an unrepresentable next range after a full chunk at
`i64::MAX`. It now recognizes that end boundary as EOF, matching generated
sequential and bounded Branch chunk paths. A test-only native probe sets the
iterator to the reachable saturated-offset state without creating an
enormous file. The C ordering review confirms full-window join before slot
inspection, zero live read tokens at terminal commit, indexed Value/Raised/EOF
publication, and speculative mapper rejection when `may_panic` or required
provider facts are unresolved. Possible divergence remains under the
specification's finite-completion assumption; process memory exhaustion is
outside the typed-raise guarantee. C's O9 implementation evidence is complete
for the reviewed paths, subject to final all-phase integration review.

After the final source edit, focused Phase 21 FileIO, generated Root cleanup,
Branch ordering/large-read, and Phase 20 FileIO semantics/hardening gates
passed. Full `make check` ended with `all Moss v0.1 tests passed`;
`make examples`, strict C++17 `-Wall -Wextra -Werror -pedantic` syntax
compilation, and `git diff --check` passed.

The remaining A blocker is concrete: handler-local names in `on_fail` are
currently rejected as unknown; the exported provider body returns only
`__MossRaised` on failure. The ABI validates capture metadata but explicitly
rejects nonempty source-free capture records. Completing A requires a D7
exceptional-path proof for each owned local, a materialized provider-owned
capture frame that survives the attempt body, and source-free Root dispatch
that passes that frame to the selected exported arm after cleanup and fresh
lock acquisition. D's wider toolchain pins and final documentation review
also remain. Phase 21 is not accepted or merged to `main`.

## Phase 21.D Float formatter and 21.C large-read checkpoint — 2026-10-10

The approved Float text tie rule is now explicit in the normative spec:
among shortest round-trip decimals, choose the nearest exact value, then an
even final significand digit on an exact tie. Native generated Rust uses the
pinned Ryū 1.0.23 binary64 shortest-decimal core with Moss's own notation
thresholds and special-value spellings; it no longer delegates Float echo to
Rust Debug. The embedded source is Apache-2.0 attributed. Fast Debug's
`std::to_chars` path and native output agreed for 100,883 seeded/boundary
binary64 bit patterns in an isolated comparison. The focused native/Fast Debug
golden gate includes exact halfway vectors, subnormals, maximum finite, and
the plain/scientific boundaries. Strict `rustc --check-cfg -D warnings` is
covered. The language design's consistency summary now includes completed
typed-raise exits under Phase 21 Theorem A2.

The C/B review found that a maximum-size checked read on a short file
allocated the entire request before reaching EOF. FileIO now reads into a
buffer that grows in at most 1 MiB steps; reservation failure returns typed
`FileError.IO`. Bounded Branch lanes check both start and end offset
arithmetic and publish ordered EOF for unrepresentable ranges. The
sequential chunk fallback uses the same end-boundary rule. A native
maximum-Int chunk request against a four-byte file exercises both paths and
returns the expected `131` without aborting.

After the final source edit, focused Float, Branch, FileIO, 10.6D/10.6E/10.6F
and source-free specialization checks passed; full `make check` ended with
`all Moss v0.1 tests passed`; `make examples`, strict C++17
`-Wall -Wextra -Werror -pedantic` syntax compilation, and `git diff --check`
passed. The full gate used an absolute repository-local `TMPDIR`; an earlier
relative `TMPDIR` run invalidated three Emacs tests, and an earlier source
revision failed generated-code scans that had not yet excluded the new
runtime. Both issues were corrected before the final passing run.

Phase 21 is still open. A's handler-local owned `on_fail` capture frame and
final O1–O9 proof review, B's final FileIO source/API audit, C's remaining
speculative allocation/panic review, and D's wider toolchain pins and final
documentation migration remain. No Phase 21 acceptance or merge to `main`
is claimed.

## Phase 21.D numeric parsing and checked conversion checkpoint — 2026-10-10

`parse_int(String)`, `parse_float(String)`, and `to_int(Float)` now have
variant-precise typed raises in the checker, native Rust output, and Fast
Debug. The two parsers share the approved ASCII decimal grammar, exact
whitespace and nonfinite spellings, and overflow/underflow behavior.
`to_int` checks finite range before truncating. Numeric calls retain their
owning module's error enum identity, including through `try`/`recover` and
cross-module raised-result bridges. Discovery, the practical guide, and the
repository-local language skill now describe these operations.

The focused gate covers lexical successes and failures, Int and Float bounds,
signed zero, nonfinite values, checked conversion, structured diagnostics,
effect summaries, standalone native/Fast Debug parity, and single- and
cross-module Margo native/Fast Debug parity. The focused gate, full `make
check` (`all Moss v0.1 tests passed`), `make examples`, strict C++17
`-Wall -Wextra -Werror -pedantic` compilation, and `git diff --check` pass
after the final source edit.

Phase 21 remains open. D still needs the Rust-independent native Float
formatter/tie-break, wider toolchain pins, and remaining documentation
migration. A's handler-local owned `on_fail` capture frame and final O1–O9
review, B's final FileIO source/API audit, and C's final speculative
allocation/panic review remain open. No Phase 21 acceptance or merge to
`main` is claimed.

## Phase 21.B FileIO open flag failure audit — 2026-10-10

The native FileIO OS-site review found that `open_checked` ignored failures
from `fcntl(F_GETFL)` and `fcntl(F_SETFL)` when clearing `O_NONBLOCK`. It could
return an owner whose descriptor had different blocking behavior from the
FileIO contract. Both failures now close the descriptor before any inode
registry claim and return typed `FileError.IO`. A test-only `OpenFlagClear`
fault proves the error is catchable, exactly one descriptor close occurs,
and the same path can be reopened, so no claim/descriptor is left live. The
focused typed FileIO test, full `make check` (`all Moss v0.1 tests passed`),
`make examples`, strict C++17 `-Wall -Wextra -Werror -pedantic` compilation,
and `git diff --check` pass after the edit.

The reviewed OS sites are open/reopen, descriptor metadata, flag setup,
parent-directory open, `pread`, `pwrite`, file/parent `fsync` and `fdatasync`,
and file/parent close. `fstatfs` is diagnostic-only. Invalid offsets, modes,
bounds, and closed-resource use remain programmer-misuse panic paths; the
runtime `is_open()` helper exists, while the Moss source API does not expose
it. Final B proof and source API review remain open.

## Phase 21.A source-free Root trailer execution checkpoint — 2026-10-10

Provider codegen now exports each checked `on_fail` arm as a typed callable
over a public provider failure-state view. The application reconstructs the
fresh failure pseudo-handler plan from validated ABI-v8 arm state effects and
dispatches source-free Root errors into the provider callable after the normal
attempt releases its guards. Nested calls continue to propagate typed errors
without executing the trailer. A provider's external handler uses the
conservative full-entry plan because its `.rlib` exports the regular body,
not application-specific path-split continuations.

Real `.mossi`/`.rlib` regressions remove provider Moss source and verify a
state-writing reply trailer, caller-side `recover` not bypassing that trailer,
nested propagation, a routed reporting-domain message, and two enum-specific
failure arms. A separate Rust consumer links directly to the exported arm
callable after source removal. The focused source-free gate, full `make check`
(`all Moss v0.1 tests passed`), `make examples`, strict C++17
`-Wall -Wextra -Werror -pedantic` compilation, and `git diff --check` pass
after the final edits.

This is not A acceptance. Handler-local owned D7 capture frames and their
exceptional-path definite-initialization proof remain absent; the checker
currently rejects such local references in `on_fail`. Source-free ABI records
claiming captures still fail closed. Final O1–O9 review, B's FileIO audit,
C's remaining speculative allocation/panic review, and D's numeric
parsing/conversion/formatter work remain open. No Phase 21 acceptance or
merge to `main` is claimed.

## Phase 21.C bounded Branch offset arithmetic checkpoint — 2026-10-10

An eligible FileIO chunk pipeline could generate raw signed `next + lane`,
`index * chunk_size`, and `next += K` arithmetic in bounded Branch lowering.
The compiler accepts a maximum-Int chunk-size probe as eligible. Its native
run aborted on the enormous first read allocation before reaching offset
arithmetic, so that run is not an overflow witness. The lowering now uses
checked index/offset arithmetic; a nonrepresentable offset publishes an
ordered EOF outcome without a read. The focused Branch gate inspects the
generated checked operations and runs the ordinary native ordering cases.

After this edit, the focused Branch gate, complete `make check` (`all Moss
v0.1 tests passed`), and strict C++17 `-Wall -Wextra -Werror -pedantic`
compilation pass. This closes the generated signed-arithmetic hazard, not
the entire O9 review: the extreme-size allocation path and final speculative
panic audit remain open. A's source-free `on_fail` callable/capture/Root
dispatch, B's final FileIO audit, and D's numeric parsing/conversion/formatter
work also remain open. No Phase 21 acceptance or merge to `main` is claimed.

## Phase 21.D Float predicates checkpoint — 2026-10-10

`is_nan(Float) -> Bool` and `is_finite(Float) -> Bool` are implemented as
nonraising pure builtins in the checker, native Rust lowering, Fast Debug,
module rewriting, and live `source_surface` discovery. The practical guide
documents them. The focused gate checks NaN, infinities, finite and signed
zero values in both engines; rejects Int arguments and wrong arity with
structured diagnostics; verifies resolved effect summaries without raises or
panics; and builds/runs a generic explicit-module call in both engines.

After integration, the focused gate, full `make check` (`all Moss v0.1 tests
passed`), `make examples`, strict C++17 `-Wall -Wextra -Werror -pedantic`
build, and `git diff --check` pass. D still needs raising `parse_int` and
`parse_float`, checked `to_int`, the general Rust-independent native Float
formatter/tie-break, wider toolchain pins, and remaining documentation
migration. A's executable source-free `on_fail` and capture frame, B's final
FileIO/proof audit, and C's final O9 review also remain open. No Phase 21
acceptance or merge to `main` is claimed.

## Phase 21.D numeric grammar and Float text checkpoint — 2026-10-10

The approved §13.3 finite decimal grammar now explicitly accepts `.5`,
`1.`, uppercase `E`, leading zeros, and explicit exponent signs, while
retaining the exact ASCII trim, underscore, and nonfinite-token rules.
`parse_int`/`parse_float` raising builtins themselves are still unimplemented.

Source exponent signs such as `1e-4` and `1e+3` now remain part of the Float
literal in both checker and native codegen, and exponent literals receive
Float typing instead of Rust's plain integer/Display path. Fast Debug Float
display now uses shortest-roundtrip digits and the Phase 21 plain/scientific
thresholds. The focused native/Fast Debug golden gate covers signed zero,
nonfinite results, `0.1 + 0.2`, both notation boundaries, and binary64
subnormal/minimum-normal/maximum-finite values. The native path still relies
on Rust Debug for shortest digits; the general Rust-independent tie-break
implementation and wider cross-toolchain pins remain open, along with
`parse_int`, `parse_float`, `to_int`, and documentation migration.

After the A, C, and D checkpoints were integrated, complete `make check`
(`all Moss v0.1 tests passed`), `make examples`, strict C++17
`-Wall -Wextra -Werror -pedantic` compilation, and `git diff --check`
passed. Phase 21 is not accepted or merged to `main`. A's executable
source-free `on_fail` and owned capture frame, B's final FileIO/proof audit,
C's final O9 review, and D's remaining numeric operations are open.

## Phase 21.C native Branch read-fault checkpoint — 2026-10-10

The ordered-Branch focused gate now runs generated native Rust with a real
one-shot `FileIO` `ReadBefore`/`IO` fault while bounded-K chunk reads are
outstanding. Every input byte is a successful mapper value, so whichever lane
consumes the fault, the first Root recovers `FileError.IO`; a second Root then
raises its expected parent-fold error. The test checks zero live Branch scopes
between Roots and before executor join, and repeats the native run 32 times.
The existing owned read-token harness still checks all tokens drop before a
terminal decision. The focused Phase 21.C suite and the complete `make check`
gate pass after this edit (`all Moss v0.1 tests passed`). This strengthens the
O9/quiescence evidence but does not close the remaining final C proof review
or Phase 21 acceptance.

## Phase 21.A source-free failure-arm state effects — 2026-10-10

In isolated branch `phase-21-a-source-free`, each checked source `on_fail`
arm now retains its own inferred state-leaf footprint, and `.mossi` exports
those reads/writes/consumes in the arm's ABI-v8 record. A real provider
fixture has a normal handler with no state effects and a trailer that reads
and writes `count`; the interface records `handler_state_effects` as empty
for the normal path and `count` as both read and write for the trailer. The
source-backed executable prints the typed payload `4` and reply `4`.

The user also approved D's remaining finite `parse_float` spellings on
2026-10-10: accept leading zeros (`00.5`) and explicit exponent signs
(`1e+3`, `1e-3`), in addition to the previously approved `.5`, `1.`, and
uppercase `E`. The grammar is now documented; the parser remains pending.

The focused source-free ABI suite, full `make check` (`all Moss v0.1 tests
passed`), `make examples`, strict C++17 `-Wall -Wextra -Werror -pedantic`
build, and `git diff --check` pass after these edits. The existing source-free
`on_fail` fail-closed diagnostic remains
until callable bodies, owned capture frames, and application Root dispatch
are materialized. No integration merge or Phase 21 acceptance is claimed.

## Phase 21.I source-free `on_fail` fail-closed guard — 2026-10-10

A real provider with a typed `on_fail` trailer builds and runs when its Moss
source is present (`4`, then reply `5`). After removing that source, its
ABI-v8 `.mossi` still names failure-arm callables, but the provider `.rlib`
does not export them and the consumer cannot construct the Root wrapper.
Previously the consumer could mis-handle this path by letting caller-side
`recover` bypass the trailer. The checker now reports
`SOURCE_FREE_ON_FAIL_UNMATERIALIZED` for such imports until executable arm
materialization and Root dispatch are implemented. The source-free ABI test
checks both direct Root and caller-side `recover` cases, while the real
source-free handler without a trailer remains executable.

Focused source-free ABI validation, `make examples`, and strict C++17
`-Wall -Wextra -Werror -pedantic` compilation pass. The repository-wide
`make check` rerun also passes (`all Moss v0.1 tests passed`).
This is a safety guard, not A acceptance: the
source-free trailer callable/capture/Root work remains necessary.

## Phase 21.I source-free handler effect checkpoint — 2026-10-10

The integration checker now preserves the checked observable effects from an
ABI-v8 `.mossi` handler instead of inferring an empty raise set from its
synthetic placeholder body. A real two-crate fixture builds a raising provider,
removes its Moss source, rebuilds the consumer, and verifies both native typed
recovery (`4`) and rejection of an unhandled raised call in `main`. The older
loader fixture still checks forged nonempty metadata, but no longer attempts
to link those records to a deliberately nonraising provider binary.

Validation after these edits: focused source-free ABI suite PASS; complete
`make check` PASS (`all Moss v0.1 tests passed`); `make examples` PASS; strict
C++17 `-Wall -Wextra -Werror -pedantic` compiler build PASS. No merge to
`main` has occurred.

The remaining A blocker is concrete: source-free `.mossi` files declare
`on_fail` callable symbols, but provider codegen does not export those bodies;
the imported handler also has no source `failure_arms` for Root wrapper
generation or totality. An actual source-free Root with a trailer therefore
cannot execute that trailer. This remains open alongside A's owned D7 captures
and exceptional-CFG consumer proof, B's final review, C's O9/read-fault
matrix review, and D's numeric parser/formatter/documentation work. The A–D
branches are clean committed checkpoints, not Phase 21 acceptance.

For D's still-pending finite `parse_float` grammar, the user approved accepting
`.5`, `1.`, and uppercase `E` (for example `1E3`) on 2026-10-10. This decision
has not yet been implemented or incorporated into the normative numeric text.

## Phase 21.I ordered Branch raises and source-free view proof — 2026-10-10

The green integration base now has an actual native Root/Executor fixture for
ordered chunk failures. The bounded-K Branch code stores both checked FileIO
read failures and typed mapper failures as owned `__MossRaised` outcomes,
commits them only after joining the window, and propagates a raising parent
fold. The invocation-local nonempty `chunk[0]` proof retains the mapper's
typed raise set; it removes only the proven index panic. The native fixture
repeats both ordering cases 32 times: the earlier mapper error wins over a
later mapper error, and a parent fold error wins over a later mapper error.

The borrowed-view regression now builds a generic WRITE helper as a provider,
removes its source, and checks that source-free `Range` and `RangeBatch` calls
are rejected while an `Int` specialization builds and runs. This verifies
fail-closed behavior across the `.mossi` boundary; the specific source-free
diagnostic is currently `FILEIO_PINNED_OWNERSHIP` rather than the direct
formal's `FILEIO_VIEW_PARAMETER_WRITE`.

Validation after these edits: focused Phase 21.C and borrowed-view suites
PASS; complete `make check` PASS (`all Moss v0.1 tests passed`); `make
examples` PASS; strict C++17 `-Werror` build PASS. Phase 21 still needs A's
source-free failure-arm/capture and exceptional-CFG work, B's final proof
review, C's remaining O9/read-fault matrix review, and D's numeric
parsing/formatting/docs work. No merge to `main` has occurred.

## Phase 21.I integration gate restored — 2026-10-10

The integration branch now passes the complete `make check` suite, `make
examples`, and a strict C++17 compiler build (`-Wall -Wextra -Werror
-pedantic`). This supersedes the earlier `make check` failures below. The
strict Phase 20 review suite passes 36 bug probes, 30 guards, and 2
pre-existing probes; its 8 design cases remain informational.

The Phase 20 FileIO semantics suite and 59 older review sources were migrated
to the Phase 21 rule that `main` handles typed errors. The compiler now counts
raising message expressions in effect inference, while preserving exhaustive
Root `on_fail` handling. Native enum display uses debug formatting for payload
types without `Display`, fixing the EXPRESS-005 vector-payload case.

This is an integration checkpoint, not Phase 21 completion. Agent A's
source-free failure-arm/capture and exceptional-CFG proof, Agent B's remaining
integration/proof review, Agent C's ordered-error/O9 proof, and Agent D's
numeric parsing/formatter contract, documentation, and source-free view
proof remain open. No merge to `main` or Phase 21 acceptance claim has been
made. Next, complete the outstanding agent acceptance matrix on this green
integration base, then review the combined result before merging to `main`.

## Phase 21.I checked chunk FileIO follow-up — 2026-10-10

After pushed integration checkpoint `0614399`, migrated the older Phase 20
Executor/FileIO sources to declare the six `FileError` variants and make Root
handlers or `main` total under typed failure. The range-copy fixture keeps
its bounded read/write in a helper because a direct `try` wrapper lost the
checker proof of its finite write payload. The physical Rust lock observer
now unwraps the generated typed outcomes instead of assuming bare values.

Native chunk lowering no longer emits undefined failure placeholders. It
returns `Err(__moss_raise_fileio(error))` from a checked outcome context,
mapping `MossFileError` to the existing `__MossRaised` tags. Sequential
pipelines now propagate raising map and combine calls; explicit-module code
gets a `FileError` alias for its qualified enum when needed. A permanent
native regression compiles and runs an ordinary read (`4`) and an injected
read error recovered through an ordinary helper (`99`). The complete Phase
20 Executor/FileIO suite passes, including its source-free package case;
the focused 21.C Branch-error and physical lock gates also pass.

The latest `make check` passes all gates through Phase 15.9, including 40/40
generic and 129/129 builtin typing probes, then fails in
`tests/tooling/check_phase20_fileio_semantics.py`: its first positive source,
`root_read`, lets typed FileIO errors escape `main`. This is the next legacy
source migration gate. `make examples` and strict C++17 `-Werror` compilation
pass on the latest source. Phase 21 proof gates remain open, notably native
speculative mapper raised-outcome coverage, source-free failure-arm capture,
and the numeric parsing/formatting contract.

## Phase 21.I combined A+B+C+D integration checkpoint — 2026-10-10

The integration branch merged the current 21.D checkpoint (`a20d250`) and
21.A failure-trailer checkpoint (`60aefd0`) with the earlier A+B+C base and
the corrected 21.C fixture (`529ef0b`). The merge needed one missing closing
brace after `proven_index_access` in `src/moss.cpp`; the combined compiler now
builds. The Root FileIO cleanup witness now catches all handler failures,
handles cleanup's own FileIO errors inside `on_fail`, and still proves the
successful path returns `11` after reopening the file. Two older Phase 20
Executor negative fixtures now handle FileIO raises so they continue to test
their intended capability-boundary diagnostics. The generated basic FileIO
fixture in the Phase 20 Executor suite now declares `FileError` and handles
typed failure. The direct FileIO integration fixture also now declares that
enum and catches its `main` error set.

Validated on this combined tree: `make` PASS; focused A native Root,
21.B typed FileIO and Root cleanup, 21.C Branch error, 21.D Float and view
WRITE checks PASS; 40/40 generic specialization and 129/129 builtin typing
probes PASS; `make examples` PASS; strict C++17 `-Werror` compiler build PASS.
The full `make check` gate advances beyond
the two corrected negative Executor fixtures but is **not yet green**: the
Phase 20 Executor/FileIO suite next fails compiling
`tests/tooling/fixtures/phase20_domain_fileio_native.moss` because its
`main` leaves typed FileIO errors unhandled. Other older positive FileIO
fixtures in that suite need a source migration audit. This result
supersedes the earlier integration notes below that predate the current A/C/D
merges. No O1–O9 or Phase 21 completion is claimed;
source-free typed failure capture/arm materialization, full fixture migration,
numeric parsing/formatter contracts, documentation, and review remain open.

## Phase 21.A typed recovery continuation — 2026-10-10

Continued `phase-21-a-recovery` from checkpoint `620b567` in
`tmp/moss-worktrees/21-a`. The current worktree has uncommitted implementation
and regression changes; no push or integration merge was performed.

This continuation closes several native/checker gaps after the first tagged
outcome checkpoint:

- path-sensitive handlers now retain their generated Root `on_fail` wrapper;
- explicit-module rewriting covers typed failure patterns and arm bodies;
- fixed-point raise inference suppresses provisional warnings and performs one
  final authoritative annotation/diagnostic pass, so method raises no longer
  leave false unreachable-arm warnings and `main` totality sees transitive
  function/method/handler effects;
- handler trailers are checked for typed pattern validity, overlap/order,
  exhaustiveness, empty escaping raise sets, and reply totality; their bodies
  now receive normal type, ownership, and FileIO lifecycle checking;
- payload patterns introduce checked immutable field bindings in `recover` and
  `on_fail`, and native lowering projects their owned values;
- Root-total messages in `main` account for a validated exhaustive trailer;
- source provider failure patterns are converted to ABI identities correctly;
  application/provider crates now receive stable-identity conversions between
  their internal raised enums, allowing an exported handler to raise in a
  provider crate and execute its Root trailer in the application crate.

Permanent coverage now includes method propagation, payload binding use,
path-sensitive normal/failure lock plans, nested-versus-Root dispatch,
value-returning and asynchronous Roots, explicit two-module provider lowering,
and negative diagnostics for non-exhaustive trailers, escaping trailer raises,
missing failure replies, and ambiguous multi-enum catch-all bindings.

Validation on the current uncommitted revision:

- strict C++17 `-Wall -Wextra -Werror -pedantic` compiler build: PASS;
- all four focused Phase 21 recovery/native/source-free scripts: PASS;
- the real two-module provider fixture compiles and runs through Margo, printing
  the payload `4` and failure reply `5`: PASS;
- Phase 20C executor regression suite: 54/54 PASS;
- `git diff --check`: PASS.

The repository-wide `tests/run.sh` rerun reaches the known A/B integration
boundary: 37/40 generic-specialization probes pass, while `range_fileio`,
`range_direct`, and `fileio_direct` fail native Rust compilation because 21.A
now carries `FileError` alternatives but the generated FileError definition and
native FileIO tagged-result lowering remain on 21.B. No later full-suite gate
was claimed. Remaining Phase 21.A/integration work includes true source-free
failure-arm callable/capture materialization (not merely same-project separate
crates), D7 capture joins, complete exceptional-CFG consumer wiring, provider
path-split ABI visibility, and A/B Root FileIO quiescence/teardown sequencing.
This remains a checkpoint, not Phase 21.A completion.

## Phase 21.D settled parity and borrowed-view checkpoint — 2026-10-10

The transferred `ed8374f` WIP now builds and has a follow-up repair. Native
Float echo lowering gives its temporary an explicit `f64` type, avoiding Rust
E0689, and uses Debug formatting as an interim shortest-roundtrip mechanism.
Fast Debug no longer rejects Float division by zero and renders signed zero,
infinity, NaN, and integral Float values in the tested Phase 21 forms. The
focused native/Fast Debug test runs generated Rust under `-D warnings` and
passes for `inf`, `-inf`, `NaN`, and `-0.0`. Legacy test goldens for integral
Float output changed from `3`/`0`/`8` to `3.0`/`0.0`/`8.0` where the normative
Phase 21 text requires a fractional digit.

The borrowed-view WRITE validator originally compared canonical types against
lowercase `range`/`rangebatch` and never rejected them. It now compares
`Range`/`RangeBatch`. The new focused gate proves direct `moss check --json`
rejects WRITE through both formal types with `FILEIO_VIEW_PARAMETER_WRITE`,
including the documented `RangeBatch` retargeting source, while an ordinary
`Int` formal retains inferred WRITE. It does not yet prove the source-free
`.mossi` specialization path; that remains required by V4-03.

Final validation for this checkpoint: `make`, focused Float and borrowed-view
scripts, `make check` (all Moss v0.1 tests, 40 generic probes, 129 builtin
probes, and 54 Executor runtime probes), `make examples`, strict C++17
`-Werror` compilation, and `git diff --check` passed. Earlier full-gate runs
that stopped at old Float goldens are superseded by the final passing run.

Phase 21.D is not complete. The normative general shortest-roundtrip formatter
and tie-break algorithm are not frozen or implemented; current Debug/
`setprecision(15)` formatting is interim and does not establish all §13.2
goldens. The accepted finite decimal grammar, raising parse operations,
`to_int` error cases, predicates, source-free view specialization test,
supported Fast Debug `for` parity, and remaining documentation migration are
open. Do not merge this WIP checkpoint as full Phase 21.D acceptance.

## Phase 21.I A+B+C integration continuation — 2026-10-10

The integration branch also merged committed 21.C head `f0c0464` after the
A+B checkpoint. Four small conflicts were resolved: keep B's catchable
interior-NUL `FileError.IO` mapping and its regression, retain A's external-I/O
effect while passing C's proven-index context, and register all A/B/C tests.
The combined compiler builds with `make`. The 40-case generic specialization
suite passes 40/40; B's typed FileIO test and A's local native recovery test
pass after the merge.

The C focused gate fails during native lowering with `checked raising Root
message lacks a Phase 21 outcome context`. Its original fixture sends a
potentially raising handler message from `main` without a recovery arm; simply
adding a try/recover in the fixture does not fix current checked/lowered
message-effect agreement, so that experiment was reverted. A's separate Root
fixtures remain blocked as recorded below. The 21.A worktree contains newer
uncommitted Root/checker edits that have not been integrated. The branch is a
durable partial integration checkpoint; full `make check`, `make examples`,
O1–O9, source-free typed failures, and review before merging to main remain
open. No Phase 21 completion is claimed.

## Phase 21.I A+B integration checkpoint — 2026-10-10

The integration branch merged committed 21.A head `620b567` and 21.B head
`a47e639` on the frozen ABI-v8 base. The only merge conflict was the additive
test registration in `tests/run.sh`; both workstreams' checks are retained.
This is an integration checkpoint, not Phase 21 completion or a merge to main.

The three legacy FileIO generic-specialization fixtures now declare the
six-variant `FileError` enum required by their native typed-error lowering.
With 21.B's runtime included, the 40-case generic-specialization suite passes
40/40. The 21.B deterministic FileIO typed-error test and 21.A local native
recovery test pass. No compiler source was changed in this integration step.

Two Root fixtures remain blocked at `moss check` by `main may not let typed
errors escape`: 21.A's nested/Root failure fixture and 21.B's Root FileIO
cleanup fixture. The current fixed-point raise walker reports the underlying
handler's raises at a top-level message despite a handler `on_fail` trailer;
the latter fixture's `on_fail` also performs fallible FileIO open/close that
needs its own recovery. Do not treat this as a validated Root implementation.
21.A has separate new uncommitted work after `620b567`; it has not been folded
into integration. Await a reviewed checkpoint before merging that work.
O1–O9, source-free typed failure integration, full `make check`, `make examples`,
and the human review gate before any merge to main remain open.

## Phase 21.A native recovery checkpoint — 2026-10-10

On `phase-21-a-recovery`, the in-progress native lowering now generates tagged
`raise`/`try`/`recover` outcomes, nested handler propagation, Root `on_fail`
dispatch, and separate failure synchronization plans. The new native fixtures
exercise local recovery, typed re-raise, nested versus Root behavior, and class
repartitioning. The source-free ABI fixture was made total under its injected
provider error metadata. The earlier Fast Debug checkpoint remains in place.

The full `make check` gate exposed three legacy FileIO specialization fixtures
whose `main` functions let Phase 21 typed errors escape. They now use
`try`/`recover`. This exposed a FileIO lifecycle join bug: a file opened and
explicitly closed inside a try suite was carried into the sibling recover arm
as an uncertain owner. The lifecycle walker now requires try/recover-local
owners to be closed on normal arm exit and removes those scoped owners before
joining. Focused regression checks cover all three accepted fixtures and an
unclosed owner rejection.

Validation after the lifecycle fix: branch `make` passed; all four focused
Phase 21 recovery/native/source-free scripts passed; the recovery script's new
FileIO positive/negative checker probes passed; `moss check --json` passed for
the `range_fileio` fixture. Strict C++17 `-Werror` compilation and
`git diff --check` passed. The rerun of the 40-case generic specialization
suite reaches native compilation in its three FileIO cases but fails because
21.A alone references `FileError` while its generated runtime definition is on
the separate 21.B branch. The other 37 probes pass. The earlier `make check`
failure was before this lifecycle fix and is superseded by the focused rerun;
no full gate passes on this branch. A/B integration, exceptional-CFG consumer
review, Root cleanup sequencing, and complete source-free recovery coverage
remain open. This is a checkpoint, not Phase 21.A completion.

## Phase 21.A restart checkpoint — 2026-10-10

Work resumed on `phase-21-a-recovery` in
`tmp/moss-worktrees/21-a` from transferred head
`299fdf3fdfe9f81c225a92b1ec065e46140ede43`. All seven Phase 21 worktrees were
recreated locally at the exact remote heads recorded by the Phase 21 planning
register. This checkpoint is intentionally limited to the 21.A worktree; no
integration merge or push was performed.

In-flight implementation adds checked Fast Debug execution for Phase 21
`try`/`recover`/`raise`: typed raised values propagate through ordinary helper
calls, qualified and catch-all recovery arms select the originating error, bare
`raise` preserves the selected typed value through an outer recovery, bound
catch-all errors are installed and restored lexically, and structured traces
emit `raise`/`recover` events. The primary type-environment walker now also
introduces the already-supported `recover err:` binding, matching the effect and
ownership walkers. Focused coverage exercises variant-precise effects, both
normal variants, a bound catch-all, bare re-raise, outer recovery, stdout, and
trace counts.

Completed validation on the current source revision:

- Branch compiler rebuilt successfully with `make`.
- `python3 tests/tooling/check_phase21_recovery_semantics.py ./moss`: PASS
  (`Phase 21 recovery checker, effects, and Fast Debug probes passed`).
- `python3 tests/tooling/check_phase21_shared_abi.py
  tmp/resume-phase21-shared-abi-1`: PASS.

Known next issue: `check_phase21_source_free_abi.py` now reaches the synthetic
provider stage and fails because 21.A correctly enforces `main` totality after
the test rewrites provider `Bump` metadata to raise
`provider::FileError.Full`, while the fixture's `main` still sends `Bump`
without `try`/`recover`. Update that fixture to include a valid recovery arm
that is merely unreachable before the metadata rewrite, rerun the source-free
gate, then run strict compilation. Native generator lowering remains the major
21.A blocker: its statement switch still warns that `Try`, `Recover`, and
`Raise` are unhandled. Do not claim 21.A or Phase 21 complete from this Fast
Debug checkpoint.

## Phase 21.0 corrective shared effects/error ABI checkpoint — 2026-10-10

Worktree: `tmp/moss-worktrees/21-0`, branch
`phase-21-0-effects-abi`, corrective base
`d33e3be434a1e899e95590c80b12bce4a070b53b`. Repository-local Moss skills were
loaded and bootstrap confirms `moss-0.1`. Three Codex subagents independently
reviewed the ABI/schema and implemented the executable Rust fixture. Agents A-D
were not launched and remain gated on human approval.

Native/provider ABI v8 now freezes cross-specialization typed-error transport.
A caller never returns a callee frame as its own type: it nests the opaque callee
frame in a caller-private carrier, translates specialization-local tags through
`RaisedIdentity`, and uses consuming projection/rebuild callables to recover
owned payload fields or exactly bare re-raise the original payload/residual.
Final application linkage verifies the named callee specialization, outcome and
frame types, callee/caller tags, identity, and ordered payload schema. There is
no source-visible mixed union, trait object, serialization, or host unwinding.

The `.mossi` v8 schema includes reversible projections, imported frame bridges,
and a counted canonical `handler_owned_type_shapes` table. Payload/capture
fingerprints must match the table; every recursive dependency is declared and
intrinsic/exported shapes are independently resolved. Unknown scalar safety
claims, unbounded payloads, and borrowed/capability values at any nesting depth
fail closed. Legal owned non-capability captures include String and private
aggregates with complete authoritative structural declarations.

Exceptional CFG verification now requires each raising node's complete variant
set and a disjoint exact routing partition. It rejects missing/impossible or
duplicate alternatives, invalid/contradictory destinations, incomplete consumer
masks, repeated lexical scopes, duplicate `(scope, match_order)` recovery arms,
and routes that skip the nearest lexical match. Bare re-raise remains confined
to the selected-error slot and designated outer scope. Locally recovered errors
need not appear in the handler outcome.

Provider validity and concrete application Root validity are separate. A
provider may expose typed errors for nested propagation with partial/no
`on_fail`; `verify_root_totality` requires the concrete Root escaping set to
equal the handler outcome and every alternative to select exactly one nonraising,
reply-total arm. Empty/subset/superset Root sets are covered by negatives.

The source-free regression now compiles a real provider crate as a separate
`rlib`, removes its Rust source, and compiles/runs a consumer against the
materialized provider. It covers a genuinely fallible body, opaque frame,
typed `on_fail`, nested forwarding across different frame/outcome types, owned
payload and captures, variant recovery, exact re-raise, Root dispatch, and typed
ordinary replies instead of the former `-1` sentinel.

Final validation after all corrective edits:

- `./moss agent bootstrap --json`: passed; `moss-0.1` confirmed.
- Focused shared ABI C++ test: passed.
- ABI-v8 source-free loader and two-crate Rust fixture: passed.
- Strict `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic src/moss.cpp -o
  tmp/moss-phase21-v8-strict`: passed.
- The ABI-v8 source-free/two-crate fixture rerun with that strict compiler:
  passed.
- `make check`: passed all Moss v0.1 tests, including Phase 21 focused/source-free
  coverage, 40 specialization probes, 129 typing probes, 61 editor ERT tests,
  and 54 Phase 20 Executor runtime probes.
- `make examples`: passed; the intentional negative example was skipped.
- Independent final ABI review: no remaining checkpoint blocker.
- `git diff --check`: passed before this status update.

No implementation or validation blocker remains. ABI v8 intentionally rejects
tentative v7 and all earlier artifacts with a full-rebuild diagnostic. The
corrective checkpoint awaits human approval before it becomes the base for
Agents A-D.

The following main-branch transfer entries are historical; the current
integration review above supersedes their status and Git directions.

## Phase 21 continuation — 2026-10-10

The user authorized repository-local commits and pushes for this task and
directed removal of the old no-push rule from `AGENTS.md` before pushing.
That rule has been updated. The historical transfer notes below that say
Codex must not push are superseded for this task.

Phase 21 implementation remains off `main` in the registered worktrees.
Workstream A advanced through native recovery and a FileIO try-scope checker
repair at `620b567`. Partial A+B+C integration is pushed on
`phase-21-integration` at `78da3d4`; it builds and passes 40/40 generic
specialization probes but still fails Root and C focused gates. B, C, and D
retain their checkpoint branches. No Phase 21 implementation has been merged
to `main` or claimed complete. The planning register's human review gate
before merging to `main` remains open. O1–O9 and final validation are unfinished.

## Multi-worktree transfer correction — 2026-10-10

The first machine-transfer audit inspected only the primary worktree. A
follow-up `git worktree list --porcelain` audit found active Phase 21 work in
repository-local worktrees under `tmp/moss-worktrees/`. The earlier statement
below that implementation had not started is superseded by this section.

All registered Phase 21 worktrees are now clean. Their transfer heads are:

- `phase-21-0-effects-abi` at `f27b2629efa2c321bef953e62ca73d230e56f96f`;
  this branch already matches `origin/phase-21-0-effects-abi`.
- `phase-21-a-recovery` at `299fdf3fdfe9f81c225a92b1ec065e46140ede43`;
  it has no configured upstream and is not contained in `main`.
- `phase-21-b-fileio-errors` at `a47e6393737dc14f80778e2199383dc89a09a2e8`;
  it has no configured upstream and is not contained in `main`.
- `phase-21-c-branch-errors` at `f0c04648cc74a303a71345063128dc863fa4bc76`;
  it has no configured upstream and is not contained in `main`.
- `phase-21-d-language-parity` at
  `ed8374f` (`Checkpoint Phase 21 language parity work`); this explicit WIP
  commit preserves the five files that were uncommitted when the audit began.
  It has no configured upstream and is not contained in `main`. Only
  `git diff --check` was run for that WIP on this resource-constrained laptop.
- `phase-21-integration` at
  `f27b2629efa2c321bef953e62ca73d230e56f96f`; it currently has no unique
  integration commit beyond the Phase 21.0 head.
- `phase-21-planning` at `458fa52a66adae45ebefc94dca62152c194fae86`;
  it is ten commits ahead of `origin/phase-21-planning`. Its
  `.codex/PHASE_21_TASK_REGISTER.md` is the authoritative detailed coordinator
  record, including current integration blockers.

These workstream commits are intentionally not merged into `main`: the Phase
21 handoff requires contract-aware integration and lists unresolved blockers.
To transfer them through the remote, the user must push `main` plus the local
workstream/planning branch heads. Codex must not run `git push`. A pull/fetch on
the stronger machine can then recover the branch refs and recreate worktrees;
do not rely on the disposable `tmp/moss-worktrees/` directories themselves.

## Machine-transfer checkpoint — 2026-10-10

This checkpoint was requested so work can resume on a more powerful machine.
Before this note was added, `main` was clean at
`ed0b3d23596e189dec7c259319dd3d570a2120b0` and exactly matched
`origin/main`; there were no staged, unstaged, or untracked implementation
changes to preserve. Live startup discovery passed and confirmed
`moss-0.1`. No build or test job was started on this laptop because the
checkpoint is documentation-only and the transfer is specifically intended
to move subsequent concurrent/heavy work elsewhere.

The work to resume is **Moss Phase 21 error handling**. Its two durable inputs
are already committed:

- `080f6edb50ca530f7626e0d51e00e69abf4915fd` adds the consolidated normative
  design, `docs/MOSS_PHASE_21_ERROR_HANDLING.md` (v4.5.2).
- `ed0b3d23596e189dec7c259319dd3d570a2120b0` adds the independent review and
  implementation plan, `docs/MOSS_PHASE_21_IMPLEMENTATION_HANDOFF.md`.

At the time of this first note, the primary worktree showed no implementation
changes. That was not a complete repository-wide conclusion: the active
worktrees and implementation heads are recorded in the correction above.
Resume with the repository startup sequence (load both Moss skills, run
`./moss agent bootstrap --json`, and confirm `moss-0.1`), then read the two
Phase 21 documents and the planning branch's task register. Phase 21.0 has
since been committed, and workstreams 21.A–21.D have work in progress; use the
recorded branch heads and blocker register rather than restarting Phase 21.0.
Do not claim the phase complete from design-level proofs; the handoff's O1–O9
and integration matrix require implementation evidence.

Important open contract checks from the review must remain visible during
21.0: synchronization-class repartitioning can change normal-handler lock
counts without changing its per-leaf write footprint; the tutorial cross-enum
example needs a genuinely reachable `FileError.Full` path; and Float
shortest-roundtrip tie-breaking plus the accepted finite-decimal grammar need
explicit freeze decisions rather than agent invention.

Transfer note: the commit containing this checkpoint is local until the user
pushes it. Repository instructions prohibit Codex from running `git push`.
After the user pushes this commit, the other machine can pull `main`, open
this file, and resume at Phase 21.0. No earlier validation result below should
be treated as Phase 21 implementation validation.

## Non-Copy vector-literal ownership repair — closeout 2026-10-08

Implementation: `dbe62d3c88aad7a052a856a8deb1f695f54b1c76` (`changes`),
reviewed and already on `main`. Closeout began from that exact clean HEAD.
Startup skills loaded; live bootstrap confirms `moss-0.1`. This session makes
bookkeeping changes only; no compiler/test refactor or language redesign.

Baseline bug: non-Copy `x` in `[x]` was inferred READ, so native lowering
emitted a borrowed `&Payload` into an owned `Vec<Payload>`. Vector construction
was not modeled as an ownership boundary in general effect/ownership analysis;
leaf-effect handling separately treated vector elements as READ.

The repair shares `parse_vector_literal` and concrete `vector_element_effect`:
Copy elements are READ and owned non-Copy elements are CONSUME. Parameter
effect inference, ownership checking, and leaf observation use the same rule.
Static specialization re-evaluates unresolved generic element ownership under
concrete types. Native lowering and Fast Debug follow the checked ownership
contract; no implicit cloning was introduced.

Permanent regression coverage adds 25 vector-literal probes to
`tests/tooling/check_builtin_generic_typing.py`: direct Payload construction,
use-after-consume, duplicate use, typed helper, generic helper, constructor
`[x]`, String CONSUME, Int READ, nested literals, parenthesized literals and
elements, fresh temporaries, READ/CONSUME call contexts, domain state,
`replace(state, ...)`, and leaf effects. The suite now has 129 probes (104
previous plus 25 new), with 52 accepted and 77 rejected. All 16 accepted new
cases have exact native/Fast Debug stdout parity; all 9 rejected new cases
fail at the shared front end in check, lowering, and Fast Debug.

Explicit semantic-query guards on the reviewed HEAD passed: both `effects`
and `ownership` report `x: Payload` CONSUME for typed `fn:singleton` returning
`[x]` and generic `fn:singleton<Payload>`. Both report `x: Int` READ for typed
`fn:singleton` returning `[x]`. The suite also checks generic Int READ and
String CONSUME, plus domain-state leaf READ and replacement WRITE effects.

Final validation from the reviewed HEAD, without CPU pinning or scheduler
restrictions (scratch files/logs use repository-local `tmp/`):

- `./moss agent bootstrap --json`: passed, `moss-0.1` confirmed.
- `python3 tests/tooling/check_builtin_generic_typing.py ./moss`: 129/129 passed.
- `python3 tests/tooling/check_generic_specialization.py ./moss`: 40/40 passed.
- Phase 20 review `--strict`: 36 bug, 30 guard, 2 pre-existing probes passed;
  8 design probes informational.
- `python3 tests/tooling/check_phase20c_executor.py ./moss`: 54/54 passed.
- `python3 tests/tooling/check_phase20_executor_fileio.py ./moss build/tests`: passed.
- `sh tests/run.sh ./moss build/tests`: passed normally, including 129 typing
  probes, 40 specialization probes, and all 54 Executor runtime probes.
- `make check`: passed normally with the same complete suite and expectations.
- `make examples`: passed; intentional negative example skipped as expected.
- Strict `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic
  src/moss.cpp -o tmp/moss-strict`: passed.
- `python3 tests/tooling/check_builtin_generic_typing.py tmp/moss-strict`:
  129/129 passed, including native/Fast Debug parity and semantic-query guards.
- `git diff --check` and staged diff check: passed on the final status revision.

Both full runs passed all 61 editor ERT tests. Optional live LLDB CLI/DAP
checks used the unchanged suite's skip for unavailable sandbox process tracing;
no CPU-affinity workaround or test-expectation changes were used. No compiler
or test changes followed these gates, so none of these results are invalidated.
No unfinished work or blockers remain for the vector-literal repair. The
closeout commit records this status and final validation; published history
is not rewritten and this session does not push.

Compiler-agent self-report: bootstrap confirmed the current language contract;
explicit effects/ownership queries removed ambiguity about typed and concrete
specialized element ownership. Existing probes established native/Fast Debug
parity, rejection behavior, and leaf effects without any edit/check/repair cycle
or remaining semantic guess. Impact/affected tests were unnecessary for this
bookkeeping closeout; the requested full gates ran. No new compiler-agent
improvement is proposed by this validation-only session.

## Executor lost-wakeup harness ordering — 2026-10-07

Base: `0d0f96990f483360967af8d51ebac070034f57ef`, clean `main`.
Startup guidance loaded; live bootstrap confirms `moss-0.1`.
This is a test-harness repair, not an Executor runtime semantics change.

Unrestricted isolated baseline reproduced the historical failure on run 4:
exit 3, `timed out waiting for publisher signalled` (8 CPUs available). The
hook was armed before the Root publisher acquired the executor mutex for its
preparatory notification. An idle worker could enter that hook holding the
same mutex and wait for a publisher that was blocked acquiring it.

Probe 38 now completes the preparatory notification with the hook unarmed,
records completion, then arms the hook while still holding that mutex. Only
releasing the mutex permits the target worker to enter the tested window.
Channel handshakes announce window entry, publication, and Branch execution;
20-second watchdogs remain unchanged. This probe no longer polls or sleeps.
The hook still holds the executor mutex after its idle predicate check and
before Condvar wait until publication bumps the Branch generation. No later
Root/publication/wake rescues it; the original different-worker and exact
worker assertions remain. No global Executor serialization was added.

The runtime suite supports `--probe NAME --repeat COUNT`, compiling once and
stopping on the first failed process/timeout/assertion. Default full-suite
coverage and expectations are unchanged. The repaired probe passed 1000
consecutive captured-output runs under unrestricted multicore scheduling.
A disposable generated-Rust mutant suppressing only the Branch wake after
publication compiled, then failed at `idle worker woke and ran the Branch`;
it passed the preparation/publication handshakes. The mutant is not committed.
All production source files and the generated minimal root Rust are byte-for-
byte unchanged. No compiler rebuild or runtime behavior change is needed.

Final validation, all without affinity or scheduler restrictions:

- `./moss agent bootstrap --json`: `moss-0.1` confirmed.
- `python3 tests/tooling/check_phase20c_executor.py ./moss --probe
  38-branch-lost-wakeup --repeat 1000`: 1000/1000 passed; repeated after gates.
- Full standalone Phase 20 runtime suite: 54/54 passed.
- `python3 tests/tooling/check_phase20_executor_fileio.py ./moss build/tests`: passed.
- `sh tests/run.sh ./moss build/tests`: passed normally.
- `make check`: passed normally.
- `make examples`: passed.
- `git diff --check`: passed.

The old CPU-affinity workaround is no longer needed. Historical timeout and
pinned-run evidence below is retained; ordinary multicore validation now
passes with the deterministic harness handshake. No unfinished work or
blockers remain in this harness repair. Completion commit
`8d1922d3e1fb0479cdead1a1a685b99238997cd4` records implementation, stress,
sensitivity, runtime integrity, and full validation and is already on `main`.
The separate non-Copy vector-literal issue was outside that harness repair;
it was subsequently fixed by the vector-literal ownership repair above.
Compiler-agent self-report: bootstrap and source-guided test reduction isolated
a harness ordering bug; stress and a missing-wake mutant validate its repair.

## Internal semantic placeholders and underscore nominal types — follow-up 2026-10-07

Base: `7cf3f69208e0a0d89b7310931364a609b51aa3c9`, clean `main`.
Review found that legal `_`-prefixed user nominal types collided with the new
constructor placeholder predicate. Startup skills loaded; live bootstrap confirms
`moss-0.1`. Placeholder classification is now explicit rather than based on a
leading underscore. User `_Type` names remain ordinary concrete nominal types;
constructor deferral remains limited to real compiler-internal unresolved markers.
No identifier/parser or generic specialization design changes.

Source audit found these compiler-generated unresolved type markers:
`_`, `_value`, `_none`, `_method_value`, `_generic:`, `_element:`, `_field:`,
`_method_result:`, `_specialized_result:`, `_iterator_element:`,
`_functional_result:`, `_functional_element:`, `_functional_collection:`, and
`_functional_callable_result:`. Bare `_` occurs in the synthetic Iterator
`option[_]` signature and codegen fallback `option[_]`. `_method_value` has
only an exact spelling; `_method_` is not an open marker namespace. Generated
Rust identifiers (`__moss_*`, `__specialized_*`, suffixes, etc.) are symbols,
not unresolved semantic type markers, and remain unchanged.

Every broad underscore type check was inspected before replacement. All were
category B (unresolved semantic evidence), including contexts where that evidence
controls eligibility rather than direct type comparison:

- Inference and checking: constructor/builtin deferral, expected results, function
  signatures/returns/arguments, indexed assignments, branch environment merges,
  and enum diagnostic wording.
- Specialization and functional analysis: concrete input/result eligibility,
  callback results, pipeline sources, and iterator element validation.
- Effects and synchronization: borrow eligibility, possible receiver method
  effects/call targets, and concrete state leaf layouts.
- Native lowering and interfaces: borrow eligibility, generated expression and
  callback type recovery, joined bindings, closed typed exports, and executor
  payload compatibility.

The shared `type_placeholders.hpp` owns the precise
`internal_semantic_placeholder` spelling classifier. Checker/native paths use
recursive `contains_internal_placeholder`; Fast Debug parameter binding uses the
same outer classifier for its caller-type fallback. Empty types, bare
collections, traits, domains, and Copy checks retain their separate existing
rules. Marker-specific resolution tests remain specific; broad `_method_`
return exemptions now recognize exactly `_method_value` or `_method_result:`.
No category A type namespace rule or category C broad type check was found.
The equivalent Fast Debug `type.front()` test is also category B and now uses
the shared classifier. Other underscore checks concern source placeholders,
identifier syntax, or generated symbols (category C) and remain unchanged.

Permanent probes added to the general builtin/generic suite cover direct and
generic constructors, Vector including nested template evidence, inferred Map
and Queue fields, typed nominal-result pipelines, repeat READ borrows, nominal domain
state and handler payloads, branch joins, typed return rejection, builtin push
rejection, and nominal names resembling marker families. Original 83 fixture expectations are unchanged.

Final validation on the final compiler/test revision:

- Strict `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic` build passed;
  `./moss` is copied from the final `tmp/moss-strict` binary.
- `./moss agent bootstrap --json`: `moss-0.1` confirmed.
- `python3 tests/tooling/check_builtin_generic_typing.py ./moss`: 104/104 passed
  (83 original expectations unchanged plus 21 new underscore probes).
- The same full 104-probe suite rerun with `tmp/moss-strict`: passed. All 12
  accepted new probes have exact native/Fast Debug stdout parity; all 9 rejected
  new probes fail at check/lowering/Fast Debug before backend execution.
- `python3 tests/tooling/check_generic_specialization.py ./moss`: 40/40 passed.
- Phase 20 review `--strict`: 36 bug, 30 guard, 2 pre-existing passed;
  8 design probes informational.
- Phase 20 Executor/FileIO gate: passed.
- `sh tests/run.sh ./moss build/tests`: passed via the unchanged `make check`
  recipe under `taskset -c 0`; the entire suite and all expectations ran.
- `make check`: passed under `taskset -c 0` (see harness race below).
- `make examples`: passed.
- Direct classifier probes for nominal/marker-like names, Vector/Map/Queue
  nesting, `_generic:x`, nested method/field markers, and `option[_]`: passed.
- `git diff --check`: passed. Parser/assertion fixtures and effects/sync guards
  retain their existing behavior. No compiler edits followed the final build.

Earlier build/probe results are superseded by these final gates. Two unrestricted
final full-run attempts timed out in unchanged Executor probe `38-branch-lost-wakeup` waiting
for its publisher signal (53/54 runtime probes passed). The same binary passed
in isolation, then passed three captured-output trials pinned to CPU 0.
The harness arms its lock-held pre-wait hook before the publisher obtains the
executor lock for its preparatory notification: an idle worker can enter the hook
first and block that publisher. This is separate from type placeholders; runtime
and harness sources/expectations are unchanged. Final full-suite validation used
CPU affinity to stabilize scheduling, retaining every assertion and worker test.
An earlier unrestricted full run also passed, before the shared-header edit.
At that closeout, hardware-parallel validation was not clean because of
this harness race. The Executor harness follow-up above supersedes that
CPU-affinity limitation with ordinary multicore validation; the observed
timeouts and historical pinned results remain recorded here.
The generated minimal root Rust source is byte-for-byte identical with a compiler
built from the reviewed HEAD, confirming unchanged runtime emission.

A separate pre-existing native lowering issue was reduced and confirmed with a
compiler built from reviewed HEAD: `fn wrap(x): return Box(items = [x])` with a
non-Copy `Payload` parameter infers READ, then emits that borrowed parameter into
an owned vector (`&Payload` vs `Payload`, including its view wrapper). Ordinary
`Payload` had the same failure. This follow-up left it for a separate
ownership/effect repair. It was subsequently fixed by
`dbe62d3c88aad7a052a856a8deb1f695f54b1c76`, the vector-literal ownership repair
recorded above, and is no longer outstanding. At discovery, the new generic
Vector probes transferred an already-owned vector; original `[x]` Int template
probes remained unchanged/green.

No unfinished work or blockers remain within this follow-up's scope. Commit `0d0f96990f483360967af8d51ebac070034f57ef` records the repair and
is already on `main`. The prior repair is also on `main`.
Compiler-agent self-report: bootstrap, structured diagnostics, effects queries,
reduced probes, reviewed-HEAD reproduction, native/Fast Debug parity, and final
full gates resolved the requested behavior without specialization redesign.

## Builtin/generic typing and assertion correctness repairs — 2026-10-07

Base: `a975db321cc53dadcbee3d0aeefee868888da7d7` on `main`.
These are pre-existing compiler correctness bugs exposed during Phase 20.5
review. All three were independently reproduced on pre-Phase-20 `8b9e668`;
the baseline checkpoint below preserves the original sources and outcomes.
Phase 20 FileIO/Executor and concurrency semantics are unchanged.

Root causes and implementation:

- Builtin push checked its expression without comparing its type with the
  concrete element type. Authoritative builtin descriptors now carry
  receiver-relative argument type relationships as well as results, effects,
  arity, and failure facts. Ordinary member calls and generic requirements share
  validation for Vector/Queue, String, and Map. Argument expressions are checked
  first to preserve domain-handle diagnostics. Writable arguments still require
  storage and normal ownership checks. Statement calls use the same path.
- Constructor inference mistook unresolved specialization evidence for a
  concrete field conflict or could commit it to a nominal layout. Placeholder
  evidence, including nested collection types, is deferred until concrete body
  checking revisits the constructor. Expression bodies use the same check;
  object projections check their receiver so helper calls specialize before
  native lowering. Concrete field conflicts report TYPE_MISMATCH. Nominal
  fields retain one globally inferred layout; missing concrete field evidence
  still reports TYPE_INFERENCE_FAILED. No generic object types were introduced.
- Bare reserved assertion statements fell through to Raw. The parser rejects
  malformed assert/assertEqual word forms with INVALID_ASSERT_SYNTAX before
  that fallback. Canonical assertions and their existing type/arity diagnostics
  remain unchanged; prefix identifiers remain valid. Four existing interpreter/
  callable fixtures and two project-test generators were migrated to documented
  parenthesized assertions without changing expressions or expected results.

The tooling missing-map probe now uses repository-local tmp instead of a
hardcoded /tmp path denied by the current environment; its diagnostic expectation
is unchanged.

Permanent coverage: `tests/tooling/check_builtin_generic_typing.py` and its
83 fixture/manifest cases are wired into `tests/run.sh`. The original eight
baseline sources remain unchanged. All 24 accepted cases have exact native/
Fast Debug stdout parity; all 59 negative cases reject in check, native lowering,
and Fast Debug at the shared front end. Coverage includes direct, typed and
generic collection mutations, builtin argument/arity and writable-flag guards,
constructor statement/expression bodies, direct result projection, independent
nominal wrappers and repeated calls, nested placeholder inference, and assertion
syntax/type/arity checks. Type/effects/ownership/inspect guards verify container
WRITE, non-Copy push CONSUME, Map get READ and delete READ/CONSUME/WRITE arguments,
and EXCLUSIVE synchronization for a generic domain-state push helper.

The nested Pair guard uses `left = x + ""` and `right = x + "!"` to construct
two owned values. The illustrative `left = x` followed by `x + "!"` consumes then
rereads x; typed and untyped forms retain OWNERSHIP_USE_AFTER_CONSUME.
Expression bodies use the existing `fn wrap(x) = Box(s = x)` spelling.

Final validation on the final compiler/test revision:

- `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic src/moss.cpp -o tmp/moss-strict`: passed.
- `./moss agent bootstrap --json`: moss-0.1 confirmed; ./moss is the strict binary.
- `python3 tests/tooling/check_builtin_generic_typing.py tmp/moss-strict`: 83/83 passed.
- `python3 tests/tooling/check_generic_specialization.py ./moss`: original 40/40 passed.
- `python3 tests/tooling/phase20_review/run_review_probes.py ./moss --strict`:
  36 bug, 30 guard, and 2 pre-existing probes passed; 8 design cases informational.
- `python3 tests/tooling/check_phase20_executor_fileio.py ./moss build/tests`: passed.
- `sh tests/run.sh ./moss build/tests`: passed, including the new suite.
- `make check`: passed, including the new suite.
- `make examples`: passed.
- `git diff --check`: passed, including final status/commit review.

Earlier focused/full-gate attempts are superseded: fixture spelling/ownership
assumptions were corrected, legacy assertion generators migrated, domain-handle
diagnostic priority preserved, and direct constructor projection checking fixed.
No compiler changes followed the final strict build. The environment change
broke the sandbox launcher; approved validation used repository-local TMPDIR.
No unfinished work or blockers remain for these repairs. One local completion
commit `7cf3f69208e0a0d89b7310931364a609b51aa3c9` records implementation,
coverage, parity, and final validation and is already on `main`.

Compiler-agent self-report: bootstrap, structured checks, concrete semantic
queries, native/Fast Debug parity, and full gates resolved the checked behavior.
The final gates supersede all validation invalidated by earlier compiler edits.


## Builtin/generic typing and assertion repair — baseline 2026-10-07

Base: `a975db321cc53dadcbee3d0aeefee868888da7d7`, clean `main`.
Startup skills loaded; live bootstrap confirms `moss-0.1`. These are classified
as pre-existing compiler correctness bugs exposed during Phase 20.5 review,
not defects in Phase 20 concurrency or FileIO/Executor semantics. All three were
independently reproduced using a compiler built from `8b9e668`, the revision
immediately preceding the Phase 20 bootstrap commit: wrong-type push and bare
assert check/interpret successfully but fail Rust compilation; the generic Box
constructor fails with the same unresolved-parameter field conflict.

Before any compiler edits, eight permanent minimal sources were added under
`tests/tooling/fixtures/builtin_generic_typing/` and run through structured check,
native lowering, `rustc -O -D warnings`, native execution when compilation
succeeded, and Fast Debug. Direct manual commands reconfirmed all three bugs.

- Direct, typed-helper, and generic-helper Vector[Int] pushes of String all
  pass Moss checking/lowering, fail Rust type checking, and execute in Fast Debug.
  Queue() followed by Int then String pushes has the same outcome.
- Generic `wrap(x)` returning `Box(s = x)` rejects before either backend with
  `MOSS_COMPILE_ERROR`: String conflicts with `_generic:x`. Its typed String
  equivalent checks, compiles, and prints `hello` in both native and Fast Debug.
- Bare `assert x == 1` and `assertEqual x, 1` pass Moss checking/lowering and
  fail Rust compilation. Fast Debug executes the former; the latter reaches
  an unsupported-expression interpreter error rather than front-end rejection.

Full disposable baseline records: `tmp/builtin-generic-typing/baseline.json`.
The original sources are retained unchanged. Implementation and final validation
remain pending at this baseline checkpoint.

## Phase 20 specialization documentation synchronized — 2026-10-07

Base: `0d42c71e5cfc98c088efffe1560bee8c8f1625c9` on `main`.
Documentation/status-only cleanup records the settled language-wide static
specialization model and Phase 20.5 A–D correctness repair alongside the retained
performance follow-up. The former specialization-breadth owner-decision item is
retired in Phase 20 §29 and the formal language-design open-question list.
Range/String equality and startup/dynamic-clamp bound narrowing remain open.
Repair integration wording is current; historical baseline evidence is preserved.

Validation passed: `./moss agent bootstrap --json` confirmed `moss-0.1`;
`python3 tests/tooling/check_phase20_doc_examples.py`, `make check`, and
`git diff --check` passed. Final diff review confirms documentation/status changes
only; no compiler/runtime/test files changed. No unfinished work or blockers remain.
Compiler-agent self-report: live discovery and existing gates were sufficient;
no semantic probes or implementation changes were needed.

## Generic specialization semantics fixed — 2026-10-07

Base: `4645e3a78edee822cd858bc286a8bba13daa4a44` on `main`.
Completed compiler repair of A–D; no new syntax or language design. The
baseline checkpoint below was written before implementation. All original
reviewer probe sources and expectations are unchanged. The completed repair is
now incorporated into `main` at `0d42c71e5cfc98c088efffe1560bee8c8f1625c9`.

Root causes and repair:

- A: native call lowering treated generic operator parameters as Copy values
  while concrete signatures borrowed non-Copy types. Source-only effect inference
  also treated a returned unresolved parameter as READ. Each complete tuple now
  retains concrete parameter effects and mutation facts, inferred using the same
  body effect walker and concrete parameter TypeEnv as an equivalent typed helper.
  One concrete parameter ABI decision controls signatures, ordinary calls,
  functional calls, view selection, borrows, and write-through bookkeeping.
  READ Copy arguments remain values; existing primitive WRITE semantics and
  their mutable write-through ABI remain unchanged. String and Vector identity
  specializations are CONSUME and return owned values; later use rejects.
- B: generic requirements resolved user objects and FileIO/Range, omitting the
  normal builtin receiver descriptors. The existing builtin table now owns result
  types, arity, receiver/argument ownership effects and failure effects. Generic
  validation, ordinary inferred result typing, ownership/effect analysis, native
  type/receiver lowering, and completion project that shared table. No second
  builtin method whitelist was introduced. Unknown methods/invalid arguments
  reject with TYPE_MISMATCH. User-object and FileIO/Range guards remain valid.
- C: call-based inference ran before some untyped functions were marked static,
  freezing shared parameter types at the first call. Untyped templates are now
  recognized before call inference. Cached instances continue to use the full
  canonical parameter tuple, with independent effects, return types, dependencies,
  and generated names. Binding/receiver types and existing functional/join metadata
  are selected by exact specialization context, including Map locals of different
  key/value types in the same program.
- D: an unresolved builtin method result made functional analysis fall through
  to scalar pipeline normalization, interpreting `count` as a local callable.
  Source templates retain deferred return/pipeline requirements; each concrete
  body validates expressions and ownership in its concrete TypeEnv. The existing
  environment walker now infers specialized local types, including indexed Map
  writes, before concrete results and functional IR are built. No keys/count
  special case was added. Keys, values and split pipelines all have native parity.
  Unresolved template operator checks are likewise deferred to concrete instances.
  Concrete effects also honor the existing indexed-result materialization rule:
  returning an indexed result reads its container, while returning the parameter
  itself consumes a non-Copy value. The shared effect walker now agrees with the
  existing ownership walker for both typed and untyped indexing helpers.

C and D are fixed generic-specialization defects, not owner decisions. Earlier
status wording about pending specialization breadth is superseded and corrected.
The detailed Phase 20 document has now been synchronized with this settled
model: language-wide static specialization remains the existing Moss rule, and
the former §29 specialization-breadth owner-decision item has been retired.
Range/String equality and broader bound narrowing remain open owner decisions.

Coverage and baseline evidence:

- 40 permanent general probes: 36 accepted, 4 structured negative guards.
  The original reference compiler fails 31 final expectations, while 9 guards
  pass (`--baseline`; full disposable records in tmp/generic-specialization).
  The original A–D sources, both tuple call orders, and mixed tuples are retained.
  Direct Range/FileIO baseline guards pass; the extra nested helper originally
  failed unresolved result inference and now passes. Initial FileIO guard setup
  was corrected to the documented `ro` token before the expanded baseline rerun;
  no A–D reproducer or existing reviewer source was changed.
- Accepted probes run check, native lowering, rustc -O -D warnings, execution,
  Fast Debug, and type/effects/ownership/calls/inspect. All 31 supported non-FileIO
  accepted probes have exact expected/native/Fast Debug stdout parity.
  Three FileIO and two Executor/for sources run natively; the latter explicitly
  encounter Fast Debug's existing unsupported-for diagnostic. An ordinary-message
  domain WRITE source separately verifies Fast Debug/native parity.
- Both 4-worker, 2000-invocation domain helpers print 2000. Store.Add effects
  identify items in WRITE/LockSet with EXCLUSIVE acquisition. Typed and untyped
  helpers have identical complete target-specific synchronization plans.
- Supplemental reference comparison: all 185 baseline-accepted top-level fixtures
  remain accepted; all 147 negative fixtures retain their outcomes and diagnostic
  codes. Native compilation comparison finds no new failures among those 185
  sources (existing check-only/interpreter-only fixtures keep their existing
  default-native limitations).
- Three new Phase 20 reviewer guards cover String native parity, builtin callback
  specialization and generic WRITE synchronization. Existing untyped chunk
  expression/return probes already cover chunk callables and remain unchanged.
  All 73 original reviewer sources and manifest expectations were compared
  against the reference commit without modification.

Final validation, on the final compiler/test revision:

- ./moss agent bootstrap --json: moss-0.1 confirmed.
- python3 tests/tooling/check_generic_specialization.py ./moss: 40/40 passed.
- python3 tests/tooling/phase20_review/run_review_probes.py ./moss --strict:
  36 bug, 30 guard and 2 pre-existing probes passed; 8 design cases informational.
- python3 tests/tooling/check_phase20_executor_fileio.py ./moss build/tests: passed.
- sh tests/run.sh ./moss build/tests: passed.
- make check: passed.
- make examples: passed.
- g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic src/moss.cpp -o tmp/moss-strict:
  passed; this final strict binary was used as ./moss for all final gates.
- git diff --check: passed.

The earlier full-suite attempt caught an invalid typed Executor helper's diagnostic
precedence regression from overly broad return placeholder inference. It was fixed
by restricting deferred result inference to static specializations. A later run
identified generic remainder operand validation occurring in an unresolved template;
that check now runs in its concrete instance. The reference comparison then found
an indexed String snapshot incorrectly consuming the whole vector in the concrete
parameter summary; the effect walker was aligned with existing index ownership.
No previous test source or expectation was changed. The initial
attempt and earlier focused/strict results are superseded by the final gates above.
No source changes occurred after final validation. No unfinished work or blockers
remain for this repair. One completion commit records the implementation,
coverage, parity, and synchronization validation and is now present on `main`.


## Generic specialization repair — baseline 2026-10-07 (implementation pending)

Base: `4645e3a78edee822cd858bc286a8bba13daa4a44`, clean `main`.
Mandatory skills/startup loaded; live bootstrap confirms `moss-0.1`.
All 23 exact sources are preserved in `tests/tooling/fixtures/generic_specialization/`.
Before compiler changes each received structured check, native lowering,
`rustc -O -D warnings` and execution when lowering/compilation succeeded,
and Fast Debug execution. Type/effects/ownership/calls/inspect were queried
for every source's relevant callable. Disposable full outputs: `tmp/generic-specialization/baseline.json`.

| Probe | Moss check | Native | Fast Debug |
| --- | --- | --- | --- |
| `twice_multi` | accept | rustc failure | pass |
| `twice_string` | accept | rustc failure | pass |
| `twice_local_return` | accept | rustc failure | pass |
| `identity` | accept | rustc failure | pass |
| `numeric` | accept | pass | pass |
| `length` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `length_map` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `char_at` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `split` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `vector_push` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `queue_pop` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `map_keys` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `object_length` | accept | pass | pass |
| `missing_method` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `tuple_string_first` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `tuple_int_first` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `mixed_tuples` | accept | rustc failure | pass |
| `key_count` | UNKNOWN_SYMBOL_OR_TYPE | rejected | rejected/unsupported |
| `value_count` | UNKNOWN_SYMBOL_OR_TYPE | rejected | rejected/unsupported |
| `string_pipeline` | UNKNOWN_SYMBOL_OR_TYPE | rejected | rejected/unsupported |
| `domain_write` | TYPE_MISMATCH | rejected | rejected/unsupported |
| `domain_write_typed` | accept | pass | rejected/unsupported |
| `range_fileio` | TYPE_INFERENCE_FAILED | rejected | rejected/unsupported |

A: String twice passes checking but call passes String to an &String signature;
identity's semantic source summary remains READ and its &String parameter cannot
return owned String. Fast Debug already returns the expected outputs.
B: builtin receiver requirements reject String, Vector, Queue and Map with
TYPE_MISMATCH even though normal concrete methods exist; typed domain helper
returns 2000 natively and its semantic effects report EXCLUSIVE.
C: both tuple call orders reject the second tuple as incompatible with the first;
these are generic-specialization compiler defects, not owner decisions.
D: builtin-result pipelines reject `count` as an unknown local function in the
unresolved source context; these are generic-specialization compiler defects,
not owner decisions. Shared analysis must be fixed for native and Fast Debug.
The mixed tuple probe checks and interprets successfully but fails native ABI.
The initial combined Range/FileIO guard exposes unresolved nested result inference;
retain it and supplement it with direct previously supported guards.
Fast Debug for iteration is unsupported, so the 2000-invocation domain probe
requires native execution and semantic synchronization comparison; ordinary
non-FileIO cases require exact native/Fast Debug stdout parity.

No implementation or final validation performed at this baseline checkpoint.


## Canonical Phase 20 documentation synchronized — 2026-10-07

Base: `50d3a2b25119242c2a954d3c16e27aab902960d5` on `main`, containing the
completed Phase 20 / 20.1 / 20.2 implementation and closeouts below.

The Gentle Introduction, language-design document, and syntax reference now
document the implemented FileIO/Range, Executor/Root ingress, Solo blocking,
and ordered compiler Branch contract at their respective levels. Obsolete
ingress and milestone statements are corrected. The detailed Phase 20 document
remains authoritative for proofs, restrictions, and deferred work. Open owner
decisions and later-phase work remain open; no compiler/runtime/test changes
or language redesign were needed.

Validation passed: `./moss agent bootstrap --json` confirmed `moss-0.1`;
`python3 tests/tooling/check_phase20_doc_examples.py`, `make check`,
`make examples`, and `git diff --check` passed. All 19 introduced or materially
changed Moss blocks were checked, compiled natively with warnings denied, and
executed using repository-local fixtures and explicit context for fragments.
Section numbering, local/new Markdown links, and source continuation checks
passed across all three documents. No unfinished work or blockers remain.

Compiler-agent self-report: structured checks and native execution verified
the examples, including Root submission and ordered chunk processing.

## Phase 20.2 final review findings closed — 2026-10-07

Branch: `phase-20.2-review-completeness`; base:
`b84ff42a47a4dc58c128fed3430a7ec29f515bed`.
All three final reopened findings are closed after validation on the final
compiler and tests. This closeout supersedes the older reopened status below.

- Bounds analysis shares statement mutation roots between the loop pre-pass
  and forward walk, including RHS calls and canonical indexed-field roots.
  Six new permanent negative reviewer probes and a read-only loop guard
  supplement all previous probes without changing their expectations.
- Indexed-field stores recurse through the existing mutable-place lowering,
  preserving vector/Map storage identity and mutable object accessors. A
  general native compiler regression covers field/index chains and WRITE
  helpers while retaining ordinary index/key/RHS read lowering.
- The exact Branch publication window fixture uses one worker. Its permanent
  lowering mutation moves scope/join inside publication and demonstrates that
  the exact trace detects the regression. Separate concurrent runtime tests
  remain unchanged.

Final validation succeeded:

- `python3 tests/tooling/phase20_review/run_review_probes.py ./moss --strict`:
  36 bug, 27 guard, and 2 pre-existing probes passed; 8 design cases remain
  informational. All 66 previous probe sources and manifest expectations
  were verified unchanged; seven permanent probes were added.
- `python3 tests/tooling/check_phase20_executor_fileio.py ./moss`: passed,
  including independent `window_k=4 eligible=yes`, the exact one-worker
  publish-before-join trace, and the join-in-publication-loop mutation.
- `python3 tests/tooling/check_phase20c_executor.py ./moss`: 54/54 passed,
  including existing concurrent free-worker Branch tests.
- `python3 tests/tooling/stress_phase20_branch_window.py
  tmp/phase20-final-review/agent-d/phase20_chunk_handler_bin --runs 1000`:
  1,000 isolated executions passed with zero failures on the final compiler.
- `make check`, `make examples`, and `git diff --check`: passed. The general
  native indexed-field fixture passed both separately and in `make check`.

The earlier partial `make check` was interrupted for the compound-RHS bounds
refinement and is superseded by the complete final run. Earlier focused
results likewise do not substitute for the final runs above. No blockers or
unfinished work remain for these three findings. Bounds and codegen fixes landed together in the Phase 20.2 closeout and have been rebased into main.
Existing Phase 20 semantics and open owner decisions below remain unchanged.

Compiler-agent self-report: bootstrap confirmed `moss-0.1`; structured
diagnostics, canonical formatting checks for accepted fixtures, native
regressions, and the runtime observer resolved the relevant uncertainties.
Three compiler edit/check rounds were needed, including compound expression
coverage. Full required gates were used rather than impact/affected-test
selection for these checker/backend changes. A compiler-owned expression
mutation query would reduce future reliance on textual expression parsing.

## Phase 20.2 documentation consistency

Base: `a9be72e0bd38a96791cacf234fa1aaacccc5effc` on
`phase-20.2-review-completeness`. This work has not been merged to `main`.

Documentation-only cleanup restores `Range.slice(start, length)` as settled
Phase 20 semantics: negative start, non-positive length, or start at/past the
end yields an empty Range; otherwise length is clipped to remaining bytes.
Theorem 1a explicitly depends on the ranked two-phase-locking portion of A2,
without its fair-waiter premise; Theorem 1b retains that fairness condition.
Range/String equality and startup/clamp bounds remain open owner decisions.
Generic multi-specialization and builtin-result pipeline failures are compiler defects
addressed by the generic-specialization repair above. Compiler/runtime sources, tests, and
language skill guidance are unchanged.

Validation: strict reviewer probes passed (30 bug, 26 guard, 2 pre-existing;
8 design cases remain informational). The documentation example check,
`make check`, and `git diff --check` passed. Bootstrap confirmed `moss-0.1`;
the cleanup required no semantic changes.

## Phase 20.2 reopened reviewer completeness — 2026-10-06

Branch: `phase-20.2-review-completeness`; reviewed starting HEAD:
`e0094600096fe8734d69a1238f3faab4cd1010ec`. The earlier Phase 20.2
"complete" entry below is superseded by this reopened review. This work has
not been merged to `main`.

Implemented:

- Replaced the function-wide textual bound alias map with statement snapshots
  of binding-origin facts. Rebinding, shadowing, branch joins, loop body scope,
  loop-carried writes, function scope, and specialization scope now control
  fact lifetime. Request-record vectors capture size facts at construction;
  batch legality and Range propagation share one normalized request parser.
- Bounded RangeBatch entry indexing and iteration use exact entry bounds or a
  conservative maximum. Unbounded entries fail closed. Added negative probes
  for both stale chunk-name exploits, loop-carried rebinding, and request-size
  alias rebinding.
- Kept `FUNCTIONAL_CALLABLE_UNRESOLVED` outside `NDEBUG` and added a normal vs
  `-DNDEBUG` semantic comparison gate. Builtin/constructor typing and
  observable effects query shared metadata; unknown calls remain unresolved.
- Completed Range equality, inequality, ordering, and arithmetic rejection
  checks and added an operator matrix. Iteration effects now distinguish
  read-only Range/Vector traversal from custom `Iterator.next()` WRITE effects
  at each concrete function specialization, including native call signatures.
- Rewrote §3 and §23 Executor snippets into current single-line Moss syntax,
  added a documentation continuation check, and corrected §12.7 proof wording.
  The permanent strict reviewer suite now has 30 bug, 26 guard, and 2
  pre-existing passing probes; 8 design probes remain informational. Original
  passing reviewer probes were preserved.

Validation on the final source: `tests/run.sh ./moss build/tests` passed,
including Agent A FileIO semantics, Agent B FileIO runtime, Agent C Executor
54/54, Agent D integration, domain-field physical concurrency, R5 leaf
fairness, Phase 20.1 E12 cases, agent skills/discovery, strict reviewer probes,
and normal/`-DNDEBUG` semantic equivalence. `make examples` passed. A strict
C++17 `-Wall -Wextra -Werror -pedantic` build and `make check` passed.
`git diff --check` passed. Earlier `tests/run.sh`
attempts were invalidated by later source edits; one pre-final run also had a
single Executor stress timeout under simultaneous C++ compilation, and the
same 54-test suite passed in isolation and in the final `tests/run.sh` run.

Owner decisions still pending, explicitly surfaced in §29 of the Phase 20
spec: (A) provisional Range/String equality rejection; (C) constant/immutable-alias
bound implementation scope, with startup declarations and proven dynamic clamps
deferred (clamps are compatible with the memory proof). The former item (B)
concerned generic/static specialization defects, including multi-parameter tuples
and builtin-result pipelines, fixed by the generic-specialization repair above.
Range.slice clipping/empty behavior is settled Phase 20 semantics. R5, E12, serial batch preads,
fixed K=4 windows, Fast Debug FileIO scope, and Range as a generic pipeline
source were not reopened. A2 remains tracked under `SYNC-FAIR-001`;
performance work remains in Phase 20.5.

## Phase 20.2 — Reviewer Completeness Hardening

Phase 20.2 complete on branch `phase-20.2-review-completeness`.
Hardened FileIO, Range semantics, bounds propagation, codegen, generic callback specialization, and pipeline effects against the independent Phase 20 reviewer probe suite (`tests/tooling/phase20_review/`).

Summary of fixes and hardening:
- **Permanent gating of independent reviewer suite:** Added strict reviewer probes to `tests/run.sh` (`run_review_probes.py "$compiler" --strict`) so that `make check` exercises the strict suite transitively and fails if any guard, bug probe, or formerly pre-existing probe regresses.
- **Range byte equality & type separation:** Implemented exact byte-slice equality (`PartialEq, Eq`) for `Range == Range` in `src/fileio_runtime.hpp`. Enforced compile-time rejection of `Range == String` and `String == Range` with diagnostic `TYPE_MISMATCH` in `src/moss.cpp`, preserving strict byte vs text type distinction and prohibiting lossy UTF-8 conversion.
- **Normative Range byte surface:** Documented full implemented Range operations in §12.6: `range.length() -> Int`, `range[index] -> Int` (0..255 with stderr abort diagnostic on out-of-bounds index), `for byte in range` (byte-valued Ints in 0..255), `range.slice(start, length) -> Range` (with exact behavior: `start < 0`, `length <= 0`, or `start >= length()` yields empty Range, otherwise clipped to remaining bytes), and `FileIO.write(offset, range)` when statically bounded. Synchronized agent skill documentation (`moss-language/SKILL.md`).
- **Out-of-bounds indexing diagnostics:** Updated `Range::get` and `RangeBatch::get` in `src/fileio_runtime.hpp` to output diagnostic stderr messages specifying invalid index and size bounds before aborting.
- **Domain-field FileIO chunk pipeline codegen:** Lowered `plan.fileio_receiver` via `method_receiver_place` in `src/file_chunk_codegen.inc` for both sequential chunk reads and parallel branch borrows (`moss_fileio_branch_read_borrow`).
- **Batch indexing in expressions:** Updated `write_call_place` and `read_call_place` in `src/moss.cpp` to recognize `RangeBatch` and `Range` receiver types and emit `.get(...)`.
- **Chained slicing return type:** Registered `Range.slice(start, len) -> Range` in `generated_expr_type` in `src/moss.cpp`.
- **Pure loop iteration:** Recognized builtin `range` generator as pure in `observable_expression_effects` in `src/moss.cpp`.
- **Static bounds propagation:** Extended `static_payload_byte_bound` in `src/static_bounds.inc` to propagate bounds through chunk iteration `for chunk in file.chunks(N)`, batch indexing `batch[i]` (exact entry size or max of entries), and sub-slicing `r.slice(start, len)` (`min(bound(r), len)`). Prevented non-mutating `length` and `slice` methods from invalidating bounds in `static_mark_possible_mutation`.
- **Untyped/generic callback inference & specialization:** In `src/moss.cpp`, enabled signature inference for multi-parameter binary operations (e.g. `fn merge(a, b): a + b`) and flagged unannotated functions for generic static dispatch. Specialized callback return types and observable effects with pipeline input types, and emitted structured diagnostic `FUNCTIONAL_CALLABLE_UNRESOLVED` when callable resolution fails. Covered in `tests/tooling/fixtures/phase20_proposal_word_count.moss`.
- **Canonical Phase 20 example syntax:** Rewrote front word-count example in `docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md` using valid current single-line pipeline syntax and `Executor().threads(8).start()`, preserving untyped `fn summarize(chunk)` and `fn merge(a, b)` without requiring multiline continuation.
- **A2 proof wording and debt correction:**
  - In §18.1, updated A2 "Established by" column to accurately reflect it as a synchronization-model assumption tracked by `SYNC-FAIR-001`.
  - In §18.6, clarified that R5 runtime-leaf fairness is implemented and closed via `MossFairMutex`, while Moss domain synchronization uses `std::sync::RwLock` which proves mutual exclusion and concurrent readers but not fair waiter admission. Theorem 1b remains conditional on A2. Removed false claims of domain ticket locking.
- **Phase 20.5 performance follow-up recorded:** Tracked benchmarks for `MossFairMutex`, executor central lock contention, `FileIO.inner` overhead, Solo hook locks, and generated Moss `std::sync::RwLock` read/read and read/write contention.

Validation:
- Independent Reviewer Probe Suite: `python3 tests/tooling/phase20_review/run_review_probes.py ./moss --strict`
  - 21 guard probes: PASS (21/21)
  - 14 bug probes: PASS (14/14)
  - 2 pre-existing probes: PASS (2/2) with `--strict`
  - 8 design probes: INFO (8/8) (informational only; deliberate §12.7 narrowing preserved)
- Regression suites:
  - `tests/run.sh`: passed (includes strict reviewer suite transitively).
  - `make check`: all 54 Executor tests, all Phase 10-22 tests, Emacs tests, and reviewer probes passed.
  - `make examples`: all examples compiled with `-Oshared-memory`.
  - `git diff --check`: clean.

## Phase 20.1 — Root Admission Reentrancy Hardening

Phase 20.1 complete. Root admission is structurally restricted to non-Root execution. The compiler prevents `executor.invoke` outside `main`; runtime ingress defensively rejects Root/Branch reentrancy before bounded admission; the deadlock topology is permanently covered by a deterministic full-queue regression. No Moss source semantics were added or changed.

Integrated from `phase-20.1-root-admission-hardening` at `6eda8f7`
(parent: `dd88e2b` on `phase-20-integration`).

Validation:
- `python3 tests/tooling/check_phase20c_executor.py ./moss`: 54/54 passed (including full-queue deadlock regression Test 50, Branch-context Test 51, and active recursive `runtime_invoke` Test 52).
- `python3 tests/tooling/check_phase20_executor_fileio.py ./moss`: passed (including compiler `--self-test` for `EXECUTOR_INVOKE_OUTSIDE_MAIN`).
- `make check`: all tests passed.
- `make examples`: all examples compiled with `-Oshared-memory`.
- `git diff --check`: clean.

## Phase 20 — implementation complete / Phase 20.5 swarm & soak pending

Branch: `phase-20-integration`. This section is the authoritative integrated
status; older Agent closeout records below are historical inputs and may
describe their isolated branches.

| Input | Integrated revision |
| --- | --- |
| Agent A — final FileIO semantics | `85e6eff` |
| Agent B — production FileIO runtime and integration | `7d1b05a` plus integration seam |
| Agent C — process executor runtime | `a099d3e8029c03b7f82030a704001606be532a7a` |
| Agent D — compiler concurrency lowering | `4cde23ffbb8b4419389f28d546537a12c48dbeb2` |
| Integration | rebased Phase 20 closeout commit (parent `6eda8f7`; reviewed source base `dd88e2b19716091adc581eae958c3231993378c2`) |

The earlier “Phase 20 implementation complete” status was withdrawn after review
of `ddc6948ce08c05a77595645150db744d9f5eb19a`. The corrective pass has
closed the FileIO backend, pipeline context, callable inference, byte Range,
bounded request, diagnostics, runtime fairness, agent discovery, and
proposal-derived acceptance gaps. The executor design was preserved.

Reviewer findings include domain-field FileIO lowering not using the domain's
single stored owner; chunk lowering assuming a Root for every eligible call;
untyped callback inference failing for `summarize(chunk)`; the missing usable
byte-oriented Range source surface and binary-safe display/write behavior;
incomplete request bound proof; invalid indexes conflating errors with EOF;
blocking FileIO analysis not traversing nested messages; runtime leaf-lock
fairness not fully established; and insufficient fresh-agent discoverability
and proposal-derived executable acceptance programs. These are corrected below
and covered by validation.

FileIO batch reads lower through production `FileIO::read_batch`; `RangeBatch`
indexing uses checked `get(i64)`, and iteration borrows scoped ranges through
`ranges()`. Source-free provider FileIO effects remain `fileio_unknown`, so
those pipelines are valid and lower sequentially until `.mossi` carries an
authoritative FileIO effect bit. This is an optimization limitation, not a
correctness blocker. Fast Debug FileIO execution is intentionally unsupported
in Phase 20; native FileIO is the supported backend and Fast Debug reports that
limitation directly.

The production FileIO→Solo→C compensation regression is in
`tests/tooling/check_phase20_executor_fileio.py`: with `threads=1` and
`max_threads=2`, its observer sees `SoloEnter` followed by a second
`WorkerSpawned` event while the FileIO Root is active. Agent C's separate suite
continues to prove the `T_max` worker bound.

Corrective-pass progress (2026-10-05):

- Implemented production lowering for domain-field FileIO calls, using the
  field state place and `open_in_place`; added a native Store acceptance
  fixture for Open/Read/Write/Sync/Sync(dataonly)/Chunks/Close.
- Added runtime Root-context selection for eligible chunk pipelines: Branch
  lowering runs only with a current Root, otherwise the sequential reference
  path runs.
- Added Range byte indexing, iteration, slicing, exact byte Display, and
  bounded Range write support. Invalid Range and RangeBatch indexes now abort
  rather than masquerading as byte zero or EOF.
- Extended blocking-FileIO warning analysis through statically known message
  targets and added a nested-message regression.
- Added proposal-derived native programs for untyped `summarize(chunk)`,
  binary byte inspection, all four main/helper Root contexts, and byte-exact
  read-to-write copying with invalid UTF-8 and embedded NUL. Added a separate
  native `for byte in range` regression.
- Narrowed §12.7 to the implemented constant/immutable-alias bound model;
  startup configuration and runtime clamp bounds are explicitly deferred.
- Exposed FileIO/Executor guidance through both skills and bootstrap source
  surface/capability discovery; added drift checks.
- `check_phase20_fileio_runtime.py`: passed, including ticket-lock contention
  coverage and binary Range formatting.
- `check_phase20_fileio_semantics.py`: passed, including Range source forms
  and shared/exclusive mode conflict checks.
- `check_phase20_executor_fileio.py`: passed, including native domain-field
  FileIO, nested-message warnings, proposal word summary, binary copy, and the
  Root-context chunk lowering. Rerun after the final word-summary fixture edit:
  passed.
- `check_agent_skills.py`: passed.
- `make check`: all Moss tests passed, including the Phase 20 Agent C 51/51
  executor suite and 61/61 Emacs tests.
- `make examples`, strict `g++ -std=c++17 -O2 -Wall -Wextra -Werror
  -pedantic src/moss.cpp -o tmp/moss-strict`, and `git diff --check`: passed.

At the start of this continuation, the only two blockers were domain-field
physical concurrency validation and the R5 runtime leaf-lock audit/fairness
proof. Both are closed below.
Under the checked-in normative §12.7, request bounds come from compile-time
constants or immutable single-assignment aliases; statically known Strings
carry their byte bounds, and Ranges inherit their bounded read size.
Startup-configuration bounds and runtime clamp bounds are deferred, not Phase 20
blockers. Performance follow-ups for serial batch preads and fixed K=4 remain
non-blocking optimization opportunities.

Historical closeout text below describes earlier checkpoints; the final
validation for this integration is recorded in this section. Do not push.

Continuation (2026-10-05): corrected the blocker list to match checked-in
§12.7. Added `check_phase20_domain_field_physical.py` to the normal test gate.
In separate production-runtime process instances, its `moss_perf` observer
holds a generated `Verify` SHARED guard while a second `Verify` acquires the
same class; `Update`, `Flush`, and `Close` each contend and acquire only after
release. A valid `Open` emits an EXCLUSIVE acquisition. The focused physical,
Agent A/B/C/D integration, and agent skill/discovery suites pass.

R5 is closed by the complete inventory in `docs/PHASE20_R5_LOCK_AUDIT.md`.
Repeated Root/Branch paths use one reusable fair leaf lock with FIFO
predecessor admission and parking, including the ordinary state lock beneath
the inode registry ticket protocol. A deterministic held-turn regression
observes eight queued entrants and their exact critical-section entry order.
One-shot reply, Branch pending/done, and compiler-owned result-slot mutexes
have finite-contender dispositions; test-only hooks are excluded. No
host-mutex-fairness assumption or R5 weakening was introduced. Existing
process ingress tickets, Executor admission tickets, inode-registry tickets,
and root FIFO queueing remain intact.

The pre-existing Moss generated handler `RwLock` still carries A2's fair-waiter
proof premise. The domain-field physical tests prove its required SHARED and
EXCLUSIVE behavior, but not A2 waiter fairness; that separate Moss
synchronization-proof issue is outside Phase 20 R5.

Validation before the rebase: Agent A FileIO semantics, Agent B FileIO runtime
(including inode-registry ticket ordering), Agent C Executor (51/51), Agent
D/integration and proposal acceptance programs (domain-field Store,
word-count/untyped summarize, main/helper chunk contexts, binary Range copy,
and Solo compensation), domain-field physical concurrency, the R5 fair
admission regression, agent skill/discovery, `tests/run.sh`, `make check`,
`make examples`, strict C++17 warnings-as-errors compilation, and
`git diff --check` passed.

Post-rebase validation (2026-10-05): rebased the closeout commit onto the
current `origin/phase-20-integration` tip `6eda8f7`, incorporating Phase 20.1
Root admission hardening. Rebuilt `./moss`; Agent A, Agent B, Agent C (54/54),
Agent D/integration and proposal acceptance, domain-field physical concurrency,
R5 fair admission, and agent skill/discovery all passed. `tests/run.sh`,
`make examples`, and strict C++17 warnings-as-errors compilation passed.
`make check` passed the full Moss v0.1 gate, including Agent C 54/54.
`git diff --check` passed. The integration is ready for Phase 20.5 swarm and
soak.

Explicit non-blocking Phase 20 limitations and follow-ups: Fast Debug FileIO
execution is unsupported; startup-bound FileIO sizes and runtime clamp bounds
are deferred; source-free `.mossi` FileIO effect remains unknown and lowers
conservatively sequentially; batch `pread`s are serial; chunk windows remain
fixed at K=4.

### Historical integration notes

The earlier integration reconciled A's semantic checker with D lowering,
connected B's owned `MossFileIOReadBorrow` to D's Branch closures, and unified
the executable process runtime across root and provider crates. It also added
production-runtime tests for Root ingress, nested messages, Branch windows,
and cross-package publication. See the historical Agent records below for
their original branch-specific validation details.

## Historical Phase 20 Agent A — FileIO semantics (closeout `85e6eff`, as recorded by Agent A)

> Integration note: on `phase-20-integration` the executor-invoke pending
> fixture is promoted to `tests/negative/phase20_fileio_executor_invoke.moss`,
> and B is integrated. The runtime-integration fixture remains pending on D's
> batch-read lowering (see the integration section above).

The corrective pass from reviewed checkpoint
`4789563c6ffe6a6612be9444a08a1d1b7afdef23` closes the helper-lifecycle
failure. The lifecycle walk now follows ordinary synchronous helpers through
normal returns, final result expressions, nested calls, inferred FileIO
parameter specializations, and branch joins. It carries open/closed state and
mode back to the caller; uncertain joined state is rejected when used. `main`
and ordinary functions retain the explicit-close obligation. Anonymous
`FileIO.open(...)` without a pinned local owner is rejected.

Offsets are checked as Int and statically negative offsets are rejected;
runtime-computed Int offsets are legal. The shared `static_bounds.inc` query
proves overflow-checked integer constant expressions and unchanged local
single-assignment sources. FileIO uses those facts for read/chunk sizes,
bounded String write payloads, batch cardinality, entry sizes, and
overflow-checked total bytes. `FileIO.write` currently accepts bounded String
payloads as an implementation restriction while the source representation
remains unresolved; the canonical Phase 20 design does not settle String-only
writes. `Vector[Int]` is rejected because it is not a byte-vector
representation, and Range-to-write is not currently supported. No byte type or
new syntax is introduced. A computed local Vector of records
with exactly `offset: Int` and `size: Int` is accepted when its count and sizes
are proven. The documented inline `(offset, size)` batch remains accepted.
Collections passed to a potentially mutating call lose their construction
bound, including mutation in later comma-separated expression components and
calls through transitive local aliases. Parenthesized direct FileIO.open
initializers establish the same pinned owner as unparenthesized initializers in
both lifecycle and static-bound analysis, including bounded String writes.
General startup-configuration and clamp/range facts are not retained in
the checker at this phase, so those sources are conservatively rejected until
general compiler analysis supplies a proof. Ordinary tuple literals also lack
a first-class Vector element type; a computed batch uses the existing named
record representation. This is a source-representation limitation, not a new
FileIO runtime contract.

Structural scoped-type checks now follow nested wrappers and object fields
without rejecting unrelated `RangeFinder` or `FileIOStats` names. Domain-field
FileIO READ/WRITE attribution, including inferred and nested helper borrows,
remains intact. `FILEIO_BLOCKING_WITH_SHARED_WRITE` remains a warning and now
finds nested helper, `echo`, and chained-view operations while excluding the
FileIO field's own protection. Native and Fast Debug reject the same focused
ownership, bound, lifecycle, and collection cases.

Final validation at `ee9ea0b` plus the parenthesized static-bound correction
passed: `./moss agent bootstrap --json` reported `moss-0.1`, the focused
`check_phase20_fileio_semantics.py` suite passed, `make check` passed, `make
examples` passed, `git diff --check` passed, and `sh -n tests/run.sh` passed.
The focused suite also verified native/Fast Debug diagnostic parity for byte-
vector and Range writes, anonymous opens in `echo`, hidden comma-expression
mutation, simple and transitive local-alias mutation, and conflicting duplicate
FileIO CLOSE/READ and WRITE/READ aliases. The positive duplicate READ/READ
case remains accepted. The pending
`tests/tooling/fixtures/phase20_fileio_executor_invoke.pending.moss` remains
for Agent D to enforce the same scoped-capability predicate at executor
boundaries. The pending
`tests/tooling/fixtures/phase20_fileio_runtime_integration.pending.moss` is the
A+B native FileIO smoke fixture for create, String write, read, batch read,
indexing, dynamic offsets, sync, and explicit close; native integration remains
pending. It does not prove compensation. A+B+C compensation still requires a
separate deterministic integration test. Agent B supplies native FileIO
runtime/lowering; Agents C/D supply Executor compensation/scheduling and invoke
integration. No B/C/D implementation was pulled into this branch, and native
FileIO execution remains unvalidated until Agent B integration. The
`executor.invoke` boundary remains Agent D responsibility.

## Historical Phase 20 Agent B — FileIO runtime corrective handoff — COMPLETE (2026-10-04)

Resumed from handoff snapshot `633ad536c7a5` (in progress, parent
`4ff337d`). The snapshot's earlier "COMPLETE" record overstated its state:
the fair lock spun on `yield_now` rather than using a Condvar, and the snapshot
failed `make check`. Snapshot hardening that was kept: ticket ordering,
production/test ABI split, checked conversions, immediate errno capture,
close ordering, and the Agent C contract docs.

1. **Root-runtime portability (blocker) fixed.** The Linux x86_64/aarch64
   `compile_error!` now lives only in `fileio_runtime_rust()` (`moss_fileio`,
   the POSIX implementation). It is removed from `fileio_root_runtime_rust()`
   (`moss_root_runtime`: inode registry, fair lock, Solo hook storage,
   process-wide symbols). That module is std-only and portable. Every root
   emits it, so ordinary non-FileIO Moss programs keep their portability.
2. **Event-driven fair ticket lock.** `FairRegistryLock` is now private:
   `AtomicU64` ticket, `Mutex<RegistryState { serving_ticket, inode_set }>`,
   and `Condvar`. Waiters park on the Condvar until their ticket is served,
   then the holder increments the ticket and calls `notify_all`. The public
   `with_set` callback facility is gone, replaced by private concrete
   `claim`/`release` (plus test-only `contains`/`reset`). Critical sections are
   a single HashSet insert/remove: no I/O, no callbacks, no nested lock, no
   unwind. Poisoning aborts, so a taken ticket is never stranded.
3. **Snapshot `make check` regression fixed.** The snapshot gated test APIs on
   `any(test, moss_test, moss_perf)`. `moss_test` is undeclared, so the
   `--check-cfg` instrumented benchmark build (`check_phase106f`) failed under
   `-D warnings`. The gate is now the repo convention `any(test, moss_perf)`,
   and the Phase 20 suite compiles test builds with `--cfg moss_perf` plus
   `--check-cfg` to catch recurrences.
   The root runtime is now textually present in every root, which exposed
   two older whole-file scans. (a) `tests/run.sh` greps fused output for
   `Vec::new()`, so the test-only trace is an `Option<Vec<_>>` initialised to
   `None` (no behavior change). (b) The 10.6E legacy-runtime guard in
   `check_phase106e_domains.py` rejects `Condvar`. It now exempts only the
   single `pub mod moss_root_runtime` block, and only for `Condvar`. That block
   is still scanned for every other legacy token, and the rest of the file
   keeps the full check including `Condvar`.
4. **Deterministic fairness regression (Test 37).** Uses cfg-gated
   `moss_root_runtime::test_support`. It holds one admitted turn and waits
   until all 8 waiters hold tickets and are parked on the Condvar, then
   releases and checks that critical-section entry follows assigned-ticket
   order (N before N+1). A free-running 8×400 claim/release phase gets the
   same check. Passed 40/40 repeated runs and 15/15 pinned to one CPU. An
   unfair mutant ("release all waiters") failed 5/5. The test also asserts the
   lock uses `Condvar` wait with no `yield_now` or `with_set`.
5. **Platform regression corrected (Test 39).** Asserts the real
   Linux && (x86_64 || aarch64) guard precedes `mod moss_fileio` in the
   emitted string, and that the root runtime has no
   `compile_error!`/`target_os`/`target_arch`. Compiles `fn main(): value = 42`
   through `./moss` and checks: `moss_root_runtime` emitted, `moss_fileio` not
   emitted, no FileIO guard. The result builds with `-D warnings` and runs.
   Also checks that `src/moss.cpp` emits `fileio_runtime_rust()` only under
   `program_uses_fileio`. No cross-target std is installed, so there is no real
   negative cross-compile. A Moss-source FileIO program cannot compile yet
   (`FileIO` is an unknown identifier until Agent A's semantics land), so the
   FileIO-side guard is checked on the emitted runtime.
6. **Production ABI verified (Test 38).** The production build exports
   claim/release and lacks the reset/contains symbols (`nm`). Nine
   production-mode probes fail to resolve: reset/contains (bare and
   qualified), close override, sync hook, and `test_support` trace/hold.
   Cross-crate production ABI, executable-internal helpers
   (`moss_set_solo_hooks`), and test-only instrumentation are now documented
   separately in `docs/ROOT_RUNTIME_ABI.md`.
7. **Cross-crate (Tests 30/31).** The existing test-cfg provider `.rlib` +
   executable test is joined by a production-cfg variant. Provider FileIO
   reaches the executable's Solo hooks (enter/leave balanced), the
   executable's registry sees the provider's live inode claim, and a duplicate
   open across crates fails closed.
8. Audit: checked conversions, offset arithmetic, and immediate errno capture
   are preserved. `MossChunks` offset advance now uses `checked_add`. The close
   ordering is unchanged: parent close, then file close, release claim, abort.
   FileIO emission detection (Tests 35/36) is unchanged.

Validation (final tree):
- `./moss agent bootstrap --json`: OK, `language_version` = `moss-0.1`.
- `python3 tests/tooling/check_phase20_fileio_runtime.py`: all passed (47
  `[PASS]` lines, Tests 1–40 including the expanded 30/31, 37, 38, 39).
- `make check`: passed ("all Moss v0.1 tests passed"; ERT 61/61; 10.6E and
  10.6F/instrumentation checks passed).
- `make examples`: passed.
- `git diff --check HEAD`: clean. `sh -n tests/run.sh`: clean.
- Snapshot baseline, reproduced in a clean worktree at `633ad536c7a5`: Phase
  20 suite passed, `make examples`/`git diff --check`/`sh -n` passed, and
  `make check` **failed** in `check_phase106f` (the `moss_test` cfg issue
  above).

Remaining dependency: the end-to-end A+B+C executor compensation regression
(contract in `docs/ROOT_RUNTIME_ABI.md`) has **not** run on this branch. It
needs Agent C's executor and Agent A's FileIO source semantics.
## Historical Phase 20 Agent C — Final Hardened Executor Runtime — COMPLETE (2026-10-04)

Final proof-level hardening and concurrency safety completed for Phase 20 Agent C executor and root runtime on branch `phase-20-c-executor-runtime`.
- **Checkpoint & Base:** HEAD `de65b182b536b33e12deae3e60a900ab75b2d22f` -> hardened implementation (base: `d3cdc52571e6d292f3cdc4e513af39596c7b7b69`).
- **Files modified/implemented:**
  - `src/executor_runtime.hpp`: Hardened lock hierarchy, ticket FIFO fairness, gate closing, TLS worker context, fail-closed validation, Linux affinity/priority, event-driven branch joins, and cross-crate process-wide runtime symbol export.
  - `src/moss.cpp`: Propagated module identity `p_.explicit_module` to `executor_runtime_rust(bool is_library)`.
  - `tests/tooling/check_borrowed_reads.py`: Updated assertion hygiene checks.
  - `tests/tooling/check_phase20c_executor.py`: Expanded to a 30-test proof-level test suite.
  - `docs/ROOT_RUNTIME_ABI.md`: Documented physical signatures and Agent B/C FileIO Solo integration seam.

### Key Hardened Capabilities & Fixes
1. **Single Lock Ownership (Solo Lock-Order Deadlock Eliminated)**:
   - Moved `solo_blocked_workers` into `MossExecInner`. Both worker scheduling state (`active_workers`, `target_threads`, `max_threads`) and Solo blocked worker set are guarded by the single `gx.inner` mutex.
   - Completely eliminated lock inversion cycles between `worker_loop` and `solo_enter`/`solo_leave`.
   - Dedicated concurrency stress test under barriers verified zero deadlocks across repeated iterations.
2. **Gate-Closing INLINE Admission on `start()`**:
   - Added `start_pending` gate-closing flag in `MossProcessRuntimeInner`.
   - `start()` immediately asserts `start_pending = true`, preventing new INLINE admissions from entering.
   - Any currently running INLINE root finishes cleanly; queued callers wait and automatically re-evaluate once the executor transitions to `ACTIVE`, routing through executor admission.
3. **Deterministic FIFO Ticket Fairness for INLINE Roots**:
   - Replaced boolean-only admission with ticket counters (`next_inline_ticket`, `serving_inline_ticket`).
   - Callers take a monotonic ticket and wait on `ingress_cv` until `my_ticket == serving_inline_ticket`.
   - Upon completion, `serving_inline_ticket` is incremented and `ingress_cv.notify_all()` wakes waiters in deterministic FIFO order.
4. **Fair ACTIVE Queue Admission & Unified `admit_root`**:
   - Ticket admission (`next_admission_ticket`, `serving_admission_ticket`) for root queue capacity in `MossExecInner`.
   - Unified `admit_root(&gx, desc)` primitive returning `MossAdmissionResult::Admitted` or `MossAdmissionResult::DrainingBeforeAdmission(desc)` without premature work consumption.
5. **Thread-Local Worker Identity & Agent-B Solo Seam**:
   - Added `thread_local! { pub static MOSS_CURRENT_WORKER_ID: Cell<Option<usize>> }` with RAII `WorkerTlsGuard`.
   - Added `solo_enter_current()` and `solo_leave_current()` returning `bool` (safely ignoring non-worker threads).
   - Added `executor_solo_enter_from_fileio(op_name)` and `executor_solo_leave_from_fileio(op_name)` adapter bridge matching Agent B's exact integration requirements.
6. **Process-Wide Runtime Across Crates / `.rlib`s**:
   - Exported `#[no_mangle] pub extern "C" fn __moss_process_runtime_raw() -> *mut ()` from root crates.
   - Dependency module crates link against the root symbol via `extern "C" { fn __moss_process_runtime_raw() -> *mut (); }`, ensuring all `.rlib` provider crates share the identical process-wide `MossProcessRuntime`.
7. **Defensive Configuration Validation**:
   - Validated `threads`, `max_threads`, and `queue_capacity` during `start()`.
   - Configs with `max_threads < threads`, `threads == 0`, `max_threads == 0`, or `queue_capacity == 0` fail closed immediately via panics.
8. **Linux Best-Effort Thread Affinity & Priority**:
   - Configured optional CPU affinity via `libc::sched_setaffinity` and thread scheduling priority via `libc::setpriority` (PRIO_PROCESS) where available.
9. **Event-Driven Branch Join**:
   - `MossBranchScope` uses `done_cond` condition variable to signal branch completion, eliminating polling sleeps in `branch_join`.

### Validation
- `python3 tests/tooling/check_phase20c_executor.py`: **30 passed, 0 failed (30/30)**.
- `sh tests/run.sh ./moss build/tests`: **All Moss v0.1 tests passed**.
- `make examples`: **PASSED** (all 29 examples compiled and verified).
- `git diff --check`: **Clean**.
## Historical Phase 20 Agent D — Compiler concurrency lowering (2026-10-04)

Branch `phase-20-d-concurrency-lowering` recovered at
`5d7a90b3302df8a136f35d2c401a3595e5dd0342` (`Checkpoint`) after preserving the
pre-existing `tests/run.sh` edit in the separate Agent C checkout. The
checkpoint was treated as unvalidated, then imported after the earlier D merge
and reconciled with completed Agent A, B, and C runtime work on
`phase-20-integration`. Final integration validation is recorded below. Agent
D defines no Executor, Root, Branch, or FileIO runtime in `src/`.

### Complete compiler functionality

**Executor / `executor.invoke`** (`src/executor_invoke_lowering.inc`,
`src/executor_invoke_codegen.inc`).

- `Executor()` and `executor.invoke` are main-only, compiler-recognized
  forms. There is no `executor` entry in `valid_type()`, so storage,
  parameters, and payloads are rejected by the ordinary type checks.
- The checker builds an `ExecutorStartPlan` with static type and range checks.
- It also builds one `RootSubmissionPlan` per invoke. Invoke arguments are
  type-checked like a handler call and take part in untyped
  handler-parameter inference.
- Codegen lowers to `MossExecutor::new().<config>.start()`, then
  `enqueue_root(MossRootDescriptor::with_target(next_root_id(), identity,
  move || ...))`, then `join()`.
  - Snapshots are owned values.
  - The thunk is `Send + 'static`.
  - The lowering never becomes a synchronous call.
- Each configuration value is evaluated once, in source order. It is then
  converted with a checked conversion (`<usize|i32 as
  std::convert::TryFrom<i64>>`, never `as`). A negative count, a negative
  affinity core, or an out-of-range priority fails closed with a clear
  message before `start()`.
- The lifecycle checker tracks one state per binding
  (Unstarted/Active/Consumed) across if/match/loops.

**Top-level `message` root ingress.**

- The checker marks every statement reached while walking `main`'s statement
  tree with `Stmt::message_root_ingress`, including messages inside `if`,
  `match`, and loop bodies. Messages in handlers and ordinary helpers are
  never marked. A regression now checks all three nested control-flow forms.
- Codegen lowers a marked message to `runtime_invoke(move || ...)`:
  - the target domain is cloned;
  - each argument is evaluated once into an owned snapshot;
  - the call waits for completion and returns the reply.
  - Exported domains use their `__moss_message_<H>` bridge.
- This applies before `start()` (INLINE), while ACTIVE, after `join()`, and in
  programs that never construct an Executor.
- A nested message inside a Root stays a direct synchronous
  `<Handler>_shared` call.
- The checked main-body context marks messages through nested `if`, `match`,
  and loop blocks; generated main ingress uses `runtime_invoke` unconditionally.
- Fast Debug runs root messages synchronously through the ordinary handler
  machinery (a synchronous Root under the sequential schedule). There is no
  scheduler.

**FileIO / Range typing.**

- `FileIO.open/read/write/sync/close/chunks` and `Range.length()` are
  typed. `chunks` returns a scoped `seq[range]`, consumed only by
  `chunks(C) |> map(f) |> reduce(init, combine)`.
- FileIO and Range are legal as ordinary helper parameters. They are illegal
  as returns, domain state, handler parameters, or payloads.
- Generated calls match Agent B.
- FileIO operations set `ObservableEffects::fileio`, directly and
  transitively.

**Chunk pipelines** (`src/file_chunk_lowering.inc`,
`src/file_chunk_codegen.inc`).

- A pipeline is eligible when:
  - map is pure;
  - neither map nor combine reaches FileIO;
  - map captures nothing;
  - neither callable's FileIO effect is unknown.
- Eligible pipelines lower to windows of `window_k = 4` Branches in one join
  scope (`branch_scope_new_current()`, `branch_publish`, `branch_join`).
  - Each Branch reads through an owned `moss_fileio_branch_read_borrow`
    token and writes to a compiler-owned slot.
  - Ordered parent commit: zero-length stops; short folds once and stops.
- Ineligible pipelines lower to the sequential reference loop.
- **Conservative provider handling.** A function from a source-free provider
  module (`.mossi` carries no FileIO fact) has
  `ObservableEffects::fileio_unknown` set. A map or combine reaching one
  lowers sequentially ("... FileIO effect unavailable for compiled
  provider").

**FileIO semantic ownership.** Agent A remains authoritative for FileIO
legality, capability boundaries, static size/bounds, and lifecycle. D's
`fileio` / `fileio_unknown` facts are scheduling inputs and do not bypass A's
checker. Source-free provider callables with no FileIO metadata remain
`fileio_unknown` and lower sequentially.

### Integrated runtime and ownership seams

- Agent B provides the production owned, read-only, `Send + 'static`
  `MossFileIOReadBorrow` token used by Branch reads.
- Agent C's final runtime is emitted in executable roots and supplies
  `runtime_invoke`, linear Executor handles, Root TLS,
  `branch_scope_new_current()`, Branch publish/join, and D-facing root adapters.
- Fast Debug reports FileIO execution as unsupported; native Phase 20
  FileIO/Branch execution is exercised against production runtimes.

### Validation

`tests/tooling/check_phase20_executor_fileio.py` (run by `tests/run.sh`)
compiles generated code against the production A/B/C runtimes; the observer
fixture only records runtime events. Coverage:

- the negative legality matrix;
- invoke parity between native and Fast Debug;
- config evaluation order;
- dynamic fail-closed config: negative threads, negative queue_capacity,
  out-of-range priority, and a negative affinity core;
- root-message ingress:
  - `runtime_invoke` in INLINE, ACTIVE alongside `enqueue_root`, and after
    `join()`;
  - a nested message without `runtime_invoke`;
  - arguments evaluated once;
  - native output equal to Fast Debug output;
- B-aligned FileIO lowering;
- K=4 publish-before-join traces with `branch_scope_new_current()`;
- the read-borrow `Send + 'static` contract;
- FileIO-reaching and domain-capturing stages lowering sequentially;
- a source-free provider regression: a provider combine lowers sequentially
  while a local combine stays eligible, both producing correct output.

### Integration validation

The following were rerun on the integrated branch after the final edits:

- `python3 tests/tooling/check_phase20_executor_fileio.py ./moss build/tests` —
  passed against production A/B/C runtime code, including nested-control-flow,
  executor-free, exported-domain, Branch, and source-free-provider cases.
- `python3 tests/tooling/check_phase20_fileio_runtime.py ./moss build/tests/phase20_fileio` — passed.
- `python3 tests/tooling/check_phase20c_executor.py ./moss` — 51/51 passed.
- `sh tests/run.sh ./moss build/tests` — passed.
- `make check` — passed, including the integrated Agent C suite.
- `make examples` — passed.
- `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic src/moss.cpp -o
  tmp/moss-strict` — passed.
- `sh -n tests/run.sh` and `git diff --check` — passed.

The bootstrap contract reported `language_version: moss-0.1`.

### Production integration against final Agent C

Representative generated programs compile and run against the integrated C
runtime: Executor configuration and multiple invokes; main messages before
start, while ACTIVE, after join, and without an Executor; value and one-way
messages; exactly-once argument evaluation; nested handler messaging; nested
`if`/`match`/loop messages; exported-domain bridges; and eligible K=4 Branch
publication/join using Agent B's production read-borrow token. Source-free
provider FileIO uncertainty selects sequential lowering. No A/B/C integration
blocker remains for D's compiler work.

## Root runtime ABI naming contract — names reserved (2026-10-04)

`docs/ROOT_RUNTIME_ABI.md` reserves all six requested interface groups and
maps their behavior to `docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md`:
FileIO runtime ABI, Executor/root runtime ABI, RootDescriptor representation,
branch publish/join API, solo-block enter/leave hooks, and the `runtime_invoke`
host ABI. It fixes the runtime-facing semantic names `FileIO`, `Range`,
`RangeBatch`, `Executor`, `Root`, and `Branch`, plus the lifecycle
`INLINE → ACTIVE → DRAINING → INLINE`. Future implementation belongs in new
`.hpp` / `.inc` modules with only narrow wiring in `src/moss.cpp`.

The phase specification was supplied during this session. It fixes semantic
behavior but leaves physical Rust signatures, layouts, and error carriers to
the implementation. Those remain unfinished. No Moss source or compiler code
changed. Validation: repository bootstrap reported `moss-0.1`; the worktree
was initially clean; `git diff --check` passed after the documentation edit.

## Emacs Package Build Hardening (Canonical Artifacts, Toolchains, Spaces) — COMPLETE (2026-10-01)

Hardening pass on Emacs package build integration to consume compiler/Margo-owned canonical artifact metadata without filename guessing, maintain exact build profile tracking across debug and release builds, propagate configured toolchain commands, and parse compilation diagnostics containing spaces.

Key capabilities and fixes verified:
1. **Canonical Package Artifact Retrieval (`moss--package-build-result`, `moss--get-package-artifacts`)**: Replaced raw package and source basename guessing with structured Margo build JSON metadata. Artifact lookups (`moss-show-generated-rust`, `moss--source-map-file`, `moss-goto-generated-rust`, `moss-show-debug-map`, `moss-debug`, and `moss-disassemble-at-point`) consume exact canonical paths (`executable`, `generated_rust`, `debug_map`, and `cache_metadata`), correctly handling hyphenated package names (`phase7-demo` -> `phase7_demo`) and root module names (`planner` -> `app.rs`).
2. **Build Profile Tracking & Cache (`moss--last-build-artifacts`)**: Implemented last-successful-build artifact tracking keyed by package root, updated upon compilation success. Verified that building with `--release` supersedes older debug artifacts and ensures `C-c C-g` navigates to `build/release/<artifact>.rs` and `.mossmap`. Cleaned packages or missing artifacts immediately signal user-errors rather than falling back to standalone `build/emacs/` paths.
3. **Toolchain Consistency (`MOSS`, `RUSTC`)**: Propagated user-configured `moss-compiler-command` and `moss-rustc-command` to Margo via environment variables in compilation starts and structured build queries.
4. **Environment Isolation**: Ensured Margo package builds do not receive synthesized editor `MOSS_MODULE_PATH` or `MOSS_FAST_DEBUG_SOURCE_ROOTS`, leaving dependency resolution and build order strictly to Margo. Also filtered conflicting package build directories in Margo's `package_environment`.
5. **Clickable Diagnostics with Spaces**: Updated `compilation-error-regexp-alist-alist` to match diagnostic paths containing spaces and optional column numbers for direct and Margo-forwarded diagnostics.
6. **Package Debug Build (`moss-build-debug-buffer`)**: Routed `moss-build-debug-buffer` for package sources through Margo in debug profile from the package root.
7. **Validation & ERT Coverage**:
   - Added `moss-mode-package-artifacts-and-profile-selection`, `moss-mode-toolchain-and-env-propagation`, and `moss-mode-package-debug-build-command` ERT tests; expanded `moss-mode-compilation-diagnostics-match-margo-and-compiler` for paths with spaces and column numbers.
   - All 61 ERT tests passed (61/61).
   - Full repository `make`, `make check`, `make examples`, `git diff --check`, `sh -n tests/run.sh` passed.

Recursive audit and verification across `.moss` source files, multi-module projects, and dependency packages for Emacs compilation (`C-c C-c` / `moss-check-buffer`, `C-c C-b` / `moss-compile-buffer`, `C-c C-r` / `moss-run-buffer`) and generated Rust navigation (`C-c C-g` / `moss-goto-generated-rust`).

Key capabilities and fixes verified:
1. **Margo Dependency Build Pre-requisites**: Verified that module projects with dependencies (such as `projects/build_planner`, `examples/projects/ledger`, `tests/tooling/fixtures/phase15_8`, etc.) have their external dependencies built via `./margo build` to emit `.mossi` interfaces under `<dep>/build/debug`.
2. **Emacs Module Dependency Resolution (`moss--dependency-module-paths`)**: Added `moss--dependency-module-paths` to `moss-mode.el` to discover and propagate `MOSS_MODULE_PATH` into `compilation-environment` and `process-environment` during `moss--compilation-start` and `moss--semantic-query`. Refined path resolution to select `build/debug` or `build/release` and `deps` without ambiguous parent directories.
3. **Debug Map Generation for Normal Compilation**: Added `--emit-debug-map` to `moss--native-command` in `moss-mode.el` so `C-c C-g` (`moss-goto-generated-rust`) immediately maps source lines to generated Rust after compilation.
4. **ERT & Repo Validation**:
   - Added `moss-mode-dependent-package-check-and-compilation` and `moss-mode-goto-generated-rust-navigation` ERT tests in `editors/emacs/moss-mode-tests.el`.
   - All 55 ERT tests passed (55/55).
   - Full repository `make check` passed (hygiene, benchmark suite, 30 agent tasks, fast debug convergence, and compiler tests).

## Cross-Package & Multi-Module Emacs Navigation & Symbol Traversal — COMPLETE (2026-09-30)

Recursive audit across all 1,444 `.moss` source files and 387 `Moss.toml` manifests was conducted to verify that Emacs semantic features (`goto-definition`, `goto-declaration`, `find-references`, `moss-callers`, `moss-callees`, `moss-call-tree`, `imenu`, and `xref-backend-apropos`) work across module and package boundaries.

Key capabilities and fixes verified:
1. **Emacs Dependency Source Discovery**: `moss-mode.el` discovers path dependencies declared in `[dependencies]` under `Moss.toml` (`moss--dependency-source-roots`) and supplies `MOSS_FAST_DEBUG_SOURCE_ROOTS` during semantic query execution.
2. **Compiler Query Closure**: `moss` CLI semantic query dispatch (`symbols`, `references`, `calls`, `resolve`, `complete`, `type`, `effects`) now resolves transitive source closures (`fast_debug_source_closure`) with exact source provenance across package boundaries, enabling jump-to-definition (`M-.`) directly to physical `.moss` files in dependent packages.
3. **Diagnostic Source Provenance**: Parse and compile error handling in `fast_debug_source_closure` preserves exact source filenames and line numbers across submodules.
4. **Validation Matrix & ERT Coverage**:
   - Added `moss-mode-dependent-package-cross-module-navigation` ERT test to `editors/emacs/moss-mode-tests.el` verifying cross-package xref definitions, callers/callees, and apropos search using `projects/build_planner` (`planner` -> `graphlib`).
   - All 53 ERT tests passed (53/53).
   - Full repository `make check` passed (hygiene, benchmark suite, 30 agent tasks, fast debug convergence, and compiler tests).

## Emacs browsing across `examples/projects` — COMPLETE (2026-09-30)

Emacs semantic browsing is now exercised against every one of the ten `.moss`
files under `examples/projects` and all four enclosing `moss.toml` project
contexts. The durable ERT matrix checks compiler-owned symbols in each physical
file, at-point definition lookup for every reported use, find-references counts,
and callers/callees for every function, method, handler, and main entry. It also
covers source, test, and benchmark logical contexts. All four example projects
build successfully through Margo.

The audit found three related compiler provenance/navigation defects. Nominal
types, traits, fields, and some members did not retain their physical source in
semantic target facts; explicit module entities pointed at the first exported
declaration instead of the `module` declaration; and module references omitted
imports/qualified uses while qualifier-at-point resolution missed constructor
and type uses. Those facts now retain exact source provenance, module entities
point at the declaration, module references include `module_import` and
`module_qualifier`, and qualifiers resolve through checked semantic uses as well
as call edges.

The stock Emacs mode now recognizes `module`, `import`, `export`, and exported
declarations in font lock, defun movement, indentation, and Imenu. Imenu exposes
a Modules group and indexes exported functions, types, domains, and their
members in the real ledger example.

Validation passed: compiler rebuild; focused Phase 22.5 semantic IDE checks;
all 52 ERT tests; the exhaustive real-example browsing matrix; `make check`;
`git diff --check`; `sh -n tests/run.sh`; and Margo builds for `ledger`,
`phase10_modules`, `phase7_demo`, and `phase7_failing_test`. The expected
user-bus warning emitted by the sandboxed Margo launcher did not affect any
build, and the intentionally failing project's tests were not run as a success
criterion.

Session report: bootstrap, resolve, references, symbols, calls, editor overlays,
and real-compiler ERT integration were used. Exact at-point probes on the real
examples exposed all three issues with three substantive edit/check/repair
cycles. Impact/affected testing was not a useful selector for this compiler and
editor cross-cut, so focused coverage and the full suite were run. A single
compiler audit command that enumerated editor operations over a project would
have saved the most bespoke matrix work. No generated Rust was used to infer
semantics, and no work remains for this request.

## Phase 22.5 call-tree/domain-navigation corrective pass — COMPLETE (2026-09-30)

Current `main` was corrected from `9e8923a` (with required starting commit
`478a695` verified as an ancestor). The canonical regression is
`tests/tooling/fixtures/phase225_domain_navigation.moss` and preserves the full
Ledger, Inventory, and App routing/message chain.

The domain-goto failure was compiler-side: `at:<line>:<column>` discarded the
column and selected every retained entity on the line. At the Ledger constructor
it therefore selected the left-hand `ledger` binding, while Emacs's `--kind
call` query found no constructor call entity. The call-tree failure combined
that call-biased editor resolver with a second compiler gap: message calls
embedded in `reply` and `echo` expressions were not retained as call edges.

`resolve at:<line>:<column>` now maps the actual token through checked
declarations, `SemanticUse` identities, and resolved ordinary/message edges.
Domain, object, and enum constructors and module tokens participate in that
same compiler-owned path. Emacs `moss--entity-at-point` invokes it directly;
xref definitions/references, callers, callees, and call tree reuse the returned
durable ID. Direct call records now separate call-site provenance from
`target_source` callee-declaration provenance. Call-tree roots and children are
entities, and `RET` visits their declarations.

The focused compiler regression now checks generic resolve, references, calls,
durable IDs, declarations, callers/callees, and callee provenance for Ledger,
Ledger.Read, Inventory, Inventory.Available, App, and App.Run. Nine new
real-compiler ERT tests cover domain/handler goto, domain/handler references,
callers, callees, call-tree command opening, nested expansion, and `RET` visit.
The complete ERT suite is 48/48 green.

Validation passed: `make`; the expanded Phase 22.5 Python check; Phase 22.1
teaching and Phase 22.3 semantic-query compatibility checks; all 48 ERT tests;
`make check`; `make examples`; strict C++17 `-O2 -Wall -Wextra -pedantic
-Werror`; `git diff --check`; `sh -n tests/run.sh`; and live `agent bootstrap`,
`capabilities`, and `schema` JSON. Discovery remains coherent at `moss-0.1` /
`moss-agent-1`, including token-aware resolution and `target_source` call
provenance. A separate non-ERT Emacs smoke run passed constructor/route and
handler goto, handler references, callers/callees, call-tree open, two-level
message expansion, and declaration-backed child navigation.

Session report: bootstrap, generic resolve, semantic references, static calls,
editor overlays, and discovery schema were used. Exact `at:` probes exposed the
line-only resolver and missing embedded-message edges before implementation.
Two full-suite compatibility repairs preserved bare-name ambiguity while
keeping token-at-point declaration selection exact. No generated Rust was used
to infer Moss semantics, and no LSP server or Phase 22.6 work was added.

## Phase 22.5 pipeline completion legality — COMPLETE (2026-09-29)

The final Phase 22.5 hardening follow-up started from
`b7692e530b38db570f14be158bb6ddc6fa729402` and remains narrowly scoped to
functional pipeline completion. `Checker::functional_operation_legal` is now
the common stage-state predicate used by ordinary pipeline checking, pipeline
type inference, and semantic completion. `FunctionalPipelineState` retains the
current element type, current output type, and terminal status; completed
`map` callbacks advance the element type through the existing compiler
callable-result inference.

Completion still enumerates the compiler-owned `functional_operations()`
registry, but now asks the checker to filter those descriptors. An `Int`
collection initially offers all seven current operations. A valid `filter`
preserves that set. Mapping `Int` to non-copy `String` offers only `map`,
`reduce`, `count`, `any`, and `all`; `filter` and numeric-only `sum` are
excluded. User-defined non-numeric elements do not offer `sum`. After each of
`sum`, `count`, `any`, `all`, and `reduce`, completion offers no following
functional stage. Stage-name discovery still offers callback-taking operations
before the callback has been typed.

The focused compiler regression uses real completion overlays for pipeline
start, post-map, post-filter, all five terminals, mapped-element type changes,
and a non-numeric user type. The new real-compiler ERT test verifies the same
filtered results through Emacs CAPF. All 39 ERT tests pass.

Validation passed: `make -B -j4`; `make`; the expanded Phase 22.5 Python check;
all 39 ERT tests; `make check`; `make examples`; strict C++17
`-O2 -Wall -Wextra -pedantic -Werror`; `git diff HEAD --check`;
`sh -n tests/run.sh`; and live `agent bootstrap`, `capabilities`, `schema`, and
`session-report-template` JSON. Discovery remains coherent at `moss-0.1` /
`moss-agent-1`. No generated Rust was consulted for completion semantics.

Session report: bootstrap, semantic completion overlays, structured checking,
and the session-report template were used. The existing checker pipeline-state
and callable-result logic removed ambiguity and limited the work to one shared
predicate/state refactor. Two significant edit/check/repair cycles were needed:
the initial shared-state implementation and a strict-warning/predicate-sharing
cleanup. Impact/affected testing was not useful for this compiler/editor
cross-cut, so focused, ERT, and full suites were run. A compiler-owned
completion-explanation field naming each accepted/rejected legality rule would
have reduced the remaining source inspection.

## Phase 22.5 corrective hardening — COMPLETE (2026-09-29)

Current `main` was hardened from `1964cb733d74cbd33601a111c917bf1edc6463ca`
without redesigning Phase 22.5, adding an LSP server, or implementing Phase
22.6. Before edits, the dedicated semantic IDE check passed and all 34 existing
ERT tests passed.

Semantic references now project exact facts retained by normal checking. The
scope-aware ownership walk records minimal `SemanticUse` entries for resolved
local identities; checked declaration types retain exact nominal identities,
including nested Vector, Map, and Queue arguments. Existing
`SemanticCallEdge` and functional callable records remain authoritative for
calls. `checked_text_uses_binding` and query-time type-string equality were
removed. Regressions cover same names in separate functions, nested `let`
shadowing, strings/comments, qualified members, and composite type uses.

Normal checking and completion now share `BuiltinOperationDescriptor` for the
complete current Vector, Map, Queue, and String member surfaces. Functional
pipeline recognition and completion share `FunctionalOperationDescriptor` for
`map`, `filter`, `reduce`, `sum`, `count`, `any`, and `all`. Invalid
receiver/member combinations remain excluded.

`resolve`, `references`, `symbols`, `calls`, and `complete` accept a tooling
overlay while preserving the physical source path for project identity and
diagnostics. `moss-mode` always supplies the current buffer for these editor
operations. Navigation/search/call overlays are strict and fail explicitly on
invalid unsaved source; bounded incomplete-line recovery remains exclusive to
`complete`. The unchanged-buffer cache avoids an identical process invocation,
and edits invalidate it, as verified by deterministic ERT invocation counts.

The informational 25-sample fresh-process measurement on the Phase 22.5
representative project recorded first/median/p95 milliseconds as follows:
`complete` 5.985/6.856/15.913, `references` 5.884/6.965/10.163,
`symbols` 6.344/8.839/13.761, and `calls` 15.191/7.006/11.508. Compiler
process startup is included and is a material part of these small-query
observations. No daemon was introduced.

Validation passed: `make`; the expanded Phase 22.5 Python check; all 38 ERT
tests; `make check`; `make examples`; strict C++17
`-O2 -Wall -Wextra -pedantic -Werror`; `git diff --check`; `sh -n tests/run.sh`;
the 25-sample latency utility; and live `agent bootstrap`, `capabilities`,
`schema`, and `session-report-template` JSON. Discovery still reports
`moss-0.1` / `moss-agent-1` and now advertises editor overlays coherently.

Session report: semantic references, structured query errors, source overlays,
completion, calls, bootstrap/capabilities/schema, and the session-report
template were used. Exact query output and real compiler ERT integration most
reduced ambiguity. Two significant repair cycles followed implementation: one
strict-warning cleanup and one ERT-exposed enclosing-identity JSON handling
fix. No generated Rust was consulted. Impact/affected testing was not useful
for this compiler/editor cross-cut, so focused and full suites were run. A
compiler query explaining why each completion candidate is legal would have
saved the most remaining inspection time. No corrective Phase 22.5 work remains.

## Phase 22.5 — Emacs Semantic IDE — COMPLETE (2026-09-29)

Phase numbering is reassigned: Phase 22.5 is the Emacs Semantic IDE and the
previously reserved repair/workflow automation work moves to Phase 22.6. No
Phase 22.6 implementation is part of this branch.

The branch `phase-22.5-emacs-ide` started from current `main`. The compiler now
exposes deterministic `references`, `symbols`, and contextual `complete`
queries through `moss-agent-1`. Completion accepts a tooling-only source
overlay, recovers only the active line, and reports whether recovery was used.
The stock-Emacs client consumes those facts for xref definitions/references,
workspace symbols, CAPF completion, callers/callees, and an expandable call
tree without maintaining an editor-side semantic index or consulting generated
Rust.

Closeout fixed two issues exposed by the dedicated regressions: binding
reference queries no longer inherit calls to the enclosing function when names
overlap, and completion chooses an enclosing callable body rather than a module
or declaration symbol. The tests also use exact cursor positions and assert
the fixture's complete compiler-owned call sets: three `leaf` calls and two
`Counter.Read` method calls.

Validation used `rustc 1.98.1` and the repository compiler built from this
tree. The dedicated Phase 22.5 semantic IDE query check passed. All 34 Emacs
ERT tests passed, including real-compiler incomplete-buffer completion,
cross-file xref, method/message references, ambiguity, workspace symbols,
module completion, and contextual completion kinds. Full `make check` and
`make examples` passed. A strict C++17 compiler build with
`-O2 -Wall -Wextra -pedantic -Werror`, `sh -n tests/run.sh`, and
`git diff HEAD --check` passed. No Phase 22.6 repair/workflow automation is
included.

Session report: live bootstrap and structured JSON query probes made the
reference-set mismatch and ambiguity behavior explicit. Three significant
edit/check/repair cycles were needed. The remaining uncertainty was internal
completion-scope selection, which required a reduced probe because no query
currently explains candidate inclusion/exclusion. Impact/affected testing was
not useful for this compiler/editor cross-cut, so the dedicated checks and full
suite were used. A structured completion-explanation query would have saved the
most diagnostic time. No implementation work remains for Phase 22.5.

## Phase 22.1–22.4 consolidation — COMPLETE (2026-09-28)

The dedicated `phase-22-integration` branch starts from upstream `main`
`27b343def76897d4955d40cc0fb3ff2ad85d3913` and preserves that tree's newer
Phase 15 work. The semantic integration audit is:

| Phase | Source branch/head | Integrated through | Status |
|---|---|---|---|
| 22.1 | `phase-22.1` / `7e4e1f9767ad78377992eb01a34345027b797bfb` | substantive hardening cherry-pick `6490e3a`; current-main fixture reconciliation `6a2286b` | complete |
| 22.2 | `phase-22.2` / `b95b86b9c70a9c340305d7e5c90df140b8666582` | transitively through Phase 22.3 merge `9c2e14e` | complete |
| 22.3 | `phase-22.3` / `4682068ea3bec2aef18846e3b79aad940b68c92b` | merge `9c2e14e`; report cleanup `234ee4f` | complete |
| 22.4 | `phase-22.4` / `94d6834a144ce3d1d51ccf4aad60955960429a10` | merge `7ffc784` with the runtime query resolver reconciled to Phase 22.3 | complete |

Phase 22.2 is transitively integrated through Phase 22.3; it was not merged a
second time. Phase 22.4 reuses the Phase 22.3 resolver and semantic identities;
there is no second target resolver. Static durable identities select the
corresponding runtime trace events, including multi-module and synchronous
domain-message coverage.

Disposition of the two Phase 22.1-only source commits:

- `c3af21057884a385efd5492accc7256c20bdec4b` was still semantically missing and
  was cherry-picked as `6490e3a`. Its handler-resolution, arity/type gate,
  callable-compatibility, diagnostic-identity, documentation, and regression
  changes are retained.
- `7e4e1f9767ad78377992eb01a34345027b797bfb` is a historical status-only
  closeout. Its substantive assertions are represented by the integrated code,
  tests, and this audit, so the administrative commit was not duplicated.

Final validation used `rustc 1.98.1` and `cargo 1.98.1`. A clean strict compiler
build passed with `-std=c++17 -O2 -Wall -Wextra -pedantic -Werror`; full
`make check` and `make examples` passed. Dedicated Phase 22.1 teaching,
Phase 22.2 benchmark/baseline/analysis, Phase 22.3 semantic-query and A/B
determinism, and Phase 22.4 structured-debug/native-Margo parity checks all
passed. The suite covered multi-module and path/Git packages, source-free
`.mossi`/rlib providers, static specialization, enums/match, domains/messages,
synchronization/2PL, Fast Debug/native parity, and the cross-phase identity and
trace contracts. The Phase 22.4 context benchmark reproduced 6/6 successful
cases, 177 raw events, 64 sliced events, and a 0.3616 slice/full-trace ratio.
Optional LLDB CLI and real DAP integrations were skipped because their binaries
are absent; they are not required Phase 22.4 functionality.

No canonical Phase 22.3 A/B trial was rerun and no raw result was changed. Only
deterministic derived artifacts were regenerated for the terminology/reporting
cleanup. That checkpoint's Phase 22.5 repair/workflow reservation is
superseded: Phase 22.5 is the Emacs Semantic IDE and repair/workflow automation
is Phase 22.6.

## Phase 22.3 quantitative A/B benchmark — COMPLETE (2026-09-28)

A separate controlled semantic-query benchmark now lives under
`benchmarks/agent/phase22_3_ab/`. It contains 20 deterministic `P223NNN` tasks:
16 semantic-heavy tasks split evenly across target resolution, type/ownership,
effects/calls, and domain/synchronization, plus four negative controls. The
existing `AB001`–`AB030` corpus and `baselines/post-22.3/` evidence remain
unchanged.

Both conditions execute the same checked-out compiler binary. A benchmark-only
proxy presents either the full Phase 22.3 query/discovery contract or the
post-22.1 contract and frozen documentation from
`bb388d4aeb1df7f6dac682b427d666a44a748bf4`; it does not gate checking,
lowering, Fast Debug, native execution, or Margo. Fresh trials use an ephemeral
Codex context, an isolated task copy, empty Git metadata, no expected tree or
other run artifacts, disabled plugins/browser/memory/multi-agent facilities,
and disabled shell network access.

The legacy condition is an externally visible post-22.1 projection over the
current compiler, not execution of the historical query implementation. Shared
compiler-internal resolution improvements may therefore reduce the measured
treatment effect. Its 25% task-level `resolve` figure records attempted use of
an unavailable command, not adoption; Phase 22.3 `resolve` adoption was 68.8%.

The framework records exact semantic answers, semantic detour cost, source and
documentation reads, query status/type/adoption, validation cycles, diagnostics,
tool and Moss/Margo calls, wall time, and token usage. Deterministic aggregation
and paired comparison produce per-task/category summaries and bootstrap 95%
confidence intervals. Raw trial artifacts include prompts, manifests, tool and
agent logs, answers, final trees, validator results, and last messages.

All 20 reviewed expected solutions and ground-truth records validate. The
dedicated profile/scoring/telemetry/integrity/resume regression passes, as do the
existing agent benchmark, baseline, and baseline-analysis checks. A required
six-run smoke matrix (two semantic-heavy tasks and one control, both profiles)
completed with six validator passes and 14/14 exact requested facts; it also
confirmed treatment `resolve` adoption, control isolation, source-read
telemetry, deterministic aggregation, and comparison regeneration.

The framework and corpus were frozen in implementation commit
`6c8cd5b7ce249fb59d95231cfa52e4caf0470104`; the same SHA and compiler binary
were used by both conditions. The canonical 200-run experiment completed all
trials with no infrastructure failures, timeouts, or validator failures.

Phase 22.3 changed pooled semantic fact accuracy from 241/260 (92.7%) to
250/260 (96.2%), a +3.5 percentage-point delta. Mean semantic detour cost fell
from 3.125 to 2.975 while the median remained 3. Source inspections rose
slightly from 1.23 to 1.29 per run, with zero generated-Rust inspections in
either condition. Queries per correct fact fell from 0.969 to 0.849; agent tool
calls from 14.94 to 14.65; Moss/Margo calls from 6.01 to 5.77; mean wall time
from 44.52s to 43.40s; and mean input tokens from 258,364 to 236,486. Final,
semantic-heavy, and control pass rates were all 100% in both conditions, as were
the paired first-validation and attempts-to-green results (85% and 1.15 mean).
Semantic-query adoption was 87.5% in both profiles and treatment `resolve`
adoption was 68.8%, so discoverability remains a separate limitation.

The deterministic aggregates, paired bootstrap intervals, comparison, and
human report regenerate cleanly. Canonical raw artifacts are under
`benchmarks/agent/phase22_3_ab/runs/`; the tracked deliverables are
`comparison.json` and `REPORT.md`. The benchmark supports a modest semantic
accuracy and efficiency improvement concentrated in effects/calls and
type/ownership, but no end-to-end completion improvement and no source-reading
reduction. No Phase 22.3 compiler behavior was changed.

## Phase 22.3 — COMPLETE (2026-09-28)

Phase 22.3 native validation and final closeout completed on the integrated
implementation head `89cc354936f6b1bc9188c0878d5709cebc44aea6`. The
current-main integration commit is
`cc5846602a2bfdfa45fbf679e46b26492b52b460` (main parent
`27b343def76897d4955d40cc0fb3ff2ad85d3913`). This closeout changes no Moss
compiler, language, runtime, or query implementation.

Rust-backed validation used `rustc 1.98.1 (48a229cea 2026-09-01)` and
`cargo 1.98.1 (797e8a9bc 2026-08-05)`. The strict compiler rebuild passed
with C++17 `-O2 -Wall -Wextra -pedantic -Werror`. The Phase 22.3 semantic-query
suite passed, including source-scoped locations, project-wide durable
`entity-v1` IDs, deterministic ambiguity, `--kind`, `--enclosing`, and
structured missing resolution. Live bootstrap/schema discovery reports
`moss-0.1`, `moss-agent-1`, `resolve`, `semantic_query_expansion`, and
`resolved / ambiguous / missing` states.

All closure gates passed: agent API, teaching diagnostics, agent
skills/bootstrap, baseline analysis, the complete `make check` run, and
`make examples`. Dedicated native Margo/module coverage passed build, run,
test, path/Git dependency, multi-module, source-free `.mossi`/rlib, and
provider-resolution cases. Cross-package specialization, enum/match native and
Fast Debug cases, domain/message production/interpreter parity, project-wide
interpreted tests, and path-dependency Fast Debug all passed. Synchronization
validation passed the compiler-owned `SynchronizationPlan`, READ/WRITE/CONSUME
footprint, protected-read, lock-class/rank, conflict-witness, strict C++
normalization/lowering, generated-Rust native 2PL, source-free ABI, and
synchronous-message coverage. The current static specialization coverage passed
for untyped functions, concrete collections, callables, modules, packages, and
enum matching.

Benchmark infrastructure, baseline validation, and aggregate reproducibility
passed. The frozen post-22.3 evidence was not rerun or rewritten and remains
tied to compiler commit `ddf927ca490d092b776168c45d330c439d220268`:
final passes `28 -> 28`, first-validation successes `16 -> 18`, mean
attempts-to-green `1.467 -> 1.400`, agent tool calls `422 -> 367`, Moss/Margo
calls `156 -> 134`, 10 semantic-query calls, 8 successful semantic queries,
2 blocked by program diagnostics, 8/8 resolved queried targets, and zero
fresh-agent `resolve` uses. These measurements do not establish causality or
improved final completion, and the corpus did not record source-read telemetry.

Final hygiene passed: `sh -n tests/run.sh`, `git diff --check`, and a clean
post-closeout `git status --short`. No integration regression required a code
fix. Phase 22.3 is complete.

Deliberate limitations retained for later phases:

- direct callable effects are not retained separately;
- per-use move provenance is not retained;
- source provenance is line-oriented;
- some nested message expressions do not become semantic call edges;
- trace slicing is provided by Phase 22.4;
- the Emacs Semantic IDE is Phase 22.5; repair/workflow automation is Phase 22.6.

## 2026-09-28 — Phase 22.4 structured debugging closeout — COMPLETE

Phase 22.4 was recovered from snapshot
`27b85ec95f0896c4b0b034395fdbcf0a7033eb66` onto current-main baseline
`27b343def76897d4955d40cc0fb3ff2ad85d3913`. The snapshot is preserved by
local ref `phase-22.4-snapshot-27b85ec`; the rebased semantic snapshot is
`279c292`, and hardening is `601e429`. Conflicts in `src/interpreter.hpp` and
`src/moss.cpp` were resolved against current parser, semantic-identity,
interpreter, module, and agent-discovery infrastructure. Snapshot changes to
`.codex/config.toml`, `.gitignore`, and the old status file were dropped rather
than overwriting subsequent main work.

The final machine API is
`moss debug-query event|semantic|subtree|message-subtree|control-flow|writes|failure-slice
[selector] --source <source> --json`. Trace events expose deterministic,
execution-local `event_id`, `parent_event_id`, `call_event_id`,
`message_event_id`, `handler_event_id`, `control_event_id`, and `depth`, while
retaining compiler-owned semantic/source/specialization identities. Known but
unexecuted semantic targets are distinct from unknown targets. Local and state
write histories are scope/instance checked, and ambiguous selectors fail rather
than combining unrelated histories. This remains last-write history, not taint
or general provenance analysis.

Argument parsing is non-throwing and rejects missing, malformed, negative,
zero-where-illegal, and overflowing values through the JSON protocol. Actual
omission, rather than equality with a limit, determines `truncated` and
`available_more`. Stable errors covered by regressions are
`DEBUG_QUERY_ARGUMENT_INVALID`, `DEBUG_QUERY_OPERATION_REQUIRED`,
`DEBUG_QUERY_SOURCE_REQUIRED`, `DEBUG_QUERY_SELECTOR_REQUIRED`,
`DEBUG_QUERY_SELECTOR_INVALID`, `DEBUG_QUERY_SELECTOR_AMBIGUOUS`,
`DEBUG_QUERY_EVENT_NOT_FOUND`, `DEBUG_QUERY_TARGET_NOT_FOUND`,
`DEBUG_QUERY_NOT_EXECUTED`, `DEBUG_QUERY_NO_FAILURE`, and
`DEBUG_QUERY_UNSUPPORTED_OPERATION`.

Validation used `rustc 1.98.1` and `cargo 1.98.1`. A clean `make` passed;
`check_phase224_debug_query.py`, `check_phase224_native.py`, and
`check_agent_api.py` passed; the native check compiled generated Rust and
covered Fast Debug/native behavior plus Margo build/test/debug/run project
paths. `sh -n tests/run.sh`, full `make check`, `make examples`, and
`git diff --check` passed. The full suite's optional LLDB CLI and real DAP
integrations were skipped because `lldb`/`lldb-dap` are absent; neither is part
of Phase 22.4. Multi-module semantic identity, physical provenance,
source-backed Fast Debug, native compilation, Margo resolution, nested calls,
nested messages, branches, loop iterations, specializations, write bounds, and
failure slicing are covered.

The final benchmark reproduced 6/6 successful cases, 177 raw events, 64 sliced
events, and a 0.3615819209039548 aggregate slice/full ratio. It demonstrates a
material reduction in agent-visible dynamic context while retaining the small,
deterministic smoke cases. Structured queries intentionally remain one-run,
bounded, non-interactive Fast Debug operations. They do not add breakpoints,
stepping, native debugger integration, generated-Rust tracing, scheduler/lock
tracing, dynamic taint, or a Margo-side sliced-query engine; package-resolved
 Margo Fast Debug currently exposes the full trace.

## 2026-09-28 — Phase 15.15 EXPRESS-005 corrective closeout

The in-progress `f378def` checkpoint entry below is superseded. The existing
`phase-15.15-express-005-hardening` branch was continued without replaying peer
patches or resetting workspace changes. F1–F13 are fixed and the authoritative
D1–D13 decisions are implemented; see
`docs/PHASE_15_15_EXPRESS_005_REVIEW.md` for the individual dispositions.

The 125-probe peer registry remains primary. It now contains 61 permanent
case records covering the original breadth plus direct `replace`, reply,
owned-rvalue match, non-enum specialization rejection, loop restoration, and
Int/Bool replacement copy semantics.
`python3 tests/tooling/check_phase15_15_express005_followups.py --expect-fixed`
passed all 61. `make check`, `make examples`, and `git diff HEAD --check`
passed after the compiler and fixture edits. Standalone source-free `.mossi`
provider validation, the agent skills/bootstrap contract, and swarm issue and
feedback validators passed. Native peer cases compile generated Rust with
`rustc -D warnings`; Fast Debug and Margo runs are covered by the registry.

Key semantics: `match consume` accepts whole owned enum values and rvalues;
interior places remain non-consumable. `replace(place, replacement)` requires
writable storage and the same static type, installs replacement, consumes an
owned nontrivial replacement binding normally, and returns the old whole value
as owned. Copy/trivial replacement bindings remain usable; Fast Debug now uses
checked binding types to agree with native on this rule.
READ enum views can cross message/reply value boundaries and can feed independent
computed results. Whole-match READ lifetime remains. A consumed `var` may be
reinitialized by whole-value assignment; all reaching paths and loop back edges
must restore it before read. Untyped ordinary match parameters specialize at
concrete calls. `pass` is a reserved no-op. Legacy lowercase `option[T]` remains
for Phase 21 convergence with Result/error semantics.

No known EXPRESS-005 blocker remains after the listed full gates. The corrective
hardening is merged into `main` at `6a371f975dd2bf908efe7ff4d717df8d8a9ab01d`;
EXPRESS-005 is fixed. The older closeout and checkpoint sections below are historical.

## 2026-09-27 — EXPRESS-005 corrective hardening checkpoint (in progress)

Branch `phase-15.15-express-005-hardening` starts at current `main`
`4b1e364c22f78b27c32caad71804103e139f6d9c`. The prior “no known blockers”
closeout below is superseded by the peer review and the user's corrective task.
EXPRESS-005 must not be described as blocker-free until F1–F13 and the updated
D1–D13 regressions pass.

Startup completed: read `AGENTS.md`, both repository Moss skills, and this
status file; `./moss agent bootstrap --json` reports `moss-0.1`. The two peer
patches are in `tmp/`. They were inspected, not applied wholesale. The review
document and 125-probe harness were imported from patch 2, and the harness was
wired into `tests/run.sh`. Patch 1 remains source material for later doc edits.

Compiler edits made so far: F1 constructor observable effects; D1 exact field
names in patterns; F5 wildcard binding rejection; F6 immutable pattern bindings;
F9 trait payload rejection; F10 colon-only enum constructor fields; F12 native
`unused_assignments` allowance; D12 tag-only parentheses rejection in declaration
and pattern; D13 enum-method diagnostic; F7 enum Map key rejection; F8 enum
field-projection rejection; and a first pass at `pass` and bare-identifier
statement rejection. These later edits require a completed rebuild and focused
validation. `pass` is currently represented as a special Raw statement and
needs a review for all semantic, formatter, editor, and discovery paths.

Validation completed before the later edits: the initial `--expect-fixed` peer
harness passed F1a–F1c and reported the other 18 F bug cases failing as expected.
The D cases still assert prior behavior and **must** be rewritten to the user's
decisions. The first `make -j4` after frontend edits failed because an `err`
overload was called with four arguments; that source error was fixed, but the
subsequent rebuild was still running at checkpoint time. No `make check`,
`make examples`, source-free validation, or final `--expect-fixed` result is
claimed. Both staged and unstaged diffs passed `git diff --check` at checkpoint.

Restart from this branch. First run `git status --short`, then read this entry,
the user's task, and `docs/PHASE_15_15_EXPRESS_005_REVIEW.md`. Follow the
mandatory Moss startup again, including skills and `./moss agent bootstrap
--json`. Finish the build, run the peer harness with `--list` and
`--expect-fixed`, and continue F2–F13 plus D1–D13. Pay particular attention
to D2/D4 `replace` and owned-rvalue consuming match, D5 READ views at
message/reply, D6 specialization, D7 and F11 loop ownership. Reconcile patch 1
docs manually, update issue ledger and this status, then run all required gates.
Do not push. All work so far is local and incomplete.

## Phase 15.15 — COMPLETE (2026-09-27)

Implemented and validated on `main`: SWARM-043 rejects untyped parameter field
access while preserving typed access and static untyped-method specialization;
SWARM-044 specializes built-in Vector/Map indexing per concrete call;
EXPRESS-003 supplies the core Unicode String API; EXPRESS-004 supplies
`Map.delete(...)`; and EXPRESS-005 supplies closed enums/tagged unions with
exhaustive matching (commits `badceae` and `2e96565`). String/Map native repair
and validation landed in `580c361` and `6245c35`. Native execution, Fast
Debug, and the relevant full repository gates passed.

Rejected by explicit language-design decision: EXPRESS-002 general explicit
copying, EXPRESS-006 static `Self`/covariant trait returns, and EXPRESS-008
user-defined indexing. For EXPRESS-008, built-in indexing and SWARM-044's
per-call specialization remain; adding opt-in `x[i]` customization would
expand Moss toward operator-like overloading without compelling workload
evidence. Ordinary statically resolved methods/functions cover custom access.
The historical indexing proposal is preserved for reference.

Deferred outside Phase 15.15: SWARM-040, EXPRESS-007, and EXPRESS-009 target
Phase 21 — Error Propagation & Supervision. Phase 15.15 has no remaining open
work. Error-propagation and parsing issues previously discovered during
dogfooding are intentionally carried into Phase 21 and do not block Phase
15.15 closure. EXPRESS-001 was already fixed in earlier dogfooding work.

This administrative closeout changed no compiler/runtime code. The issue and
feedback ledger validators, agent-skill/bootstrap and agent-API consistency
checks, and `git diff HEAD --check` passed. GitHub issue #1 (EXPRESS-005) was
closed with a completion comment referencing `badceae` and `2e96565`; no other
GitHub issues in this repository remain open. The compiler-first skills and
live bootstrap confirmed that no language-surface change was needed;
impact-selected tests were not relevant to this documentation-only
reconciliation.

## Phase 15.15 EXPRESS-005 — closed on main (2026-09-27)

Main includes `badceae` and its corrective follow-up. Moss has closed enums with zero-or-more
named typed fields per case, type-qualified construction, and exhaustive
statement `match`. A plain match accepts any statically typed enum expression
allowed by ordinary READ semantics, including aggregate fields and enum-valued
helper results. It READ-borrows the matched enum place and exposes
temporary READ payload views; `match consume` consumes the whole scrutinee
before branch selection and transfers selected payload fields as branch-local
values. A consuming match requires an owning named enum binding; it cannot
partially move an enum out of an aggregate. Tag-only cases use `Enum.Tag`, not
`Enum.Tag()`. There is no partial-move state or pattern-level ownership syntax.
Parser, checking, ownership/effects, native lowering, Fast Debug, formatter,
module interfaces, agent discovery, editor surface, and canonical docs were
updated together. Focused regressions cover native/Fast Debug parity, message
boundaries, exported and source-free modules, and 32 rejected forms.

The corrective follow-up passed `make check`, `make examples`, and
`git diff HEAD --check`. Focused check, native Rust (`-D warnings`), Fast Debug,
Margo native/debug module runs, source-free provider, formatter, and all 32
negative diagnostic cases passed.
The full suite includes the ownership, borrowed-value, message, native,
interpreter, and agent/bootstrap coverage. Optional live LLDB CLI and
lldb-dap checks were skipped because process tracing is unavailable in this
environment; this is not an enum product failure. No known EXPRESS-005
semantic blocker remains. EXPRESS-005 is closed on `main`.
Compiler-first bootstrap and structured diagnostics made the
READ-versus-CONSUME distinction clear; focused tests found the field-borrow
overlap issue before the full suite. Impact/affected-test selection was not
used because full validation was required. A useful future agent improvement
would be a direct semantic query for active borrow overlap at a match arm.

## Roadmap direction (2026-09-26; historical snapshot)

The following dated roadmap records what remained at the time. The Phase
15.15 disposition above supersedes its EXPRESS-005/008 work list.

Finish the remaining Phase 15.15 designs: EXPRESS-005 closed enums/tagged unions
and exhaustive matching, EXPRESS-008 explicit user-defined indexing, and the
open questions in the Phase 20 Rust interoperability proposal. Once those
contracts are settled, start Phase 21 Error Propagation & Supervision with a
clean-slate design discussion; SWARM-040, EXPRESS-007, and EXPRESS-009 are
inputs, not predetermined solutions. Phase 22 Agent Agency Tooling has work in
progress with separate agents and awaits their review; it is not closed.
Phase 23 is **Static Compiler Optimizations**, with LLM-proposed,
compiler-verified optimizations as one possible technique. Phase 24 is
**TileIR Integration (Dynamic Optimizations)**. Both are future design work;
this roadmap update does not reinstate TileIR in the v0.1 compiler.

## Phase 15.15 String / Map native validation repair (2026-09-26)

On `phase-15.15-string-api`, full native validation with rustc exposed two
Phase-15.15 integration defects. `check_agent_api.py` still asserted the
pre-Map.delete bootstrap contract even though the compiler correctly exposed
the new API; it now expects `delete(key, fallback, found)` and deletion support.
The first valid native `Map.delete` program then failed `rustc -D warnings`:
the required initialized writable Bool flag was overwritten before Rust read its
initial value. Lowering now borrows that Bool place before assigning its presence
result, retaining eager fallback evaluation/consumption and the existing owned
removed-value behavior without adding copying or shared ownership. The existing
Unicode String/Map regression is now a native `run_case` as well as its focused
Fast Debug coverage. `make check`, `make examples`, both swarm validators, and
`git diff --check` passed; no EXPRESS or swarm issue status was changed.

## Phase 15.15 EXPRESS-002/003/004 (2026-09-26 historical pre-validation checkpoint)

The native-validation limitation described below applied only to that
checkout. It was resolved by the later String/Map native repair and completed
validation recorded above.

EXPRESS-002 was closed by user decision at `b09392c`: move and temporary
READ/WRITE borrowing remain the ownership model. The tracker has no linked
swarm program that establishes a need for independent copying; reconsider only
with a concrete compelling workload. No general copy or shared ownership was
added.

EXPRESS-003 String methods `length`, `char_at`, `chars`, `split`, and `join` use
zero-based Unicode code-point positions. EXPRESS-004 Map deletion uses
`map.delete(key, fallback, found)`, returns the removed owned value or the
eager fallback, and writes a Bool flag. Nontrivial fallback bindings are
consumed. Initial native lowering was generated and inspected, but rustc is
absent in this workspace; full `make check` and `make examples` require the
user's local native toolchain before these workstreams are green. Focused
Fast Debug checks and issue validation pass. EXPRESS-009 explicitly tracks
numeric String parsing and recoverable parse errors under Phase 21, without
an interim Option type. EXPRESS-006 is closed by user decision: unannotated
trait returns are inferred and checked at concrete uses; no `Self` return
constraint or wider concepts/where-clause system is added. EXPRESS-005 remains
for design.

## Phase 15.15 SWARM-043/044 closeout — COMPLETE (2026-09-26)

The combined implementation and native repair are on `main` at `418dd20` and
`f727895`. SWARM-043 rejects untyped parameter field access while preserving
untyped method specialization; SWARM-044 specializes existing Vector and Map
indexing by concrete call. Both are marked fixed in the canonical issue tracker.

The user reports that `make check` and `make examples` both passed locally on
`f727895` with `rustc` available, completing the full validation that was
pending in the checkpoint below. This result is user-reported rather than
independently rerun in this workspace, which has no `rustc`. The SWARM-043/044
workstream is green. Phase 15.15 remains open for EXPRESS-002 through 006 and
the separate EXPRESS-008 user-defined indexing design and implementation.
SWARM-040 remains deferred to Phase 21.

## Phase 15.15 SWARM-043/044 native validation repair (2026-09-26)

The combined commit `418dd20` was validated with rustc available. The first
actual Rust failure was the valid SWARM-043 positive control: native view
lowering called an owned method through a field-only access trait and through a
borrowed object parameter. Native lowering now materializes an access view or
clones that borrowed owned value only when invoking a consuming concrete method.

The next native failure was SWARM-044-relevant typed String Map indexing:
`HashMap<String, _>[&(key)]` passed `&&String` to Rust. String map-key reads
now lower through `.as_str()`, while non-String keys retain their reference.
The focused SWARM-043 native/Fast Debug regression and SWARM-044 Vector/Map
read/write native/Fast Debug regression pass. `make examples` passes.
The full `make check` run progressed through the new SWARM-043 test and then
exposed/fixed the String-key error; it must be rerun to completion after this
last compiler edit. SWARM-044 is fixed; EXPRESS-008 remains separate.

## Phase 15.15 SWARM-043/044 combined integration (2026-09-26)

The isolated SWARM-043 and SWARM-044 implementation commits were cherry-picked
into this integration checkout. The conflict in status and issue tracking was
reconciled: SWARM-043 is fixed by its early field diagnostic; SWARM-044 remains
open until native validation. Combined `make` compilation, issue/feedback
validators, focused Moss check, both Fast Debug positive programs, all five
negative diagnostics, and `git diff --check` passed. `make check` stops at the
agent-skill prerequisite because `rustc` is not installed in this environment;
`make examples` and native execution remain unverified. EXPRESS-008 is separate.


## Phase 15.15 SWARM-043 implementation (2026-09-26)

SWARM-043 now rejects member field access through an untyped ordinary-function
parameter or alias at the source expression with `UNTYPED_FIELD_ACCESS`. Concrete
typed fields and inferred untyped member-method calls remain accepted. Focused
negative fixtures, a positive native/Fast Debug fixture, documentation, and the
canonical issue/finding ledger were updated. SWARM-044 and EXPRESS-008 remain
separate open workstreams.

Validation after the final source edit: strict C++ build via `make all`, focused
`moss check --json` negatives, and Fast Debug positive A/A/B output; issue and
feedback validators and `git diff --check` passed. Native Rust and full gates
could not complete because `rustc` is absent: `make examples` and the skill
drift test stop on that tool requirement. Run `make check` and `make examples`
in a Rust-equipped environment before integrating this branch.
## Phase 15.15 SWARM-044 implementation checkpoint (2026-09-26)

The isolated `phase-15.15-swarm-044` branch implements per-call concrete
specialization for existing built-in indexing, checks indexed write value
types, and adds positive and negative regressions. Focused Moss checker and
Fast Debug runs pass for Vector and Map across distinct key/value types;
generated Rust has distinct concrete functions. Native compilation and the
full `make check` gate have not passed in this environment because `rustc`
is unavailable. SWARM-044 remains open pending that gate and integration.
EXPRESS-008 remains separate and unimplemented.

## Phase 15.15 SWARM-043/044 implementation plan (2026-09-26)

The approved semantic direction and executable regression/validation plan are in
`docs/PHASE_15_15_SWARM_043_044_PLAN.md`. SWARM-043 rejects field access on an
untyped parameter while preserving inferred method requirements. SWARM-044
specializes existing built-in indexing independently for each concrete call.
Neither compiler fix has been implemented by this planning commit. EXPRESS-008
remains a separate user-defined indexing workstream.

## Phase 15.15 indexing design handoff (2026-09-26)

The proposed per-call static indexing rule and implementation handoff are in
`docs/PHASE_15_15_INDEXING_PROPOSAL.md`. SWARM-044 covers built-in indexing
inference; EXPRESS-008 separately tracks explicit user-defined indexing in this
phase. The user-defined opt-in spelling remains a design checkpoint before coding.
Neither item is implemented or marked fixed by this proposal. SWARM-043 remains
separate, and SWARM-040 is deferred to Phase 21.

Updated: 2026-09-25

## Phase 15.14 — SWARM-059 inference provenance hardening — COMPLETE (2026-09-25)

Inference and finalization diagnostics now retain authoritative physical source
provenance while processing function signatures, domain state, and handler replies.
Adjacent object-field and function-signature inference traversals establish the
enclosing function, field, handler, main, test, or benchmark source before helper
diagnostics can run. Focused multi-file regressions cover non-root function
finalization, unresolved domain state, and conflicting handler replies in both human
and JSON diagnostics. Canonical tracker counts and language semantics are unchanged.

## Phase 15.14 — Agent B reconciliation follow-up — COMPLETE (2026-09-25)

Agent B's pre-integration SWARM-039/SWARM-060 work was preserved on
`agent-b-wip-preserved` and reconciled against integrated commit `a863941`.
The integrated project test runner, result reporting, filtering, tracing, and
qualified semantic-identity rename implementation superseded the older parallel
implementations. No older semantic-edit code was replayed, including the
same-module/multi-file rename hardening separately assigned to Agent C.

One SWARM-039 improvement remained material: project-wide interpreted tests now
use the canonical Fast Debug source-closure walker in test mode, treating every
root-project module as a closure root and consuming the exact dependency source
roots resolved by Margo. A focused path-dependency regression proves
`margo test --interp` executes dependency-backed tests with `rustc` deliberately
unavailable. Source-free dependency diagnostics now direct test users to
`margo test --interp` rather than `margo debug`.

Validated after the reconciliation: focused SWARM-039 and SWARM-060 tooling
checks; `make check`; `make examples`; `make all`; swarm issue and feedback
validators; `git diff --check`; and agent bootstrap reporting `moss-0.1`.
Canonical swarm counts remain 61 fixed, 2 open, 1 deferred, 1 agent misunderstanding, and
65 total. SWARM-043 and SWARM-044 remain in Phase 15.15; SWARM-040 moves to Phase 21.

## Phase 15.14 — Agent A reconciliation — COMPLETE (2026-09-25)

Agent A rebased its preserved SWARM-032 / SWARM-059 work onto the Phase 15.14
closeout at `a863941`. The formatter implementation and older formatter regression
were fully superseded by main's canonical SWARM-032 fix and
`check_swarm_032_formatter_parentheses.py`, so they were not retained.

The remaining useful SWARM-059 delta scopes `current_source_file_` while each
function is processed by signature inference. This preserves the physical provider
file for diagnostics emitted before ordinary function body checking, including a
non-root annotated-return mismatch. The additional project regression also covers
unknown local calls and missing object methods in a non-root module. Agent C's
separately assigned trait/object declaration-level provenance hardening was left
untouched, and no tracker counts or Phase 15.15 semantics changed.

Validation passed: the canonical SWARM-032 and SWARM-059 focused tests, the new
function-inference provenance regression, `make check`, `make examples`, `make all`,
both swarm validators, `git diff --check`, and the `moss-0.1` bootstrap contract.

## Phase 15.14 — Final Stabilization Closeout — COMPLETE (2026-09-25)

Phase 15.14 completes all remaining implementation and tooling stabilization work across the compiler, runtime/Fast Debug, formatter, and semantic tooling before Phase 15.15. No implementation/tooling defect remains open from the canonical swarm ledger.

1. **SWARM-051 (Control-flow join correction in module namespace rewriting)**:
   - Aligned `rewrite_module_program` with authoritative checker control-flow join semantics for module-local name visibility across `if`/`else` branches.
   - Bindings introduced on all branches survive the join and remain visible as locals after the `if` block, preventing unwanted sibling function rewriting.
   - Bindings introduced on only one branch do not survive the join, restoring visibility to sibling module callables.
   - Pre-existing outer bindings remain visible; nested branch-only and loop-local induction bindings do not leak.
   - Statically known callable references passed across explicit module boundaries retain their canonical resolved identity.

2. **Phase 15.14 for-loop local binding false-acceptance/control-flow correction**:
   - Fixed type-environment walker and ownership tracking so locals first introduced inside a `for` body and loop induction variables do not leak into the enclosing environment after the loop.
   - Definite-initialization and child-environment semantics align with native lowering and module namespace rewriting (since loops may execute zero times).
   - Pre-existing mutable locals updated inside `for` loops remain legal and available afterward.
   - Direct references to unpromoted loop locals or induction variables after the loop are rejected at Moss checking with structured `unknown identifier` diagnostics rather than leaking to rustc.
   - In explicit-module contexts, sibling functions shadowed by loop-local names become visible again after the loop.

3. **SWARM-038 (effects reports pure loop helper as divergent/unresolved while accepted as domain initializer)**:
   - Observable effect analysis enforces a conservative bounded-loop proof: the bound expression must be invariant and side-effect-free, there must be no induction-variable dependency in the bound, and no statement in the loop body may mutate variables used by the bound.
   - Collection mutation operations `pop` and `pop_front` are marked as `may_fail: true`.
   - Domain state field initializers (`field.init`) authoritatively enforce side-effect-freedom during domain checking using the same observable effect analysis.
   - Negative regressions added and verified: infinite monotonic-looking loop with mutated bound (`tests/negative/swarm_038_divergent_loop_state_init.moss`) and empty-pop initializer (`tests/negative/swarm_038_empty_pop_state_init.moss`).

4. **SWARM-066 — for/range lowering closeout**:
   - Materializes range bounds into typed let-bindings before emitting the Rust `for` loop header, avoiding ambiguity with block expressions (such as pipelines).
   - Added regression for three-argument stepped range (`range(1, 8, 2)` -> `16`) in `tests/swarm_066_for_range_lowering.moss`.
   - Status wording corrected: native checking/compilation/execution passed; existing Fast Debug `for` limitation is unchanged and tracked separately.

5. **SWARM-032 — Formatter parity for parenthesized expressions**:
   - Formatter (`moss fmt`) now accepts statements starting with `(` (grouped arithmetic, grouped comparisons, nested groupings, parenthesized calls, and pipeline operands) without misparsing keywords as function calls or jumping block levels.
   - Fixed `canonicalize_code_spacing` in `src/moss.cpp` to respect space-separated keywords before `(`.
   - Idempotence (`format(input) == format(format(input))`) and negative malformed grouping coverage added in `tests/tooling/check_swarm_032_formatter_parentheses.py`.

6. **SWARM-039 — Project-wide Fast Debug test discovery & orchestration**:
   - Enabled project-wide test execution under Fast Debug via `moss test --interp` and `margo test --interp [--trace]`.
   - Discovers and executes tests across multiple source files and modules without requiring Rust compilation or toolchain.
   - Supports test filtering by name/identity, trace generation (`--trace`), and pass/fail summary and exit codes.
   - Validated with `tests/tooling/check_swarm_039_project_fast_debug_tests.py`.

7. **SWARM-059 — Multi-file diagnostic source provenance**:
   - Preserved physical source file provenance through parsing, module composition, declaration validation (object field types, object method signatures, trait method signatures), semantic checking, specialization, and structured JSON diagnostics.
   - Declaration-level and body-level diagnostics identify the exact physical source file containing the error rather than misattributing it to the root module file.
   - Validated with human-readable and structured JSON tests covering body-level semantic errors, parse errors, object field declarations, object method declarations, and trait method declarations in `tests/tooling/check_swarm_059_diagnostic_provenance.py`.

8. **SWARM-060 — Semantic rename of module-qualified entities & multi-file modules**:
   - `moss edit rename` resolves module-qualified entity selectors (e.g. `mod.fn`, `entity-v1:function:mod__fn`) using compiler-owned canonical identity without declaring ambiguity.
   - Renames only the selected entity and its semantic references across declarations and call sites without `EDIT_TARGET_AMBIGUOUS`.
   - Supports semantic rename across multiple physical files belonging to the same explicit module, rewriting unqualified same-module call sites while preserving same-named functions and calls in other modules.
   - Genuinely ambiguous unqualified requests continue to fail with `EDIT_TARGET_AMBIGUOUS`.
   - Validated with `tests/tooling/check_swarm_060_qualified_rename.py`.

9. **Canonical tracker reconciliation & final Phase 15.14 exit statement**:
   - SWARM-059 now covers both body-level and declaration-level physical source provenance.
   - SWARM-060 now supports semantic rename across multiple physical files belonging to the same explicit module.
   - All focused and repository-wide validation gates pass (`make check`, `make examples`, `make all`, tracker validators, `git diff --check`, bootstrap discovery).
   - No known implementation, lowering, Fast Debug, formatter, diagnostic, or semantic-tooling defects remain in Phase 15.14. Phase 15.14 is fully closed.
   - Recomputed detailed counts in `examples/swarm/FINDINGS.md` and updated `examples/swarm/ISSUES.jsonl`:
     - 61 Fixed
     - 2 Open (Phase 15.15 ambiguous-spec items: SWARM-043, SWARM-044)
     - 1 Deferred (SWARM-040, Phase 21)
     - 1 Agent misunderstanding
     - 65 Total recorded findings
   - Verified with `python3 tools/check_swarm_issues.py` and `python3 tools/check_swarm_feedback.py`.

### Next: Phase 15.15 — Language Semantics & Missing Expressiveness
- Scheduled language-design and expressiveness items: SWARM-043, SWARM-044, and EXPRESS-002 through EXPRESS-006.
- SWARM-040 failure/precondition semantics are deferred to Phase 21.
- EXPRESS-007 remains deferred to Phase 21.

## Phase 15.14 — SWARM-047 / SWARM-050 corrective follow-up — COMPLETE (2026-09-25)

SWARM-047 remains fixed. Effect inference now retains each parameter's checked
WRITE observation alongside the joined READ/WRITE/CONSUME effect. Native
lowering uses it when a consumed owned parameter needs a mutable binding.
Regressions cover Map index mutation inside `if` and `while` and through a
normal helper. The functional IR assertion and Map read effect handling remain
intact; no ownership contract or reduce semantics changed.

SWARM-050 remains fixed. Control-flow join environments are recorded by exact
static specialization context, and native generation requires the matching
context for branch-created bindings. The focused regression exercises First
and Second in both discovery orders and checks each outer-to-inner native
call pair. No dynamic dispatch or trait object was introduced.

Focused native and Fast Debug parity passed for the new tests and original
committed reproducers. `make check`, `make examples`, `make all`, Moss format
checking, the swarm feedback/issue validators, and `git diff --check` passed
after compiler edits. At the follow-up closeout, FINDINGS had 54 fixed, 10
open, and 1 agent misunderstanding across 65 detailed entries; after rebasing
onto the concurrent SWARM-051/038 closeout, the recomputed totals are 56 fixed,
8 open, and 1 agent misunderstanding. No remaining
work is known for these two findings. The agent bootstrap matched `moss-0.1`;
the structured ownership/effects output helped distinguish the consumed Map
parameter from its hidden WRITE observation. One edit/check cycle fixed each
root cause; impact-selected testing was not used for compiler-wide changes.

## Phase 15.14 — SWARM-066 for/range native lowering fix — COMPLETE (2026-09-25)

SWARM-066 fixes `for i in range(...)` native lowering: range bounds are now always
materialized into typed `let __moss_range_start_<line>: i64` and `let __moss_range_end_<line>: i64`
(and `__moss_range_step_<line>` for three-argument forms) before the for-loop header.
This eliminates the precedence ambiguity in Rust lowering where functional pipeline
expressions (e.g. `values |> count` lowered as `{ … }`) were misparsed by rustc as
the loop body rather than the upper bound. The regression `tests/swarm_066_for_range_lowering.moss`
covers induction variable readability, nonzero start, computed end, pipeline as end bound,
accumulator mutation, and nested for/range in an ordinary function.

Focused native checking, compilation, and execution passed for the new regression
and the committed reproducer. `make check`, `make examples`, `make all`, Moss format checking,
and swarm tracker validation passed after compiler edits. The existing Fast Debug `for`
limitation is unchanged and tracked separately; SWARM-066 itself was a native-lowering defect.
SWARM-066 is closed. The agent skills and compiler bootstrap agreed on `moss-0.1`.


## Post-rebase Make health — GREEN (2026-09-25)

`make check` and `make examples` both complete successfully on the checked-out
`main` after the temporary-branch rebase. The compiler again emits the additive
`boolean_negation` bootstrap field expected by the agent contract checks. Generic
functional pipelines retain a result placeholder until static specialization,
while concrete pipelines still infer their terminal result. Direct calls through
statically known callable parameters now check and lower to their resolved target,
including an annotated return and a parameter sharing its target's name. The
SWARM-048 regression covers that case. The frontend syntax regression assertion
was aligned with equivalent parenthesized Rust lowering.

Final validation after the last compiler edit: `make check` passed the full Moss
v0.1 suite and `make examples` built all intended-positive examples. The optional
live LLDB tests were skipped because process tracing is unavailable. No Moss source
files changed, and no requested work remains. Bootstrap discovery and structured
diagnostics identified the failing contracts; targeted native compilations verified
the callable and generic-pipeline repairs before the full rerun. This took five
significant edit/check cycles; impact-selected testing was not used for this
compiler-wide change. A single Make health target that checks the bootstrap
contract and all examples would have exposed the rebase drift sooner.

## Phase 15.13 — Swarm Stabilization / Quasi-Stable Language Checkpoint — COMPLETE (2026-09-25)

Phase 15.13 records the corrective swarm work developed on the temporary branch,
rebased into `main`, and stabilized as the new quasi-stable Moss baseline. The
corrective sequence beginning with `c310b978` and ending with `4c6ab944`, followed
by post-rebase health commit `4b4598b`, closes the recent frontend leaks, Boolean
and String surface mismatches, parenthesized/native lowering faults, callable
parameter/interpreter parity gaps, module-closure preservation defects, nested
statement-call sequencing issue, and trait-effect projection failure that were
addressed in that stabilization run.

The Phase 15.13 baseline is green: `make check`, `make examples`, and `make all`
pass on the stabilized `main` baseline. The canonical swarm trackers are reconciled
with the landed fixes: SWARM-036 and SWARM-052 are fixed rather than open, and
EXPRESS-001 (static callable parameters for ordinary functions) is closed by the
stabilized callable-parameter path covered by `tests/swarm_048_callable_argument.moss`.
The remaining cross-module callable identity defect is SWARM-051 and remains open.

Outstanding semantic stabilization debt after this checkpoint is intentionally small:
SWARM-043 and SWARM-044 require language/spec decisions; SWARM-047, SWARM-050,
SWARM-051, and SWARM-066 are accepted-program semantic-preservation/compiler defects.
SWARM-040 remains an error/precondition specification question adjacent to the
deferred Phase 21 error-handling work. Missing expressiveness remains tracked
separately as EXPRESS-002 through EXPRESS-006, and tooling-only defects remain
SWARM-032, SWARM-038, SWARM-039, SWARM-059, and SWARM-060.

### Next: Phase 15.14 — Full Stabilization Closeout

Phase 15.14 closes implementation/tooling defects without reopening language design.
The parallel lost-semantics batch owns SWARM-047, SWARM-050, SWARM-051, and SWARM-066;
SWARM-038 travels with that batch because it is a semantic-analysis consistency defect.
The remaining tooling batch owns SWARM-032, SWARM-039, SWARM-059, and SWARM-060.

The Phase 15.14 exit criterion is: no known implementation, lowering, interpreter,
formatter, diagnostic-attribution, semantic-query-consistency, or semantic-edit defect
remains from the Phase 15 swarm ledger. Ambiguous language semantics are deliberately
not resolved inside this implementation-stabilization phase.

### Then: Phase 15.15 — Language Semantics & Missing Expressiveness

Phase 15.15 is the deliberate language-rethink phase. It owns the ambiguous semantic
questions SWARM-040, SWARM-043, and SWARM-044 together with the remaining expressiveness
work: EXPRESS-002 (explicit copy for nontrivial values), EXPRESS-003 (core String
manipulation), EXPRESS-004 (Map deletion), EXPRESS-005 (enums/tagged unions), and
EXPRESS-006 (static Self/covariant trait return). These items may change or extend the
language contract, so they follow stabilization rather than being mixed into compiler
repair work.

EXPRESS-007 remains explicitly deferred to Phase 21 — Error Propagation & Supervision
because recoverable errors belong to that larger semantics design.

## Historical Phase 22.3 — Semantic Query Expansion (superseded closeout checkpoint)

> **Historical / superseded.** This section records the in-progress Phase 22.3
> closeout state before native validation, the quantitative A/B benchmark, and
> the final Phase 22.1–22.4 consolidation. The authoritative current status is
> the completed Phase 22.1–22.4 consolidation and Phase 22.3 COMPLETE sections
> above. No work should be resumed from this checkpoint.

Foundation commit `ae126f2` merges Phase 22.1 teaching diagnostics and the Phase
22.2 benchmark onto the Phase 22.3 branch. Implementation commit `ddf927c` adds
robust `resolve` targeting by stable/current ID, name, source location, kind, and
enclosing entity; deterministic ambiguity candidates; structured type, ownership,
direct/transitive effect, call/message, domain, synchronization, and why evidence;
and explicit availability limits rather than approximated semantics. The query
layer projects existing checker, specialization, `ObservableEffects`,
`SemanticCallEdge`, `ConcreteDomainGraph`, and `SynchronizationPlan` records.

Checked synchronous-message edges are now retained by the semantic call graph.
Phase 22.1 diagnostic semantic identities resolve through the same target layer.
Focused coverage is in `tests/tooling/check_semantic_queries.py`; the schema and
benchmark behavior mapping are documented in `docs/AGENT_API.md` and
`docs/PHASE_22_3_QUERY_MAPPING.md`. Commit `3886992` fixes benchmark telemetry so
structured why reason codes are not misclassified as diagnostics.

The canonical `post-22.3` AB001–AB030 run is complete with no infrastructure
failures: 28/30 final passes, 18 first-validation successes, 30/30 eventually
green, 1.400 mean attempts, 367 agent tool calls, and 134 Moss/Margo invocations.
Against post-22.1 this is flat final completion, +2 first-validation successes,
-0.067 mean attempts, -55 agent tool calls, and -22 Moss/Margo calls. Ten semantic
queries were made; eight returned facts, two were blocked by the program's
diagnostic, and all eight recorded target resolutions succeeded. No agent invoked
the new `resolve` command, AB022 retained its diagnostic loop, and one stochastic
trial does not establish causality. Raw and derived evidence is under
`benchmarks/agent/baselines/post-22.3/`.

Strict C++17 `-Werror`, Phase 22.3 focused queries, existing agent API, Phase 22.1
teaching diagnostics, Phase 22.2 benchmark/baseline tooling, Python compilation,
shell syntax, and diff hygiene have passed. Full repository, examples, Fast Debug,
Margo/module overlap, and final clean-worktree validation remain to be recorded
below before closeout.

### 2026-09-28 current-main integration checkpoint

The old Phase 22.3 head is `ca50b580cea6ec12d331d5e0e60239b2ba1c4cb1`.
The cached current-main ref is `27b343def76897d4955d40cc0fb3ff2ad85d3913`;
integration commit `cc58466` merges it into Phase 22.3. A replay rebase was
abandoned after the first of 19 historical commits conflicted in four files;
the single merge produced three conflicts. The language-design and milestone
conflicts retain main's Phase 15.13–15.15 and Phase 23–24 roadmap changes while
recording completed Phase 22.1–22.3 work. The compiler conflict retains main's
enum/trait type validation beside Phase 22.1 teaching diagnostics and attaches
current physical source provenance to teaching errors.

`tests/tooling/check_semantic_queries.py` now has a temporary multi-file project
regression for the final `ca50b58` resolver policy. Same-line location queries
are restricted by physical source; an exact durable `entity-v1` ID remains a
project-wide identity even when the supplied source points to another file;
same-name entities remain explicitly and deterministically ambiguous; `--kind`
and `--enclosing` apply only their documented filters; and a line absent from
the requested source returns structured `missing` even when another file has an
entity on that line. The resolver implementation itself did not require a
semantic change, so the frozen 30-task post-22.3 run still represents it.

Completed validation in the integration scratch checkout: strict C++17
`-O2 -Wall -Wextra -pedantic -Werror`; focused semantic queries; agent API;
teaching diagnostics; baseline aggregate analysis; shell syntax; diff hygiene;
enum rejection and diagnostic provenance regressions; qualified semantic rename;
Fast Debug basic execution and trace; whole-project Fast Debug; project-wide
interpreted tests/filtering; path-dependency interpreted tests through Margo;
and the strict C++ synchronization-plan and lowering unit regressions. The
cross-package Fast Debug suite passed its seven source-only tests and stopped
three native-dependent tests at the unavailable Rust compiler.

Closeout remains in progress. This session's permission profile exposes the
original checkout's `.git` metadata read-only, so `git fetch origin` could not
write `FETCH_HEAD` and the integration was performed in
`tmp/phase-22.3-integration`. The sandbox also exposes neither `rustup` nor
`rustc`. Consequently benchmark expected-solution validation, baseline tooling,
`make examples`, native Margo/module/specialization checks, and synchronization
provider builds stop at `BUILD_TOOL_NOT_FOUND`; `make check` was executed and
stops immediately after retired-syntax hygiene because the agent-skill test
requires `rustc`. Phase 22.3 must not be marked complete until those native gates
and a full green `make check` run are completed in an environment with writable
Git metadata and the repository's Rust toolchain.

The integrated scratch branch is preserved for handoff in the repository-local
`phase-22.3-integration.bundle` Git bundle.

The remaining Phase 22.3 limitations are unchanged:

- direct callable effects are not separately retained;
- per-use move provenance is not retained;
- source provenance is currently line-oriented;
- some nested message expressions are not represented as semantic call edges;
- trace slicing belongs to Phase 22.4;
- the Emacs Semantic IDE belongs to Phase 22.5; automated repair/workflow belongs to Phase 22.6.

The frozen post-22.3 evidence remains tied to compiler commit
`ddf927ca490d092b776168c45d330c439d220268`: final passes 28 to 28,
first-validation success 16 to 18, mean attempts-to-green 1.467 to 1.400,
agent tool calls 422 to 367, Moss/Margo calls 156 to 134, ten semantic queries,
eight successful queries, two queries blocked by program diagnostics, and 8/8
resolved queried targets. Fresh agents invoked `resolve` zero times. One
stochastic trial does not establish causality, does not show improved final
completion, and records no source-read telemetry.

## Repository-local Codex autonomy configuration — COMPLETE (2026-09-23)

`.codex/config.toml` now selects a repository-scoped `moss-repository`
permission profile with `approval_policy = "never"`. The profile grants recursive
read/write access to the active workspace roots (including Git metadata and
repository-local Codex files), retains only the minimal external read access
needed by developer tools, denies `/tmp`/`$TMPDIR`, and disables command network
access. `AGENTS.md` now explicitly authorizes unattended in-repository Git,
Python, build, test, and file operations; requires all scratch work under
`./tmp/`; and prohibits `sudo` and `git push`.

Validation: Python `tomllib` parsed the config successfully; installed
`codex-cli 0.156.0` supports permission profiles; the Moss startup bootstrap
reported `moss-0.1`, matching both repository-local Moss skills. No Moss source
or compiler semantics changed. Project-local Codex config applies to trusted
repositories and is expected to take effect for new sessions.

## Canonical swarm operating contract — COMPLETE (2026-09-23)

`examples/swarm/SWARM.md` is now the canonical operating contract for future
dogfood, swarm, fresh-agent, and language-surface experiments. Future
experiment prompts should specify only their unique workload/scope and
reference this file instead of restating process boilerplate. Canonical feedback
and issue database templates remain under `examples/swarm/feedback/` and
`examples/swarm/issue_tracking/`.

## Canonical swarm feedback observation contract — COMPLETE

Future dogfood, swarm, fresh-agent, and language-surface experiments use
`examples/swarm/<experiment>/FEEDBACK.jsonl` as their authoritative structured
observation inventory. The canonical template, schema, practical workflow, and
dependency-free validator are in `examples/swarm/feedback/` and
`tools/check_swarm_feedback.py`. The validator rejects malformed JSONL,
duplicate observation IDs, noncanonical vocabularies/matrix values, invalid
SWARM links, and missing supplied repository evidence paths; its focused
self-test is now part of `tests/run.sh`.

Validated before commit: JSON parsing for schema/template, validator self-test,
repository feedback scan, Python compilation, and `git diff --check`. No Moss
compiler source or semantics changed. This establishes the forward-looking
contract only; historical swarm reports were deliberately not migrated.

## Canonical corrective issue classification database — COMPLETE (2026-09-23)

`examples/swarm/ISSUES.jsonl` now classifies outstanding discoveries above the
raw feedback and detailed SWARM findings layers into False Acceptance,
Ambiguous Spec, Lost Semantics, and Missing Expressiveness. Deliberate v0.1
non-goals—dynamic dispatch/runtime trait objects, escaping closures or general
runtime function values, and recursion—are documented as intentional language
differences, not expressiveness requests. Recoverable error handling is tracked
as deferred to **Phase 21 — Error Propagation & Supervision**. No compiler issue
is marked fixed and no Moss behavior was changed.

## Phase 15.12 — Deep Dogfooding & Torture-Testing Umbrella — COMPLETE (2026-09-23)

Phase 15.12 executed four comprehensive sub-swarms torture-testing Moss v0.1 core semantics, boundaries, lowering, and tooling prior to corrective compiler fixes. Corrective compiler implementation remains explicitly pending; Phase 15.12 is discovery and dogfooding.

### Phase 15.12D — Module, Package, Visibility & Public-API Boundary Torture — COMPLETE (2026-09-23)

Phase 15.12D evaluated module, package, visibility, and compilation-unit boundary semantics across 6 independent workloads under `examples/swarm/modules/`:
1. `algo_chain`: Reusable algorithms (`heapkit` library: min-heap, DSU forest, order helpers) and routing application (`routekit`: Dijkstra, spanning forest, scheduler). `heapkit` passes 8/8 tests cleanly; `routekit` is blocked by SWARM-046.
2. `calc_interpreter`: Multi-module calculator / AST interpreter across 11 internal modules (`ast`, `calc`, `env`, `errors`, `eval`, `lexer`, `parser`, `node`, `report`, `token`, `main`). 32 unit tests passing natively; executable runs cleanly; Fast Debug achieves 100% bit-for-bit parity; formatter checking blocked by SWARM-033/059 on `lexer.moss:46`.
3. `data_pipeline`: Multi-package pipeline (`records` validation provider consumed by `analytics` downstream application via Margo path dependency). 16 unit tests passing natively (9 in provider, 7 downstream); executable runs cleanly; Fast Debug achieves 100% parity.
4. `domain_services`: Three-package architecture (`svc-common` records, `svc-services` synchronous actors, `svc-app` coordinator). 15 unit tests passing natively (3 common, 2 services, 10 app); executable runs cleanly; Fast Debug achieves 100% bit-for-bit parity.
5. `lib_and_app`: 2D geometry library (`geolib`) consumed by scene composition application (`sceneapp`). 14 unit tests passing natively (9 geolib, 5 sceneapp); executable runs cleanly; Fast Debug achieves 100% logical output parity.
6. `text_toolkit`: Reusable text/stats library (`toolkit`) consumed by `app` shop and Fast Debug `probe_debug`. 24 unit tests passing natively (21 toolkit, 3 app); `app` and `probe_debug` run cleanly.

Validation:
- 109 unit tests passed natively across all workloads (8 + 32 + 16 + 15 + 14 + 24 = 109 green).
- 5 project executables ran natively with 100% output correctness.
- Fast Debug executed with bit-for-bit parity across supported subsets.
- 9 new findings (SWARM-056 through SWARM-064) were isolated and recorded in `examples/swarm/FINDINGS.md` with paired single-vs-split reproducers.
- 6 existing findings were independently reproduced and updated with new observations (SWARM-033, SWARM-034, SWARM-037, SWARM-045, SWARM-046, SWARM-053).
- Synthesized coordinator report committed at `examples/swarm/modules/COORDINATOR_REPORT.md`.

### Phase 15.12C — Static Polymorphism Torture — COMPLETE (2026-09-23)

Phase 15.12C executed a 4-subagent dogfood swarm evaluating Moss v0.1's static polymorphism model (untyped statically specialized functions, structural traits, reusable helpers, statically known callable parameters, pipelines, and reuse across modules and packages) across 4 substantive independent workloads under `examples/swarm/polymorphism/`:
1. `sort_search`: Reusable sorting (quicksort, selection sort, insertion sort), partitioning, and binary search over multiple concrete types (`Vector[Int]`, `Vector[String]`, `Player` records, `Item` records). 18 unit tests passing natively and in Fast Debug.
2. `geometry_modules`: Multi-module geometry suite (`geo_types`, `transforms`, `pipeline_ops`, `main`) exercising structural traits (`Shape`, `Bounded`, `Measurable`, `Containment`), polymorphic transforms across `Circle`, `Rectangle`, `Square`, and collection pipelines. 10 unit tests passing natively and in Fast Debug.
3. `collection_pipeline`: Multi-package architecture (`data_tools` library consumed by `log_analytics` application via Margo path dependency) exercising cross-package higher-order functions (`map_by`, `filter_by`, `fold_left`), windowing, batching, and polymorphic accumulation. 16 unit tests passing natively across packages (8 in library, 8 in consumer).
4. `tree_serialization`: Iterative tree traversal (BFS queue, DFS stack), metrics, and structural serializers (indented text, JSON lines) over 3 concrete tree types (`OrgNode`, `DocNode`, `CategoryNode`) via structural `TreeNode` trait without recursion. 7 unit tests passing natively and in Fast Debug; 6,976 execution trace events emitted.

Validation:
- All 51 unit tests passed natively across all workloads (18 + 10 + 8 + 8 + 7 = 51 green).
- All 4 executables ran cleanly with 100% output correctness.
- Fast Debug executed with 100% bit-for-bit parity across workloads without callable parameter locals.
- 13 new findings (SWARM-043 through SWARM-055) were isolated, categorized, and recorded in `examples/swarm/FINDINGS.md` with minimal reproducers, with 5 findings independently reproduced by multiple subagents.

### Phase 15.12B — Control-Flow Torture — COMPLETE (2026-09-23)

Three independent fresh-agent projects now live at
`examples/swarm/control_flow_2026/`: a workflow state machine (6 tests), an
iterative dependency-aware planner (3), and a five-day supply simulation (4).
All final programs pass canonical formatter check, Moss checking, native Margo
build/run/test, Fast Debug, and traced Fast Debug; all 13 native tests pass and
native/interpreter demo output agrees. Their application-first failures,
semantic-query evidence, and final validation details are retained in local
`REPORT.md` files and the coordinator synthesis
`examples/swarm/control_flow_2026/COORDINATOR_REPORT.md`.

Strong positive evidence: `while` loops with mutation, nested branches,
accumulators, helper returns, ordinary early results, and `break` compose
correctly in native and Fast Debug. Identified constraints include intended
ownership consumption for indexed non-copy record vectors, concrete-type
requirements for indexed helper inference, and an undocumented precedence
requirement for a pipeline in a comparison condition (`x < (values |> count)`).
The workflow's nested negative literal return is a formatter-only extension of
existing SWARM-033.

Control-flow finding: an indexed `for i in range(...)` loop is accepted unchanged
by formatter and checker but native lowering loses the induction binding and emits
a unit/result mismatch; Fast Debug separately reports its documented lack of `for`
support. Allocated as **SWARM-066** (see `examples/swarm/FINDINGS.md`). The full
simulation and minimal reproducer are in
`examples/swarm/control_flow_2026/simulation_engine/failed_attempts/01_for_native_reproducer/`.

### Phase 15.12A — Expression Torture — COMPLETE (2026-09-23)

Three independent fresh-agent Moss packages were completed under
`examples/swarm/expression_surface_2026/`: a parser/evaluator (3 native tests),
a numerical analysis tool (5), and a validation/normalization engine (3). Each
agent performed the required skill/bootstrap workflow before source work, kept
application-first failed attempts and minimized reproducers, and only compared
against the ledger after independent completion. The final programs all pass
formatter check, Moss check, native Margo build/run/test, Fast Debug, and traced
Fast Debug; native and interpreter demo output agrees in every final program.

The coordinator report is
`examples/swarm/expression_surface_2026/COORDINATOR_REPORT.md`; individual
reports contain detailed iterations, diagnostics, semantic-query results, and
evidence. Independent overlap was confirmed for SWARM-031/032 (parenthesized
expression native/formatter faults) and a Fast Debug regression/expanded
reproducer for fixed SWARM-025 in a `map`-reached sibling method.

A distinct confirmed parity defect: a checked and Fast-Debug-valid
`Vector[Int] |> filter(_) |> any(_)` function fails native lowering with the
invalid generated return type `_functional_result:has_large_shift`. Allocated as
**SWARM-065** (see `examples/swarm/FINDINGS.md`). The full application and smallest
package reproducer live at
`examples/swarm/expression_surface_2026/numerical_tool/failed_attempts/07_boolean_pipeline_native/`.
All three agents also independently encountered rejected indented
multiline literals/constructors; bootstrap only documents one-line literals, so
this is retained as an ergonomic/diagnostic observation rather than a confirmed
language defect.

## Phase 15.11 — Multi-Domain Swarm Torture-Testing — COMPLETE

Phase 15.11 executed a 5-subagent swarm torture-testing Moss v0.1 multi-domain
architectures, synchronous messaging, 2PL synchronization derivation, ownership
semantics, and compiler boundaries across 5 specialized projects under
`examples/swarm/domain_torture/`:
1. `linear_pipeline`: 4-hop cascading synchronous `message` pipeline (`Ingest -> Validate -> Transform -> Aggregate`), early-reply filtering, payload transformations, and 1,138 JSON execution trace events.
2. `diamond_sync`: Diamond DAG routing (`Coordinator -> [WorkerAlpha, WorkerBeta] -> SharedStore`) with 4 fine-grained synchronization classes, where 10 handler pairs were classified as non-conflicting and therefore need not mutually exclude under concurrent invocation (sequential/interleaved calls tested without claiming runtime thread concurrency).
3. `rich_payloads`: Custom structs and collections (`Vector`, `Map`, `Queue`) across domain boundaries (`Producer -> Registry -> Consumer`), verifying that borrowed synchronous lowering preserves observable by-value message semantics (sender mutation after the call cannot retroactively change the completed receiver computation; `moss cost` confirmed zero materialized bytes for internal synchronous payloads).
4. `dispatcher_fanout`: Star topology hub routing to 4 satellite domains with dynamic priority bypass, adaptive load shedding, interleaved state updates, and mixed value-returning and no-value synchronous message styles (where caller waits for target completion without capturing a value).
5. `domain_boundaries`: 27 negative boundary probes verifying zero-leak frontend enforcement on route cycles, illegal self-sends, same-domain chaining, domain handle escaping, dynamic construction, and impure initializers, plus 2 positive cases (pure-helper initialization and an idiomatic positive control).

Validation:
- All 21 package unit tests passed natively (`margo test`) and in Fast Debug (`moss test --interp`).
- All 4 project `main` programs and the positive control passed natively (`margo run`) and in Fast Debug (`moss run --interp` and `margo debug [--trace]`).
- All 29 boundary probes passed cleanly under the location-independent `run_suite.py` runner with zero compiler crashes and zero leaks to rustc.
- Genuinely new compiler defects were minimized and recorded in `examples/swarm/FINDINGS.md`: SWARM-041 (collection method on struct field during handler effect analysis triggers internal synchronization invariant), SWARM-042 (reassigning initialized mutable local across all branches triggers rustc `-D unused-assignments`), and amended SWARM-033 (`reply -literal` in formatter).

## Phase 15.10 — Fresh-Agent Collection Dogfood II — COMPLETE

Phase 15.10 validated Moss v0.1 compiler, diagnostics, bootstrap tooling,
formatter, Fast Debug, and Margo/module workflows against two independent
fresh-agent dogfood implementations of core data structures from other
languages:
1. Julia-derived `IntDisjointSets` (disjoint-set / union-find with path compression and union-by-rank);
2. Python-derived `ChainMap` (multi-map prioritized lookup, fallback default, front-only mutation, child/parent scoping, and flattening).

Neither experiment required expanding Moss's language surface or altering core semantics.

### Julia Workload (`IntDisjointSets`)
- **Workload Implemented**: `IntDisjointSets` in `examples/swarm/Julia/disjoint_sets/`, based on `JuliaCollections/DataStructures.jl`. Features: 0..n-1 set initialization, iterative two-pass `find_root` (root discovery + path compression), `union` by rank with tree balancing and group count decrement, `in_same_set` connectivity check, `num_groups`, and `num_elements`.
- **First-Attempt Result**: 100% success on first native compile attempt. `moss check --json` reported 0 diagnostics. `margo test` passed all 7 unit tests on the very first compilation.
- **Iterations**: 0 compile/syntax errors. Fast Debug initial run failed with `interpreter error: unsupported or unresolved callable 'find_root'` (SWARM-025, resolved below).
- **Agent Misunderstandings**: Zero language misunderstandings. The agent anticipated Fast Debug's for-loop limitation and wrote `while` loops directly.
- **Discoverability Issues**: Running `moss impact` from the repository root produced `PROJECT_MANIFEST_ERROR: no Moss.toml was found in this directory or any parent` because the manifest search walked upwards from `cwd` rather than resolving from `--source`. Running from within the project directory succeeded immediately.
- **Compiler/Tooling Defects**: SWARM-025 (Fast Debug interpreter omitted unqualified sibling method dispatch on `self`).
- **Unsupported-but-not-added Language Surface**: General recursion (adapted to iterative two-pass traversal, which matched the Julia iterative reference); augmented assignments (`+=`, `-=`).
- **Fixes Made**: `FastInterpreter::eval` in `src/interpreter.hpp` updated to resolve unqualified method calls on `self` in the current frame, matching native compiler lowering behavior (SWARM-025).
- **Final Test Result**: 7/7 tests passed natively (`margo test`). Native execution succeeded (`margo run` -> `true false 2`).
- **Fast Debug Result**: 100% parity with native execution (`true false 2`) under both `moss run --interp` and `margo debug [--trace]`.
- **Agent/Tooling Feedback**: `./moss fmt`, `./moss check --json`, `./moss calls`, and `./margo test` worked seamlessly. The agent felt well-supported by bootstrap JSON and existing swarm documentation, needing zero human steering.

### Python Workload (`ChainMap`)
- **Workload Implemented**: `ChainMap` in `examples/swarm/Python/chain_map/`, based on Python's `collections.ChainMap`. Features: `Vector[Map[String, Int]]` storage, front-to-back prioritized `get` with fallback default, `contains` membership check, front-only `set` mutation, key shadowing, child scope extension (`new_child`), parent scope view (`parents`), and key/value flattening (`flatten`, `keys`, `values`).
- **First-Attempt Result**: Checker passed initially on several constructs that failed during lowering; required iterative adaptation to avoid ownership consumption of vector elements and compound receiver method lowering limitations. Final version passed all 13 tests natively and in Fast Debug.
- **Iterations / Categories**:
  - Iteration 1 (Ownership): `m = maps[i]` consumed vector element (`OWNERSHIP_USE_AFTER_CONSUME`). Adapted by passing directly to helper function with inferred `READ` borrow (Category A: Agent misunderstanding).
  - Iteration 2 (Signature): `fn set_first(var v: ...)` rejected because parameter mutability is inferred, not annotated (Category A: Agent misunderstanding).
  - Iteration 3 (Nested index assignment): `v[0][key] = val` rejected by rustc lowering (Category D: unsupported nested index assignment in v0.1). Adapted to clone-and-replace front map `maps[0] = m0`.
  - Iteration 4 (`key in map` syntax): `if "a" in m:` passed `moss check`, but failed interpreter and rustc lowering (Category B / frontend validation gap). Adapted to `map_contains(m, key)`.
  - Iteration 5 (`String == &String`): Parameter string comparison lowered to `String == &String` in Rust. Adapted with `target = "" + key` (known Phase 10.6F backend limitation).
  - Iteration 6 (Method dispatch on indexed receiver): `maps[0].get("b", 0)` lowered to 1-argument Rust `HashMap::get` because `generated_expr_type` did not analyze indexed expressions. Adapted via helper `map_get(m, key, default)`.
  - Iteration 7 (Self-method inter-calling): Cross-method calls on `self` had argument borrowing mismatches in generated Rust. Adapted via pure helper functions.
- **Agent Misunderstandings**: Moss parameter effect inference (no `var` in parameters); move semantics of non-Copy vector element indexing.
- **Discoverability Issues**: `moss check` silently passed `if "a" in m:` without diagnosing that `in` is not an expression operator in Moss.
- **Compiler/Tooling Defects**: Leaked rustc compilation errors on `String == &String` and compound receiver `vec[0].get(...)`.
- **Unsupported-but-not-added Language Surface**: Python inheritance/protocols; dynamic container typing; nested chained indexing mutation (`v[0][k] = val`).
- **Fixes Made**: The agent cleanly adapted the workload using idiomatic helper functions. No new language surface was added.
- **Final Test Result**: 13/13 tests passed natively (`margo test`). Native execution succeeded (`margo run`).
- **Fast Debug Result**: Executed cleanly with 100% parity under `moss run --interp` and `margo debug [--trace]`.
- **Agent/Tooling Feedback**: `moss check --json`, `moss fmt`, and `margo test` were effective. The agent noted that clearer diagnostics when syntax is accepted by the parser but invalid in expressions would eliminate trial-and-error.

### Corrective Closeout Pass (Findings A–E / SWARM-026–030)

Following the initial fresh-agent dogfood runs, a small corrective closeout pass resolved the consistency defects and frontend boundaries identified by the Python agent:

1. **SWARM-026 (Finding A — Binary `in` Expression Rejection)**:
   - *Fix*: `check_expression` in `src/moss.cpp` now detects and rejects binary `" in "` with `UNSUPPORTED_EXPRESSION_OPERATOR` and actionable guidance to use `Map.get`, key iteration, or an explicit search.
   - *Regression*: `tests/negative/swarm_026_in_expression.moss` (registered via `reject_case`).
2. **SWARM-027 (Finding B — Method Dispatch on Indexed Collection Receivers)**:
   - *Fix*: `generated_expr_type` in `src/moss.cpp` extended to analyze indexed expressions (`parse_index`), preserving inner collection types (`Vector[T] -> T`, `Map[K, V] -> V`). Operations like `maps[0].get(key, default)` and `maps[i].keys()` dispatch through Moss collection semantics in both native lowering and Fast Debug.
   - *Regression*: `tests/swarm_027_indexed_collection_method.moss` (verified in native and Fast Debug).
3. **SWARM-028 (Finding C — Self and Sibling Method Argument Borrowing)**:
   - *Fix*: `gen_object` and `check_objects` now populate `types["self"] = t.name` / `env["self"] = object.name`. Simple call lowering in `expr` resolves sibling methods on `self` and formats arguments using `method_call_argument` matching inferred parameter effects (`READ` / `WRITE`).
   - *Regression*: `tests/swarm_028_self_method_argument_borrow.moss` (verified in native and Fast Debug).
4. **SWARM-029 (Finding D — Nested Indexed Mutation Rejection)**:
   - *Fix*: `check_statement` in `src/moss.cpp` rejects nested indexed assignment (`parse_index` on a base that is itself `parse_index`) with stable diagnostic `UNSUPPORTED_NESTED_INDEX_ASSIGNMENT`, instructing developers to extract the inner collection to a local variable, update it, and write it back.
   - *Regression*: `tests/negative/swarm_029_nested_indexed_mutation.moss` (registered via `reject_case`).
5. **SWARM-030 (Finding E — String Comparison and Handler Condition State)**:
   - *Fix*: In `generated_split_binary`, string comparisons coerce operands to string slices (`.as_str()`), allowing clean comparison between owned `String` and borrowed `&String`. In `src/handler_lowering.inc`, `ConditionState` branch lowering now populates `borrowed_message_parameters_` and `view_parameters_`, resolving string comparisons in branch conditions and permanently eliminating the pre-existing Phase 10.6F failure in `check_phase106f.py`.
   - *Regression*: `tests/swarm_030_string_comparison_borrowed.moss` (verified in native and Fast Debug).

### ChainMap Workload Simplification & Semantic Documentation
- **Simplifications**: Replaced workaround helpers in `examples/swarm/Python/chain_map/src/main.moss` with direct collection methods (`cm.maps[0].get("b", 0)`, `maps[i].keys()`), added `new_child_empty()` and `values()` methods directly to `ChainMap`, and simplified `map_contains` to compare `k == key` without manual string copy workarounds.
- **Semantic Clarity**: Updated `examples/swarm/Python/chain_map/README.md` to document the distinction between Python's reference-aliased live mutable dictionaries and Moss's deterministic value-snapshot semantics (`new_child` and `parents` operate on value snapshots, ensuring complete scope isolation).
- **Project Verification**: `margo test` (13/13 passing), `margo run`, and `margo debug [--trace]` all succeed with 100% agreement between native execution and Fast Debug.
- **Julia DisjointSets Verification**: `margo test` (7/7 passing), `margo run`, and `margo debug [--trace]` all succeed with 100% agreement.

### Comparison Against Earlier Swarm Dogfood Experiments

> Are fresh agents now learning and using Moss more successfully than during the earlier collection experiments?

**Yes, emphatically.** The evidence is clear across both workloads:

1. **Compilation Iterations and Time-to-Green**:
   - In earlier swarm experiments (e.g., Phase 15.0 Julia BinaryHeap and Python Counter), fresh agents repeatedly hit syntax and inference blockers: SWARM-001 (parenthesized pipelines in arithmetic), SWARM-002 (methods named `pop`), SWARM-003 (negative integer literals in assertEqual), SWARM-006 (empty map contextual typing), SWARM-007 (string-key insertion), and SWARM-008 (missing-key defaults).
   - In Phase 15.10, the Julia DisjointSets agent achieved **100% first-attempt compilation and test pass (0 compile errors, 7/7 tests green)** without human intervention or steering.
   - The Python ChainMap agent successfully constructed and tested a complex nested data structure (`Vector[Map[String, Int]]`) with 13 comprehensive unit tests, achieving full native and Fast Debug execution.
2. **Tooling and Discovery Adoption**:
   - Both fresh agents executed the mandatory bootstrap sequence, parsed the bootstrap JSON, followed discovery routing to `docs/` and `.agents/skills/`, and used `margo test`, `margo build`, `margo run`, `moss fmt`, `moss check --json`, and semantic queries (`inspect`, `effects`, `calls`, `impact`).
   - Neither agent reverse-engineered generated Rust to discover language idioms.
   - Neither agent required coordinator coaching or workarounds.
3. **Execution Parity**:
   - Python ChainMap executed cleanly with identical results under native compilation and Fast Debug.
   - All 5 corrective findings (SWARM-026 through SWARM-030) and the Fast Debug sibling dispatch fix (SWARM-025) have dedicated regressions in `tests/run.sh` and the interpreter differential test suite, giving both Julia and Python 100% native/Fast Debug execution parity.

## Phase 15.9 — Cross-Package Static Specialization Convergence — COMPLETE

Phase 15.9 closes the native artifact-boundary specialization gap. The root
cause was projection, not checking: `Checker` created canonical static
specializations correctly, but explicit-module lowering emitted every one into
a final-root `moss-specializations` crate. A non-root provider rlib could then
emit a call to `__moss_specialize_*` before that symbol existed in its own
artifact context.

The canonical specialization identity remains compiler-owned. Native lowering
now deterministically projects it into each module artifact whose checked body
contains the concrete call. Provider concrete wrappers therefore carry the
specialization they execute in their own rlib. A consumer can still instantiate
an imported generic: `.mossi` supplies its checked generic IR and private helper
closure, while the consumer's calling artifact owns that projection and links
the provider's ordinary rlib for concrete implementation. Native debug symbols
are artifact-scoped, avoiding duplicate link symbols when the same semantic
specialization is required in independent artifacts. No source folding, dynamic
dispatch, Rust trait object, runtime dictionary, or Margo semantic behavior was
introduced.

`.mossi` now distinguishes statically dispatched exports and retains their
semantic IR even when their open parameters are trait-constrained. Exported
trait method contracts are preserved as interface metadata, so source-free
structural specialization stays checked and static. This requires native ABI
version 6, so pre-15.9 provider interfaces fail closed and must be rebuilt.
The focused regression in `tests/tooling/check_phase15_9_cross_package_specialization.py` is registered
in `tests/run.sh`; it covers same-module and cross-module calls, the historical
provider concrete-wrapper reproduction, repeated use, structural trait
specialization, Margo package build/run/test, and a consumer built after the
provider source is hidden with only `.mossi` + `.rlib` artifacts. It also checks
that provider and consumer generated Rust have the intended artifact ownership
and no runtime-dispatch machinery.

Build Planner now uses `model.identity_task` across the `graphlib` package
boundary; the former `identity_task_typed` workaround was removed. `graphlib`
(12/12) and `planner` (7/7) build, test, and run through Margo with the natural
generic helper. Strict C++17 `-Werror`, Phase 15.9 and existing module-provider,
Margo, Fast Debug, agent, examples, shell, and whitespace checks pass. A full
`tests/run.sh` rerun passes through the new Phase 15.9 coverage and all reached
compiler/tooling/Emacs suites; after the repair it again reaches only the known
unrelated Phase 10.6F `&String == String` native Rust backend failure. Phase
15.9 intentionally does not add language syntax or runtime generic dispatch.

Post-closeout corrective hardening records concrete static-call dependencies
while checking each specialization and closes those dependencies recursively
during artifact projection. Thus `outer<Int> -> inner<Int>` is emitted in the
same provider or consumer artifact for both source-backed and source-free
`.mossi` + `.rlib` use. Private helper IR is now limited to the actual
module-local transitive closure from exported static functions; unrelated
private helpers stay out of `.mossi`. The focused regression verifies all of
these facts with a source-free fixture containing only `Provider.mossi` and
`libProvider.rlib`.

## Phase 15.8 — Cross-Package Fast Debug Source Convergence — COMPLETE

Margo now resolves the package graph and supplies the exact transitive dependency
source-root universe through the internal `MOSS_FAST_DEBUG_SOURCE_ROOTS` handoff for
`margo debug` and `margo debug --trace`. Moss does not parse Margo dependency selectors,
lockfiles, or Git sources: it indexes those supplied package roots, resolves Moss modules,
checks the reachable source closure, rejects any remaining compiled-only Moss provider as
`FAST_DEBUG_NATIVE_DEPENDENCY`, then runs interpreter-only optimization and Fast Debug.
Invalid supplied roots fail closed; cross-package source module-provider collisions report
deterministic `MODULE_IMPORT_AMBIGUOUS` errors. Same-package multi-file modules remain
valid. `margo debug` accepts only its bare and `--trace` forms, clears inherited source-root
state, and never invokes `rustc` for a fully source-backed graph.

The Phase 15.8 regression is registered in `tests/run.sh`. It verifies source-backed path,
transitive, and Git package closures; cached locked Git resolution; real source-free `.mossi`
selection and rejection; native/interpreter fixture and Build Planner output parity; no-rustc
debug; package-aware trace events; distinct same-filename provenance; invalid-root and
duplicate-provider failures; CLI rejection; and standalone `moss debug` preservation. Build
Planner runs through interpreted `graphlib` source with trace events in both packages.

Completed validation: `make`; `check_phase15_8_cross_package_fast_debug.py` (9/9);
`check_agent_skills.py`; `check_agent_api.py`; existing Fast Debug, Margo,
module-provider, multifile-project, and project-workflow regressions; `make examples`;
`sh -n tests/run.sh`; and `git diff --check`. A full `tests/run.sh` rerun passed all of
the Phase 15.8 coverage, agent/project/tooling checks, 20/20 Emacs tests, and existing
Fast Debug checks, then reached the known unrelated Phase 10.6F native Rust backend
failure (`&String == String`). No Phase 15.8 test was weakened.

Post-closeout hardening aligned Fast Debug source-provider selection with native
module semantics: unused duplicate dependency modules are legal, root source retains
precedence, and ambiguity is reported only when a reachable import requires multiple
external source providers. The Git fixture explicitly initializes and selects `main`.
Focused Phase 15.8, native provider-ambiguity, Margo, Fast Debug, Build Planner,
agent, examples, shell, and whitespace validation passed. The full suite again reached
only the unchanged Phase 10.6F `&String == String` backend failure.

## Phase 15.7 — Multi-Module Formatter Semantic Convergence — COMPLETE

Closed Phase 15.7 multi-module formatter semantic convergence:
- Authoritative Project/Module Semantic Convergence:
  - Eliminated isolated semantic checking for project files in the formatter (`run_project_format`): the formatter no longer uses a standalone merged-namespace approximation. `merge_project_program` is retained inside `check_project_sources()` where it belongs for all consumers. `analyze_project_texts()` is retained for `moss edit`, which requires full codegen.
  - Factored authoritative project analysis so semantic validation is cleanly separated from optimizer and backend code generation: `check_project_sources()` performs parsing (with in-memory `source_overrides`), module/package resolution, compiled provider (`.mossi`) loading, export validation, namespace rewriting, declaration filtering, and `Checker` execution. Formatter validation stops cleanly after `Checker`, ensuring a valid Moss project never fails `moss fmt` due to an optimization or backend code-generation defect. Normal compilation flows continue through `FunctionalOptimizer` and `Generator` via `analyze_project_sources()`.
  - Project files use authoritative project/module resolution across `Application`, `Tests`, and `Benchmarks` target generation modes.
  - Selected-file formatting (`moss fmt path/to/file.moss`) resolves the containing project context and manifest via `analyze_source_context(file)`, validating the file against its full module dependencies and qualified types (e.g. `model.Graph`), while scoping writes strictly to the selected file without mutating sibling files.
  - Genuine standalone files outside any project context continue to use standalone parse/check validation (`validate_formatted_source`); isolated checking is preserved for truly standalone files.
- Validation-Transactional Formatting:
  - Formatting is validation-transactional: all proposed source is formatted in memory and validated before any writes occur; validation failures cause zero writes, leaving every target unchanged on disk.
  - Sibling files in a selected-file formatting run remain byte-for-byte untouched when validation fails or succeeds.
  - Multi-file filesystem writes in a whole-project format are not claimed to be filesystem-rollback-atomic (the OS does not guarantee atomic multi-file commit); the transactional invariant is that no write occurs if validation fails.
- Directory and CLI ergonomics:
  - Added support for passing project directories directly to `moss fmt <dir>` and `moss fmt <dir> --check`.
- Regression Coverage:
  - Hardened `tests/tooling/check_phase15_7_formatter_convergence.py` covering:
    1. Selected-file formatting and check with imported qualified types (`model.Item`).
    2. Verification that sibling modules are not touched when formatting a selected file.
    3. Whole-project formatting, idempotency, and `--check`.
    4. Whole-project and isolated selected-file transactional failure guarantees (verifying byte-for-byte zero disk writes and isolated sibling validity). The `pkg_selected_transactional` fixture is fully isolated: the sibling file is semantically valid on its own; only the selected file contains the type error, so failure cannot be attributed to the sibling.
    5. Proof that formatter semantic validation executes `Checker` stages (`effects`, `synchronization_plan`) while demonstrably bypassing `FunctionalOptimizer` and `Generator` (`rust_generation`), via `MOSS_PROFILE_COMPILER=1` opt-in developer stage timing (emits to stderr only when the env var is set; no JSON or semantic change).
    6. Clean resolution and compilation checks.
  - Registered the regression in `tests/run.sh`.
- Completed Validation:
  - `./moss fmt projects/build_planner/graphlib/src/graph.moss --check` (exits 0).
  - `./moss fmt projects/build_planner/graphlib --check` (exits 0).
  - Regressions: `check_phase15_7_formatter_convergence.py`, `check_formatter_indexing.py`, `check_swarm_003_formatter.py`.
  - Margo build/test/run for `graphlib` (12/12 tests) and `planner` (7/7 tests, deterministic run output).
  - `check_agent_skills.py`, `check_agent_api.py`, `make examples`, `sh -n tests/run.sh`, and `git diff --check`.
  - Full `tests/run.sh` suite: all compiler, formatter, agent, synchronization, Fast Debug, module/project, and 20/20 Emacs ERT checks passed. Reached only the known environment-specific LLDB/DAP real-integration handshake failure; it was not changed.
## Phase 22.1 — Agent-Teaching Diagnostics (complete)

Phase 22.1 started from the completed Phase 22.2 closeout commit
`3668ce450ff3c5aedee3cb32d34cafe72b6d4b46`. The frozen pre-22.1 baseline under
`benchmarks/agent/baselines/pre-22.1/` remains unchanged. Commits `9caaee6`,
`75690b8`, and `bb388d4` implement and converge the evidence-backed diagnostics,
documentation, and regressions. Domain/message rejection sites now distinguish
self-message, same-instance handler chaining, direct cross-domain handler entry,
missing/broken routes, and invalid message targets. Functional diagnostics
distinguish placeholder, callable-invocation, and captured-mutation rules.
`OWNERSHIP_CONFLICTING_ACCESS`, `QUERY_TARGET_NOT_FOUND`, and
`TYPE_INFERENCE_FAILED` remain stable where they are the correct semantic classes,
with richer compiler facts and sound guidance.

The additive `moss-agent-1` teaching fields are `source`, `rule`,
`cause.entities`, `related`, and `guidance`; concise human output is rendered from
the same facts. Focused tests cover diagnostic codes, rule/cause/entity identities,
access modes, related locations, legal rewrites, and negative misleading-guidance
cases. No Moss syntax, accepted/rejected program set, ownership, domain, pipeline,
lowering, synchronization, module, or Fast Debug semantics changed.

The fixed-protocol post run is retained under
`benchmarks/agent/baselines/post-22.1/` with raw artifacts, deterministic aggregate,
machine-readable comparison, and report. It used the same AB001-AB030 corpus,
prompt wrapper, Codex CLI 0.156.0, `gpt-6-sol`, medium reasoning, isolation,
network restrictions, 900-second budget, and validators; there are no known
model/runtime/protocol confounders. Compiler/orchestration commit `bb388d4` is the
intended treatment.

Pre/post outcomes were 28→28 final passes, 17→16 first-validation successes,
30→30 eventually green, 1.433→1.467 mean attempts to green, 407→422 agent tool
calls, and 157→156 Moss/Margo invocations. Failed semantic-query calls fell 4→2,
`QUERY_TARGET_NOT_FOUND` fell 2→0, and repeated-diagnostic-loop tasks fell 2→1
because AB015 used qualified targets immediately. AB022's intentional cross-command
ownership exploration remained. The single stochastic trial therefore establishes
diagnostic specificity and one narrower recovery improvement, not a broad headline
or tool-efficiency gain. AB014 and AB017 again failed only exact output formatting
after successful compilation; all repair tasks passed and all 30 sessions reached
green.

Closeout validation passes strict C++17 `-Werror`, Phase 22.1 teaching and negative
regressions, agent API and skill drift, all 30 benchmark references/metadata,
deterministic pre aggregate and post comparison checks, ownership/domain/functional,
Margo/module, Fast Debug, `make examples`, Python/shell syntax, and whitespace.
The full `make check` run passed every reached suite and stopped at the already
documented unrelated Phase 10.6F native Rust error comparing `&String == String`
in generated `phase106f/workload.rs`; it was neither repaired nor weakened.

## Phase 22.2 — Agent Benchmark Suite

Phase 22.2A's 30-task corpus is complete, hardened, and frozen; Phase 22.2B's
runner and stable result schema are complete; Phase 22.2C's canonical
pre-Phase-22.1 fresh-agent baseline is complete; and Phase 22.2D's deterministic
aggregate analysis and evidence report are complete. Phase 22.2 as a whole is
complete. The next phase is Phase 22.1 — Agent-Teaching Diagnostics, driven by
the diagnostic and workflow evidence in the Phase 22.2D report.

The canonical baseline is stored under `benchmarks/agent/baselines/pre-22.1/`.
It uses protocol `phase-22.2c-v1`, corpus commit
`b187009f6ef06738c52216d34490342d76d7ab4c`, compiler commit
`22fdb024e4f284d17fb9abfe887856e17127aae7`, orchestration commit
`532dc4610d156946df5014573e5d69947f4d3546`, Codex CLI 0.156.0,
`gpt-6-sol`, medium reasoning, and a 900-second per-task limit. Each AB001–AB030
result came from one independent ephemeral session with no resume or retry.
Starter-only trees and normal Moss tools/docs/skills were mounted over the source
repository; benchmark references and usable Git history were absent, external web
and shell network access were disabled, and final validation ran outside the
agent-visible namespace.

The raw execution result is 28 passes and 2 failures (`AB014`, `AB017`). Seventeen
first meaningful validation commands passed. All 30 sessions eventually reached a
successful Moss/Margo correctness command; attempts-to-green have mean 1.433 and
median 1. The run recorded 407 agent tool calls and 157 Moss/Margo invocations,
with no timeout, infrastructure failure, environment mutation, or outside-allowed-
path change. Both final failures printed two requested values on separate lines
where the frozen tasks require one space-separated line. Detailed ordered tool
records, diagnostic codes, prompts, manifests, authoritative results, and final
relevant files are retained per task.

Two objective corpus corrections are documented in
`benchmarks/agent/CORPUS_REVISIONS.md`; neither changed an agent-visible prompt,
starter, expected reference, or compiler. Revision 1 made AB024 module assertions
filename-independent. Revision 2 allowed AB020's explicitly requested pure stage
to be either `map` or `filter`; its superseded session is retained under
`pre-22.1/noncanonical/` and excluded from metrics. An earlier whole provisional
series is also excluded because sanitized HOME hid rustup's compiler configuration;
a compact invalidation record is retained under
`benchmarks/agent/baselines/invalidated-pre-22.1-rustup/`. The corrected protocol
pre-resolves native `rustc`, and its sanity check proves Margo build/test behavior
inside the sanitized environment.

Completed validation: strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build;
metadata validation and all 30 reference solutions; all 30 retained canonical
workspaces against the authoritative validator; benchmark, anti-shortcut, baseline
isolation, agent API, and skill drift regressions; Margo path/Git DAG, module-
provider, multi-file, separate-compilation, and Fast Debug project tests; Python
and shell syntax checks; `make examples`; and Git whitespace checks. `make check`
passed the benchmark and all subsequent compiler, Margo, module, Fast Debug,
tooling, and editor checks until the already documented unrelated Phase 10.6F
native `&String == String` failure. No compiler, language, lowering, interpreter,
or diagnostic behavior changed.

Phase 22.2D closeout additionally passed deterministic aggregate/report checking,
the focused reconciliation and failure-separation regression (including prompt-hash
drift, missing/duplicate runs, infrastructure failures, and no-green statistics),
the agent benchmark/baseline/API/skill checks, Python and shell syntax checks,
`make examples`, and Git whitespace checks. The canonical AB001–AB030 raw artifacts
remain unchanged from the Phase 22.2C commit; no baseline task was rerun.

Benchmark tasks must not be changed merely because future diagnostics or agent
tooling improve. Any objectively necessary corpus correction must retain stable
IDs and be documented as a corpus revision before comparison.

## Phase 15.6 — Multi-Package Build Planner Dogfood

Completed Phase 15.6 multi-package dogfood experiment in `projects/build_planner/`:
- `graphlib`: Validated and strengthened with 12 unit tests covering empty graph, single/duplicate/independent tasks, linear/diamond dependency chains, disconnected components, missing dependency endpoints, total/category costs, multiple ready tasks, topological constraints, and cycle detection. Documented `empty_slots() = 32` representation. All 12 tests pass under Margo.
- `planner`: Implemented multi-module source architecture across `policy.moss`, `service.moss`, and `main.moss`, consuming `graphlib` through Margo path dependency:
  - Consumes exported concrete types (`model.Task`, `model.Graph`, `model.PlanSummary`) and typed functions.
  - Implements two synchronous domains (`PlannerDomain` -> `ReporterDomain`) with static routing in `main` composition prefix, nested messaging, and imported `model.PlanSummary` payload.
  - Implements eager functional pipelines in `service.compute_cost_metrics` utilizing `filter`, `sum`, `count`, `map`, `all`, and `any`.
  - Exercises structural traits via compile-time duck typing (`policy.Scorable` satisfied by both `service.TaskItem` and `policy.Milestone`).
  - Added 7 comprehensive planner tests in `tests/test_planner.moss`. All 7 pass under Margo.
- Compiler & backend defect repairs in `src/moss.cpp`:
  1. Multi-module test projection: `module_program()` projected tests into library rlibs when `main_module.empty()`. Fixed by accepting `root_module`.
  2. Member call rewriting: In `rewrite_module_program`, calls with `!statement.b.empty()` (e.g. `costs.push(c)`) improperly prefixed receiver variable names (`service__costs`). Fixed by checking `statement.b.empty()` and correctly rewriting imported module calls.
  3. Builtin and pipeline preservation: Prevented qualification of functional stages (`filter`, `map`, `sum`, etc.) and builtins (`Vector`, `Queue`, `Map`, `Some`, `sqrt`) with module names.
  4. Double module prefix guard: Guarded token rewriting with `token.find("__") == string::npos`.
  5. Return statement type check: Added `check_expression` to `Stmt::Kind::Return` in `walk_type_environment`.
  6. Borrowed parameter return value: Emitted `.clone()` / `.__moss_value()` for borrowed object parameters in return expressions.
  7. Foreign object view access: Guarded `gen_object_access` in `Generator::generate` to skip foreign objects whose defining module is not in `rust_dependencies_`.
- Defect classifications and limitations:
  1. Cross-package / cross-module generic structural dispatch was limited in the original Phase 15.6 dogfood; Phase 15.9 now projects specializations into their calling provider/consumer artifacts, so the concrete typed-helper workaround is no longer needed.
  2. `moss fmt` reports `FORMAT_PARSE_ERROR` on qualified parameter types in multi-module files because `validate_formatted_source` type-checks files in isolation.
  3. Fast Debug cannot mix interpreted Moss with compiled Moss module dependencies (`FAST_DEBUG_NATIVE_DEPENDENCY`), requiring reachable source.
- Completed validation: `make`; `margo build`, `margo run`, and `margo test` in both `graphlib` and `planner`; `tests/tooling/check_agent_skills.py ./moss`; `tests/tooling/check_agent_api.py ./moss`; `make examples`; `sh -n tests/run.sh`; `git diff --check`. Full test suite passes to known sandbox LLDB/DAP handshake skip.
- Post-closeout cleanup (Phase 15.6 remains COMPLETE): Normalized incidental spaced indexing syntax (`Vector [Int]` -> `Vector[Int]`, `vec [i]` -> `vec[i]`) across `graphlib` (`algorithms.moss`, `graph.moss`, `model.moss`, `test_graph.moss`). Fixed root cause in `moss fmt` (`canonicalize_code_spacing` in `src/moss.cpp`) which previously defaulted to injecting space before `[` after identifiers. Added regression `tests/tooling/check_formatter_indexing.py` locking down canonical `Vector[Int]()`, `xs[i]`, and `xs[i] = x`. Full re-validation passed.

## Dogfood compiler/interpreter repair batch

Completed in the current working tree: SWARM-011 now also hoists proven-safe
Copy sibling reads for indexed writes; empty `Queue()` uses concrete field/state
context; invalid built-in `Queue(...)`/`Map(...)` arguments are rejected before
lowering; `Vector[T]()` is pure to observable-effect analysis; and native
Vector/Queue `pop()` preserves Moss's `T` contract with fail-closed empty-pop
behavior. Fast Debug now supports the current collection operations through
object fields, concrete Map state (including nested object state), and eager
`map`/`filter`/`reduce`/`sum`/`count`/`any`/`all` pipelines. It remains limited
for `for` traversal.

`projects/cache` now writes directly through `evict_idx`; `projects/rolling_window`
uses `Queue[Int]` for alerts while preserving the separately committed clear/reuse
cleanup. Both projects pass Margo test/build/run and `moss debug`.

Completed validation: `make`; focused check/native/Fast Debug regressions;
`tests/run.sh` through all new regressions and the existing suite until the known
unrelated Phase 10.6F `&String == String` native failure; bootstrap/capabilities/
schema; agent API/skill checks; `make examples`; and shell/whitespace checks.
`make check` repeated the new regression pass and reached the existing
environment-dependent LLDB/DAP handshake failure; it was not changed.

## Discovery-only follow-up — fresh-agent cache experiment

Discovery work in commit `95f5552` extends live `moss agent bootstrap --json` with
canonical-document routing, collection operation metadata, a compact
`project_surface`, test/domain-topology facts, and the composition-initializer
rule. The skills and Gentle Introduction route unfamiliar Moss-writing work
through bootstrap and the practical guide before a minimal probe; arbitrary
example search is explicitly deferred. Drift checks cover the new structured
facts and compiler probes for collection operations, test composition, and
initializer behavior.

Important live-result nuance: direct state construction and a pure ordinary
helper initializer both check successfully. The actual restriction is
side-effect-free initialization; messages, domain access, I/O, failing,
divergent, and unresolved work are rejected. No compiler, interpreter, cache,
SWARM, or example/project implementation changed.

Completed validation: `make`; bootstrap/capabilities/schema JSON; both agent
drift checks; focused compiler probes; `make examples`; `sh -n tests/run.sh`;
and `git diff --check`. `make check` passed the new agent checks and all reached
compiler/tooling/Emacs suites, then stopped at the existing environment-specific
real LLDB/DAP initial-handshake failure; it was not changed.

Routing/status cleanup validation repeated both agent checks, `make`, `make
examples`, and the shell/whitespace checks; `make check` again reached only that
same unchanged LLDB/DAP environment failure.

## Phase 15.4 — Julia → Moss Swarm: BinaryHeap

The bounded pure-Moss `Int` min-heap experiment is complete under
`examples/swarm/Julia/BinaryHeap/`. It translates Julia's hole-moving heap
algorithm with zero-based indexes and passes four project tests plus native build/run
(`1 2 5 7`). SWARM-001 through SWARM-003 are now fixed, so the checked-in source
uses `Vector |> count`, `pop`, and ordinary negative literals; its README preserves
the original observations as historical records.

The independent CPython `heapq` experiment under
`examples/swarm/Python/BinaryHeap/` is also complete. Its zero-based `sift_down` /
two-stage `sift_up` port passes four project tests plus native build/run
(`1 1 2 5 7`). SWARM-004 and SWARM-005 are now also fixed, so the checked-in
Python port uses direct indexed comparisons and helper returns. No frozen Julia
source changed.
The canonical Phase 15 swarm findings ledger is
`examples/swarm/FINDINGS.md`. Its BinaryHeap baseline recorded five findings,
three independently reproduced across the Julia and Python ports.

The isolated Agent A Python `Counter` Map experiment now passes its core project
tests after the checked-in `Map.get`, `Map.keys`, and `Map.values` work. The
README and ledger preserve its original bounded-failure observations. SWARM-006 and
SWARM-007 remain fixed; SWARM-009 remains an agent misunderstanding. The Julia
`Accumulator` experiment remains separate.

The Julia `Accumulator` experiment under `examples/swarm/Julia/Accumulator/`
historically reproduced missing-key/default lookup and Map-value traversal gaps.
Focused current Map regressions verify those APIs and close SWARM-008/SWARM-010;
the optional checked-in merge body still needs a separate source-level follow-up
before that project is again treated as a complete project validation.

Phase 15.4 follow-up repairs now verify the checked-in `Map.get`, `Map.keys`,
and `Map.values` work and reconcile SWARM-008/SWARM-010 as fixed. Current local
work also fixes sibling-field native E0502 lowering, `not` Bool inference, and
frontend enforcement of explicit `let` immutability; SWARM-011 through
SWARM-013 track those verified repairs pending the repository's next allowed
commit. Agent discovery now advertises the compact source surface and verified
Fast Debug limits. Strict C++17 `-Werror`, focused compiler/native Rust,
agent API/skill, Map, and frozen BinaryHeap project validation, plus `make examples`,
pass. `make check` reaches only the existing sandbox LLDB/DAP real-integration
handshake failure after the compiler, tooling, and Emacs suites pass; it was not
weakened. The optional Julia Accumulator merge source remains a separate
source-level follow-up, while the direct Map API regressions pass.

The current HashMap dogfooding follow-up adds `Int % Int`, Rust backend symbol
hygiene for Moss names such as `HashMap`/`VecDeque`/`Arc`, and the empty typed
`Vector[T]()` constructor; unsupported local annotations now report
`LOCAL_TYPE_ANNOTATION_UNSUPPORTED`. The HashMap collision/tombstone tests pass
(13 project tests), and SWARM-014, SWARM-015, and SWARM-017 are recorded as fixed.
The reported owned-field replacement receiver consume was reduced to a passing
control: the RHS is consumed while the receiver remains available, so no
SWARM-016 was allocated. Strict C++17 `-Werror`, focused native/FAST Debug,
agent API/skill, HashMap project, and `make examples` validation pass. `make check`
reaches the sandbox LLDB/DAP handshake failure. A direct `tests/run.sh` pass that
skips that environment gate reaches a separate pre-existing Phase 10.6F native
failure (`&String == String`); the same failure reproduces with the base HEAD
compiler and is unrelated to this HashMap repair.

## Phase 15.3 — Path-Sensitive Synchronization Placement

Closed at the Phase 15.3 implementation plus closeout commit: eligible leading
top-level conditionals use static typed continuation splitting, branch-local
deferral, rank-forced untouched cancellation, and touched full hold. Conditions
with a message, external observable effect, or unresolved observable effect fall
back to conservative full-entry acquisition. Strict C++17 `-Werror`, focused
placement/2PL/borrowed-payload/Fast-Debug/Margo checks, and `make examples` pass.
`make check` reaches only the existing sandbox LLDB/DAP initial-handshake failure
after compiler, synchronization, package/module, Fast Debug, tooling, and Emacs
checks pass; that test was not weakened. The representative branch fixture
generates 23,138 bytes of Rust.

## Phase 15.2 — Margo package/project driver (closed)

Margo is implemented by `44b5c99` and executable closure `53cf425`. The package-to-
module closure additionally proves a real source-free chain: Git `Math` emits its
`.mossi`/rlib, path `Geometry` imports it through Margo's resolved module environment,
and root `App` imports `Geometry` and runs to `42`. One reused deterministic package
environment now supplies dependency artifact roots to build/run/test/bench; the root
test and benchmark both call `Geometry`, and deliberately fail without that environment.
The offline locked-cache rebuild repeats the actual import chain after the original Git
remote is removed; the lockfile remains byte-identical. Generated consumer crates are
checked to reference provider crates rather than fold provider source. `moss` remains
authoritative for modules, `.mossi`, checking, and lowering; Margo owns only manifests,
resolution, cache, ordering, and orchestration. Registry, publishing, updating, and
workspace management remain deliberately deferred. The normal suite reaches only the
existing sandbox LLDB/DAP handshake limitation.

Closure validation: strict C++17 `-O2 -Wall -Wextra -pedantic -Werror`, the focused
hermetic Margo regression, and `make examples` pass. The full `make check` run passes
the compiler, package/module, Fast Debug, tooling, and Emacs checks before the same
environment-dependent real LLDB/DAP initial-handshake failure; it was not weakened.

Phase 15.2 resolver hardening additionally requires every requested external Moss
module identity to select exactly one canonical `.mossi` provider. Overlapping paths
to the same artifact deduplicate; unrelated duplicate exports remain legal until an
import requests that module; direct and transitive duplicate providers now produce a
deterministic `MODULE_IMPORT_AMBIGUOUS` diagnostic listing canonical interface paths.
The selected interface is carried to native lowering so its `.rlib` is paired from the
same provider directory. No package-qualified import syntax or Margo-side import
resolution was introduced.

## Phase 15 — agent onboarding hardening

The fresh-agent chain is now `AGENTS.md` → repository-local Moss skills → live
`moss agent bootstrap --json`. `AGENTS.md` tells an agent how to build/use `./moss`,
then routes package/project work to Margo and semantic/module/compiler work to Moss.
The skills and versioned bootstrap expose `Moss.toml`, `Moss.lock`, path/Git packages,
canonical `margo build|run|test|bench|clean`, and retained Moss semantic tools. Skill
contracts are checked against live bootstrap/capability/schema facts, including Margo,
semantic edits, impact/affected tests, synchronization introspection, Fast Debug, and
trace support. This is discoverability/drift protection only; no Phase 22 work or
language/package semantic change was started. Validation: strict C++17 `-Werror`,
agent Skill/bootstrap/schema regressions, focused Margo regression, and `make examples`
pass. `make check` reaches its existing sandbox LLDB/DAP initial-handshake limitation
only after the compiler, package/module, Fast Debug, tooling, and Emacs checks pass;
that integration test was not weakened.

## Phase 15.1 — borrowed synchronous message payload lowering (closed)

The main implementation is committed as `13602ce` (`Phase 15.1: add borrowed
lowering for synchronous message payloads`). This closure patch repairs `moss cost`
parsing for the current `materializes N bytes` diagnostic and adds a JSON regression.
Internal synchronous `_shared` handler contracts pass non-Copy
ordinary payloads as `&T` and object payloads as `&impl MossAccess_T`.
`MessagePayloadLowering` is the backend representation decision: `CopyValue`,
`BorrowOwned`, `BorrowView`, or `MaterializeOwned`. The checked Moss rule is
unchanged: messages are immutable value snapshots, payload WRITE/CONSUME remains
illegal, forwarding remains legal, and replies remain owned value boundaries.

Borrowed state projections reuse the existing static `MossAccess_*` / `*View`
representation. Handler2PL retains the sender guards throughout nested messages,
so a callee can read the same stable leaves without `__moss_value()` reconstruction
or a deep clone. Exported/native message bridges intentionally retain an owned
payload and lend it only to their internal shared body. Large-payload warnings are
now emitted only for a planned owned materialization boundary.

Focused checks passed with the current compiler build: strict C++17 `-O0` and
`-O2 -Wall -Wextra -pedantic -Werror`; `check_borrowed_reads.py`; new
`check_phase151_borrowed_payloads.py`; Phase 10.6F's broad compiled/interpreted
and source-free module regression; generated Rust `-D warnings`; and shell/Git
whitespace checks. The first full `make check` exposed a generated comparison of
an incoming borrowed `String` against an owned literal; lowering now dereferences
borrowed non-object payloads in value comparisons/operators, and the affected F
regression passes. The final suite reached the optional real LLDB/DAP test after
all compiler, Phase 15.1, module/project, agent, source-map, and Emacs checks
passed, but the sandboxed adapter exited during its initial handshake
(`Connection shut down by remote side while waiting for reply to initial handshake
packet`). This is the existing environment-dependent process-tracing integration,
not a Phase 15.1 assertion; it was not weakened. `make examples` passed.

The benchmark harness compared detached baseline `7661ffc` with the Phase 15.1
working tree, three repetitions at one thread and 200 iterations. Moss-backend
1 MiB internal messages changed from approximately 44.34 us to 0.364 us for
String and 408.0 us to 0.402 us for Vector, consistent with removing deep
materialization. These are small-machine evidence, not a correctness gate; raw
data is disposable under `tmp/phase151/`.

## Repository-owned Moss agent onboarding skills

Two small current-v0.1 agent skills now live at the repository-local Codex
discovery path `.agents/skills/`: `moss-language` and `moss-agent-workflow`.
Their compact hand-maintained bodies are the canonical copies; `AGENTS.md` only
routes Moss-source agents to them. `moss-language` teaches static construction,
`domainroutes`, synchronous `message`, terminating `reply`, immutable payloads,
domain-handle closure, structural traits, inferred READ/WRITE/CONSUME, supported
functional callables, modules, and common rejected patterns. It explicitly marks
`await` and `spawn` retired. `moss-agent-workflow` teaches the existing
`moss-agent-1` bootstrap/JSON-query/format/impact/affected-test/Fast-Debug-trace
loop and keeps generated Rust as an implementation artifact.

`tests/tooling/check_agent_skills.py` verifies both `SKILL.md` metadata and their
machine-readable current-language markers, rejects stale runtime terminology in the
language skill, checks that its advertised listing exactly matches a checked fixture,
then checks, lowers, compiles generated Rust with `-D warnings`, and executes that
example. The check runs at the start of `tests/run.sh`; the existing retired-syntax
gate remains unchanged. `docs/AGENT_SKILLS.md` documents the discovery convention
and explicitly reserves broader measured onboarding/repair/trace work for Phase 22.

Validation after these documentation/tooling-only changes: strict C++17
`-std=c++17 -O2 -Wall -Wextra -pedantic -Werror` rebuild, focused skill check,
`make check`, `make examples`, `sh -n tests/run.sh`, and staged/unstaged
`git diff --check` passed. No Moss semantic, backend, or Phase 10 implementation
changed. This is a low-risk dogfooding aid; Phase 22 is not complete.

## Moss v0.1 peer-review hygiene cleanup

Active examples, projects, benchmark sources, and semantic regressions now use
current static construction, declared routes, synchronous messages, and replies.
Retired source syntax remains only at `tests/negative/await_retired.moss:8` and
`tests/negative/spawn_retired.moss:7`, one migration token each. Historical docs
remain explicitly labeled; the old cycle error example was replaced with a
concrete three-instance route-cycle example.

Twelve redundant rejection-only fixtures were removed. Four useful old cases
were rewritten as current message target/handler/arity negatives and a positive
no-value call. Self-send, same-domain chaining, route-cycle and specialization
fixtures now have descriptive current names. Seven currently valid payload-copy
and reply fixtures were moved out of `negative/`; two obsolete reply-error
examples became one positive reply-by-value example. Current topology, closure,
ownership, functional, synchronization, 2PL, module, and Fast Debug coverage is
preserved. `tests/README.md` documents the current test groups.

The new source-hygiene gate runs before the suite, rejects unallowlisted tokens
including comments/nested examples, verifies both migration fixtures, and has
self-tests. The cleanup exposed a real error-path bug: unknown expression-valued
message handlers were dereferenced during signature inference. A one-line null
guard restores the existing missing-handler diagnostic; no valid-program semantics
or production backend code changed.

Validation: strict C++17 `-O2 -Wall -Wextra -pedantic -Werror`, the initial full
suite, all examples with Rust warnings denied, all four project examples in
debug/release, module/ledger Fast Debug output equivalence, test/benchmark demo,
intentional failures, source audit/self-tests and Emacs byte compilation passed.
The final full-suite rerun after descriptive fixture renames passed, including
all current A/B/B.1/C/D/D.1/E/F/F.1, ownership/functional, native/2PL,
Fast Debug, module/project, agent, source-map/objdump and 20 Emacs ERT checks.
Live LLDB/DAP tests retain their capability skip because process tracing is
unavailable. `sh -n tests/run.sh` and staged/unstaged Git whitespace checks pass.
The final audit reports exactly the two source occurrences listed above and no
active example or benchmark occurrences. No validation remains pending.
Logs are under repository `tmp/` and disposable. This is hygiene only:
no Phase 15 work, automatic release re-closure, or release/tag publication.

## Phase 10.6F.1 checked in as `f1dd43e` — awaiting review and explicit v0.1 re-closure

The provisional Phase 10 closeout (`a907336`, status `94fca2c`) was reopened for
F.1 and the static typed-lowering implementation is now checked in as `f1dd43e`.
Phase 10.6F measured release-blocking fixed overhead from runtime synchronization
metadata interpretation. F.1 now lowers the unchanged SynchronizationPlan into
typed class-owned state and direct statically emitted acquisitions. No semantic,
partition, lock-order, Fast Debug, or source-language changes are authorized.
Phase 15 must wait for F.1 review and explicit v0.1 re-closure.

Implementation and validation complete: production now emits typed class-owned RwLocks,
direct acquisitions, in-place WRITE, and borrowed state projections. Generic
runtime plans/maps/evacuation are removed. Source-free providers expose semantic
typed borrowed handler bodies; the final application generates all physical
layouts, including for a provider built without any concrete instances. Native
ABI is bumped to 5. The 10.6C analysis and Fast Debug execution are unchanged.

Final strict C++17 `-O2 -Wall -Wextra -pedantic -Werror`, `make`, complete
`make check`, and `make examples` passed. Generated Rust uses warnings denied.
This includes all A/B/B.1/C/D/D.1/E/F tests and new F.1 structure, direct typed
layout/acquisition, no-allocation assembly, independent-instance and provider
built-without-instances checks. D's concurrency harness repeats five times;
all conflict/exclusion, nested/sibling ordering, reply release, WRITE-through,
panic/poison, no-clone/original-storage/helper/method/functional and module tests
pass. Fast Debug equivalence, deterministic plans/traces, ledger, agent/tooling,
all 20 Emacs ERT tests and warning-as-error editor byte compilation pass. Live
LLDB/DAP is capability-skipped because process tracing is unavailable.
`sh -n tests/run.sh` and Git whitespace checks pass. Subsequent edits only update
documentation, test comments/report wording and the final success-message order;
they do not invalidate implementation validation. Earlier development failures
are superseded by this final full-suite run.

The durable [F.1 report](../docs/PERFORMANCE_10_6F_1.md) contains methodology,
old/new results, example generated layout/body, native ABI impact, assembly
findings, limitations, and the full validation record. Isolated eleven-sample
tight-loop medians in ns/call (typed / handwritten Rust) are: empty 0.710/0.571,
SHARED 11.782/11.652, EXCLUSIVE 11.647/11.613, two EXCLUSIVE 20.947/20.301,
mixed two-class 21.639/21.171, three-class 30.120/29.382. The multi-fold metadata
overhead is removed; the sub-nanosecond empty result describes a near-empty
benchmark loop, not service request latency.

The unchanged original threaded F harness was rerun with both compilers, seven
repetitions each. Two disjoint callers measured 10.33 Mops/sec with typed Moss
versus 3.13 with a coarse RwLock in this workload. Instrumentation still observes
four shared readers, two disjoint writers and one conflicting writer; nested
ancestor hold amplification remains intentional. No universal speedup is claimed.
At the largest existing stress size (96 leaves/192 handlers per Store), generated
Rust shrank from 7,062,780 to 728,572 bytes; optimized rustc compile/link median
fell from 37,354 to 536 ms, C++ generation from 30.01 to 16.78 ms. Stored-plan
derivation stayed about 76/75 ms. Repeated output is deterministic. Measurements
are not correctness timing gates and raw logs are disposable.

No unsafe, new language semantics, early unlock, atomics, lock elision, layout
padding, scheduler, or alternative backend was introduced. No F.1 implementation
work remains; review and explicit v0.1 re-closure are pending. Phase 15 and Phase
20 have not begun, and no release/tag has been published. The validation below
applies only to the historical F implementation.

## Historical provisional Phase 10 closeout — superseded by F.1 blocker

Phase 10.6F implementation, benchmarks, tests, and release documentation are
checked in as **`a907336`**, based on `202d9c7` (the 10.6E documentation correction).
Phases 10.6A, 10.6B, 10.6B.1, 10.6C, 10.6D, 10.6D.1, 10.6E, and 10.6F are
complete. **Next: Phase 15 — Dogfooding.** No GitHub release or tag was created.

The [v0.1 milestone](../docs/V0_1.md) records the settled structural-trait,
effect, static-topology, synchronous-message, 2PL, borrowed-READ, and value-boundary
model. The [performance report](../docs/PERFORMANCE_10_6F.md) preserves methodology,
environment, robust measurements, fixes/deferred issues, and the final terminology
audit. These documents are durable; temporary raw logs are not status authority.

Stored-plan introspection now reports leaf/class counts and compression, exact
per-handler shared/exclusive acquisition counts, read-only/self-conflicting
handlers, pair opportunities, a deterministic conflict matrix with class/leaf
witnesses, and class-sharing/splitting explanations. No effects or synchronization
partition are independently rediscovered. Optional `moss_perf` physical events
measure actual contention, wait/hold time, handler/nested time, and current call
depth using thread-local collection. Normal production instrumentation is off.
`MOSS_PROFILE_COMPILER` provides optional stage timings outside semantic JSON.

Measurement preceded optimization. Borrowing private handler route/self handles
removes redundant Arc clones without changing locks or message/reply values.
A separate concrete backend fix publishes rustc crate-name rlib aliases so
transitive local/source-free module dependencies link correctly. Native `.mossi`
ABI remains 4; compiler version is 0.1.0 and backend cache identity is refreshed.
No unsafe, layout padding, new language semantics, synchronization algorithm,
lock elision, early unlock, atomics, scheduler, or concurrency-ingress syntax was
introduced.

Measured conclusions on the recorded i3-1315U/Linux/Rust environment:

- Shared readers reached four simultaneous holders; disjoint writers reached two;
  conflicting writers remained at one. Shared-reader throughput rose from 3.51 to
  7.98 million operations/sec at one/eight callers. Disjoint has only two writable
  classes: throughput rose from 2.99 to 4.11 million operations/sec at one/two
  callers; more callers contend within those classes.
- Empty/shared/two-class wrappers measured 81/167/330 ns median. Typed Rust shared
  reads measured about 36 ns. Small-handler metadata/view overhead remains real;
  no blanket near-Rust or coarse-lock speedup claim is made.
- Ordinary String/Vector/whole-object READ remained effectively size-independent
  from 16 to 1,048,576 bytes/elements. Large explicit copies scale with payload
  size as required. Existing non-Clone/clone-counter tests remain authoritative.
- Nested child execution accounts for roughly three quarters of parent lock hold
  time in the instrumented sample; maximum nesting is three. Instrumented durations
  include observer overhead and are separate from uninstrumented throughput.
- Runtime-shaped locks occupy 40 bytes/alignment 8. Packed and 128-byte-separated
  probes both measured about 9.1 ns/op median, providing no padding justification.
- At 96 leaves/192 handlers per Store, 24 instances/12 routes/1,152 total classes,
  plan derivation was about 75 ms of 380 ms total C++ compilation. Generated Rust
  reached 7 MB; code-size/static-projection work remains a real future cost.
- Deterministic tracing grew linearly to 60,017 events / 14.6 MB for 4,002 messages
  (about 498 ms in the recorded sample). Trace slicing is useful future tooling;
  Fast Debug is not turned into an optimizing runtime or concurrency simulator.

New seed programs include a four-module ledger with types, structural predicate,
concrete helpers/factories, domains/routes, independent payload forwarding, and
state replies; an index summary; and a production functional pipeline. Existing
trait examples exercise constrained structural specialization. The clean ledger
builds/runs/debugs with equivalent results, and its model works source-free in
production. Cross-module generic/structural dispatch, qualified statement calls,
provider-field construction, multiple-main ergonomics, Fast Debug `for`/pipeline
coverage, and trace volume are explicitly recorded for Phase 15 rather than
silently treated as finished language surface.

Final validation passed on the final implementation:

- Strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build and `make`.
- Complete `make check`: retained 10.6A/B/B.1/C/D/D.1/E suites and new F diagnostics,
  deterministic output, benchmark/instrumentation structural checks, expanded
  compiled/interpreted equivalence, clean modules and source-free providers.
- Repeated 2PL concurrency, nested/sibling ordering, reply release, primitive WRITE,
  fail-closed panic/poison, non-Clone/zero-copy READ, helper/method/functional and
  module regressions. Each D concurrency harness runs five repetitions.
- `make examples`, generated Rust with warnings denied, functional/dataflow,
  project, agent/introspection, debug-map/objdump, and tooling checks.
- All 20 Emacs ERT tests and warnings-as-errors byte compilation.
- `sh -n tests/run.sh`, working-tree/staged/HEAD Git whitespace checks, and the
  repository-wide terminology/artifact audit. Live LLDB/DAP tests were
  capability-skipped because process tracing is unavailable.

Benchmark timings are never correctness gates. No generated measurement logs,
binaries, or compiled editor files are checked in; the Rust benchmark files are
hand-written source. These results supersede development runs and failures before
the final fixes. Only this checked-in-state documentation follows the implementation
commit; no code changes invalidate the recorded validation.

Phase 10.6F completes Phase 10 and establishes Moss v0.1. The synchronous-domain
architecture has now been implemented, consolidated, instrumented, and quantitatively
validated from compiler-derived synchronization planning through production
handler-level 2PL and borrowed protected reads. Fast Debug executes the same checked
domain semantics deterministically, and the repository now contains coherent v0.1
documentation, diagnostics, benchmarks, and seed programs for real-world dogfooding.
Further language refinement will be driven by Phase 15 usage rather than speculative
extension.

## Historical Phase 10.6E closeout — checked in as `7b335d1`

Phase 10.6E is checked in as `7b335d1`, atop `e70943c`. Phase 10.6D is checked
in as `9f49e64` and 10.6D.1 as `b3f8a64`. All three phases are implemented,
validated, and committed.

Documentation-only closeout correction: README now explicitly names the single
SynchronizationPlan-driven Handler2PL backend across `-O0`, `-O`, and
`-Oshared-memory`, removes stale serialized-handler/atomic-handler wording, and
describes directly interpreted synchronous domains/messages/replies. The requested
README terminology audit and `git diff --check` pass. No compiler, runtime, or
test files changed; the implementation validation recorded below is unchanged.

Production now has one domain implementation: direct synchronous calls through
plan-driven handler entries, using the unchanged safe-Rust class runtime and
borrowed views. Legacy mailbox/worker/tracker/completion paths, coarse locks,
atomic-domain lowering, batching, coalescing, clustering, selectors/options,
and await IR/analysis/serialized metadata are removed. No unsafe, synchronization
optimization, new scheduler, source syntax, or changed 10.6C algorithm was added.
The only C traversal cleanup removes unreachable retired-Await cases.

Fast Debug owns logical instances keyed by checked concrete identities, exact
specializations, state, and immutable routes. Messages execute nested frames;
reply terminates immediately. Independent payload/reply values and helper/method
WRITE-through preserve checked semantics. Deterministic domain/state traces carry
source, semantic, instance, specialization, handler, and access-path identities,
with bounded summaries and no physical synchronization or scheduling simulation.
Transitive source-module interpretation works; source-free Moss still requires
production execution. Existing interpreter `for`/functional-pipeline limits remain
explicit. No Phase 11 foreign ABI or Phase 12 supervision was introduced.

Native `.mossi` ABI is **4**; old artifacts require rebuilding. Impact snapshots
are **v2** and retain checked concrete route edges instead of await edges. Physical
synchronization/borrow metadata remains outside provider ABI.

Final validation passed after the last source change (the internal reply-slot
rename), using a strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build:

- `make`, complete `make check`, and `make examples`; generated Rust uses
  `-D warnings`. The intentional negative use-after-transfer example is excluded.
- 10.6A/B/B.1/C semantics, closure, exact specialization, and plan regressions.
- 10.6D class layout/acquisition, nested/sibling ordering, primitive WRITE-through,
  reply release, fail-closed panic/poison, source-free domains, and whole-object
  restoration. The concurrency harness repeats five times: compatible readers
  and disjoint writers overlap, conflicting writers serialize, and readers exclude
  writers. These tests also passed in the preceding full-suite development run.
- D.1 non-Clone runtime values, zero clone counters, borrow lifetime rejection,
  original String/Vector storage, whole/nested-object READs, mixed/immutable state,
  helper/method/functional captures, value boundaries, and source-free providers.
- New 10.6E compiled/interpreted equivalence, exact static routes and specializations,
  nested/sibling order, primitive/nested/helper/method state mutation, independent
  payload/reply values, terminating reply, deterministic traces, source-module
  closure, source-free interpreter rejection, and absent legacy implementation.
- Functional/dataflow, module/native ABI, project, agent/introspection, Fast Debug,
  debug-map/objdump, tooling, and all **20 Emacs ERT tests**.
- Emacs byte compilation with warnings as errors, `sh -n tests/run.sh`,
  `git diff --check`, and staged/HEAD whitespace checks.
- Optional live LLDB/DAP tests were capability-skipped because process tracing
  is unavailable. Existing objdump missing standard-library source warnings do
  not prevent mapping checks from passing.

These results supersede earlier development failures and runs invalidated by later
edits. Temporary logs live under repository `tmp/` and are not durable evidence.
The [retirement audit](../docs/LEGACY_RUNTIME_AUDIT.md) records the changed-file
inventory, deleted fixtures, removed types/backends, and repository search
classification. Remaining retired terms are migration/absence tests, explicitly
historical records, removal/future-work descriptions, or valid unrelated names,
ordinary collection/runtime utilities, and test threads. No stale active backend
reference remains in the audited implementation.

Phase 10.6E completes the migration to the synchronous-domain architecture.
Production execution now has a single compiler-derived handler-level 2PL backend
with borrowed protected reads, while retired mailbox/worker/await/commit-order and
alternate synchronization backends have been removed. Fast Debug now executes the
same checked synchronous domain semantics directly and deterministically, including
static composition, routes, messages, handler state, nested calls, and terminating
replies, without simulating physical locks or scheduling. Phase 10.6F remains for
diagnostics, layout, and quantitative performance validation.

## Historical checked-in Phase 10.6D.1 closeout — borrowed protected READs

The 10.6D baseline is checked in as `9f49e64`. Phase 10.6D.1, checked in as
`b3f8a64`, replaces
handler-entry READ snapshots with frame-bounded borrows and generated typed object
views. `MossHandlerFrame::read` returns a reference backed by retained class guards,
or directly by immutable runtime storage. `V: Clone` and physical leaf-enum Clone
derivations are removed. Storage remains entirely safe Rust, with no unsafe,
Arc/COW value substitution, or whole-state shared mutable references.

The private handler state ABI now contains recursive typed views with borrowed
READ slots and owned EXCLUSIVE slots. Unobserved slots contain only absent
metadata, never fabricated/default user values. Exclusive evacuation/restoration
is retained; normalization, plans, exact ClassSets, modes, ranks, parent guard
retention, failure behavior, and primitive WRITE-through are unchanged.

Static Rust accessor traits and reusable generic helper/method implementations
support whole/nested-object READs without reconstructing owned objects. There is
no dynamic trait dispatch or new Moss syntax. Helpers, methods, indexed reads, and
functional captures borrow existing values. Explicit message/reply boundaries
materialize independent owned values; Rust references into caller state do not
cross messages. Owned replies remain valid after frame release and later mutation.

Source-free providers use existing semantic type/effect records. A pre-existing
off-by-one read of `public_representation field` metadata is fixed; no 10.6C
algorithm changes were needed. Generated decomposition entry points move private
provider fields into physical storage without exposing new Moss field access.
Native ABI version 3 requires rebuilding older providers for the new internal
entry points; no view layout, lifetimes, or synchronization metadata enters `.mossi`.
Debug maps retain one source/semantic identity and the original native symbol,
with exact line mappings for both owned and borrowed implementations.

Final validation passed after the last compiler and regression changes:

- Strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build, followed by `make`.
- Complete `make check`: 10.6A/B/B.1/C/D and new D.1 regressions; modules and
  source-free `.mossi` providers; functional/dataflow, agent/introspection,
  project workflows, existing Fast Debug tests, and tooling.
- `make examples`, including generated Rust with warnings denied. The existing
  intentional negative use-after-transfer example remains excluded.
- The full-suite and standalone 10.6D runtime/codegen runs both passed. Each
  runs five concurrency harness repetitions, with 20 disjoint-writer,
  shared-reader, and split-object overlaps and 800 conflicting increments per
  repetition. Reader/writer exclusion, reply release, exact class layouts,
  nested/sibling ordering, whole-object restoration, primitive WRITE-through,
  panic/poison failure, and dispatch-option convergence remain passing.
- New D.1 checks compile non-Clone runtime values, require zero clones across
  entry/repeated reads/helpers/methods, and reject a borrow escaping its frame.
  Instrumented generated wrappers/helpers/methods observe the original backing
  addresses of 100,000-element String/Vector state. Whole/nested objects,
  mixed READ/WRITE, immutable state, generics, indexed reads/writes, and captured
  map/filter/reduce work without entry snapshots. Message payload and reply
  independence, deterministic Rust, and stable debug identities are verified.
- Source and source-free provider tests pass for aggregate READ helpers,
  methods, protected domain state, and consumer-local views. No physical view
  or synchronization metadata is serialized into `.mossi`.
- All 19 Emacs ERT tests, warnings-as-errors Emacs byte compilation, debug-map
  and objdump checks, `sh -n tests/run.sh`, and Git whitespace checks pass.
  Optional live LLDB/DAP checks were capability-skipped because process tracing
  is unavailable; objdump's missing Rust standard-library source warning does
  not prevent its mapping checks from passing.

These final results supersede development runs invalidated by later edits.
Logs are disposable under repository `tmp/`; no validation remains pending.
Remaining representation costs are static accessor code size, absent-slot and
descriptor metadata, and conservative explicit-boundary copies. No new 10.6E
blocker was discovered. Fast Debug domain execution and legacy deletion remain
10.6E work; neither is completed by this change.

Changed files: `src/moss.cpp`, `src/handler_runtime.hpp`,
`src/handler_lowering.inc`, new `src/borrowed_views.inc`,
`tests/phase106d1_borrowed_reads.moss`,
`tests/phase106d1_read_projection.moss`,
`tests/tooling/check_borrowed_reads.py`, `tests/tooling/check_handler_2pl.py`,
`tests/run.sh`, `docs/SYNCHRONIZATION_PLAN.md`, `docs/MODULE_ABI.md`,
`docs/SEMANTIC_CONVERGENCE.md`, and this status file.

Phase 10.6D.1 removes the remaining hidden READ snapshots from production domain
execution. Protected READ state is now accessed through borrowed views tied to
the retained synchronization-class guards, while immutable-after-publication
state is borrowed directly. Explicit Moss value boundaries remain by value, and
the Phase 10.6D handler-level 2PL, rank ordering, deadlock proof, and
conflict-serializability guarantees are unchanged.

## Historical checked-in Phase 10.6D closeout — production handler-level 2PL

Commit `9f49e64` implements production handler-level 2PL from the authoritative
`Program::synchronization_plan`. Each constructed domain owns typed leaf storage
and one Rust `RwLock` per planned class. The synchronized `_shared` wrapper
acquires exactly ClassSet in increasing class rank, selects read/write guards
from SHARED/EXCLUSIVE modes, retains them across the full body and nested messages,
restores exclusive leaves, then releases before returning the reply. Empty
ClassSets and immutable-after-publication storage acquire no class lock.

At this historical checkpoint storage was entirely safe Rust, without unsafe,
raw pointers, or runtime type erasure. Exclusive leaves moved into a private
working value under retained guards, while READ leaves used physical snapshots.
Phase 10.6D.1 supersedes those READ snapshots with borrowed views. Exclusive
restoration before unlock and fatal panic/poison/missing-reply behavior remain.

All active compilation modes, former mailbox/reference options, cluster options,
nominal specialization adapters, and exported bridges converge on synchronized
entry. Old transport/coarse/atomic/coalescing/cluster implementations remain
dormant in source for 10.6E. Ordinary primitive WRITE parameters now use writable
caller storage, including generic/method calls and projections. Parameter effects
and the 10.6C synchronization-analysis algorithms are unchanged.

Final graph/specialization/rank/partition/mode validation rejects corrupt checked
state before emission. Source-free providers receive the final application's
descriptor at construction; no physical synchronization metadata becomes `.mossi`
ABI. Native ABI version 2 requires rebuilding older providers.

Final validation passed after the last compiler changes:

- Forced C++17 `-O2 -Wall -Wextra -pedantic -Werror` build, then `make`.
- Complete `make check`: 10.6A/B/B.1/C regressions, new corrupt-plan and 10.6D
  runtime/codegen checks, module/source-free `.mossi`, functional/dataflow,
  agent/introspection, project workflows, existing Fast Debug checks, and tooling.
- `make examples`, with generated Rust warnings denied; the intentional negative
  use-after-transfer example remains excluded by the existing target.
- Standalone 10.6D suite and the full-suite invocation both passed. Each executes
  five harness repetitions, each with 20 disjoint-writer, shared-reader, and
  split-object overlaps plus 800 conflicting increments. Barriers prove overlap;
  these are correctness checks, not timing benchmarks.
- Reader/writer exclusion, zero immutable-handler acquisitions, exact class
  sharing/splitting, increasing local ranks, parent retention, nested/sibling
  ordering, blocked-caller reply release, whole-object restoration, specialization
  layouts, primitive/generic/method/projection WRITE, source-free providers,
  deterministic generation, legacy-option convergence, panic, and poison checks.
- All 19 Emacs ERT tests; warnings-as-errors Emacs byte compilation; debug-map
  and objdump checks; `sh -n tests/run.sh`; working-tree, staged, and HEAD diff
  whitespace checks. Optional live LLDB/DAP checks were capability-skipped because
  process tracing is unavailable. Objdump's missing Rust standard-library source
  warning does not prevent the source-map checks from passing.

Earlier development runs are superseded by these final results. One added
corruption-test input initially assigned the already-present mode; correcting it
required no compiler or analysis change. The complete suite passed afterward.
Logs are disposable under repository `tmp/`; no validation remains pending.

For failure-free execution, domain-local protected state is conflict-serializable
under compiler-derived handler-level 2PL. Every newly acquired lock ranks above
every currently held lock under `(domain_rank, class_rank)`, so a wait cycle would
require the impossible `L1 < L2 < ... < Ln < L1`. Completed descendants leave the
held stack, allowing later lower-ranked siblings above their ancestor. This is
not a global multi-domain transaction, total outbound-effect order, rollback,
supervision, fairness, or starvation-freedom guarantee. Parent lock retention can
amplify nested-call latency; early unlock, elision, atomics, and clustering remain
future work. See `docs/SYNCHRONIZATION_PLAN.md` for the full architecture.

Changed files: `src/moss.cpp`, `src/handler_lowering.inc`,
`src/handler_runtime.hpp`, `src/synchronization_lowering.hpp`, `Makefile`,
`tests/run.sh`, `tests/phase106d_handler_2pl.moss`, `tests/phase106d_nested.moss`,
`tests/phase106d_parameter_write.moss`, `tests/phase106d_specializations.moss`,
`tests/tooling/check_handler_2pl.py`,
`tests/tooling/check_synchronization_lowering.cpp`,
`tests/tooling/check_synchronization_plan.py`, `docs/SYNCHRONIZATION_PLAN.md`,
`docs/SEMANTIC_CONVERGENCE.md`, `docs/MODULE_ABI.md`, `docs/AGENT_API.md`, and
this status file. No 10.6C analysis algorithms or language design changed.

Phase 10.6D makes the compiler-derived SynchronizationPlan authoritative for
production domain execution. Every compiled handler now acquires exactly its
planned shared/exclusive synchronization classes in deterministic rank order,
retains them for the full handler lifetime including nested synchronous messages,
and releases them only after normal handler completion. Nested acquisitions obey
the global `(domain_rank, class_rank)` order, establishing the failure-free
deadlock-freedom and conflict-serializability guarantees of the synchronous-domain
architecture. Legacy transport/runtime machinery remains for Phase 10.6E cleanup.

## Current checkpoint — Phase 10 / Moss v0.1 complete

Checked-in 10.6C compiler closeout: `eb4d49a`, following snapshot `486eba4`.
The 10.6D closeout is checked in as `9f49e64`, atop `3d7ccd1`.

| Phase | Implementation state |
| --- | --- |
| 10.6A | Complete: synchronous, expression-valued `message` and terminating `reply`; source `await` is retired. |
| 10.6B | Complete: static composition, `domainroutes`, a closed concrete DAG, and deterministic `domain_rank`; source `spawn` is retired. |
| 10.6B.1 | Complete: closed domain-handle universe and exact concrete `DomainSpecialization` linkage. |
| 10.6C | Complete: compiler-owned `SynchronizationPlan`, including leaf effects, classes, ranks, handler sets, conflicts, and introspection. |
| 10.6D | Complete at `9f49e64`: physical class storage, production handler-level 2PL, and primitive parameter WRITE-through. |
| 10.6D.1 | Complete at `b3f8a64`, atop `9f49e64`: borrowed protected/immutable READs, recursive object views, no runtime Clone requirement, and independent message/reply values. |
| 10.6E | Complete and validated at `7b335d1`: legacy runtime retired; deterministic Fast Debug domains supported. |
| 10.6F | Complete at `a907336`: diagnostics, instrumentation, quantitative validation, measured backend fixes, and v0.1 release closure. |
| Next | Phase 15 Dogfooding; then Phase 20 Rust Interoperability, Phase 21 Error Propagation & Supervision, and Phase 22 Agent Agency Tooling. |

Current language semantics are synchronous domain calls over the closed concrete
graph. Legacy mailbox/worker adapters and alternate synchronization implementations
have been removed; production execution uses planned synchronization classes.
Neither asynchronous mailbox behavior nor total domain serialization defines
the current Moss language model. Historical checkpoint
descriptions below are implementation history, not permission to restore retired
syntax or change language semantics.

## Historical Phase 10.6C closeout and validation

The `eb4d49a` closeout completes the synchronization-plan implementation resumed
from `486eba4`. The compiler derives leaf READ/WRITE/CONSUME
observations, X*, ProtectedRead, leaf LockSets, equal-signature synchronization
classes, local class ranks, handler ClassSets, and conflict witnesses from the
closed graph's exact specializations. JSON and human dumps consume the stored
plan. That checkpoint emitted no production class-lock storage, acquisitions,
or handler-level 2PL; the current 10.6D implementation supplies them.

This closeout preserves inferred primitive-parameter WRITE effects instead of
downgrading them based on backend Copy representation. Compiled helper interfaces
carry formal leaf effects, alongside handler-state records; missing records fail
closed with a provider-rebuild diagnostic. Tests cover source/source-free effects,
exact specialized object leaves, captures, class splitting, and primitive WRITE.

At the checked-in 10.6C closeout, final validation passed after rebuilding with
C++17 `-O2 -Wall -Wextra -pedantic -Werror`: full `make check`, `make examples` with generated Rust
warnings denied, focused Python/C++ synchronization tests, all 19 Emacs ERT
tests, warnings-as-errors Emacs byte compilation, shell syntax, and diff checks.
Account generated Rust is byte-identical to B.1 in both `-O0` and
`-Oshared-memory`. Optional live LLDB/DAP tests were capability-skipped because
process tracing is unavailable; debug-map and objdump tests passed.
No validation remains pending for this closeout. Session logs live under
repository `tmp/` and are disposable; the outcome is recorded here durably.

Known pre-existing backend limitation, reproduced using commit `d451d8a`:
an exported concrete function in a module calling that same module's exported
generic can reference a missing `__moss_specialize_*` symbol in its Rust crate.
Minimal shape: exported generic `bump(value): return value`, called from exported
concrete `append(values: Vector[Int])`. This is not introduced by synchronization
planning. The source-free leaf-effect fixture uses concrete helpers to test its
intended contract; provider-native generic specialization linkage needs separate
backend work. Ordinary/generic semantic-effect tests remain enabled.

## Primitive WRITE backend correctness item — resolved in 10.6D

Moss semantic analysis permits caller-visible WRITE through ordinary primitive
function parameters when inferred by the existing parameter-effect analysis.
For example, the existing synchronization regression has `change(value: Int)`
assign `value = value + 1` and calls it with domain state `counter`; the inferred
handler write set includes `counter`. These are ordinary function parameters,
not incoming message payloads, which remain immutable snapshots.

The historical by-value Rust mismatch is resolved by generated mutable-reference
calling conventions selected from existing parameter effects. Executable 10.6D
regressions verify ordinary primitive mutation and mutation of protected domain
state through helpers, as well as methods, generics, projections, and source-free
providers. WRITE has not been weakened to READ. No parameter modifier or semantic
effect change is introduced.

## Historical documentation preflight validation (before 10.6D)

This preflight changes only `.codex/CURRENT_STATUS.md`. The 10.6C analysis and
language design remain unchanged. Documentation consistency review checked the
phase boundary against the checked-in compiler, synchronization regressions,
and `docs/SYNCHRONIZATION_PLAN.md`; historical runtime descriptions are labeled
below. `git diff --check`, `git diff --cached --check`, and `git diff HEAD --check`
passed. Status consistency checks confirmed the completed phase entries,
historical labels, deferred backend obligations, referenced files/commits, and
that only this status file differs from HEAD. No dedicated documentation lint
target is configured in the repository.

Compiler/runtime tests were not rerun for this documentation-only preflight.
The recorded 10.6C code-validation results remain applicable to the unchanged
implementation; they do not establish physical 2PL or primitive write-through
correctness. No preflight validation remains pending. Implementation obligations
remain listed under Immediate next tasks.

Follow-up status correction: ordinary READ/WRITE/CONSUME parameter effects are
inferred; no `var`, `mut`, `write`, or `inout` modifier is planned for parameter
mutation. Old await/serialized-domain assumptions are historical and superseded
by Phase 10.6A. The historical Phase 10.5 open-design blocker is superseded by
the resolved decisions in 10.6A–C. Only this status file changed; compiler code
and tests were untouched. `git diff --check` passed for this correction.

## Historical version and commit chronology

- Historical compiler version at this checkpoint: Moss v0.2 (superseded by the v0.1 milestone above).
- Static duck-typed methods and named-trait specialization are complete on top of
  `df73771` (`Complete concrete method semantics`). This checkpoint closes the initial
  static method/trait foundation without adding runtime dispatch.
- The showcase/documentation checkpoint is built on `eecac49` and demonstrates the
  stabilized static duck-typing, named-trait, collection, pipeline, and domain syntax.
  It adds no ownership, borrow, move, optimizer, or runtime-dispatch semantics.
- Phase 2 ownership/effect safety is present in the current history (`c5396c6` and
  `58772f3`). Phase 2.5 is frozen and complete on top of `d76449f`. It adds
  backend-only batching, explicit domain-lowering plans, direct lock regions,
  RwLock specialization, and whole-domain atomics without changing Moss source
  semantics. The freeze checkpoint closes the final `-O0`/atomic equivalence gap
  by defining and explicitly lowering wrapping `i64` arithmetic.
- The historical post-Phase-2.5 await-DAG correctness checkpoint made local type environments
  branch-aware, rejected divergent concrete domain references at joins, validated
  bounded await targets in all executable code, and reported per-edge source sites in
  cycle witnesses. Source `await` and passable domain references were subsequently
  retired by 10.6A/B.1; the current concrete route DAG is recorded above.
- Phase 4 functional/dataflow compilation is implemented at core checkpoint `829b1a9`.
  Pipelines now have typed, provenance-carrying compiler IR; observable callable effects
  are inferred separately from ownership; `-O0` preserves eager stage semantics; and
  `-O` fuses proven-safe chains into explicit loops without a Rust iterator runtime.
- The current Phase 4 hardening work carries exact pipeline plan IDs from checked AST
  occurrences into Rust lowering, includes initializer effects in `Reduce`, rejects
  non-Copy placeholder projections at Moss level, preserves real result-expression
  lines with source-derived semantic identities, and propagates capture mutation through
  ordinary helper calls.
- Phase 4.5 extends the authoritative Phase 4 IR with explicit materialization plans,
  exact-count/dead-map simplification, effect-safe `any`/`all` short-circuit plans,
  single-use immutable cross-binding fusion, and shared-source terminal DAGs. It adds no
  source syntax and keeps `-O0` as the complete eager reference traversal.
- Phase 4.6 adds a bounded Moss-to-Moss semantic rewrite sequence over the same IR.
  Optimized plans explicitly compose adjacent maps/filters, perform identity-proven
  predicate pushdown, eliminate safe trailing maps before `count`, propagate terminal
  facts, preserve single-use virtual bindings, and fold inert literal cardinality. It
  adds no syntax, lazy runtime, MLIR, hardware optimizer, or domain/backend policy.
- Phase 5 tooling is frozen at closeout checkpoint `92d0953` without changing the
  frozen semantic phases. The
  compiler emits a deterministic shared `.mossmap`; debug generation retains stable
  native symbols; `editors/emacs/moss-mode.el` provides editing, compilation, map-based
  navigation, `dape`/`lldb-dap` launch support, and objdump integration; optional native
  tools remain capability-gated.
- Phase 6A is frozen. It exposes those already-computed facts through the deterministic
  `moss-agent-1` JSON protocol. Bootstrap/schema/capability discovery, structured checks,
  and `inspect`, `type`, `effects`, `ownership`, `calls`, `awaits`, and `why` queries are
  read-only views over the ordinary compiler pipeline. Statement/binding queries do not
  borrow the enclosing callable's effect summary: exact effects remain null when no
  precise target summary exists, with the callable summary exposed separately as context.
- The remaining AI-native Phase 6 work is implemented and frozen. `entity-v1` durable
  semantic identities carry deterministic implementation/interface hashes; project
  snapshots power `impact` and conservative `test --affected` selection with dependency
  paths and reuse counters. `fmt` is canonical and idempotent; exact semantic rename,
  binding-expression, and call-argument edits validate and format before writing.
  Structured diagnostics expose stable `fixes` and `legal_alternatives`, while `cost`
  reports factual materialization, traversal, copy, specialization, and backend facts.
  Bootstrap advertises the complete workflow and a deterministic session-report template.
  Phase 5 `.mossmap` source identities remain build/source-layout provenance and are not
  silently redefined as durable edit identities. MCP, a daemon, package management, new
  syntax, and AI telemetry remain deferred.
- Phase 7 is frozen. It adds a conventional `moss.toml` project layer and the `moss build`,
  `moss clean`, `moss test`, and `moss bench` workflows. Debug/release builds,
  native test/benchmark harnesses, stable project/source IDs, filtered discovery,
  benchmark sampling/baselines, and all structured results reuse the ordinary
  compiler pipeline and `moss-agent-1` envelope. It adds no package resolver,
  module system, or new optimizer semantics. Native cache reuse
  includes the resolved Rust compiler, verbose backend version, profile, and flags;
  benchmark baselines use the same fingerprint for compatibility.
- Phase 10.6A establishes synchronous domain invocation in the checked compiler and
  production lowering. `message` is a blocking, expression-valued handler call;
  `reply` terminates a handler; source-level `await` is retired with a migration
  diagnostic. Incoming payloads remain immutable value snapshots, may be forwarded,
  and may be replied by value, but cannot be written or consumed. Self-send and
  same-domain handler chaining are rejected. Transitional `spawn` construction
  existed at this checkpoint and was retired in 10.6B. Legacy mailbox transport
  adapters remain backend machinery pending later removal.
- Phase 10.6B establishes static composition and concrete topology. `spawn` is retired;
  direct domain constructors are restricted to the initial `main` composition prefix,
  and `domainroutes(...)` declarations provide immutable route slots in the shared domain
  member namespace. The compiler records one `ConcreteDomainGraph`, rejects concrete
  route cycles, preserves physical source provenance, and assigns deterministic unique
  whole-program `domain_rank` values. Synchronization classes followed in 10.6C;
  physical handler-level 2PL is implemented in 10.6D.
- Phase 10.6B.1 closes the handle universe in the checker: handles only occur as
  composition bindings, declared route bindings, and message receivers. Ordinary
  parameters/payloads, replies, state, aggregates, aliases, reassignment, and route
  shadowing are rejected. Every instance links directly to its canonical
  `DomainSpecialization` record, including typed instances that reuse a layout.
  Topology JSON exposes the closed graph, source domain, specialization, concrete
  identity, ranks, and physical edge provenance. Historical handle-passing tests
  are replaced with route equivalents and explicit negative regressions.
  Module namespace qualification, constructor metadata, synchronous exported
  bridges, and legacy cluster construction consume the same checked composition.
  This checkpoint did not add synchronization classes, LockSet/ClassSet, class
  ranks, or 2PL. Analysis followed in 10.6C and physical 2PL in 10.6D.
  Validation: strict C++17 `-O2 -Werror` build, full `make check`, `make examples`
  (generated Rust `-D warnings`), focused handle-closure/module regressions,
  Emacs warnings-as-errors byte compilation and 19 ERT tests, shell syntax, and
  diff whitespace checks pass. Optional live LLDB/DAP checks are capability-skipped
  when process tracing is unavailable; debug-map and objdump checks pass.
- Phase 10.6C is complete at `eb4d49a`: `Program::synchronization_plan` owns the
  graph-relative analysis over exact specializations. It derives leaf
  READ/WRITE/CONSUME observations, X*, ProtectedRead, LockSets, equal-signature
  classes, local class ranks, ClassSets, and conflict witnesses. JSON and human
  dumps read this stored plan. Phase 10.6D adds physical class locks and
  handler-level 2PL without replacing the analysis.

## Approved semantics

- Assignment of a uniquely owned nontrivial local transfers ownership; later source use is an error.
- `message` payloads and `reply` results are explicit value-copy boundaries. Existing non-primitive locals, parameters, state, and projections may cross while the sender retains an independent value.
- Ordinary primitive values remain usable after sending. Domain handles are routing
  capabilities and cannot be sent as payloads or used as ordinary values.
- Hidden deep copies and copy-on-write are prohibited. Independent duplication is the explicit future `deepCopy()` operation.
- Ordinary function parameter READ/WRITE/CONSUME effects are inferred. Primitive
  parameter assignment may be caller-visible WRITE; 10.6D implements physical
  write-through. No `var`, `mut`, `write`, or `inout` modifier is
  planned for parameter mutation.
- Immutable sharing, arenas, `ref object` identity, and persistent `revise` versions are deferred because retention and leak behavior is unresolved.
- Moss `Int` is currently signed 64-bit two's-complement. Overflowing integer
  arithmetic wraps modulo 2^64 in every backend; Rust overflow-check settings are
  not observable Moss semantics.

## Implemented features

- A minimal project manifest names the project/version and configured source directory
  or file. Commands locate the nearest project root, use deterministic profile artifact
  paths, hide direct Rust invocation, compile with warnings denied, and reuse unchanged
  generated Rust/debug maps/native artifacts only under the same backend-toolchain
  fingerprint. `moss clean` removes build products while
  retaining saved benchmark evidence.
- Top-level `test "name":` declarations support statically checked `assert(bool)` and
  `assertEqual(actual, expected)`. Project source and recursive `tests/*.moss` discovery,
  substring filtering, per-test panic containment, stable
  `test:<relative-source>:<name>` identities, Moss source diagnostics, human summaries,
  and versioned JSON output are implemented.
- Top-level `bench "name":` declarations compile only through the release/highest-
  optimization profile. The safe generated harness black-boxes every discarded
  value-producing expression (calls, arithmetic, and functional pipelines), uses warmup,
  31 repeated samples, 1,000 invocations per sample, and median/p25/p75 reporting.
  Stable-ID JSON baselines retain compiler/profile/platform/backend-toolchain/time/
  methodology/sample data. Incompatible backend fingerprints suppress both comparison
  and optional regression-threshold enforcement; compatible comparison and filtering are
  implemented without fragile thresholds in the compiler regression suite.
- Build, test, and benchmark generation share the parser, checker, ownership/effect
  passes, functional/dataflow optimizer, backend plan, generator, `.mossmap` contract,
  profiles, artifact directories, and Phase 6A JSON envelope. Test and benchmark code is
  absent from ordinary application artifacts.
- `moss agent bootstrap|capabilities|schema --json` provides vendor-independent protocol
  discovery and workflow/safety guidance. `moss check --json` uses stable diagnostic
  categories and a uniform source/span/identity/details shape. Semantic queries accept
  exact source locations or retained semantic identities and reuse the compiler's static
  call/concrete-domain graphs, synchronization plans, ownership/effect summaries,
  and optimization explanations. A
  statement with no precise retained effect summary reports null rather than inheriting
  all effects from its enclosing callable.
- Successful project checks/builds/tests/benchmarks persist a compact semantic snapshot
  under `.moss/semantic-cache-v1/`. `moss impact <target> --json` distinguishes unchanged,
  implementation-only, and semantic-interface changes and reports direct/transitive
  dependents, affected tests/benchmarks/domains/route edges, specializations, and reuse
  counts. `moss test --affected` uses that cone during iteration and conservatively runs
  all tests when no baseline exists.
- `moss fmt` and `moss fmt --check` provide canonical two-space Moss formatting after
  parse/check validation. `moss edit` applies only exact durable semantic targets for
  initial rename/replace-expression/change-argument operations; stale or ambiguous
  targets are rejected. `moss cost <target> --json` exposes compiler-known cost facts,
  and every structured diagnostic includes `fixes` plus `legal_alternatives`.

- A representative Phase 6 agent session used bootstrap, structured checks and
  semantic queries, exact edits, formatting, impact analysis, and affected tests.
  The structured repair for a missing block colon and the dependency path reported
  by `impact` avoided guesswork and an unrelated test run; ownership intent remains
  deliberately agent-selected from compiler-provided alternatives.

- Every Rust emission has an adjacent versioned JSON `.mossmap` with absolute source and
  output paths, real one-based source spans, deterministic source/provenance identities,
  generated ranges, line mappings, generated/native symbols, and provenance. Transient
  functional IR IDs are excluded. Fused functional nodes deliberately share one
  generated range while retaining all contributing origins.
- `--debug` selects unoptimized eager lowering with planned class locking and adds stable
  function boundaries. `tools/moss-build-debug` compiles that Rust with DWARF, frame
  pointers, no stripping, and warnings denied. Concrete functions, methods, handlers,
  and main have deterministic readable exported symbol names; synchronized handler
  entries and private bodies retain stable tooling identities.
- The dependency-light Emacs `moss-mode` includes comment/string syntax, centralized
  font-lock definitions, tab-free two-space indentation, `else` dedenting, Imenu and
  defun navigation, compilation-mode check/build/run commands, read-only artifact views,
  and bidirectional Moss/generated-Rust navigation through the shared map.
- Optional debugging uses Emacs `dape` with `lldb-dap`. Pending Moss source breakpoints
  translate deterministically after LLDB creates the native target;
  `tools/moss_lldb.py` also supplies standalone `moss-map-load`, `moss-break`,
  `moss-where`, and filtered `moss-stack` commands. Ordinary variable inspection stays
  in LLDB/DWARF rather than a custom debugger runtime.
- Emacs disassembly commands resolve a concrete symbol through `.mossmap`, prefer
  `llvm-objdump`, fall back to GNU `objdump`, use `asm-mode`, and display all fused
  provenance origins. Compiler operation never depends on editor/debugger/disassembler
  availability.

- The compiler now has extracted `src/ast.hpp`, `src/constraints.hpp`,
  `src/diagnostics.hpp`, and `src/functional_ir.hpp` modules. `moss.cpp` still contains
  most parser, checker, ownership, optimizer, and Rust-generator implementation; the
  extraction is incremental rather than a completed module split.
- Type declarations retain method bodies as children of their method nodes in the
  AST, including nested control-flow blocks.
- Concrete methods are checked with receiver fields and typed parameters in scope;
  result types are inferred across return paths and lowered through the ordinary
  statement generator to executable Rust inherent methods. Same-receiver calls,
  locals, conditionals, loops, and early returns are supported.
- Concrete method parameters must have resolved static types; unresolved parameters
  are compile errors rather than defaulting to an integer backend type.
- Untyped-function operation metadata now flows through structured `Constraint`
  records; the former `generic_ops` field has been removed.
- `ConstraintKind::Method` records the receiver relationship, method name, arity,
  argument relationships, and method-result relationships. Every concrete
  duck-typed call site is checked by the same resolver used for direct concrete method
  calls, including distinct diagnostics for missing methods, arity mismatches,
  incompatible arguments, ambiguity, and incompatible results.
- Named traits are structural compile-time contracts. Conformance resolves every
  declared method against the concrete type's inherent methods and checks annotated
  parameter and result types; there is no separate trait dispatch path.
- Functions with duck-typed method requirements or trait-typed parameters are
  specialized into concrete function instances discovered during checking. Generated
  calls target those instances directly, so multiple conforming object types execute
  through ordinary inherent calls without `dyn Trait`, vtables, runtime method search,
  implicit `Any`, or source-level generic type parameters.
- The Rust emitter lowers the existing `sum` builtin for inferred `Vector`/`seq`
  element types, including the concrete result annotation needed by strict Rust.

- Indentation-aware parser for object types, domains, handlers, local `fn` functions, and both `fn main()` and compatibility `proc main()`.
- Julia-like `type Name:` blocks, inferred object fields, expression/block-bodied `fn`
  functions, typed functional pipeline expressions, and synchronous expression-valued
  `message`; source-level `await` is retired.
- Phase 4 recognizes `map`, `filter`, `reduce(initial, fn)`, `sum`, `count`, `any`, and
  `all` as functional/dataflow operations rather than opaque nested calls. Static type
  propagation verifies element, predicate, accumulator, and terminal result types before
  Rust generation, including defined empty-input behavior. A nontrivial reduce initializer
  transfers into the terminal result rather than being copied.
- Functional stages accept named functions, bound READ-only instance methods,
  `_` placeholder expressions with immutable captures, and statically resolved method
  placeholders. Untyped higher-order callable
  parameters are closed at each call site, recorded as specialization dependencies, and
  erased from generated Rust; unbounded callable identity is rejected.
- `ObservableEffects` independently records local capture reads/mutation, domain
  reads/writes, message, I/O/external effects, unresolved effects, and possible
  failure. Transitive summaries conservatively govern fusion without changing
  READ/WRITE/CONSUME ownership inference.
- The functional IR distinguishes dense compilation-local pipeline/node handles from
  source-derived semantic identities. The checked AST carries the exact plan handle for
  each expression and static specialization, so Rust emission never re-matches plans by
  expression text/type/callable. Nodes retain real function/method result lines and stage,
  concrete input/output types, callable identity, captures, ownership/effect summaries,
  logical materialization, and stable semantic provenance. It also records element
  independence, determinism, and reduction compatibility for future planners.
- `-O0` emits explicit eager stage loops and logical intermediate collections. `-O`
  fuses safe map/map, map/filter/map, and terminal-reduction chains into one explicit
  loop, preserving order and wrapping integer accumulation while eliminating intermediate
  vectors. Effectful or possibly failing stages retain eager lowering.
- Phase 4.5 separates traversal fusion from physical materialization. Transformation
  nodes record virtual/materialized state plus escape, multiple-consumer, and barrier
  reasons. Exact count uses collection length; preceding pure/non-failing/non-divergent
  maps are dead when only cardinality is observed. Safe optimized `any`/`all` stop their
  consumer at a decisive element, while eager/effectful/failing/divergent callbacks visit
  the full source.
- Callable summaries carry `may_diverge` independently of ordinary effects and failure.
  Any function/method/handler containing `while`, plus callers reached through the
  acyclic local call graph, is conservatively marked. Work-eliminating plans require
  non-divergence; invocation-preserving ordinary fusion remains legal.
- A new local binding or `let` transformation with one adjacent immutable consumer can
  remain virtual across statements. Mutable/reassigned bindings, later uses, multiple
  consumers, intervening effects/statements, source effects, and control-flow ambiguity
  retain a concrete collection.
- `FunctionalTraversalGroup` represents one stable local source with multiple independent
  terminal consumer edges. Adjacent pure/non-failing sums, counts, filters/reductions,
  and `any`/`all` can share one explicit loop; dependent results and source mutation are
  conservative barriers. Group plans and generated comments retain every consumer's
  semantic provenance.
- `FunctionalPipeline::semantic_steps` is the Phase 4.6 post-analysis Moss semantic
  computation consumed by exact-plan codegen. A composed step references every original
  node; eliminated work retains its source identity and provenance. The bounded pass
  sequence does not reconstruct types/effects or use a generic rewrite framework.
- Phase 4.6 composes adjacent safe maps and left-to-right short-circuit filters. Its only
  predicate pushdown is through trivial `map(_)`, which the existing IR proves to be
  identity. Terminal `count` removes safe trailing maps even after a cardinality-changing
  filter, and inert scalar literal counts become constants. Effects, failure,
  `may_diverge`, ownership, multiple uses, and escapes remain conservative barriers.
- Deterministic `--dump-functional-ir` and `--explain-fusion` output exposes stage types,
  effects, spans, materialization decisions, retained provenance, fusion barriers, the
  optimized semantic-stage sequence, and successful `semantic-opt` rewrites.
- Julia-like domain state bindings (`value = initializer`) with optional `value: Type`
  constraints, statically inferred handler reply types, and `=`-named object constructors.
- Primitive, object, domain-reference, `seq`, `option`, and `table` types in the implemented slice.
- Direct composition-prefix domain construction and `domainroutes(...)` implement
  the static concrete graph. Production handles share per-instance class storage;
  all active modes use the synchronized handler entry described above.
- Phase 10.6E removes legacy mailbox, worker, coarse-lock, batching, coalescing,
  clustering, and atomic-domain implementations and their backend selectors.
- `-Oshared-memory`, `-O`, and `-O0` use the same planned domain runtime;
  only functional optimization depends on optimization level. Retired cluster
  and await-error-handling options are rejected.
- `_shared` wrappers retain planned reader/writer guards around private `_body`
  implementations. Rust checks Send/Sync legality; no unsafe storage access is used.
- Ordinary integer `+`, `-`, `*`, `/`, integer `sum`, and generated state-update
  equivalents lower through explicit `i64` wrapping operations.
- Synchronous expression/statement messages, typed/inferred reply handlers, terminating
  `reply value`, and explicit normal handler completion for no-value handlers.
- Domain-owned mutable state, synchronous handler invocation, local `let`/`var`,
  control flow, `echo`, and bare `return`. Legacy total-domain serialization is
  superseded by 10.6D's exact handler-level class acquisition.
- Ownership checks for direct local assignment, existing owned cross-domain payloads, nested non-primitive projections, domain state, and non-primitive replies.
- One indentation-aware type-environment walker now serves ordinary statement checking,
  domain-reference inference and local-call discovery. It
  forks `if`/`else` environments, merges only types present on every relevant path, and
  rejects conflicting concrete types. Definite same-type branch creation is carried to
  Rust lowering through explicit join metadata.
- Static callable/effect validation remains available across handlers, methods, local
  functions, and `main`. Await-target/cycle machinery is removed; retired syntax
  is covered by migration-negative tests. The concrete route DAG and lock ranks
  provide the active domain deadlock proof.
- Direct and mutual recursion are rejected. Compiler-internal READ/WRITE/CONSUME summaries drive local call lowering, and conflicting aliases at a call site are Moss compile-time errors.
- Copyable field projections retain a read effect; moving a nontrivial field consumes its containing value. Consuming method receivers lower by value rather than as shared receiver references.
- Generated Rust compilation with warnings denied in the test suite. Active domain
  calls enter synchronized wrappers directly and complete before the following
  Moss statement.
- Executable showcases cover method-based duck typing, two concrete named-trait
  implementations, inferred Vector/Map/Queue use, an executable optimized functional
  dataflow pipeline, and a small static job-scheduler application with domains and
  synchronous messages.
- The focused functional showcases additionally cover named and placeholder stages,
  immutable captures and source reuse, every reduction terminal and empty-input identity,
  bound methods and static higher-order specialization, observable eager effect order,
  wrapping reduction arithmetic, read-only pipelines over user-defined values, and an
  synchronous domain callback that remains an eager fusion barrier.
- Four Phase 4.5 showcases cover terminal simplification, a virtual cross-binding
  intermediate, a shared terminal traversal, and multiple-consumer materialization.
- The Phase 4.6 showcase covers semantic map/filter composition, a virtual named
  intermediate, dead-map removal, known literal cardinality, and safe short circuiting.

## Partially implemented or unimplemented

- Package/dependency resolution remains unimplemented. Phase 8 added explicit
  modules/imports; legacy implicit-module project targets still use an uber-module:
  build links `src/**/*.moss`, test links src plus
  `tests/**/*.moss`, and bench links src plus `benches/**/*.moss` as one global unit.
  Assertion panics are isolated per test in one process, but process aborts and failures
  on spawned threads can still terminate that unit. Benchmark warmup/sample counts are a
  fixed conservative first methodology rather than adaptive statistical calibration.
- Phase 5 does not invent one-to-one stepping for fused code. Several Moss nodes may
  resolve to one Rust/native location, and reverse assembly navigation is currently
  symbol/provenance-oriented rather than an exact address-level UI. Generic Rust
  implementations without a concrete Moss specialization have no stable native symbol.
- Emacs, Python, LLDB/`lldb-dap`, `dape`, and objdump are optional integrations. The
  corresponding regression runs only when each capability is installed; there is no
  VS Code extension, LSP, performance model, or Rust interoperability in Phase 5.

- Ownership analysis remains conservative around arbitrary raw expressions and indirect aliases outside the statically represented projection/call slice.
- `deepCopy()` and its cost warnings are approved but not implemented; no implicit copy is inserted.
- Top-level `fn` local functions and inferred parameter effects are implemented.
  Earlier explicit parameter-mutation syntax proposals are superseded by the
  current inferred-effect rule; no such syntax is planned.
  Self-send and same-domain handler chaining are rejected; queued self-communication
  is not an active language feature.
- Reply-path completeness is checked only syntactically; fallthrough is diagnosed at runtime.
- Production synchronization consumes the stored plan at every optimization level;
  old backend synchronization heuristics are removed. 10.6D.1 removes READ
  snapshots; static-accessor code size, slot/descriptor overhead, and conservative
  explicit-boundary copies remain possible future representation optimizations.
- The closed concrete route DAG and deterministic domain ranks are complete.
  `SynchronizationPlan` supplies class ranks and handler ClassSets; 10.6D consumes
  them for production physical class locking and handler-level 2PL.
- Fast Debug executes synchronous domains and source-module closures. Functional
  pipelines and `for` remain unsupported interpreter constructs with precise errors;
  source-free Moss dependencies require production execution.
- Primitive ordinary-parameter WRITE lowering is fixed in 10.6D; the existing
  semantic effects remain authoritative.
- Functional collection ownership is intentionally conservative. A callback that would
  WRITE or CONSUME a nontrivial element, a mutable capture, or a nontrivial `filter`
  result that would require an implicit copy is rejected rather than cloned or made
  unsafe.
- Functional fusion uses correctness-first effect rules rather than profitability data.
  Phase 4.5 scope reconstruction is deliberately lexical and adjacent rather than a
  general SSA optimizer. Automatic SIMD, threading, GPU lowering, generic stage
  reordering/algebraic predicate pushdown beyond Phase 4.6's proven identity case,
  layout/storage reuse, general lambdas, runtime callable values, user effect
  annotations, initializer-free reduction, and sophisticated profitability modeling
  are not implemented.

## Known bugs and limitations

- Static specialization currently covers method-constrained and trait-typed local
  functions in the implemented expression/call slice. Associated types, trait
  inheritance, default trait methods, runtime trait objects, and source-level generic
  declarations remain intentionally unsupported.

- Rust type errors may still surface when Moss inference lacks enough source information; normal Phase 2 ownership and conflicting-call-access errors are diagnosed by Moss.
- Source `await` and `spawn` are retired; domain construction is restricted to the
  initial `main` composition prefix. Cancellation, timeouts, and failure propagation
  remain unimplemented.
- Parent class guards remain held across nested synchronous messages, intentionally
  increasing lock-hold latency and possible head-of-line blocking. Fairness is
  unspecified; early unlock and lock elision remain future work.
- Most compiler implementation remains in one C++17 translation unit with a deliberately
  compact semantic/type checker and extracted data headers.

## Historical validation by checkpoint (before Phase 10.6)

The following records describe tests at their original checkpoints. References to
spawn, await, one-way messages, passable handles, FIFO, and serialization are
historical coverage, not current source semantics or a claim that those fixtures
remain unchanged. Current validation and the historical documentation-only
preflight are recorded separately above. Legacy physical-code expectations have
been replaced with plan-driven expectations while executable behavior stays covered.

`make check` passes with Phase 4.6 on the frozen Phase 2.5 foundation. The suite compiles ordinary,
Mutex/RwLock/atomic optimized, batched, coalesced, and clustered Rust with
`rustc -D warnings`; rejects any generated
`std::sync::mpsc` use; checks local implementations for the expected synchronization
boundary; compares behavior for checkout, object isolation, ignored replies, local and
shared domain-reference messages, FIFO, and non-reentrancy; rejects invalid cluster
layouts, cycles, naked domain calls, invalid awaits, unresolved fields/state, conflicting
reply types, state annotation mismatches, and value-returning `main`; verifies source/
backend annotations; verifies `--no-await-error-handling`; and repeatedly exercises both
the lock-backed mailbox and direct state-lock paths under contention. Dedicated positive
cases cover inferred state, optional state annotations, inferred replies, and `=`
constructors; the former one-way-reply negative fixture is now a positive compatibility
case because reply presence infers request/reply capability. Dedicated method/trait
cases execute one duck-typed function and one trait-typed function with two distinct
user-defined types, assert that two concrete specializations are emitted, and reject
runtime Rust trait machinery. Negative coverage includes missing methods, wrong arity,
incompatible method arguments, missing trait methods, incompatible trait signatures,
conflicting method-result expectations, unresolved collection element types,
heterogeneous collections, and naked cross-domain calls. The showcase suite also executes
under the regression harness: static duck typing, named traits, inferred collections and
methods, the `map |> filter |> map |> sum` dataflow shape, the domain-backed mini
application, and the focused functional examples through Phase 4.6.

Await regressions additionally accept same-concrete-domain branch joins, reject
different-domain joins and one-branch-only targets, validate an await helper reached
only from `main`, retain unreachable-branch dependencies, and cover direct, transitive,
helper-hidden, and repeated-edge cycles. Cycle assertions require the correct Moss line
for every witness edge. All of these cases run alongside the unchanged Phase 2
ownership and Phase 2.5 backend suites.

Phase 2.5 regressions additionally prove same-receiver batching and sender order,
different-receiver/effect barriers, batched tracker accounting/rollback, exclusive
one-guard regions, multi-caller rejection, shared READ versus exclusive WRITE guards,
integer and boolean atomics, mixed awaited/one-way mailbox elimination, multi-field
SeqCst use, atomic request/reply copy boundaries, locking fallbacks for invariants and
external effects, fully atomic runtime removal, atomic and read-heavy contention, and
representative `-O0`/`-O` output parity.

The overflow differential additionally runs increment-at-`i64::MAX`, decrement-at-
`i64::MIN`, positive addition overflow, negative-direction subtraction overflow,
and awaited new-value replies through both `-O0` mailboxes and `DirectAtomic`.
Both generated programs are compiled with `rustc -C overflow-checks=yes -D warnings`
and must match. It also checks ordinary wrapping multiplication and division,
integer `sum`, and a concretely specialized duck-typed integer operation.

Phase 4 regressions run representative functional programs through both `-O0` and `-O`,
compile both generated Rust programs with `rustc -D warnings`, compare their exact output,
and cover named, bound-method, and placeholder `map`, `filter`, map/map, map/filter/map,
every terminal, immutable capture, statically specialized higher-order helpers, empty
input, object-method placeholders, eager callback output order, and wrapping reduction.
Generated-code checks require one explicit loop and no intermediate vector or Rust iterator chain for fusible
pipelines. Separate I/O, possible-failure, domain-state, and synchronous `message` cases must
retain eager lowering. Terminal scalars can use message/reply boundaries directly, while
transformation collections must first materialize. Deterministic IR/explanation checks
cover types, spans, materialization, provenance, and barrier reasons. Negative tests require Moss diagnostics for invalid
predicates/callables/reductions, consuming elements, mutable capture, unbounded callable
identity, pipeline domain-boundary escape, higher-order recursion, and callback alias
violations.
They additionally distinguish identical pipeline text in pure-local and domain-effect
contexts, prove that an I/O-producing `reduce` initializer is recorded on the `Reduce`
node and preserves differential evaluation order, verify real method-result provenance,
reject non-Copy `_`/field-projection maps before Rust generation, and reject captured
mutation propagated through nested ordinary helpers.
The focused functional examples are likewise differential tests: each is compiled with
both `-O0` and `-O`, compiled by `rustc -D warnings`, executed, and compared against one
expected output.

Phase 4.5 differentials cover exact and mapped count, empty/immediate/late/no-match
`any`/`all`, complete eager effect ordering, possible-failure barriers, bare and explicit
immutable cross-binding fusion (including a function result), later-use/mutable/effect
materialization barriers, multiple consumers, sum/count/filter-count and any/all/count
shared DAGs, result-dependency and source-mutation barriers, and wrapping shared sums.
Direct/transitive `while` regressions verify that mapped-count elimination and standalone,
cross-binding, and shared-DAG `any`/`all` skipping are disabled by `may_diverge`, while
ordinary invocation-preserving fusion remains enabled.
Generated-Rust checks require eliminated callbacks/loops, optimized-only short-circuit
control flow, absent virtual collections, present required collections, and one shared
source loop. Deterministic IR checks assert materialization reasons, DAG edges, stable
group identity, and combined provenance.

Phase 4.6 differentials cover adjacent map and filter composition, identity-only legal
predicate pushdown plus non-identity/effectful rejection, safe and divergent/effectful
dead-map cases, filter/map/count terminal simplification, safe and divergence-blocked
`any`/`all`, known literal cardinality, runtime-empty terminal identities, single-use
cross-binding collapse, downstream dead work, and multiple-use materialization. The
suite inspects the semantic plan and generated Rust, including composed predicate
short circuiting, eliminated callbacks/literal allocation, retained barrier callbacks,
deterministic rewrite output, and exact `-O0`/`-O` result parity.

Phase 5 regressions build the tooling fixture twice at identical paths and require
byte-identical maps; compare `-O0`, optimized, and explicit debug maps; verify deterministic
source/provenance identities, real function/method/handler lines, readable native symbols,
bidirectional exact line resolution, and many-to-one fused provenance; and compile and
run both generated programs with warnings denied. Python map/LLDB helper checks pass,
all 19 batch ERT tests pass under Emacs 30.1, and `llvm-objdump` resolves both an ordinary
debug function and an optimized fused-pipeline symbol. Live LLDB 19.1.7 and
`lldb-dap-19` regressions load the shared helper/map, resolve exact breakpoints for main,
a method, ordinary functions, and a handler, step between two exact Moss lines, map user
and runtime stack frames, inspect integer/boolean/user-type/raw String/raw Vector locals,
and terminate cleanly. Debian's LLDB build warns that Rust-specific value presentation is
limited, so collection/string inspection remains structural. Phase 5 provenance IDs are
repeatable for an unchanged source layout but are not the durable cross-edit semantic IDs
planned for Phase 6. No compiler functionality depends on optional debugger tools.

`make examples` passed, compiling every valid example (including the executable static
trait, dataflow, reduction, callable, effect-order, and object-pipeline showcases) with
`-Oshared-memory`; the intentional `use_after_transfer.moss` negative example was skipped. A strict
`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic` build and `sh -n tests/run.sh` also
passed for this checkpoint.

Phase 7 validation additionally builds the example project in debug and release modes,
checks byte-stable map/artifact reuse under one Rust toolchain, forces rebuilds for a
changed/fake Rust identity, executes and filters passing/failing native tests,
inspects Moss assertion values/locations, executes and filters release benchmarks,
validates call/arithmetic/pipeline black-box and sampling structure, saves and compares
a toolchain-identified metadata/sample baseline, suppresses incompatible baseline
threshold enforcement, checks structured project errors, and runs `moss clean`.
Phase 6A validation also proves target-local statement effects remain unknown rather
than inheriting callable-wide I/O while the enclosing, callable, and pipeline summaries
remain available. The full `make check` suite,
`make examples`, strict C++17 warnings-as-errors build, generated Rust `-D warnings`,
Phase 4/4.5/4.6 differentials, Phase 5 map/Emacs/objdump/LLDB/DAP integrations, Phase 6A
API regressions, 19 ERT tests, `sh -n tests/run.sh`, and `git diff --check` all pass.

Two local smoke samples of the contention program completed 20 baseline runs in approximately 0.20–0.25 seconds and 20 direct shared-memory runs in approximately 0.02–0.03 seconds. This is evidence that transport elimination works for the intended request/reply shape, not a general performance claim.

## Immediate next tasks

Phases 10.6A–F are complete and checked in. Phase 10 is the Moss v0.1 milestone.

1. Phase 15 — Dogfooding: write substantial Moss programs, identify real friction,
   and let actual usage determine the next refinements. Start with the ledger,
   index, pipeline, and structural-trait seeds. This closeout does not begin new
   Phase 15 feature work.
2. Track measured tiny-handler/storage/code-size costs, interpreter `for`/pipeline
   coverage, cross-module generic linkage and ergonomics, trace slicing, package
   resolution, and standard-library gaps as evidence for subsequent priorities.
3. Post-dogfooding roadmap: Phase 20 Rust Interoperability; Phase 21 Error
   Propagation & Supervision; Phase 22 Agent Agency Tooling. Scoped domain lifetimes
   and concurrency ingress remain explicitly unresolved future directions.

The 10.6C synchronization algorithms remain authoritative and unchanged apart
from removal of unreachable retired-Await traversal branches. No language-design
change is authorized by this consolidation phase.

## Open design questions requiring Kutty's decision

- Exact `deepCopy()` warning wording and fixed-size estimates.
- Future retention-safe designs for immutable sharing, arenas, and persistent versions.
- How future automatic cluster selection should balance locality, synchronous calls, and load distribution.
- Supervision, failure propagation, transactional rollback, and restart semantics remain
  unresolved and were deliberately not changed by this frontend migration.

## Historical 2026-09-11 shared-memory and clustering handoff

This record predates 10.6A/B/B.1. Its spawn, await, one-way queue, self-message,
and serialization descriptions are historical, not the current language model.

### What changed in this session

- Replaced every Rust standard channel with generated lock-backed shared-memory queues and one-shot reply cells.
- Kept the optional awaited-only `Arc<Mutex<DomainState>>` direct optimization for unclustered domains.
- Added repeatable `--cluster=A,B` backend placement. A generated cluster runtime owns one thread, one shared ingress queue, all member states, and a plain local queue.
- Generated distinct `_shared` and `_local` call implementations and selects between them statically. Cluster-member capabilities lower to zero-sized local references. Local awaits are ordinary handler calls; one-way calls retain queued semantics without synchronized operations.
- Added `--no-await-error-handling` as an explicit backend opt-in for supervision-owned failures. It removes per-await `unwrap_or_else` diagnostics and emits unchecked reply extraction; the checked path remains the default until supervision trees exist.
- Added target-specific local flushing before direct awaits to preserve FIFO when an earlier local one-way call targets the same domain.
- Prohibited cross-domain ownership transfer of bound non-primitive data, including nested projections and replies, while permitting fresh message construction and same-domain queued transfer.
- Updated the object pipeline to send a primitive snapshot across domains and added transport, clustering, contention, FIFO, and ownership regressions.

### Decisions Kutty explicitly made

- All Rust message transport must use shared memory with locks and Rust's `Send` boundary; there is no true Rust message-passing fallback.
- Moss must continue to expose and behave as message passing; the backend representation must not change the language.
- Domain-private non-primitive values cannot transfer ownership across domains.
- Configured clusters share one execution thread, and calls among their members remove synchronization through statically selected generated implementations.

### Assumptions Codex made

- `--cluster` is a compiler-side placement input that generates the requested runtime startup call, because runtime branching would conflict with static call selection and Moss must not gain placement syntax.
- Fresh values built directly as payloads are message-owned rather than transferred from a domain. Queued self-messages remain within one ownership domain.
- The initial cluster implementation is intentionally limited to one unconditional spawn per member type.

### Tests actually executed

- `make check` — passed with baseline and shared-memory generated Rust compiled under `rustc -D warnings`.

### Exact commit hash containing the backend work

`cd65d8c` — Backend predecessor before the frontend migration. The syntax-direction
checkpoint is `c130ff2`; the completed frontend checkpoint is the commit that follows.

## Historical 2026-09-05 transfer-semantics handoff (superseded domain model)

The domain-boundary descriptions and await/serialization assumptions below are
historical. Current message/reply value boundaries, immutable payloads, closed
handles, and synchronous calls are described above.

### What changed in this session

- Made approved transfer semantics authoritative in the design records.
- Renamed user-facing move terminology to transfer terminology.
- Added the earlier source checks for detached message transfers and direct domain-state transfer rejection. The later 2026-09-11 boundary rule now rejects the cross-domain transfer case.
- Updated the earlier message lowering to avoid hidden deep copies; current lowering accepts fresh payloads and rejects existing owned non-primitive bindings at cross-domain boundaries.
- Added positive and negative examples and regression tests.

### Decisions Kutty explicitly made

- Nontrivial assignment transfers ownership; hidden deep copies and COW are prohibited.
- `deepCopy()` is explicit future syntax and is not implemented.
- Detached-local cross-domain transfer was the decision at that time and is superseded by the 2026-09-11 prohibition. Same-domain queued transfer remains allowed.
- The historical explicit-parameter proposal is superseded: ordinary function
  parameter READ/WRITE/CONSUME effects are inferred, and primitive parameter
  assignment may be caller-visible WRITE. No `var`, `mut`, `write`, or `inout`
  modifier is planned for parameter mutation.
- Immutable sharing, arenas, and `revise` remain deferred due memory-retention and leak concerns.

### Assumptions Codex made

- Historical assumption, superseded by Phase 10.6A: await/reply and total-domain
  serialization were treated as the language model. Phase 10.6A establishes
  synchronous `message` and terminating `reply`. Phases 10.6B–C add the closed
  concrete graph and compiler-owned synchronization analysis; physical
  handler-level 2PL is implemented in 10.6D.
- The lightweight checker is extended only for direct, statically recognizable transfer cases in this increment.

### Tests actually executed

- `make clean && make check` — passed; generated Rust was compiled with `-D warnings`.

### Tests that could not be executed and why

- None.

### Semantic questions still unresolved

- At this historical checkpoint: `deepCopy()` warning details and ordinary
  procedure design. The parameter-syntax proposal is superseded by the current
  inferred-effect rule above.

### Exact commit hash containing the work

`0adac33` — Implement approved Moss transfer semantics.

## 2026-09-14 temporary multi-file project linkage

This checkpoint introduced the uber-module model before Phase 8 added explicit
modules/imports. Legacy implicit-module targets retain it: `moss build` compiles
every `src/**/*.moss`; `moss test` adds
`tests/**/*.moss`; and `moss bench` adds `benches/**/*.moss`. Each target is one
global compilation unit with deterministic path discovery, order-independent
declaration resolution, duplicate-symbol diagnostics, and original physical
source provenance in diagnostics, semantic queries, generated maps, tests, and
benchmarks. Native artifact fingerprints include the complete source set.

Semantic query/edit context hardening: Phase 6A queries and edits now consume the
same temporary uber-module source sets as project build/test/bench. `src` selectors
analyze all application files, test selectors use `src + tests`, and benchmark
selectors use `src + benches`; standalone files remain single-file. Cross-file
renames are transactional within the selected context, and expression/argument edits
are validated against the complete logical program before writing. Physical source
paths remain in query results and diagnostics.
## Implicit domain specialization hardening

Each declared instance of an implicit-state domain now has one monotonic concrete
specialization. Compatible repeated calls remain valid; an incompatible state,
handler-parameter, or reply constraint is rejected with both constraint lines rather
than overwriting the layout. Separate instances of the same source domain continue to
specialize independently. Historical exact-instance await identities are now
superseded by the closed concrete graph and exact specialization linkage.

Handler parameter constraints are indexed by handler and parameter slot. Multi-parameter
handlers can therefore specialize as tuples such as `Set[0] -> Int, Set[1] -> Float`; only
an incompatible constraint for the same slot is rejected.

Historical typed domain-handle parameter support is superseded by Phase 10.6B.1:
handles cannot be parameters, payloads, or reply values. Concrete composition and
declared routes retain exact instance identity; the backend nominal route wrapper
may still route to independently materialized layouts. That wrapper is implementation
plumbing, not a passable Moss value.

## Phase 8

Modules/imports/exports are supported with qualified namespace resolution.
Typed exports are materialized contracts; untyped exports are compile-time Moss
generics represented in versioned `.mossi` interfaces. Historical await-graph
identities are superseded by 10.6B/B.1's closed concrete route DAG and exact
instance identities (`module::binding`). Each explicit module emits its own generated Rust
crate/rlib in import-DAG order; consumers link dependency rlibs with `--extern`.
Generic semantic IR and private helper closure are re-instantiable without
provider source, and final-build specializations are canonical in a dedicated
Rust crate. Legacy implicit-module projects remain supported.

## Phase 4.7 — Static Iteration and `for` Loops

**Phase 4.7 — Static Iteration and For Loops: complete and frozen.**

Phase 4.7 is implemented additively on the frozen Phase 4–4.6 semantics. `for binding
in source:` has scoped bindings and participates in the existing type, effect, and
ownership walkers. `range(start, end)` is half-open; vectors use READ traversal; and
custom traversal resolves statically through the structural `Iterator` contract
(`next() -> Option[element]`). Collection mutation during an active READ traversal is
rejected. Effect analysis reuses the canonical iterator-element resolver for loop
bindings, so method and ownership/effect summaries agree with ordinary type analysis.
No runtime iterator dispatch or vtable is emitted.

## Historical Phase 2.6 — Immutable Message Payloads and Shared-Memory Copy Elision

The physical backends below are retired in 10.6E; current production message and
reply boundaries establish independent owned values. Payload immutability remains.

Incoming handler arguments are immutable READ-only value snapshots regardless of
concrete type. The normal ownership/effect checker rejects mutation,
reassignment, consumption, and moving a payload into domain state, including
through transitive helper calls. A payload may be replied by value or forwarded
with `message`; both are explicit value boundaries. `Copy` is only a
backend/property distinction and does not weaken these Moss restrictions.
Legacy mailbox adapters retain owned payload copies. Synchronous DirectMutex,
DirectRwLock, DirectAtomic, and cluster-local lowering pass nontrivial READ-only
payloads by temporary immutable reference when safe, while primitive Copy values
remain by value. This is a backend optimization preserving identical Moss
semantics.

## Historical Phase 10.1A — Fast Debug interpreter checkpoint

The domain restriction below is superseded by Phase 10.6E.

The checked Moss AST now has a direct Fast Debug execution backend for ordinary
functions. `moss run --interp` and standalone `moss test --interp` execute
literals, arithmetic, locals, assignments, conditionals, while loops,
function calls, returns, structs, fields, methods, and assertions without
invoking Rust tooling. `--trace` emits deterministic newline-delimited JSON
semantic events. Domain scheduling, synchronous messages, and replies remain
unsupported in Fast Debug; source `await` is retired. Unsupported operations
report an interpreter diagnostic rather than falling back to generated Rust.

Fast Debug project entry points now reuse the project compiler's checked source
loading. `moss debug app` (or a project directory/entry source) interprets the
transitive source-module closure for explicit-module projects, and the complete
legacy `src/**/*.moss` uber-module otherwise. All selected Moss code executes
in one interpreter; compiled Moss interfaces/native modules are rejected rather
than mixed into the run. Project closure regressions cover cross-file calls and
verify that no Rust compiler is needed.

## Historical Phase 10.5 — Semantic convergence and AI introspection

Phase 10.5 is a non-semantic tooling/documentation checkpoint. The existing
static callable specialization, first-order effect summaries, entity-v1/source
provenance, semantic query API, impact graph, and Fast Debug trace are now
documented together. Query JSON exposes the existing source identity,
specialization identity when an exact record exists, resolved call kind, and
caller edges without creating a second analysis. Fast Debug ordinary execution
trace events retain source file and semantic identity and include local reads,
returns, branches, loops, writes, and assertion failures.

At the 10.5 checkpoint, synchronization diagnostics and the Account derivation
were reserved/design-only. This is superseded by 10.6C: schema and semantic
queries now expose the stored `SynchronizationPlan`, including derived effects,
classes, ranks, handler sets, and conflicts. `docs/SYNCHRONIZATION_PLAN.md`
records the implemented Account derivation.

The Phase 10.5 “BLOCKED BY OPEN DESIGN” paragraph is historical and superseded
by the resolved decisions in Phases 10.6A–C:
10.6A/B/B.1 settled synchronous calls, self-send/chaining rejection, closed handles,
and concrete domain ranks; 10.6C implements synchronization analysis and semantic
effect metadata. Existing ordinary parameter WRITE effects remain authoritative.
Phase 10.6D implements physical handler-level 2PL and primitive write-through;
Phase 10.6E adds Fast Debug domain execution and removes the legacy runtime.

## Fast Debug parity follow-up

Fast Debug now preserves eager source traversal for `any` and `all`, evaluates
`Map.get` defaults eagerly, and returns independent Map `get`, `keys`, and
`values` results. Typed empty vectors retain their element type in Fast Debug so
`Vector[Float]()` sums to Float zero. Focused native/Fast Debug regressions cover
these semantics, Map operations through object fields, and empty Queue pop
fail-closed parity.

`tests/run.sh ./moss` reaches the known unrelated Phase 10.6F `&String == String`
native lowering failure after all focused additions pass. `make check` reaches the
known environment-specific LLDB/DAP real-integration handshake failure first.

The remaining Fast Debug closeout repairs detach a selected non-Copy `Map.get`
fallback and retain the checked map-output element type through empty pipelines.
Focused native/Fast Debug parity regressions cover missing-key fallback ownership,
empty `Int -> Float` and `Int -> Int` maps, and an empty Float filter before `sum`.

Fast Debug frames now carry the checker-compatible functional context separately from
their human-readable trace name. Pipeline metadata lookup matches canonical context,
line, and checked stage source, including static function specializations and multiple
pipeline expressions on one line.
