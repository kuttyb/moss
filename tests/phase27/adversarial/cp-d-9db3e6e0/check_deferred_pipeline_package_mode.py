#!/usr/bin/env python3
"""Compare package-mode checks for every adjudicated deferred-pipeline CP-D case."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
BASELINE = Path(sys.argv[1]).resolve()
CANDIDATE = Path(sys.argv[2]).resolve()
WORK = ROOT / "tmp/phase27-2/package-mode-cp-d"

CASES = {
    "validation_engine": {
        "source": "examples/swarm/expression_surface_2026/validation_engine/failed_attempts/04_pipeline_collection_inference.moss",
        "package": None,
    },
    "generic_single_main": {
        "source": "examples/swarm/modules/domain_services/repro/generic_pipeline/single/src/main.moss",
        "package": "examples/swarm/modules/domain_services/repro/generic_pipeline/single",
    },
    "generic_split_main": {
        "source": "examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/main.moss",
        "package": "examples/swarm/modules/domain_services/repro/generic_pipeline/split",
    },
    "generic_split_stats": {
        "source": "examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/stats.moss",
        "package": "examples/swarm/modules/domain_services/repro/generic_pipeline/split",
    },
    "method_placeholder": {
        "source": "examples/swarm/modules/lib_and_app/repro/generic_pipeline_method_placeholder/single/src/main.moss",
        "package": "examples/swarm/modules/lib_and_app/repro/generic_pipeline_method_placeholder/single",
    },
}


def make_packages() -> dict[str, Path]:
    WORK.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, case in CASES.items():
        package = WORK / name
        if package.exists():
            shutil.rmtree(package)
        if case["package"]:
            shutil.copytree(ROOT / case["package"], package,
                             ignore=shutil.ignore_patterns("build", ".mossi"))
        else:
            (package / "src").mkdir(parents=True)
            shutil.copy2(ROOT / "examples/swarm/expression_surface_2026/validation_engine/Moss.toml",
                         package / "Moss.toml")
            shutil.copy2(ROOT / case["source"], package / "src/main.moss")
        result[name] = package
    return result


def run(compiler: Path, package: Path, mode: str | None) -> dict:
    env = os.environ.copy()
    env["MOSS"] = str(compiler)
    if mode:
        env["MOSS_EST"] = mode
    else:
        env.pop("MOSS_EST", None)
    try:
        proc = subprocess.run([str(compiler), "build", "--check", "--json"],
                              cwd=package, env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=120, check=False)
        return {"exit_code": proc.returncode, "stdout": proc.stdout,
                "stderr": proc.stderr}
    except subprocess.TimeoutExpired as error:
        return {"exit_code": "TIMEOUT", "stdout": error.stdout or "",
                "stderr": error.stderr or ""}


def main() -> int:
    packages = make_packages()
    rows = []
    for name, package in packages.items():
        row = {"case": name, "source": CASES[name]["source"], "package_mode": {}}
        for label, compiler, mode in (
                ("baseline", BASELINE, None),
                ("candidate_off", CANDIDATE, "off"),
                ("candidate_shadow", CANDIDATE, "shadow")):
            row["package_mode"][label] = run(compiler, package, mode)
        results = row["package_mode"].values()
        row["all_candidates_accept"] = all(
            row["package_mode"][label]["exit_code"] == 0
            and json.loads(row["package_mode"][label]["stdout"]).get("ok") is True
            for label in ("candidate_off", "candidate_shadow"))
        row["baseline_exit"] = row["package_mode"]["baseline"]["exit_code"]
        rows.append(row)
    report = {"baseline": str(BASELINE), "candidate": str(CANDIDATE),
              "cases": rows}
    output = WORK / "report.json"
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"cases": len(rows),
                      "baseline_package_exits": [r["baseline_exit"] for r in rows],
                      "candidate_off_shadow_accept_all": all(
                          r["all_candidates_accept"] for r in rows),
                      "report": str(output.relative_to(ROOT))}, indent=2))
    return 0 if all(row["all_candidates_accept"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
