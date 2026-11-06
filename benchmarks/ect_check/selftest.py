"""Self-tests for the harness itself: RSS independence from the runner's size,
gate sensitivity (identity passes, a mutating client fails), template errors."""
from __future__ import annotations

import json
from pathlib import Path
import shlex
import shutil
import sys

import ect_common as C

WRAPPER = '''\
import json, os, subprocess, sys
kind, path, moss, margo, mode = sys.argv[1:6]
env = dict(os.environ, MOSS=moss)
if kind == "file":
    argv = [moss, "check", path, "--json"]
else:
    argv = [margo, "--root", path, "--check", "--json", "build"]
p = subprocess.run(argv, capture_output=True, text=True, env=env)
out = p.stdout
if mode == "mutate":
    doc = json.loads(out)
    if doc.get("ok"):
        doc.setdefault("result", {})["extra_field"] = 1
    else:
        doc["extra_field"] = 1
    out = json.dumps(doc)
sys.stdout.write(out)
sys.exit(p.returncode)
'''


def check(name: str, ok: bool, detail: str, results: list) -> None:
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")


def run(moss: Path, margo: Path) -> int:
    import run as R
    results: list[bool] = []
    tc = C.Toolchain(moss, margo)
    manifest = C.load_manifest()
    fx = next(f for f in manifest["fixtures"] if f["id"] == "trivial_file_pass")
    work = C.TMP_ROOT / "selftest" / "work"
    values = C.stage_fixture(fx, work)
    argv = C.command_for(fx, {**tc.values(), **values}, None)

    def rss_median(command, n=5):
        vals = [C.run_measured(command, cwd=work, env=tc.env())["rss_kib"] for _ in range(n)]
        return C.median(vals)

    # --- RSS independence from the runner's own size ---------------------
    before_true = rss_median(["/bin/true"])
    before_moss = rss_median(argv)
    ballast = bytearray(400 * 1024 * 1024)
    for i in range(0, len(ballast), 4096):   # touch every page
        ballast[i] = 1
    after_true = rss_median(["/bin/true"])
    after_moss = rss_median(argv)
    check("rss: /bin/true unaffected by 400 MiB runner", abs(after_true - before_true) < 1024 and after_true < 20000,
          f"{before_true:.0f} -> {after_true:.0f} KiB", results)
    check("rss: moss check unaffected by 400 MiB runner",
          abs(after_moss - before_moss) <= max(512, 0.03 * before_moss),
          f"{before_moss:.0f} -> {after_moss:.0f} KiB", results)
    del ballast

    # --- timeout kills the whole process group --------------------------
    import subprocess
    marker = "4711"
    res = C.run_measured(["/bin/sh", "-c", f"sleep {marker} & wait"], cwd=work,
                         env=tc.env(), timeout=1.0)
    import time as _t
    _t.sleep(0.3)
    left = subprocess.run(["pgrep", "-f", f"sleep {marker}"], capture_output=True, text=True).stdout.split()
    check("timeout: flagged and no orphan child survives", res["timed_out"] and not left,
          f"timed_out={res['timed_out']} survivors={left}", results)
    if left:
        subprocess.run(["kill", *left])

    # --- manifest enforcement: timeout / exit code ------------------------
    fx_fail = {"expected_status": "fail", "expected_exit_code": 139}
    check("fixture_ok: timeout never satisfies expected fail",
          not C.fixture_ok({"expected_status": "fail"}, {"ok": None, "exit_code": -9, "timed_out": True}),
          "timed_out verdict rejected", results)
    check("fixture_ok: clean diagnostic does not satisfy expected crash",
          not C.fixture_ok(fx_fail, {"ok": False, "exit_code": 1}) and
          C.fixture_ok(fx_fail, {"ok": None, "exit_code": 139}), "exit code enforced", results)

    # --- stack_kib: same input crashes at 8 MiB, passes with a large stack -
    manifest0 = C.load_manifest()
    crash = next(f for f in manifest0["fixtures"] if f["id"].endswith("default_stack_crash"))
    values_c = C.stage_fixture(crash, scratch_stack := C.TMP_ROOT / "selftest" / "stack")
    argv_c = C.command_for(crash, {**tc.values(), **values_c}, None)
    small = C.run_measured(argv_c, cwd=scratch_stack, env=tc.env(), timeout=300, stack_kib=8192)
    large = C.run_measured(argv_c, cwd=scratch_stack, env=tc.env(), timeout=300, stack_kib=C.fixture_stack(
        next(f for f in manifest0["fixtures"] if f["id"] == "tx_w4_d0500")))
    check("stack_kib: depth-300 chain crashes at 8 MiB, passes with the torture stack",
          small["rc"] == 139 and large["rc"] == 0, f"rc {small['rc']} -> {large['rc']}", results)
    shutil.rmtree(scratch_stack, ignore_errors=True)

    # --- gate sensitivity ------------------------------------------------
    scratch = C.TMP_ROOT / "selftest"
    scratch.mkdir(parents=True, exist_ok=True)
    wrapper = scratch / "client.py"
    wrapper.write_text(WRAPPER)
    # Gate self-tests need a representative cheap subset (smoke fixtures plus one
    # package-with-provider and one file DAG fixture), not the multi-minute set.
    cheap = {"b1_d005_w1_k001", "b2_k01", "diamond_d006_w4"}
    fixtures = [f for f in manifest["fixtures"] if f["category"] == "smoke" or f["id"] in cheap]

    def template(mode):
        return (f"python3 {shlex.quote(str(wrapper))} {{kind}} {{path}} {{moss}} {{margo}} {mode}")

    for mode, expect in (("identity", True), ("mutate", False)):
        gate = R.run_gate(fixtures, tc, tc, template(mode), scratch / f"gate-{mode}")
        check(f"gate: {mode} client {'passes' if expect else 'fails'}",
              gate["passed"] == expect,
              f"passed={gate['passed']} identical="
              f"{[r['identical'] for r in gate['results']]}", results)
    gate = R.run_gate(fixtures, tc, tc, None, scratch / "gate-builtin")
    fail_pkg = next(r for r in gate["results"] if r["fixture"] == "trivial_pkg_fail")
    embedded = bool(fail_pkg["reference"]["diagnostics"]) and \
        isinstance(fail_pkg["reference"]["diagnostics"][0], dict) and \
        "code" in fail_pkg["reference"]["diagnostics"][0]
    check("gate: margo embedded error parsed structurally (trivial_pkg_fail)", embedded,
          str(fail_pkg["reference"]["diagnostics"])[:90], results)

    # --- broken templates fail with one clear line ------------------------
    for label, tmpl in (("unknown placeholder", "{moss} check {nope}"),
                        ("missing binary", "/nonexistent/binary {path}")):
        try:
            R.run_gate(fixtures[:1], tc, tc, tmpl, scratch / "gate-bad")
            check(f"template error: {label}", False, "no error raised", results)
        except C.ToolError as exc:
            check(f"template error: {label}", "\n" not in str(exc), str(exc)[:100], results)

    shutil.rmtree(scratch / "work", ignore_errors=True)
    print(f"selftest: {sum(results)}/{len(results)} passed")
    return 0 if all(results) else 1
