#!/usr/bin/env python3
"""Reproduce CP-B mutation, rewrite, optimization, and ABI-off probes.

Run from this checkout as:
  python3 tests/phase27/adversarial/cp-b/review_probes.py ./moss \
    tmp/cp-b-baseline/moss

All generated project data and Rust output stays under ./tmp/.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
COMPILER = Path(sys.argv[1]).resolve()
BASELINE = Path(sys.argv[2]).resolve()
WORK = ROOT / "tmp" / "cp-b-probes"


def load_ratchet(relative, name):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.ratchet


def expect_rejection(ratchet, observed, allowlist):
    try:
        ratchet(observed, allowlist)
    except AssertionError:
        return
    raise AssertionError("ratchet accepted an injected violation or stale allowlist entry")


def run(args, cwd, env):
    return subprocess.run(args, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def make_project():
    library = WORK / "library"
    app = WORK / "app"
    (library / "src").mkdir(parents=True, exist_ok=True)
    (app / "src").mkdir(parents=True, exist_ok=True)
    (library / "Moss.toml").write_text(
        '[package]\nname="cp_b_library"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n', encoding="utf-8")
    (library / "src" / "cp_b_library.moss").write_text(
        "module cp_b_library\n"
        "fn local(value):\n  return value + 1\n"
        "export fn generic(value):\n  return local(value)\n",
        encoding="utf-8")
    (app / "Moss.toml").write_text(
        '[package]\nname="cp_b_app"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n'
        '\n[dependencies]\ncp_b_library = { path = "../library" }\n',
        encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        "module cp_b_app\nimport cp_b_library\n"
        "fn main():\n  echo cp_b_library.generic(3)\n", encoding="utf-8")
    return library, app


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    ratchets = (
        load_ratchet("tests/tooling/check_phase27_accepts_rustc.py", "accepts_gate"),
        load_ratchet("tests/tooling/check_phase27_three_engine.py", "three_engine_gate"),
    )
    for ratchet in ratchets:
        ratchet({"seed": "known"}, {"seed"})
        expect_rejection(ratchet, {"new": "injected"}, set())
        expect_rejection(ratchet, {}, {"fixed"})
    print("ratchet mutations: both gates reject injected violations and fixed stale entries")

    library, app = make_project()
    dump = WORK / "rewrite.dump"
    env = os.environ.copy()
    env.update({"MOSS": str(COMPILER), "MOSS_EST": "shadow",
                "MOSS_EST_DUMP": "1", "MOSS_EST_CONTRACT_TEST": "1",
                "MOSS_EST_DUMP_FILE": str(dump)})
    built = run([str(ROOT / "margo"), "build", "--check", "--json"], app, env)
    if built.returncode:
        raise SystemExit("rewrite package build failed:\n" + built.stdout + built.stderr)
    lines = dump.read_text(encoding="utf-8").splitlines()
    rewritten = [line for line in lines if '"cp_b_library__local"' in line]
    if not rewritten or not all("origin=0 " not in line for line in rewritten):
        raise SystemExit("module-rewritten EST handle missing or has zero origin")
    checked = run([str(COMPILER), "debug", str(app / "src" / "main.moss")], app,
                  {**env, "MOSS_MODULE_PATH": str(library / "build" / "check-debug")})
    if checked.returncode or checked.stdout != "4\n":
        raise SystemExit("source-free imported Fast Debug failed:\n" + checked.stdout + checked.stderr)

    optimized = WORK / "syntax_27_1b.rs"
    opt_env = {**os.environ, "MOSS_EST": "shadow", "MOSS_EST_CONTRACT_TEST": "1"}
    emitted = run([str(COMPILER), "-O", str(ROOT / "tests/phase27/est/syntax_27_1b.moss"),
                   "-o", str(optimized)], ROOT, opt_env)
    if emitted.returncode or not optimized.exists():
        raise SystemExit("optimized EST fixture failed:\n" + emitted.stdout + emitted.stderr)
    print("module rewrite + imported Fast Debug + -O EST contract probes passed")

    # Use one shared source/project path for both compilers so absolute checkout
    # path differences cannot explain an interface hash difference.
    abi = WORK / "abi"
    lib = abi / "library"
    app = abi / "app"
    for path in (lib / "src", app / "src"):
        path.mkdir(parents=True, exist_ok=True)
    (lib / "Moss.toml").write_text(
        '[package]\nname="cp_b_abi_library"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n', encoding="utf-8")
    (lib / "src" / "cp_b_abi_library.moss").write_text(
        "module cp_b_abi_library\n"
        "export fn generic(value):\n  return value + 1\n"
        "export fn concrete(value: Int) -> Int:\n  return value * 2 + 1\n",
        encoding="utf-8")
    (app / "Moss.toml").write_text(
        '[package]\nname="cp_b_abi_app"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n'
        '\n[dependencies]\ncp_b_abi_library = { path = "../library" }\n',
        encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        "module cp_b_abi_app\nimport cp_b_abi_library\nfn main():\n"
        "  echo cp_b_abi_library.generic(3)\n"
        "  echo cp_b_abi_library.concrete(4)\n", encoding="utf-8")
    hashes = {}
    for name, compiler in (("frozen", COMPILER), ("baseline", BASELINE)):
        build_env = {**os.environ, "MOSS_EST": "off", "MOSS": str(compiler)}
        result = run([str(ROOT / "margo"), "build", "--json"], app, build_env)
        if result.returncode:
            raise SystemExit(f"{name} ABI project build failed:\n" + result.stdout + result.stderr)
        targets = (lib / "build" / "debug" / "cp_b_abi_library.mossi",
                   app / "build" / "debug" / "cp_b_abi_app.mossi")
        hashes[name] = [hashlib.sha256(path.read_bytes()).hexdigest() for path in targets]
    print(json.dumps({"mossi_sha256": hashes}, sort_keys=True))
    if hashes["frozen"] == hashes["baseline"]:
        raise SystemExit("expected CP-B's observed .mossi hash mismatch was not reproduced")
    print("confirmed: MOSS_EST=off .mossi hashes differ from 4d6d0c8 on same project path")


if __name__ == "__main__":
    main()
