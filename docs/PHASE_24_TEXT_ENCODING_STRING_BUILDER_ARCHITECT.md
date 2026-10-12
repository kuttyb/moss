# Team T focused String/builder independent Architect review

## Reviewed candidate and verdict

The independent L6 Architect reviewed exact frozen engineering SHA
`1fc4def7ddb040967a560ffa2a16cb30394f8563`.

**Verdict: NO-GO.** The broad implementation direction is sound and the normal
correctness suite passes, but two independently reproduced blockers prevent the
requested final acceptance: a nested alias escapes the new mutable String
ownership check and fails native Rust compilation, and one advertised
preconstructed benchmark compares different timed work.

## Blocking finding 1: nested receiver/argument alias

The direct form `value.append(value)` is correctly rejected with
`OWNERSHIP_CONFLICTING_ACCESS`. The fresh counterexample below is incorrectly
accepted:

```moss
fn identity(value: String) -> String:
  return value + ""

fn main():
  var value = "x"
  value.append(identity(value))
```

`moss check --json` returns success even though `identity` READs `value` while
`append` requires WRITE access to the same storage. Generated Rust acquires
`&mut value` and then evaluates `identity(&value)`; rustc rejects it with E0502.
Fast Debug can execute the checked source, so native and interpreted coverage
also diverge. `tests/tooling/check_phase24_string_concat.py` now contains the
independent regression expectation. The candidate needs transitive/nested
receiver-argument overlap detection before it can receive GO.

## Blocking finding 2: reusable-build timing mismatch

The Moss case named `capacity builder reusable build preconstructed 128
segments x16` constructs the owned part, reserves the builder, and performs all
128 appends inside its generated `__moss_bench_*` function. That function is
invoked inside every timed iteration. The Rust baseline constructs its part and
builder outside the `measure` closure, so only the sixteen snapshots are timed.

This directly contradicts the report's statement that both inputs are
preconstructed before timing. The published ratio for reusable build is not a
like-for-like measurement. Either equivalent setup must be excluded from both
timed regions using a supported control, or identical setup must be included in
both and the workload relabeled. Favorable timings do not waive workload
equivalence.

## Independently confirmed behavior

- Fresh nested and flat concatenation probes evaluate operands exactly once and
  in source order under both native execution and Fast Debug, including Unicode
  and empty operands.
- Self-aliasing concatenation takes the independent-result path; a function-call
  alias in concatenation also avoids in-place reuse. A separately cloned alias
  permits safe accumulator reuse.
- Semantic queries report capacity append as `WRITE + READ`, reusable build as
  `READ`, and finish as `CONSUME`.
- Direct append self-aliasing and imported source-free ABI-v9 self-aliasing are
  rejected. The source-free positive consumer passes.
- The existing `Vector[String]` builder API remains present and its tests pass.
- Capacity-builder Unicode, empty, nonpositive reservation, reusable snapshot,
  append-line, and consuming-finish cases pass in native and Fast Debug tests.
- The full Team T acceptance runner passes 12 Text tests, five Encoding tests,
  three cross-package tests, compiled-only providers, and the 66-valid/
  15-invalid UTF-8 oracle in native debug, native release, and Fast Debug.
- Release Margo generation used optimized providers. The fresh run measured
  finish medians of 100, 543, and 2,680 ns for 8/128/1,024 segments; append-only
  was 306 ns. Strict optimized Rust measured 70, 276, and 1,901 ns plus 198 ns.
  The noisy 128-segment and append rows in this single independent run exceed
  1.5x, reinforcing the need to preserve distributions rather than select one
  favorable run. This variance is secondary to the two correctness/methodology
  blockers above.
- Rust allocation instrumentation reproduced 2 allocations for each finish or
  append-only case and 16 allocations for sixteen snapshots. Moss allocation
  counts remain unmeasured; generated Rust only supplies structural evidence.

## Commands

```text
./moss agent bootstrap --json
python3 tests/tooling/check_phase24_string_concat.py
../../margo test --json                       # from sdk/text
python3 sdk/text/tests/check_source_free.py
python3 sdk/text-encoding-acceptance/run_all.py --full
MOSS=../../moss ../../margo bench --release --json
rustc --edition=2021 -D warnings -O -C debuginfo=1 case_builder_baseline.rs
rustc --edition=2021 -D warnings -O case_builder_allocations.rs
python3 tests/tooling/check_agent_feedback.py
```

The focused regression passes before the newly added nested-alias expectation;
with that independent expectation present it intentionally fails on the frozen
candidate. A repaired candidate must pass it, correct the benchmark equivalence,
rerun release/provider validation and repository gates, and receive a new
exact-SHA Architect review.

## Re-review of repaired candidate `ca6cd0d`

The independent L6 Architect next reviewed exact frozen engineering SHA
`ca6cd0d58b95d63d09d90b50603cf3dcca4e2e9b`.

**Verdict: NO-GO.** Both originally reported defects were repaired at their
immediate sites, but a fresh source-free public-API counterexample exposes the
same ownership hole through an ordinary imported wrapper call.

The builtin forms now behave correctly. Direct self-alias, two-level wrapped
append alias, and two-level wrapped reserve alias all return structured
`OWNERSHIP_CONFLICTING_ACCESS`; the cause identifies the writable receiver and
the wrapped READ expression. A disjoint Unicode control passes checking,
native rustc with warnings denied, and Fast Debug with identical output.

The remaining blocker is:

```moss
module app
import text

fn copy_twice(value: String) -> String:
  return value + ""

fn main():
  var builder = text.new_capacity_builder(16)
  text.capacity_builder_append(builder, copy_twice(builder))
```

With the source removed and `text.mossi` plus `libtext.rlib` supplied, `moss
check --json` incorrectly returns success. `moss build --json` then fails rustc
E0502 at the imported call because `builder` is borrowed WRITE for the first
argument and READ inside the second. Thus ABI-v9 carries the correct exported
`WRITE + READ` effects, but ordinary call-site overlap checking still ignores
nested argument storage accesses. The permanent source-free test now expects
the structured Moss rejection.

The benchmark repair is independently confirmed. Generated Moss and tracked
Rust both time construction of one owned `"abcdefgh"` part, exact reservation
of 1,024 bytes, 128 appends, sixteen reusable snapshots, scalar counts, and a
16,384 checksum assertion. Both setup sequences are inside their timed
function/closure. The release validator reports `-O` and SHA-256 equality for
all six linked providers. A fresh paired run measured the corrected end-to-end
row at 864 ns Moss versus 893 ns Rust (0.97x). Other capacity rows measured
100/72 ns, 388/298 ns, 2,460/1,839 ns, and 294/198 ns: all at or below 1.48x.

The allocation-only Rust harness remains intentionally different in scope: it
constructs the reusable builder before tracking and measures sixteen snapshot
allocations (16,384 bytes). This is correctly labeled setup-excluded and is not
used as timing evidence. Finish and append-only allocation counts reproduced
the documented two allocations each. Moss allocation counts remain unmeasured.

Full Team T native/release/Fast Debug acceptance, compiled-only providers, and
the 66-valid/15-invalid three-mode UTF-8 oracle pass. The repository `make
check` progressed through the focused String regression and extensive compiler,
SDK, SWARM, FileIO, and Executor gates before the Architect stopped it after the
new source-free blocker was confirmed; a completed full rerun remains required
on the next repaired candidate.
