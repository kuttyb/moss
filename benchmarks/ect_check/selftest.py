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


STANDIN = '''\
"""SELFTEST-ONLY STAND-IN for a resident warm client. NOT a daemon, NOT a Phase 28.5
fix, never used for any baseline claim: it memoizes the whole verdict of a check by
the SHA-256 of the project tree (build dirs excluded) in an on-disk cache that
plays the role of resident state, so selftest can prove that warm mode, the
template plumbing, edit invalidation and the gate work end to end."""
import hashlib, json, os, subprocess, sys
from pathlib import Path
kind, path, root, moss, margo, cache = sys.argv[1:7]
h = hashlib.sha256()
for f in sorted(Path(root).rglob("*")):
    rel = f.relative_to(root)
    if f.is_file() and "build" not in rel.parts:
        h.update(str(rel).encode()); h.update(f.read_bytes())
key = Path(cache) / h.hexdigest()
if key.exists():
    doc = json.loads(key.read_text())
    sys.stdout.write(doc["out"]); sys.exit(doc["rc"])
env = dict(os.environ, MOSS=moss)
argv = [moss, "check", path, "--json"] if kind == "file" else [margo, "--root", path, "--check", "--json", "build"]
p = subprocess.run(argv, capture_output=True, text=True, env=env)
Path(cache).mkdir(parents=True, exist_ok=True)
key.write_text(json.dumps({"out": p.stdout, "rc": p.returncode}))
sys.stdout.write(p.stdout); sys.exit(p.returncode)
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

    scenario_tests(R, tc, manifest, scratch, results)
    shutil.rmtree(scratch / "work", ignore_errors=True)
    print(f"selftest: {sum(results)}/{len(results)} passed")
    return 0 if all(results) else 1


def scenario_tests(R, tc, manifest, scratch, results) -> None:
    """Edit sequences, patches, the stand-in warm client and worktrees."""
    import os
    import subprocess
    import scenarios as S
    fx_by = {f["id"]: f for f in manifest["fixtures"]}
    sc_by = {s["id"]: s for s in manifest["scenarios"]}
    sc = sc_by["b3_dag_a"]
    fx = fx_by[sc["fixture"]]
    patch = sc["patches"]["a"]

    # --- frozen patches: exact-once, hashes, byte-identical revert -----------
    work = scratch / "patch"
    C.stage_fixture(fx, work)
    C.apply_patch(work, patch, "forward")
    changed = C.tree_sha256(work) != fx["sha256"]
    C.apply_patch(work, patch, "backward")
    check("patch: forward changes the tree, backward restores it byte-identically",
          changed and C.tree_sha256(work) == fx["sha256"], "tree hash", results)
    rel = patch["edits"][0]["file"]
    C.stage_fixture(fx, work)
    (work / rel).write_text((work / rel).read_text() + "\n# drift\n")
    try:
        C.apply_patch(work, patch, "forward")
        check("patch: refused on a drifted pre-image", False, "applied", results)
    except C.ToolError as exc:
        check("patch: refused on a drifted pre-image", "pre-image hash" in str(exc), str(exc)[:70], results)
    C.stage_fixture(fx, work)
    doubled = (work / rel).read_text() + patch["edits"][0]["old"]
    (work / rel).write_text(doubled)
    forged = dict(patch, before_sha256={rel: C.text_sha256(doubled)})   # hash matches: only the count can object
    try:
        C.apply_patch(work, forged, "forward")
        check("patch: refused when the old text matches twice", False, "applied", results)
    except C.ToolError as exc:
        check("patch: refused when the old text matches twice", "exactly one" in str(exc), str(exc)[:70], results)
    shutil.rmtree(work, ignore_errors=True)

    # --- edit sequence, cold, on the real reference compiler ------------------
    rows = S.measure_scenario(sc, fx, tc, None, "cold", 3, 1, scratch / "seq")
    ok = [r["status_ok"] for r in rows] == [True] * 3 and \
        all(r["functions_analyzed"] == 81 for r in rows)
    check("sequence: cold b3_dag_a rows pass and count 81 callables per check", ok,
          str([(r["step"], round(r["wall_ms_median"])) for r in rows]), results)

    # --- stand-in warm client (SELFTEST ONLY) ---------------------------------
    standin = scratch / "standin.py"
    standin.write_text(STANDIN)
    cache = scratch / "standin-cache"
    shutil.rmtree(cache, ignore_errors=True)
    template = (f"python3 {shlex.quote(str(standin))} {{kind}} {{path}} {{root}} {{moss}} {{margo}} "
                f"{shlex.quote(str(cache))}")
    warm = S.measure_scenario(sc, fx, tc, template, "warm", 3, 1, scratch / "seqw")
    faster = all(w["wall_ms_median"] < 0.5 * c["wall_ms_median"] for w, c in zip(warm, rows))
    check("warm plumbing: stand-in client rows exist, faster than cold, statuses ok (SELFTEST ONLY)",
          faster and all(w["status_ok"] for w in warm) and all(w["mode"] == "warm" for w in warm),
          str([(w["step"], round(w["wall_ms_median"], 1)) for w in warm]), results)
    gate = R.run_gate([], tc, tc, template, scratch / "gate-seq", [sc])
    check("gate: stand-in client passes the edit sequence", gate["passed"], "identical verdicts per step", results)
    wrapper = scratch / "client.py"
    mut = R.run_gate([], tc, tc, f"python3 {shlex.quote(str(wrapper))} {{kind}} {{path}} {{moss}} {{margo}} mutate",
                     scratch / "gate-seq-mut", [sc])
    check("gate: mutating client fails the edit sequence", not mut["passed"], "extra field detected", results)

    # --- group runner ---------------------------------------------------------
    env = tc.env()
    conc = C.run_measured_group([(["/bin/sleep", "0.4"], scratch)] * 3, env=env)
    seq = [C.run_measured(["/bin/sleep", "0.4"], cwd=scratch, env=env) for _ in range(3)]
    check("group: three 0.4 s sleeps run concurrently (wall ~0.4 s) vs sequentially (~1.2 s)",
          conc["group_wall_ms"] < 800 and sum(r["wall_ms"] for r in seq) > 1150,
          f"{conc['group_wall_ms']:.0f} ms vs {sum(r['wall_ms'] for r in seq):.0f} ms", results)
    check("group: summed RSS is the sum of per-process peaks",
          conc["sum_rss_kib"] == sum(r["rss_kib"] for r in conc["results"]) and 0 < conc["sum_rss_kib"] < 3 * 20000,
          f"{conc['sum_rss_kib']} KiB", results)
    marker = "4712"
    tg = C.run_measured_group([(["/bin/sh", "-c", f"sleep {marker} & wait"], scratch)] * 2, env=env, timeout=1.0)
    import time as _t
    _t.sleep(0.3)
    left = subprocess.run(["pgrep", "-f", f"sleep {marker}"], capture_output=True, text=True).stdout.split()
    check("group: timeout kills every process group", tg["timed_out"] and not left,
          f"timed_out={tg['timed_out']} survivors={left}", results)
    if left:
        subprocess.run(["kill", *left])

    # --- worktrees: deterministic repo, real worktrees, clean teardown --------
    wsc = sc_by["b5_wt_dag"]
    wfx = fx_by[wsc["fixture"]]
    main_before = subprocess.run(["git", "worktree", "list"], cwd=C.REPO_ROOT, text=True,
                                 capture_output=True).stdout
    branches_before = subprocess.run(["git", "branch", "--list"], cwd=C.REPO_ROOT, text=True,
                                     capture_output=True).stdout
    repo_a, repo_b = scratch / "wt-a" / "repo", scratch / "wt-b" / "repo"
    c1, c2 = S.make_fixture_repo(wsc, wfx, repo_a), S.make_fixture_repo(wsc, wfx, repo_b)
    check("worktrees: fixture repository commit id is deterministic", c1 == c2 and len(c1) == 40, c1[:12], results)
    g = S.run_worktree_group(wsc, wfx, tc, None, repo_a, scratch / "wt-a" / "wt", 3, True, 0)
    listed = subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=repo_a, text=True,
                            capture_output=True).stdout.count("worktree ")
    main_after = subprocess.run(["git", "worktree", "list"], cwd=C.REPO_ROOT, text=True,
                                capture_output=True).stdout
    branches_after = subprocess.run(["git", "branch", "--list"], cwd=C.REPO_ROOT, text=True,
                                    capture_output=True).stdout
    check("worktrees: 3 concurrent real worktrees pass, all removed afterwards, main repo untouched",
          g["all_ok"] and g["n"] == 3 and listed == 1 and not (scratch / "wt-a" / "wt" / "wt00").exists()
          and main_before == main_after and branches_before == branches_after,
          f"registered after={listed - 1} wall={g['group_wall_ms']:.0f} ms", results)
    # distinct edits per worktree, different content across reps
    d0, d1 = scratch / "slot0", scratch / "slot1"
    for d in (d0, d1):
        C.stage_fixture(wfx, d)
    S.apply_slot(wsc, d0, 0, 0)
    S.apply_slot(wsc, d1, 0, 1)
    t0, t1 = (d0 / wsc["slots"][0]["file"]).read_text(), (d1 / wsc["slots"][0]["file"]).read_text()
    check("worktrees: slot edit differs between reps (content-addressed clients see new divergence)",
          t0 != t1 and t0 != (C.FIXTURES_DIR / wfx["path"] / wsc["slots"][0]["file"]).read_text(),
          "rep-salted value", results)
    for d in (d0, d1, scratch / "wt-a", scratch / "wt-b"):
        shutil.rmtree(d, ignore_errors=True)
