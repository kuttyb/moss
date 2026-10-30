#!/usr/bin/env python3
"""Compare the overflow probe across phase27-2 and frozen Phase 27.3 engines."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[4]
source = Path(__file__).with_name("wrapping-message-argument.moss").resolve()
baseline, candidate = map(lambda x: Path(x).resolve(), sys.argv[1:3])
out_dir = root / "tmp/cp-f-final-cache/cross-engine"
out_dir.mkdir(parents=True, exist_ok=True)


def run(argv, env, label):
    clean_argv = []
    for arg in argv:
        if arg == str(baseline):
            clean_argv.append("<baseline>/moss")
        elif arg == str(candidate):
            clean_argv.append("<candidate>/moss")
        else:
            try:
                clean_argv.append(Path(arg).resolve().relative_to(root).as_posix())
            except (ValueError, OSError):
                clean_argv.append(arg)
    try:
        p = subprocess.run(argv, cwd=root, env=env, text=True,
                           capture_output=True, timeout=90)
        stdout = p.stdout.replace(str(root), "<REVIEW_ROOT>")
        stderr = p.stderr.replace(str(root), "<REVIEW_ROOT>")
        stdout = stdout.replace(str(baseline.parent), "<BASELINE_ROOT>")
        stderr = stderr.replace(str(baseline.parent), "<BASELINE_ROOT>")
        return {"argv": clean_argv, "exit": p.returncode,
                "stdout": stdout, "stderr": stderr}
    except subprocess.TimeoutExpired as e:
        return {"argv": clean_argv, "timeout": 90,
                "stdout": e.stdout or "", "stderr": e.stderr or ""}


rows = {}
for label, compiler in (("baseline", baseline), ("candidate", candidate)):
    env = os.environ.copy()
    env["MOSS_EST"] = "off"
    item = {
        "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
        "check": run([str(compiler), "check", str(source), "--json"], env, label),
        "interpreter": run([str(compiler), "run", "--interp", str(source)], env, label),
        "fast_debug": run([str(compiler), "debug", str(source)], env, label),
    }
    for profile, extra in (("default", []), ("optimized", ["-O"])):
        rust = out_dir / f"{label}-{profile}.rs"
        binary = out_dir / f"{label}-{profile}.bin"
        emit = run([str(compiler), *extra, str(source), "-o", str(rust)], env, label)
        record = {"emit": emit}
        if rust.is_file():
            rust_bytes = rust.read_bytes()
            record["rust_sha256"] = hashlib.sha256(rust_bytes).hexdigest()
            record["rustc"] = run(["rustc", "--edition=2021", str(rust), "-o", str(binary)], env, label)
            if record["rustc"]["exit"] == 0:
                record["native"] = run([str(binary)], env, label)
        item[profile] = record
    rows[label] = item

result = {"source": source.relative_to(root).as_posix(),
          "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
          "baseline_sha": "04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4",
          "candidate_sha": "2bdc2ea16cff57882dcfb45f99b5c2df39d6d98f",
          "results": rows}
path = root / "tests/phase27/adversarial/cp-f-phase27-3-final-cache/cross-engine.json"
path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
print(json.dumps({label: {
    "check_exit": item["check"]["exit"],
    "interp": [item["interpreter"]["exit"], item["interpreter"]["stdout"]],
    "fast_debug": [item["fast_debug"]["exit"], item["fast_debug"]["stdout"]],
    "default_native": item["default"].get("native", {}).get("stdout"),
    "optimized_native": item["optimized"].get("native", {}).get("stdout"),
} for label, item in rows.items()}, indent=2))
