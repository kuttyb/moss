#!/usr/bin/env python3
"""Exercise attempt/failure guard handoff and FileIO cleanup under real Roots."""

from pathlib import Path
import json
import os
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
SOURCE = ROOT / "tests/tooling/fixtures/phase21_lock_fileio_adversarial.moss"
SCRATCH = ROOT / "tmp/phase21-lock-fileio-adversarial"
SCRATCH.mkdir(parents=True, exist_ok=True)
GENERATED = SCRATCH / "test.rs"
EXECUTABLE = SCRATCH / "test"


def run(*command):
    result = subprocess.run(
        [str(part) for part in command], cwd=ROOT, capture_output=True,
        text=True, timeout=180, env={**os.environ, "TMPDIR": str(SCRATCH)})
    if result.returncode:
        raise AssertionError((command, result.returncode, result.stdout, result.stderr))
    return result


plan = json.loads(run(
    COMPILER, "effects", "handler:Worker.Fail", "--source", SOURCE,
    "--json").stdout)["result"]["synchronization_plan"]["domains"][0]
handlers = {handler["name"]: handler for handler in plan["handlers"]}
assert len(handlers["Fail"]["class_set"]) == 1, handlers
assert len(handlers["Fail$fail"]["class_set"]) == 1, handlers
assert handlers["Fail"]["class_set"] != handlers["Fail$fail"]["class_set"], handlers

run(COMPILER, SOURCE, "-o", GENERATED)
with GENERATED.open("a") as output:
    output.write(r'''

#[cfg(test)]
mod adversarial {
    use super::*;
    use std::cell::{Cell, RefCell};
    use std::os::unix::fs::MetadataExt;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::Arc;

    thread_local! {
        static PHASE: Cell<u8> = const { Cell::new(0) };
        static PATH: RefCell<Option<String>> = const { RefCell::new(None) };
    }
    static BAD_ORDER: AtomicBool = AtomicBool::new(false);
    static BAD_CLAIM: AtomicBool = AtomicBool::new(false);
    static ROOTS_COMPLETED: AtomicUsize = AtomicUsize::new(0);

    fn lock_hook(event: &str, _instance: &str, handler: &str,
                 _domain: usize, _class: usize, _exclusive: bool) {
        PHASE.with(|phase| {
            let old = phase.get();
            if event == "handler_enter" && handler.ends_with("::handler:Fail") {
                if old != 0 && old != 4 { BAD_ORDER.store(true, Ordering::SeqCst); }
                phase.set(1);
            } else if event == "handler_exit" && handler.ends_with("::handler:Fail") {
                if old != 1 { BAD_ORDER.store(true, Ordering::SeqCst); }
                phase.set(2);
            } else if event == "lock_acquire" && handler.ends_with("::pseudo-handler:Fail$fail") {
                if old != 2 { BAD_ORDER.store(true, Ordering::SeqCst); }
                PATH.with(|path| {
                    let guard = path.borrow();
                    let metadata = guard.as_ref().and_then(|p| std::fs::metadata(p).ok());
                    if metadata.as_ref().map_or(true, |m|
                            moss_fileio_registry_contains(m.dev(), m.ino())) {
                        BAD_CLAIM.store(true, Ordering::SeqCst);
                    }
                });
                phase.set(3);
            } else if event == "lock_release" && handler.ends_with("::pseudo-handler:Fail$fail") {
                if old != 3 { BAD_ORDER.store(true, Ordering::SeqCst); }
                phase.set(4);
            }
        });
    }

    fn call(worker: WorkerRef, path: String) -> i64 {
        runtime_invoke(move || {
            PATH.with(|slot| *slot.borrow_mut() = Some(path.clone()));
            let value = worker.Fail_root(path);
            PHASE.with(|phase| {
                if phase.get() != 4 { BAD_ORDER.store(true, Ordering::SeqCst); }
            });
            ROOTS_COMPLETED.fetch_add(1, Ordering::SeqCst);
            value
        })
    }

    fn call_missing(worker: WorkerRef, path: String) -> i64 {
        runtime_invoke(move || worker.Missing_shared(&path).unwrap())
    }

    fn call_read(worker: WorkerRef, path: String, batch: bool) -> i64 {
        runtime_invoke(move || if batch {
            worker.ReadBatch_shared(&path).unwrap()
        } else {
            worker.Read_shared(&path).unwrap()
        })
    }

    fn call_sync_expression(worker: WorkerRef, path: String) -> i64 {
        runtime_invoke(move || worker.SyncExpression_shared(&path).unwrap())
    }

    #[test]
    fn failure_handoff_fileio_and_concurrent_roots() {
        moss_set_solo_hooks(executor_fileio_solo_enter_callback(), executor_fileio_solo_leave_callback());
        MOSS_LOCK_HOOK.set(lock_hook).unwrap();
        let worker = construct_worker(0, 0, "adversarial::worker", 0,
            "domain-specialization:Worker:worker::handler:Fail",
            "domain-specialization:Worker:worker::handler:Missing",
            "domain-specialization:Worker:worker::handler:Read",
            "domain-specialization:Worker:worker::handler:ReadBatch",
            "domain-specialization:Worker:worker::handler:SyncExpression");
        let root = "tmp/phase21-lock-fileio-adversarial";
        std::fs::create_dir_all(root).unwrap();
        let paths: Vec<String> = (0..48).map(|i| format!("{}/{}.dat", root, i)).collect();
        for path in &paths { let _ = std::fs::remove_file(path); }

        // Secondary close failure during abandonment must preserve the
        // selected typed error and release the inode before failure locking.
        moss_inject_fileio_fault(MossFileIoFaultSite::FileClose, MossFileError::IO, 0);
        assert_eq!(call(worker.clone(), paths[0].clone()), 1);

        // A failure inside the trailer's own FileIO operation crosses a
        // nested re-raise, is caught by the outer arm, and still releases the
        // fresh failure guard before the final Root reply.
        moss_inject_fileio_fault(MossFileIoFaultSite::FileSync, MossFileError::IO, 0);
        assert_eq!(call(worker.clone(), paths[1].clone()), -1);

        // Partial write raises before the explicit source raise. It must still
        // abandon the owner and select the Full failure arm.
        moss_inject_fileio_fault(MossFileIoFaultSite::WriteAfterPrefix, MossFileError::Full, 2);
        assert_eq!(call(worker.clone(), paths[2].clone()), 3);

        let executor = MossExecutor::new().threads(4).max_threads(8).start();
        let mut clients = Vec::new();
        for path in paths.iter().skip(3) {
            let w = worker.clone();
            let p = path.clone();
            clients.push(std::thread::spawn(move || call(w, p)));
        }
        let mut replies: Vec<i64> = clients.into_iter().map(|join| join.join().unwrap()).collect();
        let mut missing_clients = Vec::new();
        for i in 0..45 {
            let w = worker.clone();
            let p = format!("{}/absent-{}.dat", root, i);
            let _ = std::fs::remove_file(&p);
            missing_clients.push(std::thread::spawn(move || call_missing(w, p)));
        }
        let missing_replies: Vec<i64> = missing_clients.into_iter()
            .map(|join| join.join().unwrap()).collect();
        executor.join();
        replies.sort_unstable();
        assert_eq!(replies, (4..=48).collect::<Vec<i64>>());
        assert_eq!(ROOTS_COMPLETED.load(Ordering::SeqCst), 48);
        assert!(missing_replies.iter().all(|value| *value == -1),
            "concurrent handler-local NotFound recovery must stay typed");
        assert!(!BAD_ORDER.load(Ordering::SeqCst), "attempt/failure lock event order");
        assert!(!BAD_CLAIM.load(Ordering::SeqCst), "Root FileIO claim survived into failure plan");

        moss_inject_fileio_fault(MossFileIoFaultSite::ReadBefore, MossFileError::IO, 0);
        assert_eq!(call_read(worker.clone(), paths[0].clone(), false), -1);
        moss_inject_fileio_fault(MossFileIoFaultSite::ReadBefore, MossFileError::IO, 0);
        assert_eq!(call_read(worker.clone(), paths[1].clone(), true), -1);
        assert_eq!(call_read(worker.clone(), paths[0].clone(), false), 4);
        assert_eq!(call_read(worker.clone(), paths[1].clone(), true), 1);
        moss_inject_fileio_fault(MossFileIoFaultSite::FileSync, MossFileError::IO, 0);
        assert_eq!(call_sync_expression(worker.clone(), paths[0].clone()), -1);
        assert_eq!(call_sync_expression(worker, paths[0].clone()), 1);
    }
}
''')
run("rustc", "--edition=2021", "-Dwarnings", "--test", "--cfg", "moss_perf",
    GENERATED, "-o", EXECUTABLE)
run(EXECUTABLE, "--test-threads=1")

# A provider-owned failure body must keep the same checked FileIO behavior
# after its Moss source is removed and the consumer is rebuilt from .mossi.
PROJECT = SCRATCH / "source-free"
(PROJECT / "src").mkdir(parents=True, exist_ok=True)
(PROJECT / "Moss.toml").write_text(
    '[project]\nname="phase21_lock_fileio_sourcefree"\nversion="0.1.0"\n')
provider = PROJECT / "src/service.moss"
provider.write_text('''module service
export enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO
export domain Worker:
  fn Run(path: String) -> Int:
    raise FileError.Full
  on_fail FileError.Full:
    try:
      file = FileIO.open(path, ro)
      file.close()
    recover:
      reply -1
    reply 1
  on_fail:
    reply -2

  fn ReadWrite(path: String) -> Int:
    try:
      file = FileIO.open(path, rw)
      chunk = file.read(0, 1)
      size = chunk.length()
      file.write(0, "y")
      file.sync()
      file.close()
      reply size
    recover FileError.NotFound:
      reply -1
    recover:
      reply -2
''')
provider_check = json.loads(run(COMPILER, "check", provider, "--json").stdout)
assert provider_check["ok"] and not any(
    item["code"] == "RECOVER_UNREACHABLE_VARIANT"
    for item in provider_check["result"]["diagnostics"]), provider_check
missing = PROJECT / "tmp/phase21-lock-fileio-adversarial/absent-sourcefree.dat"
if missing.exists():
    missing.unlink()
present = PROJECT / "tmp/phase21-lock-fileio-adversarial/present-sourcefree.dat"
present.parent.mkdir(parents=True, exist_ok=True)
present.write_bytes(b"x")
driver = ROOT / "margo"
project_env = {**os.environ, "MOSS": str(COMPILER), "TMPDIR": str(SCRATCH)}


def margo(*args):
    result = subprocess.run(
        [str(driver), *args], cwd=PROJECT, capture_output=True,
        text=True, timeout=180, env=project_env)
    if result.returncode:
        raise AssertionError((args, result.returncode, result.stdout, result.stderr))
    return result


margo("clean")
margo("build")
consumer = PROJECT / "src/main.moss"
consumer.write_text('''module app
import service
fn main():
  worker = service.Worker()
  echo message worker.Run("tmp/phase21-lock-fileio-adversarial/absent-sourcefree.dat")
  echo message worker.ReadWrite("tmp/phase21-lock-fileio-adversarial/present-sourcefree.dat")
  echo message worker.ReadWrite("tmp/phase21-lock-fileio-adversarial/absent-sourcefree.dat")
''')
assert margo("run").stdout == "-1\n1\n-1\n"
provider.rename(provider.with_suffix(".removed"))
assert margo("run").stdout == "-1\n1\n-1\n"
assert present.read_bytes() == b"y"
print("Phase 21 adversarial Root lock/FileIO native test passed")
