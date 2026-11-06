"""B6 agent-loop replay (Phase 28.5, Team 4).

Replays the last 50 non-merge commits that touched `.moss` files under `sdk/`
and/or `packages/` (fixed, frozen: see ANCHOR / COUNT / PATHSPECS), one edit at
a time, against TODAY's compiler:

  1. materialize the commit's PARENT tree (`git archive` of sdk/ and packages/
     into a scratch directory under tmp/; no `git worktree`, nothing is ever
     checked out inside the repository),
  2. run the entry-point checks on it            -> phase `pre`  (baseline)
  3. apply the commit's tracked changes           (`.moss` files and `Moss.toml`
     under sdk/ and packages/; added, modified, deleted; every blob verified
     against the frozen SHA-1)
  4. run the same entry-point checks again        -> phase `post` (the edit)

ENTRY POINTS (frozen per commit in the spec, derived from git alone):
  * every package (nearest ancestor directory with a `Moss.toml`) that owns a
    changed tracked file: `margo --root <pkg> --check --json build`. margo
    checks the package's dependencies first (cold in `pre`; the package-level
    fingerprint cache skips the unchanged ones in `post`) and, for the root
    package, `moss test --check` on its test sources.
  * every changed `.moss` file that no package owns: `moss check <file> --json`.
  Packages that merely DEPEND on a changed package are not checked (cost and
  scope: an agent normally checks the package it is editing).
A package that does not exist in the parent tree (a commit that adds a package)
has no `pre` run; a package deleted by the commit has no `post` run.

All requests go through the same command templates as every other benchmark, so
a resident client or a future compiler is swapped in from the command line.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import io
import tomllib

import ect_common as C

ANCHOR = "9b840e8ae8cc99e37915fb15367ca31c47b160f8"   # merge-base of phase28-5-bench and main
COUNT = 50
PATHSPECS = [":(glob)sdk/**/*.moss", ":(glob)packages/**/*.moss"]
ROOTS = ("sdk", "packages")
EXPECTATIONS_PATH = C.SUITE_DIR / "replay_expectations.json"
STEP_TIMEOUT_S = 300
FAMILY = "b6_replay"
KIND = "replay_step"


# --------------------------------------------------------------------------
# git plumbing
# --------------------------------------------------------------------------

def _git(args: list[str], *, check: bool = True) -> bytes:
    p = subprocess.run(["git", *args], cwd=C.REPO_ROOT, capture_output=True)
    if check and p.returncode != 0:
        raise C.ToolError(f"git {' '.join(args[:3])} failed: {p.stderr.decode(errors='replace').strip()[:200]}")
    return p.stdout


def git_blob_sha(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def commit_list() -> list[str]:
    """The frozen range, oldest first."""
    out = _git(["log", "--no-merges", "--format=%H", "-n", str(COUNT), ANCHOR, "--", *PATHSPECS])
    shas = out.decode().split()
    if len(shas) != COUNT:
        raise C.ToolError(f"expected {COUNT} commits from {ANCHOR[:12]}, found {len(shas)}")
    return shas[::-1]


def tree_paths(commit: str) -> set[str]:
    out = _git(["ls-tree", "-r", "--name-only", "-z", commit, "--", *ROOTS])
    return {p for p in out.decode().split("\0") if p}


def raw_changes(parent: str, commit: str) -> list[dict]:
    out = _git(["diff-tree", "-r", "--no-renames", "--raw", "-z", "--no-abbrev", parent, commit,
                "--", *ROOTS]).decode()
    parts = out.split("\0")
    changes = []
    i = 0
    while i + 1 < len(parts) and parts[i].startswith(":"):
        meta = parts[i][1:].split()
        changes.append({"old_mode": meta[0], "new_mode": meta[1], "old_blob": meta[2],
                        "new_blob": meta[3], "status": meta[4], "path": parts[i + 1]})
        i += 2
    return sorted(changes, key=lambda c: c["path"])


def is_tracked(path: str) -> bool:
    return path.endswith(".moss") or path.rsplit("/", 1)[-1] == "Moss.toml"


def owner_dir(path: str, pkgs: set[str]) -> str | None:
    d = path.rsplit("/", 1)[0] if "/" in path else ""
    while True:
        if d in pkgs:
            return d
        if "/" not in d:
            return None
        d = d.rsplit("/", 1)[0]


def package_source_dir(commit: str, pkg: str) -> str:
    """`[build] source` of the package's Moss.toml (default `src`)."""
    data = _git(["show", f"{commit}:{pkg}/Moss.toml"], check=False)
    try:
        doc = tomllib.loads(data.decode())
        return (doc.get("build") or {}).get("source", "src")
    except (ValueError, UnicodeDecodeError):
        return "src"


def build_specs(expectations: dict | None = None) -> tuple[list[dict], dict]:
    """Derive every step spec from git (deterministic: immutable history) plus
    the recorded expectations; returns (scenario rows w/o sha256, range spec)."""
    expectations = expectations or {}
    rows = []
    for idx, commit in enumerate(commit_list(), 1):
        parent = _git(["rev-parse", f"{commit}^"]).decode().strip()
        subject = _git(["log", "-1", "--format=%s", commit]).decode().strip()
        all_changes = raw_changes(parent, commit)
        for ch in all_changes:
            for mode in (ch["old_mode"], ch["new_mode"]):
                if mode not in ("000000", "100644", "100755"):
                    raise C.ToolError(f"{commit[:12]}: unsupported file mode {mode} for {ch['path']}")
        tracked = [c for c in all_changes if is_tracked(c["path"])]
        pre_paths = tree_paths(parent)
        pre_pkgs = {p.rsplit("/", 1)[0] for p in pre_paths if p.rsplit("/", 1)[-1] == "Moss.toml"}
        post_pkgs = set(pre_pkgs)
        for c in tracked:
            if c["path"].rsplit("/", 1)[-1] == "Moss.toml":
                (post_pkgs.discard if c["status"] == "D" else post_pkgs.add)(c["path"].rsplit("/", 1)[0])
        entries: dict[str, dict] = {}
        changes = []
        for c in tracked:
            alive = c["status"] != "D"
            pkg = owner_dir(c["path"], post_pkgs if alive else pre_pkgs)
            if pkg is not None:
                eid, ekind, epath = f"pkg:{pkg}", "package", pkg
                src_commit = commit if pkg in post_pkgs else parent
                if c["path"].rsplit("/", 1)[-1] == "Moss.toml" and c["path"] == f"{pkg}/Moss.toml":
                    role = "manifest"
                else:
                    role = "source" if c["path"].startswith(f"{pkg}/{package_source_dir(src_commit, pkg)}/") else "aux"
                entries.setdefault(eid, {"id": eid, "kind": ekind, "path": epath,
                                         "in_pre": pkg in pre_pkgs, "in_post": pkg in post_pkgs})
            else:
                role = "standalone"
                eid = f"file:{c['path']}"
                if alive:
                    entries.setdefault(eid, {"id": eid, "kind": "file", "path": c["path"],
                                             "in_pre": c["status"] != "A", "in_post": True})
            changes.append({**{k: c[k] for k in ("path", "status", "old_mode", "new_mode", "old_blob", "new_blob")},
                            "role": role})
        ordered = [entries[k] for k in sorted(entries)]
        exp = expectations.get(commit, {})
        for e in ordered:
            ee = exp.get(e["id"], {})
            e["expect_pre"] = ee.get("pre") if e["in_pre"] else None
            e["expect_post"] = ee.get("post") if e["in_post"] else None
        ignored = sorted(c["path"] for c in all_changes if not is_tracked(c["path"]))
        roles = [c["role"] for c in changes]
        rows.append({
            "id": f"b6_{idx:02d}_{commit[:8]}", "family": FAMILY, "kind": KIND, "fixture": None,
            "index": idx, "commit": commit, "parent": parent, "subject": subject,
            "description": f"replay of {commit[:12]}: {subject}",
            "changes": changes, "untracked_changes": ignored, "entries": ordered,
            "timeout_s": STEP_TIMEOUT_S,
            "new_package": any(not e["in_pre"] for e in ordered if e["kind"] == "package"),
            "touches_source": "source" in roles,
            "touches_manifest": "manifest" in roles,
        })
    return rows, {}


# --------------------------------------------------------------------------
# materialization and edit application
# --------------------------------------------------------------------------

def materialize(commit: str, dest: Path) -> None:
    """Extract sdk/ and packages/ of `commit` into `dest` (recreated)."""
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    present = [r for r in ROOTS if _git(["ls-tree", "--name-only", commit, r + "/"], check=False).strip()]
    if not present:
        return
    data = _git(["archive", "--format=tar", commit, *present])
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(dest, filter="data")


_BLOBS: dict[str, bytes] = {}


def blob(sha: str) -> bytes:
    if sha not in _BLOBS:
        data = _git(["cat-file", "blob", sha])
        if git_blob_sha(data) != sha:
            raise C.ToolError(f"blob {sha[:12]} content does not hash to itself")
        _BLOBS[sha] = data
    return _BLOBS[sha]


def apply_changes(sc: dict, work: Path) -> None:
    """Apply the commit's tracked changes to the materialized parent tree.
    Each pre-image and post-image is verified against the frozen blob ids."""
    for ch in sc["changes"]:
        path = work / ch["path"]
        if ch["status"] in ("M", "D"):
            if not path.is_file() or git_blob_sha(path.read_bytes()) != ch["old_blob"]:
                raise C.ToolError(f"{sc['id']}: {ch['path']} does not have the frozen pre-image blob")
        if ch["status"] == "D":
            path.unlink()
            continue
        data = blob(ch["new_blob"])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(0o755 if ch["new_mode"] == "100755" else 0o644)
        if git_blob_sha(path.read_bytes()) != ch["new_blob"]:
            raise C.ToolError(f"{sc['id']}: {ch['path']} does not match the frozen post-image blob")


def verify_tree_matches_commit(sc: dict, work: Path) -> bool:
    """After apply, every tracked path of the post commit equals the work tree."""
    post = {p for p in tree_paths(sc["commit"]) if is_tracked(p)}
    have = {p.relative_to(work).as_posix() for p in work.rglob("*") if p.is_file() and is_tracked(p.relative_to(work).as_posix())}
    return post == have


# --------------------------------------------------------------------------
# verification of the frozen spec against git
# --------------------------------------------------------------------------

def verify_spec_against_git(sc: dict) -> None:
    """The commit, its parent and every blob id in the spec must be what the
    repository says (history is immutable; this catches a shallow clone or a
    tampered spec)."""
    for ref, key in ((sc["commit"], "commit"), (sc["parent"], "parent")):
        if _git(["cat-file", "-t", ref], check=False).strip() != b"commit":
            raise C.ToolError(f"{sc['id']}: {key} {ref[:12]} is not in this repository")
    if _git(["rev-parse", f"{sc['commit']}^"]).decode().strip() != sc["parent"]:
        raise C.ToolError(f"{sc['id']}: parent mismatch")
    for ch in sc["changes"]:
        for ref, blobid in ((sc["parent"], ch["old_blob"]), (sc["commit"], ch["new_blob"])):
            if set(blobid) == {"0"}:
                continue
            got = _git(["rev-parse", f"{ref}:{ch['path']}"], check=False).decode().strip()
            if got != blobid:
                raise C.ToolError(f"{sc['id']}: blob of {ch['path']} at {ref[:12]} is {got[:12]!r}, "
                                  f"spec says {blobid[:12]}")


# --------------------------------------------------------------------------
# running a step
# --------------------------------------------------------------------------

def phase_entries(sc: dict, phase: str) -> list[dict]:
    return [e for e in sc["entries"] if e["in_pre" if phase == "pre" else "in_post"]]


def labels(sc: dict) -> list[tuple[str, str]]:
    """(phase, entry id) in execution order."""
    return [(ph, e["id"]) for ph in ("pre", "post") for e in phase_entries(sc, ph)]


def entry_argv(sc: dict, entry: dict, tc: C.Toolchain, template: str | None, work: Path) -> list[str]:
    fx = {"id": f"{sc['id']}:{entry['id']}", "kind": entry["kind"], "entry": entry["path"]}
    values = {"root": str(work), "path": str(work / entry["path"]), "id": fx["id"], "kind": entry["kind"]}
    return C.command_for(fx, {**tc.values(), **values}, template)


def run_step(sc: dict, tc: C.Toolchain, template: str | None, work: Path, *,
             counter_dir: Path | None = None) -> dict[str, dict]:
    """One complete replay of the commit: parent tree -> pre checks -> apply ->
    post checks. Returns {"pre:<entry>"/"post:<entry>": run_measured result}."""
    materialize(sc["parent"], work)
    results: dict[str, dict] = {}
    for phase in ("pre", "post"):
        if phase == "post":
            apply_changes(sc, work)
        for e in phase_entries(sc, phase):
            env = tc.env()
            label = f"{phase}:{e['id']}"
            sink = None
            if counter_dir is not None:
                sink = counter_dir / (f"{sc['id']}.{phase}.{e['id'].replace('/', '_').replace(':', '_')}.jsonl")
                sink.parent.mkdir(parents=True, exist_ok=True)
                sink.unlink(missing_ok=True)
                env.update(C.counter_env(sink))
            res = C.run_measured(entry_argv(sc, e, tc, template, work), cwd=work, env=env,
                                 timeout=float(sc["timeout_s"]))
            if sink is not None:
                res["records"] = C.read_counter_records(sink)
            results[label] = res
    return results


def diag_of(verdict: dict) -> str | None:
    """Stable one-string summary of the first diagnostic (None when ok)."""
    if verdict.get("ok"):
        return None
    diags = verdict.get("diagnostics") or []
    if not diags:
        return (verdict.get("raw") or "")[:300] or "no diagnostic"
    d = diags[0]
    if isinstance(d, dict):
        return f"{d.get('code')}|{d.get('message')}"
    return str(d)[:600]


def observed(verdict: dict) -> dict:
    return {"status": C.status_of(verdict), "exit_code": verdict.get("exit_code"), "diag": diag_of(verdict)}


def expect_ok(expect: dict | None, verdict: dict) -> bool:
    if expect is None or verdict.get("timed_out"):
        return False
    return observed(verdict) == expect


def expect_ok_obs(expect: dict | None, obs: dict) -> bool:
    return expect is not None and obs == expect


def verdicts_of(results: dict[str, dict], work: Path, tc: C.Toolchain) -> dict[str, dict]:
    repl = C.path_replacements(work, tc)
    return {label: C.extract_verdict(r, repl) for label, r in results.items()}


def expectation_for(sc: dict, label: str) -> dict | None:
    phase, eid = label.split(":", 1)
    e = next(x for x in sc["entries"] if x["id"] == eid)
    return e["expect_pre" if phase == "pre" else "expect_post"]


def phase_summary(phase: str, labels_: list[str], samples: dict[str, list[dict]], sc: dict, work: Path,
                  tc: C.Toolchain) -> dict | None:
    if not labels_:
        return None
    repl = C.path_replacements(work, tc)
    n = len(next(iter(samples.values())))
    rep_walls, rep_cpus, rep_rss, ok_all, timed = [], [], [], True, 0
    entries = []
    for label in labels_:
        runs = samples[label]
        verdicts = [C.extract_verdict(r, repl) for r in runs]
        exp = expectation_for(sc, label)
        ok = all(expect_ok(exp, v) for v in verdicts)
        good = [r for r in runs if not r["timed_out"]]
        ok_all &= ok
        timed += len(runs) - len(good)
        entries.append({"id": label.split(":", 1)[1], "status": C.status_of(verdicts[-1]),
                        "exit_code": verdicts[-1].get("exit_code"), "status_ok": ok,
                        "wall_ms_median": C.median([r["wall_ms"] for r in good]),
                        "cpu_ms_median": C.median([r["cpu_ms"] for r in good]),
                        "rss_kib_median": C.median([r["rss_kib"] for r in good]),
                        "diag": diag_of(verdicts[-1])})
    for i in range(n):
        rs = [samples[l][i] for l in labels_]
        if any(r["timed_out"] for r in rs):
            continue
        rep_walls.append(sum(r["wall_ms"] for r in rs))
        rep_cpus.append(sum(r["cpu_ms"] for r in rs))
        rep_rss.append(max((r["rss_kib"] or 0) for r in rs))
    return {"wall": rep_walls, "cpu": rep_cpus, "rss": rep_rss, "ok": ok_all and timed == 0,
            "timed_out_reps": timed, "entries": entries,
            "status": "pass" if all(e["status"] == "pass" for e in entries) else "fail",
            "exit_code": max((e["exit_code"] or 0) for e in entries)}


def measure_step(sc: dict, tc: C.Toolchain, template: str | None, mode: str, reps: int, primes: int,
                 run_dir: Path, warmup: bool = True) -> list[dict]:
    """Rows `pre` and `post` for one commit: per rep the wall of a phase is the
    sum over its entry points; medians over reps. cold: fresh tree every rep
    (every sequence materializes the parent tree anew), one discarded sequence
    first. warm: one stable work path, `primes` unmeasured sequences."""
    work = run_dir / "work" / f"{mode}-{sc['id']}"
    all_labels = [f"{ph}:{eid}" for ph, eid in labels(sc)]
    samples: dict[str, list[dict]] = {l: [] for l in all_labels}
    if mode == "warm":
        for _ in range(primes):
            run_step(sc, tc, template, work)
    elif warmup:
        run_step(sc, tc, template, work)
    for _ in range(reps):
        for l, r in run_step(sc, tc, template, work).items():
            samples[l].append(r)
    counters: dict[str, dict] = {}
    if mode == "cold":
        cres = run_step(sc, tc, template, work, counter_dir=run_dir / "counters")
        for phase in ("pre", "post"):
            recs = [rec for l, r in cres.items() if l.startswith(phase + ":") for rec in r["records"]]
            counters[phase] = C.summarize_counters(recs)
    rows = []
    for phase in ("pre", "post"):
        labs = [l for l in all_labels if l.startswith(phase + ":")]
        s = phase_summary(phase, labs, samples, sc, work, tc)
        if s is None:
            continue
        row = {
            "fixture": f"{sc['id']}:{phase}", "scenario": sc["id"], "step": phase,
            "base_fixture": None, "kind": "replay", "mode": mode, "reps": reps,
            "expected_status": "pass" if all((expectation_for(sc, l) or {}).get("status") == "pass" for l in labs) else "fail",
            "status": s["status"], "status_ok": s["ok"], "timed_out_reps": s["timed_out_reps"],
            "timed_out": s["timed_out_reps"] > 0,
            "wall_ms_median": C.median(s["wall"]), "wall_ms_min": min(s["wall"], default=None),
            "wall_ms_max": max(s["wall"], default=None), "wall_ms_first": s["wall"][0] if s["wall"] else None,
            "cpu_ms_median": C.median(s["cpu"]), "rss_kib_median": C.median(s["rss"]),
            "wall_ms_samples": [round(w, 3) for w in s["wall"]],
            "exit_code": s["exit_code"], "category": sc["family"],
            "metadata": {"commit": sc["commit"], "index": sc["index"], "entries": s["entries"],
                         "new_package": sc["new_package"], "touches_source": sc["touches_source"]},
            "fixture_path": None, "stack_kib": None,
        }
        row.update(counters.get(phase) or {"checker_runs": None, **{n: None for n, _ in C.WORK_COUNTERS}})
        rows.append(row)
    shutil.rmtree(work, ignore_errors=True)
    return rows


def observe_step(sc: dict, tc: C.Toolchain, work: Path) -> dict[str, dict]:
    """One reference run -> {label: observed(verdict)} (for recording/validating)."""
    res = run_step(sc, tc, None, work)
    out = {l: observed(v) for l, v in verdicts_of(res, work, tc).items()}
    for l, r in res.items():
        if r["timed_out"]:
            out[l] = {"status": "timeout", "exit_code": None, "diag": None}
    return out


# --------------------------------------------------------------------------
# gate / validate / record
# --------------------------------------------------------------------------

def gate_step(sc: dict, reference: C.Toolchain, candidate: C.Toolchain, candidate_template: str | None,
              run_dir: Path) -> dict:
    """Reference vs candidate on EVERY check of the step (each entry point, pre
    and post): identical full JSON verdicts (volatile keys excluded, margo's
    checked/skipped bookkeeping informational only) and reference == the frozen
    expectation (status, exit code, diagnostic)."""
    import scenarios as S
    verdicts = {}
    for side, tc, template in (("reference", reference, None), ("candidate", candidate, candidate_template)):
        work = run_dir / "work" / f"gate-{side}-{sc['id']}"
        res = run_step(sc, tc, template, work)
        verdicts[side] = verdicts_of(res, work, tc)
        shutil.rmtree(work, ignore_errors=True)
    steps = []
    for phase, eid in labels(sc):
        label = f"{phase}:{eid}"
        ref, cand = verdicts["reference"][label], verdicts["candidate"][label]
        exp = expectation_for(sc, label)
        steps.append({"step": label, "identical": S.gate_view(ref) == S.gate_view(cand),
                      "bookkeeping_reference": S.bookkeeping(ref),
                      "bookkeeping_candidate": S.bookkeeping(cand),
                      "reference_matches_expected": expect_ok(exp, ref),
                      "candidate_matches_expected": expect_ok(exp, cand),
                      "status": C.status_of(ref), "reference": ref, "candidate": cand})
    return {"scenario": sc["id"], "fixture": None, "steps": steps,
            "passed": all(s["identical"] and s["reference_matches_expected"] for s in steps)}


def validate_step(sc: dict, tc: C.Toolchain, run_dir: Path) -> list[str]:
    """Spec matches git, the applied tree equals the commit's tracked tree, and
    one reference run reproduces every frozen expectation."""
    work = run_dir / "work" / f"validate-{sc['id']}"
    failures = []
    try:
        verify_spec_against_git(sc)
        materialize(sc["parent"], work)
        apply_changes(sc, work)
        if not verify_tree_matches_commit(sc, work):
            failures.append(f"{sc['id']}: tracked files after applying the changes differ from the commit's tree")
        obs = observe_step(sc, tc, work)
    except C.ToolError as exc:
        shutil.rmtree(work, ignore_errors=True)
        return [f"{sc['id']}: {exc}"]
    for l, o in obs.items():
        if o != expectation_for(sc, l):
            failures.append(f"{sc['id']}:{l}: observed {o['status']} (exit {o['exit_code']}) "
                            f"{(o['diag'] or '')[:100]!r} != frozen expectation")
    shutil.rmtree(work, ignore_errors=True)
    return failures


def record_expectations(tc: C.Toolchain, run_dir: Path, ids: list[str] | None = None) -> dict:
    """Observe every step twice with the reference compiler; the two
    classifications must agree. Returns {commit: {entry: {pre:..., post:...}}}."""
    rows, _ = build_specs()
    out: dict = {}
    for sc in rows:
        if ids and sc["id"] not in ids:
            continue
        runs = []
        for k in range(2):
            work = run_dir / "work" / f"record-{sc['id']}"
            runs.append(observe_step(sc, tc, work))
            shutil.rmtree(work, ignore_errors=True)
        if runs[0] != runs[1]:
            raise C.ToolError(f"{sc['id']}: two reference runs disagree: not deterministic")
        entry: dict = {}
        for label, o in runs[0].items():
            phase, eid = label.split(":", 1)
            entry.setdefault(eid, {})[phase] = o
        out[sc["commit"]] = entry
    return out


# --------------------------------------------------------------------------
# churn driver (untimed)
# --------------------------------------------------------------------------

def _side_units(sc: dict, phase: str, tc: C.Toolchain, work: Path) -> dict:
    import churn
    units = {}
    for e in phase_entries(sc, phase):
        if e["kind"] == "package":
            root = work / e["path"]
            res = C.run_measured(entry_argv(sc, e, tc, None, work), cwd=work, env=tc.env(),
                                 timeout=float(sc["timeout_s"]))
            check_status = C.status_of(C.extract_verdict(res, C.path_replacements(work, tc)))
            mp = churn.module_path_for(root)
            files = churn.source_files(root)
            if not files:
                units[f"{e['path']}/(no sources)"] = {"entry": e["id"], "check": check_status, "ok": True,
                                                      "error": None, "functions": {}}
            for f in files:
                units[f.relative_to(work).as_posix()] = {"entry": e["id"], "check": check_status,
                                                         **churn.collect_file(tc, root, f, mp)}
        else:
            f = work / e["path"]
            units[e["path"]] = {"entry": e["id"], "check": None, **churn.collect_file(tc, f.parent, f, [])}
    return units


def churn_step(sc: dict, tc: C.Toolchain, work: Path) -> dict:
    import churn
    materialize(sc["parent"], work)
    pre_units = _side_units(sc, "pre", tc, work)
    apply_changes(sc, work)
    post_units = _side_units(sc, "post", tc, work)
    shutil.rmtree(work, ignore_errors=True)

    def merge(units):
        merged = {}
        for rel in sorted(units):
            for k, rec in units[rel]["functions"].items():
                key, n = k, 1
                while key in merged:
                    n += 1
                    key = f"{k}#u{n}"
                merged[key] = rec
        return merged

    bad = [(side, rel, u["error"]) for side, units in (("pre", pre_units), ("post", post_units))
           for rel, u in sorted(units.items()) if not u["ok"]]
    rec = {"scenario": sc["id"], "commit": sc["commit"], "index": sc["index"],
           "new_package": sc["new_package"], "touches_source": sc["touches_source"],
           "units": {side: {rel: {"entry": u["entry"], "check": u["check"], "ok": u["ok"], "error": u["error"],
                                  "functions": len(u["functions"])}
                            for rel, u in sorted(units.items())}
                     for side, units in (("pre", pre_units), ("post", post_units))},
           "available": not bad,
           "unavailable_reason": "; ".join(f"{s} {r}: {e}" for s, r, e in bad[:3]) or None}
    if not bad:
        rec.update(churn.classify(merge(pre_units), merge(post_units)))
    return rec


# --------------------------------------------------------------------------
# the frozen range (manifest key `replay`)
# --------------------------------------------------------------------------

def replay_range(scenarios: list[dict]) -> dict:
    """Frozen description of the B6 range: how it was selected, the ordered
    commit list and the hash of every step spec, with its own SHA-256."""
    steps = [s for s in scenarios if s.get("kind") == KIND]
    body = {"anchor": ANCHOR, "count": COUNT, "pathspecs": PATHSPECS,
            "selection": "git log --no-merges -n COUNT ANCHOR -- PATHSPECS, replayed oldest first",
            "commits": [s["commit"] for s in steps], "scenario_ids": [s["id"] for s in steps],
            "scenario_sha256": [s["sha256"] for s in steps]}
    return {**body, "sha256": C.text_sha256(C.canonical_json(body))}


def verify_range(manifest: dict) -> None:
    rng = manifest.get("replay")
    steps = [s for s in manifest.get("scenarios", []) if s.get("kind") == KIND]
    if not steps:
        return
    if rng is None or replay_range(manifest["scenarios"]) != rng:
        raise C.ToolError("replay range does not match the frozen B6 scenario specs (or its SHA-256); "
                          "refusing to run")
    if commit_list() != rng["commits"]:
        raise C.ToolError("the commit range selected from git no longer equals the frozen list")


def fingerprint_step(sc: dict, tc: C.Toolchain, run_dir: Path) -> dict:
    """Everything that must repeat exactly across runs (timing excluded):
    per check the observed status/exit code/diagnostic, and per phase the
    compiler counters (separate counter run)."""
    work = run_dir / "work" / f"fp-{sc['id']}"
    res = run_step(sc, tc, None, work, counter_dir=run_dir / "counters")
    obs = {l: observed(v) for l, v in verdicts_of(res, work, tc).items()}
    counters = {}
    for phase in ("pre", "post"):
        recs = [rec for l, r in res.items() if l.startswith(phase + ":") for rec in r["records"]]
        if recs:
            counters[phase] = C.summarize_counters(recs)
    shutil.rmtree(work, ignore_errors=True)
    return {"observed": obs, "counters": counters}
