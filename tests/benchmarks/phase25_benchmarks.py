#!/usr/bin/env python3
"""G5-05: bounded Map/Set scaling across Fast Debug, native release and Margo bench.

The separate G4-05 Unicode benchmark supplies the String traversal half of
G5-05. This suite exercises insert, lookup and eager key/value iteration in
real Moss source. Ratios compare a 10x input increase; the generous 30x
ceiling rejects quadratic behavior without claiming stable nanosecond timing.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import statistics
import subprocess
import time


ROOT = Path(__file__).resolve().parents[2]
MOSS = ROOT / "moss"
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp/phase25-g5-map-set-benchmarks"
RUNS = 3
MAX_TENFOLD_RATIO = 30.0

SOURCE = """module app
fn main():
  var m = Map()
  var s = Set()
  var i = 0
  while i < SIZE:
    m[i] = i
    s.insert(i)
    i = i + 1
  var total = 0
  var j = 0
  while j < SIZE:
    if s.contains(j):
      total = total + m.get(j, 0)
    j = j + 1
  let map_keys = m.keys()
  let set_values = s.values()
  for key in map_keys:
    total = total + key
  for key in set_values:
    total = total + key
  echo total
  echo map_keys |> count
  echo set_values |> count
"""

BENCH = """bench "Map and Set insert lookup snapshot":
  var m = Map()
  var s = Set()
  var i = 0
  while i < 1000:
    m[i] = i
    s.insert(i)
    i = i + 1
  assert(m.get(999, 0) == 999)
  assert(s.contains(999))
  assert((m.keys() |> count) == 1000)
  assert((s.values() |> count) == 1000)
"""


def environment() -> dict[str, str]:
    env = dict(os.environ)
    env.update(MOSS=str(MOSS), MOSS_STAGED_ABI="v10",
               MARGO_HOME=str(ROOT / "tmp/phase25-g5-margo-home"),
               TMPDIR=str(ROOT / "tmp"), PYTHONDONTWRITEBYTECODE="1")
    return env


def package(size: int) -> tuple[Path, Path]:
    root = SCRATCH / f"size_{size}"
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "phase25_mapset_{size}"\nversion = "0.1.0"\n')
    source = root / "src/main.moss"
    source.write_text(SOURCE.replace("SIZE", str(size)))
    return root, source


def expected(size: int) -> str:
    return f"{3 * size * (size - 1) // 2}\n{size}\n{size}\n"


def timed(command: list[str], *, cwd: Path, size: int) -> float:
    samples = []
    for _ in range(RUNS):
        start = time.perf_counter()
        result = subprocess.run(command, cwd=cwd, env=environment(),
                                text=True, capture_output=True, check=False,
                                timeout=120)
        samples.append(time.perf_counter() - start)
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stdout == expected(size), (size, result.stdout)
    return statistics.median(samples)


def native_binary(root: Path) -> Path:
    result = subprocess.run([str(MARGO), "build", "--release", "--json"],
                            cwd=root, env=environment(), text=True,
                            capture_output=True, check=False, timeout=300)
    assert result.returncode == 0, result.stdout + result.stderr
    report = json.loads(result.stdout)
    assert report["ok"] is True, report
    executable = Path(report["artifacts"]["artifacts"]["executable"])
    assert executable.is_file() and executable.is_relative_to(root)
    return executable


def scaling(profile: str, sizes: tuple[int, int]) -> None:
    elapsed = []
    for size in sizes:
        root, source = package(size)
        if profile == "Fast Debug":
            command = [str(MOSS), "run", "--interp", str(source)]
        else:
            command = [str(native_binary(root))]
        elapsed.append(timed(command, cwd=root, size=size))
    ratio = elapsed[1] / max(elapsed[0], 0.000001)
    assert ratio <= MAX_TENFOLD_RATIO, (
        f"G5-05 {profile} 10x Map/Set workload grew {ratio:.2f}x "
        f"({elapsed[0]:.6f}s -> {elapsed[1]:.6f}s), expected <= {MAX_TENFOLD_RATIO}x")
    print(f"G5-05 {profile} Map/Set 10x workload ratio {ratio:.2f} PASS")


def margo_bench() -> None:
    root, _ = package(1000)
    (root / "benches").mkdir(exist_ok=True)
    (root / "benches/map_set.moss").write_text(BENCH)
    result = subprocess.run([str(MARGO), "bench", "--json"], cwd=root,
                            env=environment(), text=True, capture_output=True,
                            check=False, timeout=300)
    assert result.returncode == 0, result.stdout + result.stderr
    # A successful command must name the exact benchmark, rather than merely
    # returning an empty success report for an undiscovered bench directory.
    assert "Map and Set insert lookup snapshot" in result.stdout, result.stdout
    print("G5-05 Margo Map/Set benchmark executes PASS")


def main() -> None:
    assert MOSS.is_file() and MARGO.is_file()
    SCRATCH.mkdir(parents=True, exist_ok=True)
    scaling("Fast Debug", (1000, 10000))
    scaling("Native Release", (10000, 100000))
    margo_bench()
    print("G5-05 Map/Set end-to-end benchmark suite PASS")


if __name__ == "__main__":
    main()
