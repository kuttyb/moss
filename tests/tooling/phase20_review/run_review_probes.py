#!/usr/bin/env python3
"""Phase 20 review probes (review of phase-20-integration at 2f62205).

Usage:
  python3 tests/tooling/phase20_review/run_review_probes.py [COMPILER]
      [--strict] [--only SUBSTRING] [--json-out FILE]

COMPILER defaults to ./moss (run `make` first). Paths resolve against the
repository root, so the script can be run from anywhere. Each probe listed in
manifest.json is checked with `moss check --json`; accepted probes that carry
a `run` expectation are lowered to Rust, compiled with `rustc -O -D warnings`,
and executed from the repository root against fresh data in
tmp/phase20-review/.

Categories:
  guard         fixed by the corrective pass; must keep passing
  bug           crash, invalid Rust, wrong answer, or contradiction of the
                checked-in spec
  pre-existing  reproduces before Phase 20 (d3cdc52); fails only with --strict
  design        depends on an open design decision; reported, never failed

Exit status is 1 when any guard or bug probe fails (and pre-existing probes
too with --strict).
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]

DATA_FILES = {
    "input.txt": b"hello world this is a test file with some words\n",
    "input2.txt": b"second file\n",
    "binary.dat": bytes([0xFF, 0xFE, 0x00, 0x41]),
    "store.dat": b"",
}


def first_line(text, limit=200):
    for line in text.strip().splitlines():
        line = line.strip()
        if line:
            return line[:limit]
    return ""


def rustc_error(text):
    for line in text.splitlines():
        if line.startswith("error"):
            return line.strip()[:200]
    return first_line(text)


def reset_data(data):
    out = data / "out"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    for name, content in DATA_FILES.items():
        (data / name).write_bytes(content)


def check(compiler, probe):
    result = subprocess.run([str(compiler), "check", str(probe), "--json"],
                            cwd=REPO, capture_output=True, text=True,
                            timeout=120)
    try:
        doc = json.loads(result.stdout)
    except ValueError:
        text = result.stderr or result.stdout
        location = re.search(r"(src/[\w./]+:\d+)", text)
        assertion = re.search(r"Assertion `(.+?)' failed", text)
        if assertion:
            detail = "assertion `%s` at %s" % (
                assertion.group(1), location.group(1) if location else "?")
        else:
            detail = first_line(text) or "exit %d" % result.returncode
        return {"status": "crash", "detail": detail,
                "diagnostics": [], "errors": []}
    diagnostics = list((doc.get("result") or {}).get("diagnostics") or [])
    if doc.get("error"):
        diagnostics.append(doc["error"])
    errors = [d for d in diagnostics if d.get("severity", "error") == "error"]
    return {"status": "reject" if errors else "accept",
            "diagnostics": diagnostics, "errors": errors}


def build_and_run(compiler, probe, build):
    build.mkdir(parents=True, exist_ok=True)
    rust = build / (probe.stem + ".rs")
    binary = build / probe.stem
    lowered = subprocess.run([str(compiler), str(probe), "-o", str(rust)],
                             cwd=REPO, capture_output=True, text=True,
                             timeout=120)
    if lowered.returncode != 0:
        return {"stage": "codegen", "detail": first_line(lowered.stderr or lowered.stdout)}
    compiled = subprocess.run(["rustc", "-O", "-D", "warnings", str(rust),
                               "-o", str(binary)],
                              cwd=REPO, capture_output=True, text=True,
                              timeout=600)
    if compiled.returncode != 0:
        return {"stage": "rustc", "detail": rustc_error(compiled.stderr)}
    executed = subprocess.run([str(binary)], cwd=REPO, capture_output=True,
                              text=True, timeout=120)
    return {"stage": "run", "exit": executed.returncode,
            "stdout": executed.stdout, "stderr": executed.stderr}


def describe_check(result):
    if result["status"] == "crash":
        return "checker crashed: " + result["detail"]
    if result["status"] == "reject":
        error = result["errors"][0]
        return "rejected %s: %s" % (error.get("code"), first_line(error.get("message", "")))
    return "accepted"


def describe_run(run):
    if run is None:
        return ""
    if run["stage"] != "run":
        return "%s failed: %s" % (run["stage"], run["detail"])
    text = "exit %d" % run["exit"]
    stdout = run["stdout"].strip()
    if stdout:
        text += ", stdout %r" % stdout
    if run["stderr"].strip():
        text += ", stderr %r" % first_line(run["stderr"], 120)
    return text


def evaluate(entry, checked, run):
    """Return a list of unmet expectations (empty means PASS)."""
    want = entry.get("check", "accept")
    if checked["status"] == "crash":
        return [describe_check(checked)]
    if checked["status"] != want:
        if checked["status"] == "reject":
            return [describe_check(checked)]
        return ["accepted; expected rejection %s" % entry.get("code", "")]
    problems = []
    if want == "reject":
        code = entry.get("code")
        if code and not any(e.get("code") == code for e in checked["errors"]):
            problems.append("rejected with %s; expected %s"
                            % (checked["errors"][0].get("code"), code))
        return problems
    warning = entry.get("warning")
    if warning and not any(d.get("code") == warning and d.get("severity") == "warning"
                           for d in checked["diagnostics"]):
        problems.append("missing warning " + warning)
    expect = entry.get("run")
    if expect is None:
        return problems
    if run["stage"] != "run":
        return problems + [describe_run(run)]
    exit_want = expect.get("exit", 0)
    if exit_want == "nonzero":
        if run["exit"] == 0:
            problems.append("exit 0; expected a fail-closed abort")
    elif run["exit"] != exit_want:
        problems.append("exit %d; expected %d (%s)"
                        % (run["exit"], exit_want, first_line(run["stderr"], 120)))
    if "stdout" in expect and run["stdout"].strip() != expect["stdout"]:
        problems.append("stdout %r; expected %r" % (run["stdout"].strip(), expect["stdout"]))
    if "stdout_sorted" in expect and sorted(run["stdout"].split()) != sorted(expect["stdout_sorted"]):
        problems.append("stdout %r; expected lines %r"
                        % (run["stdout"].strip(), expect["stdout_sorted"]))
    if expect.get("stderr_nonempty") and not run["stderr"].strip():
        problems.append("no diagnostic on stderr")
    for left, right in expect.get("file_equals", []):
        a, b = REPO / left, REPO / right
        if not a.exists() or a.read_bytes() != b.read_bytes():
            problems.append("%s differs from %s" % (left, right))
    for path, content in expect.get("file_content", {}).items():
        target = REPO / path
        if not target.exists() or target.read_bytes() != content.encode():
            problems.append("%s does not contain %r" % (path, content))
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("compiler", nargs="?", default=str(REPO / "moss"))
    parser.add_argument("--strict", action="store_true",
                        help="also fail on pre-existing probes")
    parser.add_argument("--only", action="append", default=[],
                        help="run probes whose name contains SUBSTRING")
    parser.add_argument("--json-out", help="write per-probe results as JSON")
    args = parser.parse_args()

    compiler = Path(args.compiler).resolve()
    if not compiler.exists():
        sys.exit("compiler not found: %s (run make)" % compiler)
    manifest = json.loads((HERE / "manifest.json").read_text())
    data = REPO / manifest["data_dir"]
    build = data / "build"

    results, failed = [], []
    for entry in manifest["probes"]:
        if args.only and not any(s in entry["name"] for s in args.only):
            continue
        reset_data(data)
        probe = HERE / entry["file"]
        checked = check(compiler, probe)
        runnable = checked["status"] == "accept" and (
            "run" in entry or entry["category"] == "design")
        run = build_and_run(compiler, probe, build) if runnable else None
        category = entry["category"]
        if category == "design":
            verdict = "INFO"
            detail = describe_check(checked)
            if run is not None:
                detail += "; " + describe_run(run)
            detail += " | proposal: " + entry.get("proposal", "-")
        else:
            problems = evaluate(entry, checked, run)
            if checked["status"] == "accept" and entry.get("fast_debug_parity"):
                debug = subprocess.run([str(compiler), "run", "--interp", str(probe)],
                                       cwd=REPO, capture_output=True, text=True, timeout=120)
                if debug.returncode != 0 or run is None or run.get("stdout") != debug.stdout:
                    problems.append("native/Fast Debug parity failed")
            if checked["status"] == "accept" and entry.get("exclusive_effects"):
                effects = subprocess.run([str(compiler), "effects", entry["exclusive_effects"],
                                          "--source", str(probe), "--json"],
                                         cwd=REPO, capture_output=True, text=True, timeout=120)
                if effects.returncode != 0 or '"EXCLUSIVE"' not in effects.stdout:
                    problems.append("missing EXCLUSIVE state acquisition")
            verdict = "PASS" if not problems else "FAIL"
            detail = "; ".join(problems) if problems else ""
            gating = category in ("guard", "bug") or (args.strict and category == "pre-existing")
            if problems and gating:
                failed.append(entry["name"])
        print("%-4s  %-12s  %s%s" % (verdict, category, entry["name"],
                                     ("  -- " + detail) if detail else ""))
        sys.stdout.flush()
        results.append({"name": entry["name"], "category": category,
                        "verdict": verdict, "detail": detail,
                        "check": describe_check(checked),
                        "run": describe_run(run)})

    counts = {}
    for r in results:
        key = (r["category"], r["verdict"])
        counts[key] = counts.get(key, 0) + 1
    print("\nsummary: " + ", ".join("%s %s=%d" % (c, v, n)
                                    for (c, v), n in sorted(counts.items())))
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(results, indent=1))
    if failed:
        print("gating failures: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
