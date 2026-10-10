#!/usr/bin/env python3
"""Phase 21.C: chunk speculation proofs and ordered Branch outcomes.

This focused gate checks the real compiler's stage-local P21-10A proof,
function-local P21-10B proof, emitted indexed/join-before-observe lowering, and
the production Branch runtime with a deterministic Value/Raised/EOF fixture.
The fixture uses an owned raised payload and owned read-token sentinels; it does
not define or replace the frozen provider ABI-v8 frame representation.
"""

from pathlib import Path
import json
import re
import subprocess
import sys


REPO = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else REPO / "moss").resolve()
OUT = Path(sys.argv[2] if len(sys.argv) > 2 else REPO / "build/tests").resolve()
OUT.mkdir(parents=True, exist_ok=True)


def run(args, *, expected=0):
    result = subprocess.run(
        list(map(str, args)), cwd=REPO, text=True, capture_output=True, timeout=120
    )
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr
    )
    return result


SOURCE = r'''enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO

fn first(chunk: Range) -> Int:
  return chunk[0]

fn second(chunk: Range) -> Int:
  return chunk[1]

fn stable(r: Range) -> Int:
  var total = 0
  for i in range(0, r.length()):
    total = total + r[i]
  return total

fn positive_step(r: Range) -> Int:
  var total = 0
  for i in range(1, r.length(), 2):
    total = total + r[i]
  return total

fn shifted(r: Range) -> Int:
  var total = 0
  for i in range(0, r.length()):
    total = total + r[i + 1]
  return total

fn different(r: Range) -> Int:
  t = r.slice(0, r.length())
  var total = 0
  for i in range(0, r.length()):
    total = total + t[i]
  return total

fn rebound(r: Range) -> Int:
  var s = r.slice(0, r.length())
  var total = 0
  for i in range(0, s.length()):
    total = total + s[i]
    s = s.slice(1, s.length())
  return total

fn extended_bound(r: Range) -> Int:
  var total = 0
  for i in range(0, r.length() + 1):
    total = total + r[i]
  return total

fn add(acc: Int, value: Int) -> Int:
  return acc + value

domain Worker:
  fn Check(path: String) -> Int:
    file = FileIO.open(path, ro)
    a = file.chunks(2) |> map(first) |> reduce(0, add)
    b = file.chunks(2) |> map(second) |> reduce(0, add)
    file.close()
    reply a + b

fn main():
  worker = Worker()
  executor = Executor().threads(2).start()
  try:
    message worker.Check("tmp/phase21c-branch-errors.data")
  recover:
    pass
  executor.join()
'''

source = OUT / "phase21c_branch_errors.moss"
source.write_text(SOURCE)
run([COMPILER, "check", source, "--json"])


def effects(name):
    data = json.loads(run([
        COMPILER, "effects", name, "--source", source, "--json"
    ]).stdout)
    assert data["ok"], data
    return data["result"]["observable_effects"]


# P21-10B is a local fact. Exact same-Range indexing, including a positive
# literal step, discharges only that index panic. Counterexamples stay
# conservatively panicking.
assert effects("stable")["may_panic"] is False
assert effects("positive_step")["may_panic"] is False
for name in ("shifted", "different", "rebound", "extended_bound"):
    assert effects(name)["may_panic"] is True, (name, effects(name))

# P21-10A is invocation-local: the generic helper remains MAY_PANIC, while
# only its nonempty chunk[0] stage becomes Branch eligible. chunk[1] retains
# its panic and uses the sequential fallback.
assert effects("first")["may_panic"] is True
rust = OUT / "phase21c_branch_errors.rs"
ir = run([COMPILER, source, "--dump-functional-ir", "-o", rust]).stdout
assert re.search(
    r"callable=first MAY_PANIC[\s\S]*?chunk-plan:[^\n]*eligible=yes "
    r"reason=[^\n]*contextual nonempty chunk\[0\] proof", ir
), ir
assert re.search(
    r"callable=second MAY_PANIC[\s\S]*?chunk-plan:[^\n]*eligible=no "
    r"reason=map stage is not pure: possible panic ordering", ir
), ir

# Actual generated bounded-K code carries explicit Value/Raised/EOF outcomes,
# uses checked FileIO reads, verifies each logical index at commit, and cannot
# inspect the first result slot before joining the complete window. The
# FileIO owner closes only after that joined commit region.
text = rust.read_text()
scope_match = re.search(r"let (__moss_chunk_scope_\d+) = branch_scope_new_current\(\);", text)
assert scope_match, text
scope = scope_match.group(1)
join_at = text.index("branch_join(" + scope + ");")
observe = re.search(r"let __moss_chunk_outcome_index = match &", text[join_at:])
assert observe, text[join_at:]
observe_at = join_at + observe.start()
assert join_at < observe_at
assert re.search(
    r"if __moss_chunk_outcome_index != __moss_chunk_next_\d+"
    r"\.checked_add\(__moss_chunk_lane_\d+ as i64\)"
    r"\.unwrap_or\(i64::MAX\) \{ std::process::abort\(\); \}",
    text[observe_at:]
), text[observe_at:]
assert re.search(
    r"let __moss_chunk_index_\d+ = __moss_chunk_next_\d+"
    r"\.checked_add\(__moss_chunk_lane_\d+ as i64\)"
    r"\.unwrap_or\(i64::MAX\);", text[scope_match.start():join_at]
), text[scope_match.start():join_at]
assert re.search(
    r"match __moss_chunk_index_\d+\.checked_mul\(__moss_chunk_size_\d+\)"
    r" \{\s*None => __MossChunkOutcome_\d+::Eof", text[scope_match.start():join_at]
), text[scope_match.start():join_at]
assert "MossFileError" in text
assert "::Raised { index:" in text
assert "::Eof { index:" in text
assert "::Value { index:" in text
assert ".read_checked(" in text
assert ".read(" not in text[scope_match.start():join_at]
raise_at = text.index(
    "return Err(__moss_raise_fileio(error))",
    observe_at,
)
assert raise_at > join_at
close_at = text.find(".close();", observe_at)
assert close_at > observe_at, (join_at, observe_at, close_at)


def runtime_preamble(generated):
    start = generated.find("struct MossAbortOnUnwind")
    end_marker = "// ─── End Phase 20 Executor Runtime"
    end = generated.find(end_marker, start)
    assert start >= 0 and end >= 0, "executor runtime markers missing"
    return generated[start:generated.index("\n", end) + 1]


# Deterministic native outcome harness. Every Branch publishes one indexed
# Value/Raised/EOF value. The parent joins before slot access, commits in index
# order, lets a parent-fold error beat later speculative errors, and drops all
# later owned outcomes. ReadToken drops prove zero live Branch read tokens at
# every terminal decision.
fixture = "#![allow(dead_code, unused_variables)]\n" + runtime_preamble(text) + r'''

#[derive(Clone, Copy)]
enum Spec { Value(i64), Short(i64), Raised(&'static str), Eof }

struct ReadToken(std::sync::Arc<std::sync::atomic::AtomicUsize>);
impl Drop for ReadToken {
    fn drop(&mut self) {
        self.0.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    }
}

struct RaisedPayload {
    name: &'static str,
    drops: std::sync::Arc<std::sync::atomic::AtomicUsize>,
}
impl Drop for RaisedPayload {
    fn drop(&mut self) {
        self.drops.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    }
}

enum ChunkOutcome {
    Value { index: i64, value: i64, short: bool },
    Raised { index: i64, error: RaisedPayload },
    Eof { index: i64 },
}

fn outcome_index(value: &ChunkOutcome) -> i64 {
    match value {
        ChunkOutcome::Value { index, .. } => *index,
        ChunkOutcome::Raised { index, .. } => *index,
        ChunkOutcome::Eof { index } => *index,
    }
}

fn window(specs: Vec<Spec>, fold_raise: Option<i64>) -> (String, usize, usize, usize) {
    let token_drops = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let error_drops = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let mapper_calls = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let scope = branch_scope_new_current();
    let mut slots = Vec::new();
    for (lane, spec) in specs.into_iter().enumerate() {
        let slot = std::sync::Arc::new(std::sync::Mutex::new(None));
        let output = std::sync::Arc::clone(&slot);
        let td = std::sync::Arc::clone(&token_drops);
        let ed = std::sync::Arc::clone(&error_drops);
        let mc = std::sync::Arc::clone(&mapper_calls);
        branch_publish(&scope, move || {
            let _read_token = ReadToken(td);
            let outcome = match spec {
                Spec::Value(value) => {
                    mc.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    ChunkOutcome::Value { index: lane as i64, value, short: false }
                }
                Spec::Short(value) => {
                    mc.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    ChunkOutcome::Value { index: lane as i64, value, short: true }
                }
                Spec::Raised(name) => ChunkOutcome::Raised {
                    index: lane as i64,
                    error: RaisedPayload { name, drops: ed },
                },
                Spec::Eof => ChunkOutcome::Eof { index: lane as i64 },
            };
            *output.lock().unwrap_or_else(|_| std::process::abort()) = Some(outcome);
        });
        slots.push(slot);
    }
    branch_join(scope);
    assert_eq!(token_drops.load(std::sync::atomic::Ordering::SeqCst), slots.len());

    let mut decision = String::from("complete");
    let mut sum = 0i64;
    for (lane, slot) in slots.into_iter().enumerate() {
        let outcome = slot.lock().unwrap_or_else(|_| std::process::abort())
            .take().unwrap_or_else(|| std::process::abort());
        assert_eq!(outcome_index(&outcome), lane as i64);
        match outcome {
            ChunkOutcome::Value { index, value, short } => {
                if fold_raise == Some(index) {
                    decision = format!("fold:{}", index);
                    break;
                }
                sum += value;
                if short {
                    decision = format!("short:{}:{}", index, sum);
                    break;
                }
            }
            ChunkOutcome::Raised { index, error } => {
                decision = format!("raised:{}:{}", index, error.name);
                break;
            }
            ChunkOutcome::Eof { index } => {
                decision = format!("eof:{}:{}", index, sum);
                break;
            }
        }
    }
    if decision == "complete" { decision = format!("complete:{}", sum); }
    (
        decision,
        token_drops.load(std::sync::atomic::Ordering::SeqCst),
        error_drops.load(std::sync::atomic::Ordering::SeqCst),
        mapper_calls.load(std::sync::atomic::Ordering::SeqCst),
    )
}

fn main() {
    let executor = MossExecutor::new().threads(4).start();
    for _ in 0..32 {
        let lowest = runtime_invoke(|| window(vec![
            Spec::Value(1), Spec::Raised("low"), Spec::Raised("late"), Spec::Eof
        ], None));
        assert_eq!(lowest, (String::from("raised:1:low"), 4, 2, 1));

        let fold = runtime_invoke(|| window(vec![
            Spec::Value(1), Spec::Raised("later"), Spec::Value(3), Spec::Eof
        ], Some(0)));
        assert_eq!(fold, (String::from("fold:0"), 4, 1, 2));

        let eof = runtime_invoke(|| window(vec![
            Spec::Eof, Spec::Raised("discard"), Spec::Value(3), Spec::Value(4)
        ], None));
        assert_eq!(eof, (String::from("eof:0:0"), 4, 1, 2));

        let short = runtime_invoke(|| window(vec![
            Spec::Value(2), Spec::Short(1), Spec::Raised("discard"), Spec::Value(4)
        ], None));
        assert_eq!(short, (String::from("short:1:3"), 4, 1, 3));
    }
    executor.join();
    println!("phase21c ordered outcomes passed");
}
'''

fixture_rs = OUT / "phase21c_ordered_outcomes.rs"
fixture_rs.write_text(fixture)
fixture_bin = OUT / "phase21c_ordered_outcomes"
run(["rustc", "-D", "warnings", fixture_rs, "-o", fixture_bin])
assert run([fixture_bin]).stdout == "phase21c ordered outcomes passed\n"

# Compile the actual bounded-K lowering with a mapper that raises at two
# indices and a parent fold that raises before either mapper error commits.
# This catches an accidental Result<T, E> value in a Branch slot or parent
# fold, which a handwritten outcome harness cannot detect.
native_source = REPO / "tests/tooling/fixtures/phase21c_native_ordered_raise.moss"
native_data = REPO / "tmp/phase21c-native-ordered-raise.data"
native_data.write_bytes(b"ABCD")
run([COMPILER, "check", native_source, "--json"])
native_rust = OUT / "phase21c_native_ordered_raise.rs"
native_ir = run([COMPILER, native_source, "--dump-functional-ir", "-o",
                 native_rust]).stdout
assert native_ir.count("chunk-plan:") == 2 and \
    native_ir.count("eligible=yes") >= 2, native_ir
native_text = native_rust.read_text()
assert "match mark(&(" in native_text and "Err(error) =>" in native_text
assert "return Err(error)" in native_text
assert re.search(r"first_fold\([^\n]+\)\?;", native_text), native_text
native_bin = OUT / "phase21c_native_ordered_raise"
run(["rustc", "--edition=2021", "-D", "warnings", native_rust,
     "-o", native_bin])
for _ in range(32):
    assert run([native_bin]).stdout == "11\n21\n"

# Exercise a real checked FileIO read fault while bounded-K Branch reads are
# outstanding. Every byte maps successfully, so whichever lane consumes the
# one-shot fault, the first Root must recover FileError.IO. The second Root
# proves that the executor and FileIO owner remain usable after quiescence.
native_data.write_bytes(b"AAAA")
fault_marker = "    // Moss line 48: try:"
between_roots = "    // Moss line 56: try:"
before_join = "    // Moss line 64: executor.join()"
for marker in (fault_marker, between_roots, before_join):
    assert native_text.count(marker) == 1, marker
fault_text = native_text.replace(
    fault_marker,
    "    moss_inject_fileio_fault(MossFileIoFaultSite::ReadBefore, "
    "MossFileError::IO, 0);\n" + fault_marker,
    1,
).replace(
    between_roots,
    "    assert_eq!(moss_live_branch_scopes(), 0);\n" + between_roots,
    1,
).replace(
    before_join,
    "    assert_eq!(moss_live_branch_scopes(), 0);\n" + before_join,
    1,
)
fault_rs = OUT / "phase21c_native_read_fault.rs"
fault_rs.write_text(fault_text)
fault_bin = OUT / "phase21c_native_read_fault"
run(["rustc", "--edition=2021", "-D", "warnings", "--cfg", "moss_perf",
     fault_rs, "-o", fault_bin])
for _ in range(32):
    assert run([fault_bin]).stdout == "13\n21\n"

print("phase21c branch-error checks passed")
