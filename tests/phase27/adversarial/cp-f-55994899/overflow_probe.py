#!/usr/bin/env python3
"""Re-run the checked wrapping-message-argument regression on the frozen pair."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


EXPECTED = "-9223372036854775808\n"


def run(command: list[str], root: Path, mode: str, timeout: int = 120) -> dict:
    env = os.environ.copy(); env["MOSS_EST"] = mode
    try:
        result = subprocess.run(command, cwd=root, env=env, text=True,
                                capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        return {"kind": "timeout", "stdout": exc.stdout or "",
                "stderr": exc.stderr or "", "timeout_seconds": timeout}
    return {"kind": "process", "returncode": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate_root", type=Path)
    parser.add_argument("source")
    parser.add_argument("baseline_compiler", type=Path)
    parser.add_argument("candidate_compiler", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = args.candidate_root.resolve()
    baseline, candidate = args.baseline_compiler.resolve(), args.candidate_compiler.resolve()
    source = (root / args.source).resolve()
    if not source.is_file():
        raise SystemExit(f"missing source probe: {source}")
    evidence_source = root / "tests/phase27/adversarial/cp-f-55994899/probes/wrapping_message_argument.moss"
    evidence_source.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, evidence_source)
    relative_probe = evidence_source.relative_to(root).as_posix()
    results = {"checks": {}, "fast_debug": {}, "native": {}}
    for label, compiler, mode in (("baseline_off", baseline, "off"),
                                  ("candidate_off", candidate, "off"),
                                  ("candidate_shadow", candidate, "shadow")):
        results["checks"][label] = run([str(compiler), "check", relative_probe, "--json"],
                                       root, mode)
    for label, compiler, mode in (("baseline_off", baseline, "off"),
                                  ("candidate_shadow", candidate, "shadow")):
        results["fast_debug"][label] = run([str(compiler), "run", "--interp", relative_probe],
                                           root, mode)
    scratch = root / "tmp/phase27-3/cp-f-55994899/overflow"
    scratch.mkdir(parents=True, exist_ok=True)
    for label, compiler in (("baseline", baseline), ("candidate", candidate)):
        for moss_profile, moss_flags in (("default", []), ("optimized", ["-O"])):
            rust_path = scratch / f"{label}-{moss_profile}.rs"
            env = os.environ.copy(); env["MOSS_EST"] = "off"
            emitted = run([str(compiler), relative_probe, *moss_flags, "-o", str(rust_path)],
                          root, "off")
            if emitted["kind"] != "process" or emitted["returncode"] or not rust_path.is_file():
                results["native"][f"{label}:{moss_profile}"] = {"emit": emitted,
                                                                 "rustc": None, "run": None}
                continue
            for rustc_profile, rustc_flags in (("default", []), ("optimized", ["-O"])):
                binary = scratch / f"{label}-{moss_profile}-{rustc_profile}"
                compiled = run(["rustc", "--edition=2021", *rustc_flags,
                                str(rust_path), "-o", str(binary)], root, "off")
                native = (run([str(binary)], root, "off")
                          if compiled["kind"] == "process" and compiled["returncode"] == 0 else None)
                results["native"][f"{label}:{moss_profile}:{rustc_profile}"] = {
                    "emit": {"kind": emitted["kind"], "returncode": emitted["returncode"],
                             "stderr": emitted["stderr"]},
                    "rustc": compiled, "run": native}
            rust_path.unlink(missing_ok=True)
    values = []
    for engine, group in (("fast_debug", results["fast_debug"]),
                          ("native", results["native"])):
        for name, result in group.items():
            if engine == "native":
                result = result.get("run")
            if result and result.get("kind") == "process":
                values.append({"engine": f"{engine}:{name}", "returncode": result["returncode"],
                               "stdout": result["stdout"]})
    errors = []
    if any(value["returncode"] != 0 or value["stdout"] != EXPECTED for value in values):
        errors.append({"kind": "wrong_output", "values": values})
    if len(values) != 10:
        errors.append({"kind": "coverage", "expected_engine_outputs": 10,
                       "observed_engine_outputs": len(values)})
    report = {"source": args.source, "copied_probe": relative_probe,
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "expected_stdout": EXPECTED,
              "baseline_compiler_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
              "candidate_compiler_sha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
              "checks": results["checks"], "fast_debug": results["fast_debug"],
              "native": results["native"], "observed_outputs": values,
              "errors": errors}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"probe={relative_probe} outputs={len(values)} errors={len(errors)}")
    print(f"report={args.out}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
