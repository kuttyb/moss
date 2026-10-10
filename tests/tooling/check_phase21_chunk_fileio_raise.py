#!/usr/bin/env python3
"""Check native chunk read errors propagate through a helper to recovery."""

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/tooling/fixtures/phase21_chunk_fileio_raise.moss"
OUTPUT = ROOT / "tmp/phase21-chunk-raise"
DATA = ROOT / "tmp/phase21-chunk-fileio-raise.data"


def run(*args):
    return subprocess.run(args, cwd=ROOT, check=True, capture_output=True, text=True)


def main():
    compiler = sys.argv[1] if len(sys.argv) > 1 else "./moss"
    OUTPUT.mkdir(parents=True, exist_ok=True)
    DATA.write_bytes(b"abcd")
    generated = OUTPUT / "generated.rs"
    run(compiler, str(SOURCE), "-o", str(generated))
    native = generated.read_text()
    marker = '#[export_name = "moss__main"]\nfn main() {'
    assert native.count(marker) == 1
    assert "return Err(__moss_raise_fileio(error))" in native

    normal_binary = OUTPUT / "normal"
    run("rustc", "-D", "warnings", "--cfg", "moss_perf", str(generated),
        "-o", str(normal_binary))
    assert run(str(normal_binary)).stdout == "4\n"

    injected = OUTPUT / "injected.rs"
    injected.write_text(native.replace(
        marker,
        marker + "\n    moss_inject_fileio_fault("
        "MossFileIoFaultSite::ReadBefore, MossFileError::IO, 0);",
        1,
    ))
    injected_binary = OUTPUT / "injected"
    run("rustc", "-D", "warnings", "--cfg", "moss_perf", str(injected),
        "-o", str(injected_binary))
    assert run(str(injected_binary)).stdout == "99\n"
    print("Phase 21 native chunk FileIO raise propagation passed")


if __name__ == "__main__":
    main()
