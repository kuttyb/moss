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
import os
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
PINNED_REFERENCE_SHA256 = "e9a22cf0a6029d31b617baf863c5c1e866a3641a22c38bb1cd1e78c31bfe4d36"   # moss-opt the expectations were recorded with
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
                srcdir = package_source_dir(src_commit, pkg)
                if c["path"].rsplit("/", 1)[-1] == "Moss.toml" and c["path"] == f"{pkg}/Moss.toml":
                    role = "manifest"
                elif c["path"].startswith(f"{pkg}/{srcdir}/"):
                    role = "source"
                elif c["path"].startswith(f"{pkg}/tests/"):
                    role = "tests"      # covered by margo's `moss test --check` of the root package
                else:
                    role = "aux"        # benches/, fixtures/, ...: never checked by margo --check
                entries.setdefault(eid, {"id": eid, "kind": ekind, "path": epath,
                                         "in_pre": pkg in pre_pkgs, "in_post": pkg in post_pkgs,
                                         "pre_has_sources": any(
                                             q.startswith(f"{pkg}/{srcdir}/") and q.endswith(".moss")
                                             for q in pre_paths) if pkg in pre_pkgs else False})
                if role == "aux" and c["path"].endswith(".moss"):
                    aid = f"aux:{c['path']}"
                    entries.setdefault(aid, {"id": aid, "kind": "auxfile", "path": c["path"], "pkg": pkg,
                                             "in_pre": c["status"] != "A" and pkg in pre_pkgs,
                                             "in_post": alive and pkg in post_pkgs})
            else:
                role = "standalone"
                eid = f"file:{c['path']}"
                if alive:
                    entries.setdefault(eid, {"id": eid, "kind": "file", "path": c["path"],
                                             "in_pre": c["status"] != "A", "in_post": True})
            changes.append({**{k: c[k] for k in ("path", "status", "old_mode", "new_mode", "old_blob", "new_blob")},
                            "role": role})
        rank = {"file": 0, "package": 1, "auxfile": 2}   # an aux file is checked after its package
        ordered = [entries[k] for k in sorted(entries, key=lambda k: (rank[entries[k]["kind"]], k))]
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
            # a package whose Moss.toml existed but had no source file yet: its first
            # post check is a first-source check, not an edit
            "effectively_new": any(not e["in_pre"] or not e["pre_has_sources"]
                                   for e in ordered if e["kind"] == "package"),
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
    if entry["kind"] == "auxfile":
        import churn
        pkg = work / entry["pkg"]
        values["modpath"] = os.pathsep.join([str(pkg / "build" / "check-debug"), *churn.module_path_for(pkg)])
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


def verdicts_of(results: dict[str, dict], work: Path, tc: C.Toolchain,
                position_map: str | None = None) -> dict[str, dict]:
    repl = C.path_replacements(work, tc)
    return {label: C.extract_verdict(r, repl, position_map, {"label": label}) for label, r in results.items()}


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


class ReplayStores:
    """Store policy of the replay (Phase 28.5.1): ONE store per replay run, kept across the
    commits. A replay run = one pass over the steps: cold mode keeps one store per rep index
    (rep i of step n sees what rep i of steps 1..n-1 left, an agent loop; reps never share a
    store), warm mode keeps a single store; a separate counter store follows the same evolution.
    Every measurement attempt (spread-guard retry, noisy-block retry) runs on CLONES of the
    masters and only the accepted attempt is promoted, so a retry never sees its own earlier run."""

    def __init__(self, stores: "C.StoreSet", mode: str, reps: int):
        self.stores, self.mode = stores, mode
        n = reps if mode == "cold" else 1
        self.timed = [stores.new(f"b6-{mode}-timed{i}") for i in range(n)]
        self.counter = stores.new(f"b6-{mode}-counters")
        self._pending: list[dict] = []

    def clone(self) -> dict:
        c = {"timed": [self.stores.clone(p, "b6-attempt") for p in self.timed],
             "counter": self.stores.clone(self.counter, "b6-attempt-counters")}
        self._pending.append(c)
        return c

    def promote(self, chosen: dict) -> None:
        for m, c in zip(self.timed, chosen["timed"]):
            self.stores.replace(m, c)
        self.stores.replace(self.counter, chosen["counter"])
        self._pending.remove(chosen)
        self.discard_pending()

    def discard_pending(self) -> None:
        for c in self._pending:
            for p in [*c["timed"], c["counter"]]:
                shutil.rmtree(p, ignore_errors=True)
        self._pending.clear()

    def snapshot(self) -> dict:
        return {"timed": [self.stores.clone(p, "b6-snap") for p in self.timed],
                "counter": self.stores.clone(self.counter, "b6-snap-counters")}

    def restore(self, snap: dict) -> None:
        for m, c in zip(self.timed, snap["timed"]):
            shutil.rmtree(m, ignore_errors=True)
            shutil.copytree(c, m)
        shutil.rmtree(self.counter, ignore_errors=True)
        shutil.copytree(snap["counter"], self.counter)

    def drop_snapshot(self, snap: dict) -> None:
        for p in [*snap["timed"], snap["counter"]]:
            shutil.rmtree(p, ignore_errors=True)

    def finish(self) -> None:
        self.discard_pending()
        for p in [*self.timed, self.counter]:
            self.stores.release(p)


def measure_step_once(sc: dict, tc: C.Toolchain, template: str | None, mode: str, reps: int, primes: int,
                 run_dir: Path, warmup: bool = True, store_state: dict | None = None,
                 stores: "C.StoreSet | None" = None) -> list[dict]:
    """Rows `pre` and `post` for one commit: per rep the wall of a phase is the
    sum over its entry points; medians over reps. cold: fresh tree every rep
    (every sequence materializes the parent tree anew), one discarded sequence
    first. warm: one stable work path, `primes` unmeasured sequences."""
    work = run_dir / "work" / f"{mode}-{sc['id']}"
    all_labels = [f"{ph}:{eid}" for ph, eid in labels(sc)]
    samples: dict[str, list[dict]] = {l: [] for l in all_labels}
    stores = stores or C.StoreSet(run_dir / "stores")
    state = store_state or ReplayStores(stores, mode, reps).clone()      # standalone use: private stores
    if mode == "warm":
        wtc = tc.with_store(state["timed"][0])
        for _ in range(primes):
            run_step(sc, wtc, template, work)
        for _ in range(reps):
            for l, r in run_step(sc, wtc, template, work).items():
                samples[l].append(r)
    else:
        if warmup:       # the discarded warm-up rep gets its own fresh store like any cold rep
            wst = stores.new("b6-warmup")
            try:
                run_step(sc, tc.with_store(wst), template, work)
            finally:
                stores.release(wst)
        for i in range(reps):
            for l, r in run_step(sc, tc.with_store(state["timed"][i]), template, work).items():
                samples[l].append(r)
    counters: dict[str, dict] = {}
    if mode == "cold" or C.counters_available(template):
        # Counter pass: the same replay step under the same store policy in its own store
        # (the counter master, which evolves across commits exactly as a timed store does). In
        # warm mode the series is replayed in full (primes, then the timed sequences) and the
        # counters of the last sequence are reported.
        ctc = tc.with_store(state["counter"])
        n_seq = 1 if mode == "cold" else primes + reps
        for _ in range(n_seq):
            cres = run_step(sc, ctc, template, work, counter_dir=run_dir / "counters")
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
            "spread": (max(s["wall"]) / min(s["wall"])) if s["wall"] and min(s["wall"]) > 0 else None,
            "exit_code": s["exit_code"], "category": sc["family"],
            "metadata": {"commit": sc["commit"], "index": sc["index"], "entries": s["entries"],
                         "new_package": sc["new_package"], "effectively_new": sc["effectively_new"],
                         "touches_source": sc["touches_source"]},
            "fixture_path": None, "stack_kib": None,
        }
        row.update(counters.get(phase) or {"checker_runs": None, **{n: None for n, _ in C.WORK_COUNTERS}})
        rows.append(row)
    shutil.rmtree(work, ignore_errors=True)
    return rows


def measure_step(sc: dict, tc: C.Toolchain, template: str | None, mode: str, reps: int, primes: int,
                 run_dir: Path, warmup: bool = True, rs: "ReplayStores | None" = None) -> list[dict]:
    """measure_step_once under the per-step spread guard (see guarded_measure). With `rs`
    (the replay's stores) every attempt runs on clones of the masters and the accepted
    attempt is promoted; the attempt chosen is the one guarded_measure keeps."""
    if rs is None:
        return guarded_measure(lambda: measure_step_once(sc, tc, template, mode, reps, primes, run_dir, warmup))
    attempts: list[dict] = []

    def once():
        state = rs.clone()
        rows = measure_step_once(sc, tc, template, mode, reps, primes, run_dir, warmup, state, rs.stores)
        for r in rows:
            r["_attempt"] = len(attempts)
        attempts.append(state)
        return rows

    rows = guarded_measure(once)
    chosen = attempts[rows[0]["_attempt"]] if rows else attempts[-1]
    for r in rows:
        r.pop("_attempt", None)
    rs.promote(chosen)
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
              run_dir: Path, side_stores: dict | None = None, position_map: str | None = None) -> dict:
    """Reference vs candidate on EVERY check of the step (each entry point, pre
    and post): identical full JSON verdicts (volatile keys excluded, margo's
    checked/skipped bookkeeping informational only) and reference == the frozen
    expectation (status, exit code, diagnostic)."""
    import scenarios as S
    verdicts = {}
    for side, tc, template in (("reference", reference, None), ("candidate", candidate, candidate_template)):
        work = run_dir / "work" / f"gate-{side}-{sc['id']}"
        # one store per side for the whole replay gate, kept across the commits (never shared by sides)
        tcs = tc.with_store(side_stores[side]) if side_stores else tc
        res = run_step(sc, tcs, template, work)
        verdicts[side] = verdicts_of(res, work, tc, position_map if side == "candidate" else None)
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
    pin = reference_pin(reference)
    return {"scenario": sc["id"], "fixture": None, "steps": steps, "reference_pin": pin,
            "reference_note": pin["note"] if not all(x["reference_matches_expected"] for x in steps) else None,
            "passed": all(x["identical"] and x["reference_matches_expected"] for x in steps)}


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
        elif e["kind"] == "file":
            f = work / e["path"]
            units[e["path"]] = {"entry": e["id"], "check": None, **churn.collect_file(tc, f.parent, f, [])}
        # auxfile entries (benches/fixtures) are latency entry points only: the churn
        # pass reads package source files, so it is vacuous for tests/benches-only commits
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
           "new_package": sc["new_package"], "effectively_new": sc["effectively_new"],
           "edits_existing_source": any(c["role"] == "source" and c["status"] == "M" for c in sc["changes"]), "touches_source": sc["touches_source"],
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


# --------------------------------------------------------------------------
# measurement guards (Team 4 review): per-step spread, mid-run noise, reference pin
# --------------------------------------------------------------------------

SPREAD_LIMIT = 1.3       # a phase whose rep max/min exceeds this was disturbed
SPREAD_ATTEMPTS = 3
BLOCK_STEPS = 5          # the calibration fixture is re-timed after every block
BLOCK_DRIFT_PCT = 10.0
BLOCK_RETRIES = 2


def guarded_measure(measure_once, limit: float = SPREAD_LIMIT, attempts: int = SPREAD_ATTEMPTS) -> list[dict]:
    """Run `measure_once()` (rows with a `spread` = max/min rep wall) until every
    phase's spread is within `limit`, at most `attempts` times. Documented
    selection rule: the attempt with the LOWEST WORST-PHASE SPREAD is kept (ties:
    the earliest); the median is never looked at. Rows record the attempts."""
    tried = []
    for _ in range(attempts):
        rows = measure_once()
        worst = max((r["spread"] for r in rows if r.get("spread") is not None), default=1.0)
        tried.append((worst, rows))
        if worst <= limit:
            break
    best_worst, best_rows = min(tried, key=lambda t: t[0])   # min() keeps the earliest on ties
    for r in best_rows:
        r["attempts"] = len(tried)
        r["attempt_spreads"] = [round(w, 3) for w, _ in tried]
        r["spread_flagged"] = best_worst > limit
    return best_rows


def run_blocks(items: list, measure_fn, probe_fn, baseline_ms: float, block: int = BLOCK_STEPS,
               drift_pct: float = BLOCK_DRIFT_PCT, retries: int = BLOCK_RETRIES, log=None,
               snapshot=None, restore=None, drop=None) -> tuple[list[list[dict]], list[dict]]:
    """Measure `items` in blocks; after each block `probe_fn()` re-times the
    calibration fixture (median wall ms). A block whose probe drifted more than
    `drift_pct` from `baseline_ms` is discarded and measured again (up to
    `retries` times, then kept and flagged `block_noisy`). Returns (rows per
    item, probe log)."""
    out: list[list[dict]] = []
    probes: list[dict] = []
    for start in range(0, len(items), block):
        chunk = items[start:start + block]
        snap = snapshot() if snapshot else None      # store state at the block's start (B6 stores)
        for attempt in range(retries + 1):
            if attempt and restore:
                restore(snap)                         # a re-measured block starts from the same store state
            rows = [measure_fn(it) for it in chunk]
            probe = probe_fn()
            drift = abs(probe - baseline_ms) / min(probe, baseline_ms) * 100.0
            noisy = drift > drift_pct
            probes.append({"first_step": start + 1, "attempt": attempt + 1, "probe_ms": probe,
                           "drift_pct": round(drift, 1), "noisy": noisy})
            if log:
                log(probes[-1])
            if not noisy:
                break
        if snap is not None and drop:
            drop(snap)
        for step_rows in rows:
            for r in step_rows:
                r["block_attempts"] = attempt + 1
                r["block_noisy"] = noisy
        out.extend(rows)
    return out, probes


def reference_pin(tc: C.Toolchain) -> dict:
    """The frozen expectations were recorded with one specific optimized
    compiler. State plainly whether the reference is that binary."""
    sha = C.sha256_file(tc.moss)
    ok = sha == PINNED_REFERENCE_SHA256
    note = None if ok else (
        f"reference compiler {tc.moss.name} (sha256 {sha[:12]}) is NOT the binary the B6 expectations were "
        f"recorded with (moss-opt sha256 {PINNED_REFERENCE_SHA256[:12]}). A step whose verdict differs from "
        "the frozen expectation then means the expectations must be re-frozen deliberately "
        "(`run.py replay-record`, then `generate.py --regenerate`), not that anything regressed.")
    return {"sha256": sha, "pinned_sha256": PINNED_REFERENCE_SHA256, "matches": ok, "note": note}
