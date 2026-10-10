#!/usr/bin/env python3
"""Focused Phase 21.0 representation, CFG, wrapper-order and Rust ABI checks."""

from pathlib import Path
import os
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "tmp/phase21-shared-abi")
out.mkdir(parents=True, exist_ok=True)


def run(args, *, cwd=None):
    completed = subprocess.run(
        [str(item) for item in args], cwd=cwd, text=True,
        capture_output=True, timeout=180)
    assert completed.returncode == 0, (args, completed.stdout, completed.stderr)
    return completed


cxx = os.environ.get("CXX", "c++")
binary = out / "phase21_shared_abi_test"
run([
    cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-Isrc", "tests/tooling/phase21_shared_abi_test.cpp", "-o", binary,
], cwd=repo)
run([binary])

# The provider owns payload/capture representation. The application can read
# only a stable tag and move the opaque frame back into the selected callable.
rust = out / "provider_contract.rs"
rust.write_text(r'''#![allow(dead_code)]
mod provider {
    struct PrivateCapture(i64);
    enum PrivateRaised { ParseInvalid, FileFull }

    pub struct FailureFrame {
        tag: u32,
        raised: PrivateRaised,
        capture: PrivateCapture,
    }
    impl FailureFrame {
        pub fn tag(&self) -> u32 { self.tag }
    }
    pub enum BodyOutcome<T> { Normal(T), Raised(FailureFrame) }
    pub struct FailureState<'a> { pub repaired: &'a mut i64 }

    pub fn body(fail: bool) -> BodyOutcome<i64> {
        if fail {
            BodyOutcome::Raised(FailureFrame {
                tag: 1, raised: PrivateRaised::FileFull,
                capture: PrivateCapture(9),
            })
        } else { BodyOutcome::Normal(42) }
    }
    pub fn on_fail_file_full(
        state: &mut FailureState<'_>, frame: FailureFrame,
    ) -> i64 {
        match frame.raised { PrivateRaised::FileFull => {}, _ => unreachable!() }
        *state.repaired = frame.capture.0;
        -1
    }
}

fn application_root(fail: bool) -> i64 {
    let outcome = provider::body(fail);
    match outcome {
        provider::BodyOutcome::Normal(value) => value,
        provider::BodyOutcome::Raised(frame) => {
            // join Branches; close Root-local FileIO; release old guards
            let tag = frame.tag();
            // acquire fresh h_fail locks; execute selected arm; fulfill reply
            let mut repaired = 0;
            let mut state = provider::FailureState { repaired: &mut repaired };
            match tag {
                1 => provider::on_fail_file_full(&mut state, frame),
                _ => unreachable!(),
            }
        }
    }
}

fn application_nested(fail: bool) -> provider::BodyOutcome<i64> {
    provider::body(fail)
}

fn main() {
    assert_eq!(application_root(false), 42);
    assert_eq!(application_root(true), -1);
    assert!(matches!(application_nested(true), provider::BodyOutcome::Raised(_)));
}
''')
rust_binary = out / "provider_contract"
run(["rustc", "--edition=2021", "-D", "warnings", rust, "-o", rust_binary])
run([rust_binary])

print("Phase 21.0 effects, owned tagged ABI, exceptional CFG, wrapper order, and h_fail repartition checks passed.")
