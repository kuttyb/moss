#!/usr/bin/env python3
"""Close-time ENOSPC reaches a Moss FileError.Full recovery arm."""

from pathlib import Path
import json
import os
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
SOURCE = ROOT / "tests/tooling/fixtures/phase21_fileio_close_full.moss"
SCRATCH = ROOT / "tmp/phase21-fileio-close-full"
SCRATCH.mkdir(parents=True, exist_ok=True)
DATA = SCRATCH / "input.dat"
RUST = SCRATCH / "program.rs"
EXECUTABLE = SCRATCH / "program"
DATA.write_bytes(b"content")


def run(*command):
    result = subprocess.run(
        [str(part) for part in command], cwd=ROOT, capture_output=True,
        text=True, timeout=180, env={**os.environ, "TMPDIR": str(SCRATCH)})
    assert result.returncode == 0, (command, result.returncode,
                                    result.stdout, result.stderr)
    return result


checked = json.loads(run(COMPILER, "check", SOURCE, "--json").stdout)
assert checked["ok"] and not any(
    item["code"] == "RECOVER_UNREACHABLE_VARIANT"
    for item in checked["result"]["diagnostics"]), checked

run(COMPILER, SOURCE, "-o", RUST)
with RUST.open("a") as output:
    output.write(r'''

#[cfg(test)]
mod close_full_moss {
    use super::*;
    use std::os::unix::io::RawFd;

    extern "C" { fn __errno_location() -> *mut i32; }

    fn no_space_after_close(fd: RawFd) -> i32 {
        assert_eq!(unsafe { moss_fileio::posix::close(fd) }, 0);
        unsafe { *__errno_location() = moss_fileio::posix::ENOSPC; }
        -1
    }

    fn worker() -> WorkerRef {
        construct_worker(Default::default(), "test::worker", 0,
            "domain-specialization:Worker:worker::handler:Open",
            "domain-specialization:Worker:worker::handler:Close")
    }

    #[test]
    fn close_full_recovery() {
        moss_set_solo_hooks(executor_fileio_solo_enter_callback(),
                            executor_fileio_solo_leave_callback());
        let path = "tmp/phase21-fileio-close-full/input.dat".to_string();
        let normal = worker();
        normal.Open_shared(&path).unwrap();
        assert_eq!(normal.Close_shared(), Some(1));

        let failing = worker();
        failing.Open_shared(&path).unwrap();
        moss_set_close_override(no_space_after_close);
        let outcome = failing.Close_shared();
        moss_clear_close_override();
        assert_eq!(outcome, Some(4));
        let reopened = FileIO::open_checked(&path, "ro").unwrap();
        reopened.close_checked().unwrap();
    }
}
''')
run("rustc", "--edition=2021", "-Dwarnings", "--test", "--cfg",
    "moss_perf", RUST, "-o", EXECUTABLE)
run(EXECUTABLE, "--test-threads=1")
print("Phase 21 close-time Full Moss recovery passed")
