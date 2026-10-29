#!/usr/bin/env python3
"""Independent CP-B checks for a frozen Phase 27 compiler checkout.

Usage: python3 tests/phase27/adversarial/cp-b-final/recheck.py \
  ./build/compiler/moss-dev ../cp-b-final-baseline/build/compiler/moss-dev
All generated projects and output stay under tmp/cp-b-final/.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
COMPILER = Path(sys.argv[1]).resolve()
BASELINE = Path(sys.argv[2]).resolve()
WORK = ROOT / "tmp" / "cp-b-final"


def run(args, env=None, cwd=ROOT):
    result = subprocess.run(args, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {args}\n{result.stdout}")
    return result.stdout


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    output = []
    focused = run([sys.executable, str(ROOT / "tests/tooling/check_phase27_est.py"),
                   str(COMPILER)])
    output.append("focused EST contract:\n" + focused)
    mossi = run([sys.executable, str(ROOT / "tests/tooling/check_phase27_est_mossi.py"),
                 str(COMPILER)])
    output.append("same-binary raw .mossi + imported Section 1/2 EST:\n" + mossi)
    baseline = run([sys.executable, str(ROOT / "tests/tooling/check_phase27_mossi_baseline.py"),
                    str(COMPILER), str(BASELINE)])
    output.append("baseline-identity recomputation:\n" + baseline)

    optimized = WORK / "syntax-optimized.rs"
    env = os.environ.copy()
    env.update({"MOSS_EST": "shadow", "MOSS_EST_CONTRACT_TEST": "1"})
    optimized_output = run([str(COMPILER), "-O",
                            str(ROOT / "tests/phase27/est/syntax_27_1b.moss"),
                            "-o", str(optimized)], env=env)
    if not optimized.is_file():
        raise AssertionError("-O did not emit the requested artifact")
    output.append("optimized compile:\n" + optimized_output + f"artifact={optimized.relative_to(ROOT)}\n")

    # A source package merge exercises the module-name rewrite while retaining
    # EST handles. The imported provider exercise above separately loads .mossi.
    library, app = WORK / "rewrite-library", WORK / "rewrite-app"
    for path in (library / "src", app / "src"):
        path.mkdir(parents=True, exist_ok=True)
    (library / "Moss.toml").write_text(
        '[package]\nname="cp_b_final_library"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n', encoding="utf-8")
    (library / "src/cp_b_final_library.moss").write_text(
        "module cp_b_final_library\n"
        "fn local(value):\n  return value + 1\n"
        "export fn generic(value):\n  return local(value)\n", encoding="utf-8")
    (app / "Moss.toml").write_text(
        '[package]\nname="cp_b_final_app"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n'
        '\n[dependencies]\ncp_b_final_library = { path = "../rewrite-library" }\n',
        encoding="utf-8")
    (app / "src/main.moss").write_text(
        "module cp_b_final_app\nimport cp_b_final_library\n"
        "fn main():\n  echo cp_b_final_library.generic(3)\n", encoding="utf-8")
    dump = WORK / "rewrite.dump"
    env = os.environ.copy()
    env.update({"MOSS": str(COMPILER), "MOSS_EST": "shadow",
                "MOSS_EST_CONTRACT_TEST": "1", "MOSS_EST_DUMP": "1",
                "MOSS_EST_DUMP_FILE": str(dump)})
    merged = run([str(ROOT / "margo"), "build", "--check", "--json"],
                 env=env, cwd=app)
    dump_text = dump.read_text(encoding="utf-8")
    rewritten = [line for line in dump_text.splitlines()
                 if '"cp_b_final_library__local"' in line]
    if not rewritten or any("origin=0 " in line for line in rewritten):
        raise AssertionError("module rewritten EST occurrence missing or has no source origin")
    output.append("module merge/rewrite:\n" + merged +
                  f"rewritten_occurrences={len(rewritten)}\n")

    log = WORK / "recheck.log"
    log.write_text("\n".join(output), encoding="utf-8")
    print(f"CP-B recheck passed; log={log.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
