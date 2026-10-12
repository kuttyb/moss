#!/usr/bin/env python3
"""Generate an independent, deterministic Foundations consumer oracle.

All generated Moss, native artifacts, and logs stay in the caller's --workdir.
"""

import argparse
import hashlib
import json
import random
import shutil
import subprocess
from pathlib import Path


SEED = 240024


def vector(values):
    if not values:
        return "Vector[Int]()"
    return "[" + ", ".join(map(str, values)) + "]"


def oracle_source(quick=False):
    rng = random.Random(SEED)
    cases = [[], [0], [1, 1], [-7, 4, 0], [7] * 17,
             list(range(32)), list(range(31, -1, -1)),
             [-9223372036854775807, 9223372036854775807, 0, -1, 1]]
    sizes = [3, 16, 64] if quick else [0, 1, 2, 3, 5, 8, 16, 31, 64, 127, 256, 513]
    for n in sizes:
        for _ in range(1 if quick else 3):
            cases.append([rng.randrange(-1000, 1001) for _ in range(n)])
    lines = ["module oracle", "import core", "import bytes", "import collections",
             "import algorithms", "", "fn main():"]
    for number, values in enumerate(cases):
        expected = sorted(values)
        lines += [f"  var actual_{number} = {vector(values)}",
                  f"  expected_{number} = {vector(expected)}",
                  f"  algorithms.sort_int(actual_{number})",
                  f"  assert((actual_{number} |> count) == (expected_{number} |> count))",
                  f"  var i_{number} = 0",
                  f"  while i_{number} < (expected_{number} |> count):",
                  f"    assert(actual_{number}[i_{number}] == expected_{number}[i_{number}])",
                  f"    i_{number} = i_{number} + 1"]
        targets = [-1001, -7, -1, 0, 1, 7, 1001]
        if values:
            targets += [expected[0], expected[len(expected) // 2], expected[-1]]
        for target in sorted(set(targets)):
            lb = next((i for i, value in enumerate(expected) if value >= target), len(expected))
            found = lb if lb < len(expected) and expected[lb] == target else -1
            first = next((i for i, value in enumerate(expected) if value == target), -1)
            occurrences = expected.count(target)
            lines += [f"  assert(algorithms.lower_bound_int(actual_{number}, {target}) == {lb})",
                      f"  assert(algorithms.binary_search_int(actual_{number}, {target}) == {found})",
                      f"  assert(collections.index_of(actual_{number}, {target}) == {first})",
                      f"  assert(collections.count_equal(actual_{number}, {target}) == {occurrences})"]
    for number, size in enumerate([0, 1, 2, 3, 7, 31, 256, 1025]):
        values = [rng.randrange(256) for _ in range(size)]
        lines += [f"  original_bytes_{number} = {vector(values)}",
                  f"  var copied_bytes_{number} = bytes.copy_bytes(original_bytes_{number})",
                  f"  assert((copied_bytes_{number} |> count) == {size})",
                  f"  var b_{number} = 0",
                  f"  while b_{number} < {size}:",
                  f"    assert(copied_bytes_{number}[b_{number}] == original_bytes_{number}[b_{number}])",
                  f"    b_{number} = b_{number} + 1"]
        if size:
            lines += [f"  assert(bytes.set_if_valid(copied_bytes_{number}, 0, 42))",
                      f"  assert(copied_bytes_{number}[0] == 42)",
                      f"  assert(original_bytes_{number}[0] == {values[0]})"]
    lines += [f'  echo "oracle ok {len(cases)} sort cases 8 byte cases"', ""]
    return "\n".join(lines), len(cases)


def run(command, cwd, env, expected="oracle ok"):
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode or (expected and expected not in result.stdout):
        raise RuntimeError(f"{command}: exit {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--foundations-root", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--margo", type=Path, required=True)
    parser.add_argument("--quick", action="store_true", help="short corpus for make check")
    args = parser.parse_args()
    foundation = args.foundations_root.resolve()
    workdir = args.workdir.resolve()
    repository_tmp = Path(__file__).resolve().parents[3] / "tmp"
    if not workdir.is_relative_to(repository_tmp) or workdir == repository_tmp:
        parser.error("workdir must be a child of this repository's tmp directory")
    if not all((foundation / name / "Moss.toml").exists() for name in ("core", "bytes", "collections", "algorithms")):
        parser.error("foundations root must contain all four packages")
    source, count = oracle_source(args.quick)
    digest = hashlib.sha256(source.encode()).hexdigest()
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "src").mkdir(parents=True)
    (workdir / "src" / "main.moss").write_text(source)
    dependencies = "\n".join(f'"moss.{name}" = {{ path = {json.dumps(str(foundation / name))} }}'
                             for name in ("core", "bytes", "collections", "algorithms"))
    (workdir / "Moss.toml").write_text('[package]\nname = "foundations_oracle"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n\n[dependencies]\n' + dependencies + "\n")
    import os
    env = os.environ.copy()
    env["PATH"] = str(args.margo.resolve().parent) + os.pathsep + env.get("PATH", "")
    env["TMPDIR"] = str(workdir)
    margo = str(args.margo.resolve())
    results = {"seed": SEED, "cases": count, "quick": args.quick, "source_sha256": digest,
               "margo": margo, "foundations_root": str(foundation)}
    for name, command in (("native_o0", [margo, "run"]),
                          ("native_release", [margo, "run", "--release"]),
                          ("fast_debug", [margo, "debug"])):
        results[name] = run(command, workdir, env)
    print(json.dumps(results, sort_keys=True))


if __name__ == "__main__":
    main()
