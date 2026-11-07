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
if mode == "bookkeeping":
    doc = json.loads(out)
    doc["checked"], doc["skipped"] = [], ["answered-from-store"]
    out = json.dumps(doc)
if mode == "diag":
    doc = json.loads(out)
    doc["ok"], doc["error"] = False, "boom"
    out = json.dumps(doc)
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
    replay_tests(R, tc, manifest, scratch, results)
    guard_tests(R, tc, manifest, scratch, results)
    rust_tests(R, tc, manifest, scratch, results)
    rust_gate_strength_tests(R, manifest, scratch, results)
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

    env = tc.env()
    # --- scenario gate: bookkeeping volatile, diagnostics strict ----------------
    for mode, expect in (("bookkeeping", True), ("diag", False)):
        g = R.run_gate([], tc, tc, f"python3 {shlex.quote(str(wrapper))} {{kind}} {{path}} {{moss}} {{margo}} {mode}",
                       scratch / f"gate-seq-{mode}", [sc])
        check(f"gate: client differing only in {'checked/skipped' if expect else 'the verdict/diagnostic'} "
              f"{'passes' if expect else 'fails'}", g["passed"] == expect,
              f"passed={g['passed']}", results)
    info = g  # noqa: F841
    # --- frozen hashes are re-verified by the runner ---------------------------
    import copy
    forged = copy.deepcopy(sc)
    p0 = forged["patches"]["a"]
    p0["edits"][0]["new"] = p0["edits"][0]["new"].replace("k = 2", "k = 3")
    rel0 = p0["edits"][0]["file"]
    p0["after_sha256"][rel0] = C.text_sha256((C.FIXTURES_DIR / fx["path"] / rel0).read_text()
                                             .replace(p0["edits"][0]["old"], p0["edits"][0]["new"]))
    try:
        S.verify_frozen(manifest, [], [forged])
        check("frozen: consistently forged patch with a stale scenario sha256 is refused", False, "accepted", results)
    except C.ToolError as exc:
        check("frozen: consistently forged patch with a stale scenario sha256 is refused",
              "scenario" in str(exc), str(exc)[:70], results)
    bad_fx = dict(fx, sha256="0" * 64)
    try:
        S.verify_frozen(manifest, [bad_fx], [])
        check("frozen: fixture with a wrong frozen hash is refused", False, "accepted", results)
    except C.ToolError as exc:
        check("frozen: fixture with a wrong frozen hash is refused", "fixture" in str(exc), str(exc)[:70], results)
    S.verify_frozen(manifest, [fx], [sc])
    check("frozen: genuine fixture and scenario verify", True, "ok", results)

    # --- whole-tree RSS sampler ------------------------------------------------
    r = C.run_measured(["/bin/sh", "-c", "python3 -c 'b=bytearray(150*1024*1024);import time;time.sleep(0.5)'; true"],
                       cwd=scratch, env=env, sample_tree_rss=True)
    check("sampler: whole-tree peak sees a grandchild's 150 MiB",
          r["tree_rss_peak_kib"] >= 150 * 1024 and r["tree_rss_peak_kib"] < 400 * 1024,
          f"{r['tree_rss_peak_kib'] / 1024:.0f} MiB", results)
    g2 = C.run_measured_group([(["/bin/sh", "-c", "python3 -c 'b=bytearray(100*1024*1024);import time;time.sleep(0.6)'"], scratch)] * 3,
                              env=env, sample_tree_rss=True)
    check("sampler: group peak is the simultaneous sum of the trees",
          g2["group_tree_rss_peak_kib"] >= 2.5 * 100 * 1024, f"{g2['group_tree_rss_peak_kib'] / 1024:.0f} MiB", results)

    # --- group runner ---------------------------------------------------------
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


SYN_BASE = """type Box:
  v: Int

fn leaf(b: Box) -> Int:
  return b.v

fn mid(b: Box) -> Int:
  return leaf(b) + 1

fn other(x: Int) -> Int:
  return x * 2

fn main():
  var b = Box(v: 1)
  echo mid(b)
  echo other(2)
"""


def replay_tests(R, tc, manifest, scratch, results) -> None:
    """B6 replay and summary churn: classifier on injected/comment-only edits,
    frozen spec verification, replay rows and determinism, gate plumbing."""
    import copy
    import subprocess
    import churn
    import replay as RP
    import scenarios as S

    # --- churn classifier, pure ----------------------------------------------
    def fn(access="READ", text="x", eff=False):
        return {"summary": {"types": {"t": 1}, "access": {"p": access}, "raises": {"r": 0},
                            "effects": {"io": eff}}, "text": text}
    pre = {"a": fn(), "b": fn(), "c": fn()}
    r = churn.classify(pre, {**pre, "a": fn(text="y")})
    check("churn: edited function with an unchanged summary is not a change",
          r["edited"] == ["a"] and not r["any_change"], str(r["edited"]), results)
    r = churn.classify(pre, {**pre, "a": fn(access="WRITE", text="y"), "b": fn(access="WRITE")})
    check("churn: own change and ripple are reported separately, by category",
          r["changed_own"] == {"a": ["access"]} and r["changed_other"] == {"b": ["access"]}
          and r["categories_other"] == ["access"], str(r["changed"]), results)
    r = churn.classify(pre, {"a": fn(), "b": fn(), "d": fn()})
    check("churn: added/removed functions are membership only, not summary changes",
          r["added"] == ["d"] and r["removed"] == ["c"] and r["membership_only"] and not r["any_change"],
          "membership_only", results)

    # --- churn classifier, end to end through the real queries ---------------
    syn = scratch / "churn-syn"
    shutil.rmtree(syn, ignore_errors=True)
    syn.mkdir(parents=True)
    variants = {"base": SYN_BASE,
                "comment": SYN_BASE.replace("  return b.v\n", "  # reworded comment\n  return b.v\n"),
                "inject": SYN_BASE.replace("  return b.v\n", "  b.v = b.v + 1\n  return b.v\n")}
    funcs = {}
    for name, src in variants.items():
        f = syn / f"{name}.moss"
        f.write_text(src)
        got = churn.collect_file(tc, syn, f, [])
        funcs[name] = got["functions"]
        if not got["ok"]:
            check(f"churn: queries answer on synthetic '{name}'", False, str(got["error"]), results)
    if all(len(v) == 3 for v in funcs.values()):
        c = churn.classify(funcs["base"], funcs["comment"])
        check("churn: comment-only edit is detected as an edit and ignored as a summary change",
              c["edited"] == ["entity-v1:function:leaf"] and not c["any_change"], str(c["edited"]), results)
        i = churn.classify(funcs["base"], funcs["inject"])
        check("churn: deliberately injected summary change is detected (own) and its caller ripple (other); "
              "an unrelated function is untouched",
              list(i["changed_own"]) == ["entity-v1:function:leaf"]
              and list(i["changed_other"]) == ["entity-v1:function:mid"]
              and "entity-v1:function:other" not in i["changed"] and "access" in i["categories_own"],
              str(i["changed"]), results)
    shutil.rmtree(syn, ignore_errors=True)

    # --- frozen B6 spec ---------------------------------------------------------
    steps = [s for s in manifest["scenarios"] if s["kind"] == RP.KIND]
    sc = next(s for s in steps if s["id"].startswith("b6_02_"))
    S.verify_frozen(manifest, [], [sc])
    check("replay: genuine step spec, range and git blobs verify", len(steps) == 50 and
          manifest["replay"]["count"] == 50, f"{len(steps)} steps", results)
    forged = copy.deepcopy(sc)
    forged["entries"][0]["expect_post"]["status"] = "fail"
    forged["sha256"] = C.text_sha256(C.canonical_json({k: v for k, v in forged.items() if k != "sha256"}))
    forged_manifest = copy.deepcopy(manifest)
    forged_manifest["scenarios"] = [forged if s["id"] == sc["id"] else s for s in manifest["scenarios"]]
    try:
        S.verify_frozen(forged_manifest, [], [forged])
        check("replay: consistently re-hashed forged expectation is refused by the frozen range", False, "accepted", results)
    except C.ToolError as exc:
        check("replay: consistently re-hashed forged expectation is refused by the frozen range",
              "range" in str(exc), str(exc)[:70], results)
    forged = copy.deepcopy(sc)
    forged["changes"][0]["new_blob"] = "1" * 40
    try:
        RP.verify_spec_against_git(forged)
        check("replay: spec naming a wrong blob id is refused", False, "accepted", results)
    except C.ToolError as exc:
        check("replay: spec naming a wrong blob id is refused", "blob" in str(exc), str(exc)[:70], results)
    work = scratch / "replay-apply"
    RP.materialize(sc["parent"], work)
    victim = work / sc["changes"][0]["path"]
    victim.write_text(victim.read_text() + "\n# drift\n")
    try:
        RP.apply_changes(sc, work)
        check("replay: applying onto a drifted pre-image is refused", False, "applied", results)
    except C.ToolError as exc:
        check("replay: applying onto a drifted pre-image is refused", "pre-image" in str(exc), str(exc)[:70], results)
    RP.materialize(sc["parent"], work)
    RP.apply_changes(sc, work)
    check("replay: applied tree equals the commit's tracked tree", RP.verify_tree_matches_commit(sc, work),
          "tracked paths", results)
    shutil.rmtree(work, ignore_errors=True)
    check("replay: expectation compares status, exit code and diagnostic",
          RP.expect_ok({"status": "fail", "exit_code": 1, "diag": "A|x"}, {"ok": False, "exit_code": 1,
                       "diagnostics": [{"code": "A", "message": "x"}]})
          and not RP.expect_ok({"status": "fail", "exit_code": 1, "diag": "A|x"}, {"ok": False, "exit_code": 1,
                               "diagnostics": [{"code": "A", "message": "y"}]})
          and not RP.expect_ok({"status": "pass", "exit_code": 0, "diag": None}, {"ok": True, "exit_code": 0,
                               "timed_out": True}), "diagnostic mismatch and timeout rejected", results)

    # --- replay rows, determinism --------------------------------------------
    worktrees_before = subprocess.run(["git", "worktree", "list"], cwd=C.REPO_ROOT, text=True,
                                      capture_output=True).stdout
    rows = RP.measure_step(sc, tc, None, "cold", 3, 1, scratch / "replay-rows")
    check("replay: cold rows pre and post exist, match the frozen expectation, carry counters",
          [r["step"] for r in rows] == ["pre", "post"] and all(r["status_ok"] for r in rows)
          and all(r["checker_runs"] for r in rows) and all(r["functions_analyzed"] is not None for r in rows),
          str([(r["step"], round(r["wall_ms_median"]), r["functions_analyzed"]) for r in rows]), results)
    a = RP.fingerprint_step(sc, tc, scratch / "replay-fp")
    b = RP.fingerprint_step(sc, tc, scratch / "replay-fp")
    check("replay: classification and counters repeat exactly across runs", a == b and bool(a["counters"]),
          str(a["counters"].get("post")), results)
    new_sc = next(s for s in steps if s["new_package"])
    nrows = RP.measure_step(new_sc, tc, None, "cold", 3, 1, scratch / "replay-new")
    check("replay: a commit adding a package has a post row only", [r["step"] for r in nrows] == ["post"],
          new_sc["id"], results)

    # --- gate plumbing for replay steps -----------------------------------------
    wrapper = scratch / "client.py"
    wrapper.write_text(WRAPPER)

    def tmpl(mode):
        return f"python3 {shlex.quote(str(wrapper))} {{kind}} {{path}} {{moss}} {{margo}} {mode}"
    for mode, expect in (("identity", True), ("mutate", False), ("bookkeeping", True), ("diag", False)):
        g = R.run_gate([], tc, tc, tmpl(mode), scratch / f"gate-replay-{mode}", [sc])
        check(f"gate: replay step with a {mode} client {'passes' if expect else 'fails'}",
              g["passed"] == expect, f"passed={g['passed']}", results)
    standin = scratch / "standin.py"
    standin.write_text(STANDIN)
    cache = scratch / "standin-cache-replay"
    shutil.rmtree(cache, ignore_errors=True)
    t = (f"python3 {shlex.quote(str(standin))} {{kind}} {{path}} {{root}} {{moss}} {{margo}} {shlex.quote(str(cache))}")
    g = R.run_gate([], tc, tc, t, scratch / "gate-replay-standin", [sc])
    wrows = RP.measure_step(sc, tc, t, "warm", 3, 1, scratch / "replay-warm")
    check("gate/warm: stand-in client (SELFTEST ONLY) passes the replay gate and yields warm rows",
          g["passed"] and [r["step"] for r in wrows] == ["pre", "post"] and all(r["mode"] == "warm" for r in wrows),
          str([(r["step"], round(r["wall_ms_median"], 1)) for r in wrows]), results)
    after = subprocess.run(["git", "worktree", "list"], cwd=C.REPO_ROOT, text=True, capture_output=True).stdout
    check("replay: no worktree registered or left behind in the main repository", after == worktrees_before,
          "git worktree list unchanged", results)


def guard_tests(R, tc, manifest, scratch, results) -> None:
    """Spread guard, mid-run noise blocks, reference pin, effectively_new, bench-file entry points."""
    import json
    import replay as RP

    # --- per-step spread guard (pure) -------------------------------------------
    seq = iter([[{"spread": 1.9}], [{"spread": 1.5}], [{"spread": 1.4}]])
    rows = RP.guarded_measure(lambda: next(seq))
    check("spread guard: three disturbed attempts, kept the lowest-spread one, flagged",
          rows[0]["attempts"] == 3 and rows[0]["spread"] == 1.4 and rows[0]["spread_flagged"]
          and rows[0]["attempt_spreads"] == [1.9, 1.5, 1.4], str(rows[0]), results)
    seq = iter([[{"spread": 1.6, "wall_ms_median": 1.0}], [{"spread": 1.1, "wall_ms_median": 99.0}]])
    rows = RP.guarded_measure(lambda: next(seq))
    check("spread guard: stops at the first clean attempt; selection ignores the median",
          rows[0]["attempts"] == 2 and rows[0]["wall_ms_median"] == 99.0 and not rows[0]["spread_flagged"],
          str(rows[0]), results)
    calls = []
    rows = RP.guarded_measure(lambda: calls.append(1) or [{"spread": 1.05}])
    check("spread guard: a quiet step is measured exactly once", len(calls) == 1 and rows[0]["attempts"] == 1,
          "1 attempt", results)

    # --- mid-run noise blocks (pure) --------------------------------------------
    probes = iter([100.0, 150.0, 101.0])
    measured = []
    out, log = RP.run_blocks(list(range(6)), lambda i: (measured.append(i), [{"spread": 1.0}])[1],
                             lambda: next(probes), 100.0, block=3, retries=2)
    check("mid-run noise: a drifted block is discarded and re-measured, a clean block kept",
          measured == [0, 1, 2, 3, 4, 5, 3, 4, 5] and [p["noisy"] for p in log] == [False, True, False]
          and out[3][0]["block_attempts"] == 2 and not out[3][0]["block_noisy"] and out[0][0]["block_attempts"] == 1,
          str([(p["first_step"], p["attempt"], p["noisy"]) for p in log]), results)
    measured = []
    out, log = RP.run_blocks([0, 1], lambda i: (measured.append(i), [{"spread": 1.0}])[1],
                             lambda: 300.0, 100.0, block=2, retries=2)
    check("mid-run noise: retries are bounded (3 tries) and the block is flagged noisy",
          measured == [0, 1] * 3 and out[0][0]["block_noisy"] and out[0][0]["block_attempts"] == 3,
          f"{len(log)} probes", results)

    # --- reference pin ------------------------------------------------------------
    same = scratch / "pin" / "moss-opt"
    same.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(tc.moss, same)
    pin_same = RP.reference_pin(C.Toolchain(same, tc.margo))
    other = scratch / "pin" / "moss-other"
    other.write_bytes(tc.moss.read_bytes() + b"\0")
    pin_other = RP.reference_pin(C.Toolchain(other, tc.margo))
    check("reference pin: pinned binary recognised, a different binary gets a plain re-freeze message",
          pin_same["matches"] == (C.sha256_file(tc.moss) == RP.PINNED_REFERENCE_SHA256) and not pin_other["matches"]
          and "re-frozen deliberately" in pin_other["note"] and "not that anything regressed" in pin_other["note"],
          pin_other["note"][:80], results)
    shutil.rmtree(scratch / "pin", ignore_errors=True)

    # --- effectively_new and bench-file entry points ---------------------------------
    steps = {s["index"]: s for s in manifest["scenarios"] if s["kind"] == RP.KIND}
    check("effectively_new: scaffold-without-source commits #27 #28 #34 and new packages flagged, a plain edit not",
          all(steps[i]["effectively_new"] and not steps[i]["new_package"] for i in (27, 28, 34))
          and steps[7]["new_package"] and not steps[2]["effectively_new"], "flags", results)
    sc = steps[1]
    aux = [e for e in sc["entries"] if e["kind"] == "auxfile"]
    check("bench files are entry points (checked after their package)",
          len(aux) == 3 and [e["kind"] for e in sc["entries"]][-3:] == ["auxfile"] * 3, str([e["id"] for e in aux]), results)
    work = scratch / "aux-inject"
    RP.materialize(sc["parent"], work)
    env = tc.env()
    pk = next(e for e in sc["entries"] if e["id"] == "pkg:packages/math")
    C.run_measured(RP.entry_argv(sc, pk, tc, None, work), cwd=work, env=env)
    ae = next(e for e in aux if e["path"].endswith("math_bench.moss"))
    clean = C.extract_verdict(C.run_measured(RP.entry_argv(sc, ae, tc, None, work), cwd=work, env=env), [])
    f = work / ae["path"]
    f.write_text(f.read_text() + '\nfn zz_inject() -> Int:\n  return "x"\n')
    bad = C.extract_verdict(C.run_measured(RP.entry_argv(sc, ae, tc, None, work), cwd=work, env=env), [])
    check("bench-file entry point detects an injected type error that margo --check does not see",
          clean["ok"] and not bad["ok"] and "zz_inject" in json.dumps(bad["diagnostics"]), str(RP.diag_of(bad))[:80], results)
    shutil.rmtree(work, ignore_errors=True)


MUTATE_CARGO = '''\
import os, shutil, sys, tempfile
cargo, path, scratch = sys.argv[1], sys.argv[2], sys.argv[3]
copy = os.path.join(tempfile.mkdtemp(dir=scratch), "c")     # inject a type error into a COPY (the staged tree stays intact)
shutil.copytree(path, copy)
f = os.path.join(copy, "src", "main.rs")
open(f, "a").write("\\nfn injected() -> i64 {\\n    \\"x\\"\\n}\\n")
os.execv(cargo, [cargo, "check", "--offline", "--locked", "--message-format=json",
                 "--manifest-path", os.path.join(copy, "Cargo.toml")])
'''


def rust_tests(R, tc, manifest, scratch, results) -> None:
    """Team 5: Rust fixtures/sequences, the cargo verdict gate, missing-cargo guard."""
    import os
    import random
    import rust_cmp as RC
    import rust_generators as RG
    import scenarios as S

    # --- determinism and frozen hashes (no cargo needed) ---------------------------
    outs = []
    for i in (1, 2):
        out = scratch / f"rustgen{i}"
        shutil.rmtree(out, ignore_errors=True)
        g = RG.RustDagGenerator()
        rows = g.generate(random.Random(f"{C.SUITE_SEED}:{g.name}"), out)
        outs.append(({r["id"]: C.tree_sha256(out / r["path"]) for r in rows}, g.scenarios))
        shutil.rmtree(out, ignore_errors=True)
    frozen = {f["id"]: f["sha256"] for f in manifest["rust_fixtures"]}
    frozen_sc = {s["id"]: s for s in manifest["scenarios"] if s["kind"] == RC.SEQ_KIND}
    check("rust determinism: two generations are byte-identical and equal the frozen manifest",
          outs[0] == outs[1] and outs[0][0] == frozen and {s["id"]: s for s in outs[0][1]} == frozen_sc,
          f"{len(frozen)} fixtures, {len(frozen_sc)} sequences", results)
    sc = frozen_sc["rust_dag_single_a"]
    tampered = {**sc, "description": sc["description"] + " x"}
    try:
        RC.verify_frozen(manifest, [tampered])
        check("rust frozen hash: a tampered sequence spec is refused", False, "no error", results)
    except C.ToolError as exc:
        check("rust frozen hash: a tampered sequence spec is refused", "frozen SHA-256" in str(exc), str(exc)[:80], results)
    fx = RC.fixture_by_id(manifest, sc["fixture"])
    work = scratch / "rustfz"
    C.stage_fixture(fx, work)
    (work / "crate" / "src" / "dag.rs").write_text((work / "crate" / "src" / "dag.rs").read_text() + "\n// x\n")
    check("rust frozen hash: a modified fixture tree no longer matches its SHA-256",
          C.tree_sha256(work) != fx["sha256"], "tree hash", results)
    shutil.rmtree(work, ignore_errors=True)

    # --- frozen step expectations include the exit code ------------------------------
    sc_f = next(s for s in manifest["scenarios"] if s["id"] == "b3_dag_f")
    edit = next(s for s in sc_f["steps"] if s.get("label") == "edit")
    check("B3 edit f spec: edit step expects fail + exit 1, revert expects pass",
          edit["expect"] == "fail" and edit["expect_exit"] == 1 and
          next(s for s in sc_f["steps"] if s.get("label") == "revert")["expect"] == "pass", str(edit), results)
    check("step expectation is exit-code strict: fail with the wrong exit code does not satisfy it",
          S._expect("fail", {"ok": False, "exit_code": 1}, 1) and not S._expect("fail", {"ok": False, "exit_code": 139}, 1),
          "exit 1 vs 139", results)

    # --- verdict normalization (pure) ---------------------------------------------------
    def run_of(code, path, fresh, rendered="r"):
        line = {"reason": "compiler-message", "message": {"level": "error", "code": {"code": code},
                "message": "boom", "rendered": rendered + path,
                "spans": [{"file_name": path + "/src/x.rs", "line_start": 3, "column_start": 5, "is_primary": True}]}}
        art = {"reason": "compiler-artifact", "fresh": fresh}
        return {"rc": 101, "timed_out": False, "stderr": "   Checking c v0.1.0 (/p)\n" if not fresh else "",
                "stdout": "\n".join(json.dumps(x) for x in (line, art, {"reason": "build-finished", "success": False}))}
    a = RC.extract_verdict(run_of("E0277", "/A/work", False), [("/A/work", "<F>")])
    b = RC.extract_verdict(run_of("E0277", "/B/other", True, "different render "), [("/B/other", "<F>")])
    c = RC.extract_verdict(run_of("E0308", "/A/work", False), [("/A/work", "<F>")])
    check("rust verdict: paths, rendered text and fresh/dirty bookkeeping are normalized away; a changed error code is not",
          RC.gate_view(a) == RC.gate_view(b) and RC.gate_view(a) != RC.gate_view(c) and
          a["bookkeeping"]["units_checked"] == 1 and b["bookkeeping"]["units_fresh"] == 1, "gate_view", results)

    # --- missing cargo: one clear line, skip, no crash -----------------------------------
    saved = os.environ.get("PATH")
    os.environ["PATH"] = str(scratch / "no-such-bin")
    try:
        try:
            RC.find_rust()
            msg = None
        except RC.RustUnavailable as exc:
            msg = str(exc)
        ref, cand = R.rust_toolchains(None, [{"id": "x"}])
    finally:
        os.environ["PATH"] = saved
    check("missing cargo: find_rust raises RustUnavailable with a clear one-line message and the runner skips",
          msg is not None and "\n" not in msg and "skipped" in msg and ref is None and cand is None, str(msg)[:90], results)

    try:
        rust = RC.find_rust()
    except RC.RustUnavailable as exc:
        print(f"SKIP rust selftests needing cargo: {exc}")
        return

    # --- the gate: identity passes, a mutating cargo client fails, an injected error is a real failure
    scs = [s for s in frozen_sc.values() if s["id"] in ("rust_floor_edit", "rust_dag_single_f")]
    run_dir = scratch / "rustgate"
    ident = [RC.gate_scenario(s, RC.fixture_by_id(manifest, s["fixture"]), rust, rust, None, run_dir) for s in scs]
    wrapper = scratch / "mutate_cargo.py"
    wrapper.write_text(MUTATE_CARGO)
    mut_tmp = scratch / "mutate-copies"
    mut_tmp.mkdir(parents=True, exist_ok=True)
    tmpl = f"python3 {wrapper} {{cargo}} {{path}} {mut_tmp}"
    mut = [RC.gate_scenario(s, RC.fixture_by_id(manifest, s["fixture"]), rust, rust, tmpl, run_dir) for s in scs]
    check("rust gate: identity candidate passes (incl. the failing edit f with its diagnostic)",
          all(g["passed"] for g in ident), str([g["passed"] for g in ident]), results)
    floor_mut = mut[[s["id"] for s in scs].index("rust_floor_edit")]
    check("rust gate: a candidate that injects a type error is caught (steps differ from the reference)",
          not floor_mut["passed"] and not any(x["candidate_matches_expected"] for x in floor_mut["steps"][:1]),
          str([x["identical"] for x in floor_mut["steps"]]), results)
    f_steps = ident[[s["id"] for s in scs].index("rust_dag_single_f")]["steps"]
    edit_step = next(x for x in f_steps if x["step"] == "edit")
    ref_edit = edit_step["reference"]
    check("rust error path: reference edit step fails with exit 101, code E0277, and revert passes again",
          ref_edit.get("exit_code") == 101 and ref_edit.get("ok") is False and
          ((ref_edit.get("document") or {}).get("diagnostics") or [{}])[0].get("code") == "E0277" and
          next(x for x in f_steps if x["step"] == "revert")["reference"].get("ok") is True, "E0277", results)
    shutil.rmtree(run_dir, ignore_errors=True)
    shutil.rmtree(mut_tmp, ignore_errors=True)
    (scratch / "mutate_cargo.py").unlink(missing_ok=True)


def rust_gate_strength_tests(R, manifest, scratch, results) -> None:
    """Team 5 review: the Rust gate must be sensitive to every field it claims to compare.
    Each pure case differs from its twin in exactly ONE field, so a gate that drops that
    field (spans, line, column, primary, message, level, code, exit code, ok) fails here."""
    import rust_cmp as RC
    import scenarios as S

    def run_of(*, code="E0277", msg="boom", level="error", line=3, col=5, primary=True, file="src/x.rs",
               rc=101, success=False):
        d = {"reason": "compiler-message", "message": {"level": level, "code": {"code": code}, "message": msg,
             "rendered": "r", "spans": [{"file_name": file, "line_start": line, "column_start": col,
                                        "is_primary": primary}]}}
        return {"rc": rc, "timed_out": False, "stderr": "",
                "stdout": "\n".join(json.dumps(x) for x in (d, {"reason": "build-finished", "success": success}))}

    base = RC.gate_view(RC.extract_verdict(run_of(), []))
    cases = {"line": dict(line=4), "column": dict(col=6), "primary": dict(primary=False),
             "message": dict(msg="other"), "level": dict(level="warning"), "code": dict(code="E0308"),
             "file": dict(file="src/y.rs"), "exit code": dict(rc=1), "build-finished success": dict(success=True)}
    missed = [name for name, kw in cases.items() if RC.gate_view(RC.extract_verdict(run_of(**kw), [])) == base]
    check("rust gate strength: a difference in only ONE of line/column/primary/message/level/code/file/exit code/"
          "success is a gate difference", not missed, f"missed: {missed}", results)
    twin = RC.gate_view(RC.extract_verdict(run_of(), []))
    check("rust gate strength: identical inputs are identical (no spurious difference)", twin == base, "twin", results)

    # the byte-identical revert check: a tree that differs after revert must abort the sequence
    sc = next(s for s in manifest["scenarios"] if s["id"] == "rust_floor_edit")
    fx = {**RC.fixture_by_id(manifest, sc["fixture"])}
    fx["sha256"] = "0" * 64          # a frozen hash the reverted tree cannot equal
    try:
        RC.run_sequence(sc, fx, RC.find_rust(), None, scratch / "revert-neg" / "w", scratch / "revert-neg")
        check("rust revert check: a reverted tree that differs from the frozen hash aborts the sequence", False,
              "no error", results)
    except C.ToolError as exc:
        check("rust revert check: a reverted tree that differs from the frozen hash aborts the sequence",
              "byte-identical" in str(exc), str(exc)[:80], results)
    except RC.RustUnavailable:
        pass
    shutil.rmtree(scratch / "revert-neg", ignore_errors=True)

    # frozen expect_code: a step whose verdict lacks the code does not satisfy it
    v = RC.extract_verdict(run_of(), [])
    check("expect_code: the frozen error code must be among the diagnostics (E0277 yes, E0308 no); Moss shape too",
          S._expect("fail", v, 101, "E0277") and not S._expect("fail", v, 101, "E0308") and
          S._expect("fail", {"ok": False, "exit_code": 1, "diagnostics": [{"code": "TYPE_AMBIGUOUS_OPEN_RESULT"}]},
                    1, "TYPE_AMBIGUOUS_OPEN_RESULT") and
          not S._expect("fail", {"ok": False, "exit_code": 1, "diagnostics": [{"code": "OTHER"}]}, 1,
                        "TYPE_AMBIGUOUS_OPEN_RESULT"), "E0277/E0308", results)
    sc_f = next(s for s in manifest["scenarios"] if s["id"] == "b3_dag_f")
    check("expect_code is frozen in the f specs (Moss and Rust)",
          S.expect_codes(sc_f)["edit"] == "TYPE_AMBIGUOUS_OPEN_RESULT" and
          S.expect_codes(next(s for s in manifest["scenarios"] if s["id"] == "rust_dag_single_f"))["edit"] == "E0277",
          "codes", results)

    # fail-open: a missing cargo is loud and recorded
    import os
    saved = os.environ.get("PATH")
    os.environ["PATH"] = str(scratch / "no-such-bin")
    R.RUST_SKIPPED.clear()
    try:
        R.rust_toolchains(None, [{"id": "x"}])
        try:
            class A:
                require_rust = True
            R.rust_toolchains(A, [{"id": "x"}])
            strict = False
        except C.ToolError:
            strict = True
    finally:
        os.environ["PATH"] = saved
    check("missing cargo: the skip is recorded (FAIL-OPEN) and --require-rust turns it into an error",
          len(R.RUST_SKIPPED) >= 1 and "SKIPPED" in R.RUST_SKIPPED[0] and strict, str(R.RUST_SKIPPED[:1])[:90], results)
    R.RUST_SKIPPED.clear()
