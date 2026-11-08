#!/usr/bin/env python3
"""Pin the three explicit Map-key copies to their exact Rust variants."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tmp" / "phase28-map-key-copy-occurrences"
CLONE = re.compile(r"\.clone\(\)|\.cloned\(\)|\.to_owned\(\)|\.to_vec\(\)|Clone::clone")
MOSS_LINE = re.compile(r"//\s*Moss line (\d+):\s*(.*)")
SITES = {
    "examples/swarm/Python/Counter/src/main.moss": (
        12,
        {
            ("method:Counter.increment_by", "Counter::increment_by"),
            ("method:Counter.increment_by", "MossAccess_Counter::increment_by"),
        },
    ),
    "examples/swarm/Surface/repros/swarm_061b_map_get_before_write.moss": (
        5,
        {("main", "main")},
    ),
    "examples/swarm/modules/text_toolkit/toolkit/src/stats.moss": (
        104,
        {("fn:stats__tally", "stats__tally")},
    ),
}
MUTATIONS = (
    "MOSS_TEST_DROP_COPY_FACT",
    "MOSS_TEST_DUPLICATE_COPY_FACT",
    "MOSS_TEST_PERTURB_COPY_FACT",
)


def compile_source(compiler: Path, source: Path, output: Path,
                   mode: str, mutation: str | None = None) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.pop("MOSS_EST", None)
    if mode == "shadow":
        env["MOSS_EST"] = "shadow"
    for name in MUTATIONS:
        env.pop(name, None)
    if mutation:
        env[mutation] = "1"
    output.parent.mkdir(parents=True, exist_ok=True)
    return subprocess.run(
        [str(compiler), str(source), "-o", str(output)],
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
        timeout=60,
    )


def generated_site_map(source: Path, rust: Path, source_line: int):
    source_lines = source.read_text(errors="replace").splitlines()
    if not 0 < source_line <= len(source_lines) or "copy(" not in source_lines[source_line - 1]:
        raise RuntimeError(f"{source}:{source_line} no longer contains the explicit copy")
    rust_line = None
    type_support = False
    identity = symbol = None
    observed = {}
    for rust_no, text in enumerate(rust.read_text(errors="replace").splitlines(), 1):
        if "// Moss tooling begin|" in text:
            fields = text.split("|")
            if len(fields) >= 5:
                identity = re.sub(r"@\d+(?:#.*)?$", "", fields[2])
                symbol = fields[3]
        marker = MOSS_LINE.search(text)
        if marker:
            rust_line = int(marker.group(1))
            head = marker.group(2).strip()
            if re.match(r"(?:export\s+)?(?:type|enum)\b", head):
                type_support = True
            elif re.match(r"(?:export\s+)?(?:fn|domain|on|message)\b", head):
                type_support = False
            continue
        if rust_line != source_line or type_support:
            continue
        clones = len(CLONE.findall(text))
        if clones:
            if "copy(" not in source_lines[source_line - 1]:
                raise RuntimeError(f"hidden clone at {source}:{source_line}")
            key = (identity, symbol)
            observed[key] = observed.get(key, 0) + clones
    return observed


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_map_key_copy_occurrences.py <moss-compiler>")
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        raise SystemExit(f"compiler not found: {compiler}")
    if OUT.exists():
        import shutil
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    for mode in ("default", "shadow"):
        for rel, (source_line, expected) in SITES.items():
            source = ROOT / rel
            output = OUT / mode / Path(rel).with_suffix(".rs")
            compiled = compile_source(compiler, source, output, mode)
            if compiled.returncode or not output.is_file():
                raise RuntimeError(
                    f"{mode} compile failed for {rel}: {compiled.stderr.strip()}"
                )
            observed = generated_site_map(source, output, source_line)
            if observed != {variant: 1 for variant in expected}:
                raise RuntimeError(
                    f"generated copy occurrence map differs for {mode} {rel}: "
                    f"expected={expected}, observed={observed}"
                )

            for mutation in MUTATIONS:
                bad_output = OUT / "mutations" / mode / mutation / Path(rel).with_suffix(".rs")
                faulted = compile_source(compiler, source, bad_output, mode, mutation)
                diagnostic = faulted.stdout + faulted.stderr
                if faulted.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in diagnostic or "copy" not in diagnostic.lower():
                    raise RuntimeError(
                        f"{mode} {mutation} did not fail closed for {rel}: "
                        f"exit={faulted.returncode} {diagnostic.strip()}"
                    )

    map_key_source = ROOT / "examples/swarm/Surface/repros/swarm_061b_map_get_before_write.moss"
    interpreted = subprocess.run(
        [str(compiler), "run", "--interp", str(map_key_source)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=60,
    )
    if interpreted.returncode or interpreted.stdout != "1\n":
        raise RuntimeError(
            f"Fast Debug Map-key copy output differs: {interpreted.stdout!r} "
            f"{interpreted.stderr.strip()}"
        )
    native_rust = OUT / "default" / Path(
        "examples/swarm/Surface/repros/swarm_061b_map_get_before_write.rs"
    )
    native_binary = OUT / "swarm-061b-native"
    built = subprocess.run(
        ["rustc", str(native_rust), "-o", str(native_binary)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=60,
    )
    if built.returncode:
        raise RuntimeError(f"Native Map-key copy Rust build failed: {built.stderr.strip()}")
    ran = subprocess.run(
        [str(native_binary)], cwd=ROOT, text=True, capture_output=True, timeout=30
    )
    if ran.returncode or ran.stdout != "1\n":
        raise RuntimeError(f"Native Map-key copy output differs: {ran.stdout!r} {ran.stderr!r}")

    counter_project = ROOT / "examples/swarm/Python/Counter"
    counter_copy = OUT / "counter-project"
    shutil.copytree(
        counter_project,
        counter_copy,
        ignore=shutil.ignore_patterns(".moss", "target", "build"),
    )
    counter_tests = subprocess.run(
        [str(ROOT / "margo"), "test"],
        cwd=counter_copy,
        text=True,
        capture_output=True,
        timeout=120,
    )
    if counter_tests.returncode or "4 passed\n0 failed" not in counter_tests.stdout:
        raise RuntimeError(
            f"Native Counter Map-key tests failed:\n{counter_tests.stdout}\n{counter_tests.stderr}"
        )

    print("Phase 28 Map-key copies: 4 Rust occurrences mapped to 3 Moss sites; "
          "18 drop/duplicate/perturb builds failed closed (Default/Shadow); "
          "Map-key outputs agree and Counter tests pass natively")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
