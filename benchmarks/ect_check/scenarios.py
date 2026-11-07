"""Scenario engine (Phase 28.5, Team 3): edit sequences (B3, B4) and git
worktree groups (B5).

A scenario is a frozen spec in manifest.json (`scenarios`). Edit sequences are
lists of steps over one staged fixture:

  check <label>   run the fixture's check command (default cold command, or the
                  warm-client template) once; `expect` is the required status
  apply <patch>   apply a frozen patch (exact-once match, pre/post file hashes)
  revert <patch>  undo it; the tree hash must equal the fixture's frozen hash

Every request goes through the same command template as a plain fixture, so a
resident client / future compiler is measured with no change to the specs.
Edits are applied by this module from the frozen patch specs, never by an
ad-hoc script.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

import ect_common as C


def select_scenarios(manifest: dict, ids: list[str] | None, fixture_ids: set[str] | None = None) -> list[dict]:
    scenarios = manifest.get("scenarios", [])
    if ids:
        # an id may also name a whole family ("b6_replay") or its prefix ("b6")
        def in_family(s, w):
            return s["family"] == w or s["family"].startswith(w + "_")
        wanted = set(ids)
        chosen = [s for s in scenarios if s["id"] in wanted or any(in_family(s, w) for w in wanted)]
        missing = {w for w in wanted if not any(s["id"] == w or in_family(s, w) for s in scenarios)}
        if missing:
            raise SystemExit(f"unknown scenario id(s): {sorted(missing)}")
        return chosen
    return scenarios


def fixture_of(manifest: dict, sc: dict) -> dict | None:
    if sc.get("fixture") is None:     # B6 replay steps are derived from git history
        return None
    return next(f for f in manifest["fixtures"] if f["id"] == sc["fixture"])


# --------------------------------------------------------------------------
# Edit sequences
# --------------------------------------------------------------------------

# B3/B4 comparison rows also get a sampled whole-tree peak RSS (Team 5: /usr/bin/time %M is the
# largest single process, which for a margo run is not the compiler)
SAMPLED_FAMILIES = ("b3_edits", "b3_error", "b4_cold_warm")


def check_labels(sc: dict) -> list[str]:
    return [s["label"] for s in sc["steps"] if s["op"] == "check"]


def _values(fx: dict, work: Path) -> dict[str, str]:
    return {"root": str(work), "path": str(work / fx["entry"]), "id": fx["id"], "kind": fx["kind"]}


def run_sequence(sc: dict, fx: dict, tc: C.Toolchain, template: str | None, work: Path, *,
                 stage: bool = True, counter_dir: Path | None = None, sample: bool = False,
                 provider: str = "raise") -> dict[str, dict]:
    """Run every step once; return {label: run_measured result}. With
    `counter_dir`, each check also writes its own counter sink and the result
    carries the summarized counters. A fixture with a `provider` package gets it built
    first (same toolchain, hence the same store, as the steps): `provider="raise"` aborts
    on failure (bench), `provider="record"` (gate) returns it under the key `__provider__`
    and, when it failed, runs no step (the consumer steps are skipped)."""
    values = C.stage_fixture(fx, work) if stage else _values(fx, work)
    results: dict[str, dict] = {}
    if stage and fx.get("provider"):
        pres, pverdict = C.build_provider_checked(fx, tc, work)
        if provider == "record":
            results["__provider__"] = {"result": pres, "verdict": pverdict}
            if pres["rc"] != 0:
                return results
        elif pres["rc"] != 0:
            raise C.ToolError(f"provider build of {fx['id']} failed: {(pres['stdout'] or pres['stderr'])[:300]}")
    timeout = C.fixture_timeout(fx)
    for step in sc["steps"]:
        op = step["op"]
        if op == "check":
            env = tc.env()
            sink = None
            if counter_dir is not None:
                sink = counter_dir / f"{sc['id']}.{step['label']}.jsonl"
                sink.parent.mkdir(parents=True, exist_ok=True)
                sink.unlink(missing_ok=True)
                env.update(C.counter_env(sink))
            argv = C.command_for(fx, {**tc.values(), **values}, template)
            res = C.run_measured(argv, cwd=work, env=env, timeout=timeout,
                                 stack_kib=C.fixture_stack(fx), sample_tree_rss=sample)
            if sink is not None:
                res["counters"] = C.summarize_counters(C.read_counter_records(sink))
            results[step["label"]] = res
        elif op == "apply":
            C.apply_patch(work, sc["patches"][step["patch"]], "forward")
        elif op == "revert":
            C.apply_patch(work, sc["patches"][step["patch"]], "backward")
            if C.tree_sha256(work) != fx["sha256"]:
                raise C.ToolError(f"scenario {sc['id']}: tree after reverting {step['patch']} is not "
                                  "byte-identical to the frozen fixture")
        else:
            raise C.ToolError(f"scenario {sc['id']}: unknown step op {op!r}")
    return results


def verdict_codes(verdict: dict) -> list[str]:
    """Error codes of a verdict's diagnostics (Moss: `code`; cargo: `code` of errors)."""
    out = []
    for d in verdict.get("diagnostics") or []:
        if isinstance(d, dict) and d.get("code") and d.get("level", "error") == "error":
            out.append(d["code"])
    return out


def _expect(step_expect: str, verdict: dict, exit_code: int | None = None, code: str | None = None) -> bool:
    """Does a step's verdict satisfy the frozen expectation? `exit_code` / `code` are the
    step's optional frozen `expect_exit` / `expect_code` (None for every pre-Team-5 step):
    the verdict must carry that error code among its diagnostics."""
    if code is not None and code not in verdict_codes(verdict):
        return False
    return C.fixture_ok({"expected_status": step_expect, "expected_exit_code": exit_code}, verdict)


def expect_codes(sc: dict) -> dict[str, str | None]:
    return {s["label"]: s.get("expect_code") for s in sc["steps"] if s["op"] == "check"}


def expect_exits(sc: dict) -> dict[str, int | None]:
    return {s["label"]: s.get("expect_exit") for s in sc["steps"] if s["op"] == "check"}


def diag_summary(verdict: dict) -> str | None:
    """`code: message` of the first diagnostic of a failing verdict (reports)."""
    if verdict.get("ok"):
        return None
    diags = verdict.get("diagnostics") or []
    d = diags[0] if diags else None
    if isinstance(d, dict):
        return f"{d.get('code', '?')}: {str(d.get('message', ''))[:160]}"
    return str(d if d is not None else verdict.get("raw", ""))[:160] or None


def measure_scenario(sc: dict, fx: dict, tc: C.Toolchain, template: str | None, mode: str,
                     reps: int, primes: int, run_dir: Path, warmup: bool = True,
                     stores: "C.StoreSet | None" = None) -> list[dict]:
    """One result row per check step: medians over `reps` complete sequences.

    cold: every rep stages a fresh copy and starts from an empty build dir; the
          sequence itself is the unit (a later check sees the state the earlier
          steps left, as in a real edit loop). One discarded full sequence first.
    warm: one staged copy, `primes` unmeasured full sequences, then the timed
          sequences in place, so a resident client keeps its state throughout.
          The first timed rep is also reported (wall_ms_first): a content-keyed
          client may answer later reps, which repeat the same edits, from cache.
    """
    work = run_dir / "work" / f"{mode}-{sc['id']}"
    repl = C.path_replacements(work, tc)
    stores = stores or C.StoreSet(run_dir / "stores")
    expects = {s["label"]: s["expect"] for s in sc["steps"] if s["op"] == "check"}
    exits = expect_exits(sc)
    codes = expect_codes(sc)
    samples: dict[str, list[dict]] = {label: [] for label in expects}
    last_store: dict = {"bytes": None, "files": None}

    def fresh_seq(**kw):
        """Cold policy: every sequence (warm-up, timed, sampled, counter) gets a fresh, empty
        store, kept across ITS OWN steps (a real edit loop) and deleted afterwards."""
        st = stores.new(f"cold-{sc['id']}")
        try:
            return run_sequence(sc, fx, tc.with_store(st), template, work, **kw)
        finally:
            rec = stores.release(st)
            last_store.update(bytes=rec["bytes"], files=rec["files"])

    counters: dict[str, dict] = {}
    counter_series: list[dict] = []
    tree_peaks: dict[str, list[int]] = {}
    if mode == "warm":
        # Warm policy: ONE store per scenario, kept across the primes and the timed sequences.
        wst = stores.new(f"warm-{sc['id']}")
        wtc = tc.with_store(wst)
        C.stage_fixture(fx, work)
        if fx.get("provider"):
            C.build_provider(fx, wtc, work)
        for _ in range(primes):
            run_sequence(sc, fx, wtc, template, work, stage=False)
        for _ in range(reps):
            for label, r in run_sequence(sc, fx, wtc, template, work, stage=False).items():
                samples[label].append(r)
        rec = stores.release(wst)
        last_store.update(bytes=rec["bytes"], files=rec["files"])
        if C.counters_available(template):
            # Counter pass: the SAME series (same primes, same sequences, same order) under the same
            # policy in its OWN store, so its store evolves exactly as the timed one did; the row
            # carries the counters of the last request (steady state), all requests are kept.
            cst = stores.new(f"warm-counters-{sc['id']}")
            ctc = tc.with_store(cst)
            C.stage_fixture(fx, work)
            if fx.get("provider"):
                C.build_provider(fx, ctc, work)
            for _ in range(primes + reps):
                cres = run_sequence(sc, fx, ctc, template, work, stage=False, counter_dir=run_dir / "counters")
                counter_series.append({label: r["counters"] for label, r in cres.items()})
            stores.release(cst)
            counters = counter_series[-1]
    else:
        if warmup:
            fresh_seq()
        for _ in range(reps):
            for label, r in fresh_seq().items():
                samples[label].append(r)
        if sc["family"] in SAMPLED_FAMILIES:
            # whole-process-tree peak RSS, in separate untimed passes (the /proc sampler perturbs timing)
            tree_peaks = {label: [] for label in expects}
            for _ in range(3):
                for label, r in fresh_seq(sample=True).items():
                    tree_peaks[label].append(r.get("tree_rss_peak_kib") or 0)
        cres = fresh_seq(counter_dir=run_dir / "counters")
        counters = {label: r["counters"] for label, r in cres.items()}
    shutil.rmtree(work, ignore_errors=True)

    rows = []
    for label, runs in samples.items():
        good = [r for r in runs if not r.get("timed_out")]
        verdicts = [C.extract_verdict(r, repl) for r in runs]
        row = {
            "fixture": f"{sc['id']}:{label}", "scenario": sc["id"], "step": label,
            "base_fixture": fx["id"], "kind": fx["kind"], "mode": mode, "reps": reps,
            "expected_status": expects[label], "status": C.status_of(verdicts[-1]),
            "status_ok": len(good) == len(runs) and all(_expect(expects[label], v, exits[label], codes[label]) for v in verdicts),
            "expected_exit_code": exits[label], "expected_code": codes[label],
            "tree_rss_peak_kib_median": C.median(tree_peaks.get(label, [])) if tree_peaks else None,
            "diagnostic": diag_summary(verdicts[-1]),
            "timed_out_reps": len(runs) - len(good), "timed_out": len(good) != len(runs),
            "wall_ms_median": C.median([r["wall_ms"] for r in good]),
            "wall_ms_min": min((r["wall_ms"] for r in good), default=None),
            "wall_ms_max": max((r["wall_ms"] for r in good), default=None),
            "wall_ms_first": good[0]["wall_ms"] if good else None,
            "cpu_ms_median": C.median([r["cpu_ms"] for r in good]),
            "rss_kib_median": C.median([r["rss_kib"] for r in good]),
            "wall_ms_samples": [round(r["wall_ms"], 3) for r in runs],
            "exit_code": verdicts[-1].get("exit_code"), "category": sc["family"],
            "metadata": {"classification": sc.get("classification"),
                         "edited_function": sc.get("edited_function")},
            "fixture_path": fx["path"], "stack_kib": fx.get("stack_kib"),
        }
        row["store_bytes"], row["store_files"] = last_store["bytes"], last_store["files"]
        if counter_series:
            row["counters_by_request"] = [c.get(label) for c in counter_series]
        row.update(counters.get(label) or {"checker_runs": None,
                                           **{name: None for name, _ in C.WORK_COUNTERS}})
        rows.append(row)
    return rows


# margo's package bookkeeping: which packages it ran or skipped. A client that
# answers a repeat from its store legitimately reports different values here, so
# the SCENARIO gate treats them as volatile (verdict, diagnostics, exit code and
# every other field stay strict) and reports them as information.
SCENARIO_VOLATILE = ("checked", "skipped")


def gate_view(verdict: dict) -> dict:
    doc = verdict.get("document")
    if not isinstance(doc, dict):
        return verdict
    return {**verdict, "document": {k: v for k, v in doc.items() if k not in SCENARIO_VOLATILE}}


def bookkeeping(verdict: dict) -> dict:
    doc = verdict.get("document")
    return {k: doc.get(k) for k in SCENARIO_VOLATILE} if isinstance(doc, dict) else {}


def verify_frozen(manifest: dict, fixtures: list[dict], scenarios: list[dict]) -> None:
    """Refuse to run on anything that no longer matches the frozen hashes:
    every selected fixture tree, every scenario spec (canonical JSON) and the
    fixtures the scenarios reference."""
    by_id = {f["id"]: f for f in manifest["fixtures"]}
    needed = {f["id"]: f for f in fixtures}
    for sc in scenarios:
        if sc["fixture"] is not None:
            needed.setdefault(sc["fixture"], by_id[sc["fixture"]])
    for fx in needed.values():
        if C.tree_sha256(C.FIXTURES_DIR / fx["path"]) != fx["sha256"]:
            raise C.ToolError(f"fixture {fx['id']} does not match its frozen SHA-256; "
                              "refusing to run (re-freeze deliberately with generate.py --regenerate)")
    for sc in scenarios:
        body = {k: v for k, v in sc.items() if k != "sha256"}
        if C.text_sha256(C.canonical_json(body)) != sc["sha256"]:
            raise C.ToolError(f"scenario {sc['id']} does not match its frozen SHA-256; refusing to run")
    replays = [sc for sc in scenarios if sc.get("kind") == "replay_step"]
    if replays:
        import replay
        replay.verify_range(manifest)
        for sc in replays:
            replay.verify_spec_against_git(sc)


def gate_scenario(sc: dict, fx: dict, reference: C.Toolchain, candidate: C.Toolchain,
                  candidate_template: str | None, run_dir: Path, stores: "C.StoreSet | None" = None,
                  position_map: str | None = None) -> dict:
    """Reference vs candidate on EVERY check step of the sequence: identical
    full JSON verdicts (volatile keys excluded) and reference == the step's
    expected status. Each side runs against its OWN store, kept across the steps (a real
    edit loop: the candidate's incremental path is what is compared). `position_map` rewrites
    the CANDIDATE verdicts' positions to the reference layout first (identity when None).
    A provider build that fails on either side is a verdict: both provider verdicts are
    compared, the consumer steps are skipped and the run finishes."""
    stores = stores or C.StoreSet(run_dir / "stores")
    verdicts, providers = {}, {}
    for side, tc, template in (("reference", reference, None), ("candidate", candidate, candidate_template)):
        work = run_dir / "work" / f"gate-{side}-{sc['id']}"
        st = stores.new(f"gate-{side}-{sc['id']}")
        pm = position_map if side == "candidate" else None
        try:
            res = run_sequence(sc, fx, tc.with_store(st), template, work, provider="record")
        finally:
            stores.release(st)
        repl = C.path_replacements(work, tc)
        prov = res.pop("__provider__", None)
        providers[side] = prov
        verdicts[side] = {label: C.extract_verdict(r, repl, pm, {"scenario": sc["id"], "step": label})
                          for label, r in res.items()}
        shutil.rmtree(work, ignore_errors=True)
    expects = {s["label"]: s["expect"] for s in sc["steps"] if s["op"] == "check"}
    exits = expect_exits(sc)
    codes = expect_codes(sc)
    steps = []
    failed = {side: p is not None and p["result"]["rc"] != 0 for side, p in providers.items()}
    if any(failed.values()):
        pv = {side: (p["verdict"] if p else None) for side, p in providers.items()}
        same = pv["reference"] is not None and pv["candidate"] is not None and \
            gate_view(pv["reference"]) == gate_view(pv["candidate"])
        steps.append({"step": "provider", "identical": same, "consumer_skipped": True,
                      "provider_failed": failed, "provider_reference": pv["reference"],
                      "provider_candidate": pv["candidate"],
                      "bookkeeping_reference": {}, "bookkeeping_candidate": {},
                      "reference_matches_expected": not failed["reference"],
                      "candidate_matches_expected": not failed["candidate"],
                      "status": "fail" if failed["reference"] else "pass",
                      "reference": pv["reference"], "candidate": pv["candidate"]})
        return {"scenario": sc["id"], "fixture": fx["id"], "steps": steps,
                "provider_differs": not same,
                "passed": same and not failed["reference"]}
    for label, exp in expects.items():
        ref, cand = verdicts["reference"][label], verdicts["candidate"][label]
        steps.append({"step": label, "identical": gate_view(ref) == gate_view(cand),
                      "bookkeeping_reference": bookkeeping(ref),
                      "bookkeeping_candidate": bookkeeping(cand),
                      "reference_matches_expected": _expect(exp, ref, exits[label], codes[label]),
                      "candidate_matches_expected": _expect(exp, cand, exits[label], codes[label]),
                      "status": C.status_of(ref), "reference": ref, "candidate": cand})
    return {"scenario": sc["id"], "fixture": fx["id"], "steps": steps,
            "passed": all(s["identical"] and s["reference_matches_expected"] for s in steps)}


def validate_scenario(sc: dict, fx: dict, tc: C.Toolchain, run_dir: Path) -> list[str]:
    """One reference run: patches apply, revert is byte-identical (enforced in
    run_sequence), and every check step satisfies its expected status."""
    work = run_dir / "work" / f"validate-{sc['id']}"
    failures = []
    try:
        res = run_sequence(sc, fx, tc, None, work)
    except C.ToolError as exc:
        shutil.rmtree(work, ignore_errors=True)
        return [f"{sc['id']}: {exc}"]
    repl = C.path_replacements(work, tc)
    for step in sc["steps"]:
        if step["op"] == "check":
            verdict = C.extract_verdict(res[step["label"]], repl)
            if not _expect(step["expect"], verdict, step.get("expect_exit"), step.get("expect_code")):
                failures.append(f"{sc['id']}:{step['label']}: status {C.status_of(verdict)} "
                                f"(exit {verdict.get('exit_code')}) != expected {step['expect']}")
    shutil.rmtree(work, ignore_errors=True)
    return failures


# --------------------------------------------------------------------------
# B5: git worktrees
# --------------------------------------------------------------------------

def _git_env(sc: dict) -> dict[str, str]:
    c = sc["commit"]
    name, email = c["author"].rsplit(" <", 1)
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_AUTHOR_NAME=name, GIT_AUTHOR_EMAIL=email.rstrip(">"),
               GIT_COMMITTER_NAME=name, GIT_COMMITTER_EMAIL=email.rstrip(">"),
               GIT_AUTHOR_DATE=c["date"], GIT_COMMITTER_DATE=c["date"],
               GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_SYSTEM="/dev/null",
               GIT_CONFIG_NOSYSTEM="1")
    return env


def _git(args: list[str], cwd: Path, env: dict) -> str:
    p = subprocess.run(["git", *args], cwd=cwd, env=env, text=True, capture_output=True)
    if p.returncode != 0:
        raise C.ToolError(f"git {' '.join(args)} failed in {cwd}: {p.stderr.strip()[:200]}")
    return p.stdout.strip()


def make_fixture_repo(sc: dict, fx: dict, repo: Path) -> str:
    """Build the fixture repository deterministically and return the commit id.
    The repo lives under tmp/ (never inside the Moss repository); the project is
    committed once and every worktree is a checkout of that single commit."""
    if repo.exists():
        shutil.rmtree(repo)
    shutil.copytree(C.FIXTURES_DIR / fx["path"], repo, ignore=C.copy_ignore)
    (repo / ".gitignore").write_text("build/\n")
    env = _git_env(sc)
    _git(["init", "-q", "-b", "main"], repo, env)
    _git(["add", "-A"], repo, env)
    _git(["commit", "-q", "-m", sc["commit"]["message"]], repo, env)
    return _git(["rev-parse", "HEAD"], repo, env)


def add_worktrees(sc: dict, repo: Path, base: Path, n: int, rep: int) -> list[Path]:
    env = _git_env(sc)
    dirs = []
    for i in range(n):
        d = base / f"wt{i:02d}"
        _git(["worktree", "add", "-q", "--detach", str(d), "HEAD"], repo, env)
        dirs.append(d)
    return dirs


def apply_slot(sc: dict, wt: Path, slot: int, rep: int) -> None:
    """Distinct small edit for worktree `slot`. The value depends on the rep so
    a content-addressed warm client sees new divergence every rep (the base
    stays shared); frozen: slot edits, base values and the rep stride."""
    spec = sc["slots"][slot]
    value = sc["values"][slot] + rep * sc["rep_stride"]
    path = wt / spec["file"]
    text = path.read_text()
    if text.count(spec["old"]) != 1:
        raise C.ToolError(f"{sc['id']}: slot {slot} old text must match exactly once in {path}")
    path.write_text(text.replace(spec["old"], spec["new_fmt"].format(v=value)))


def remove_worktrees(sc: dict, repo: Path, dirs: list[Path]) -> None:
    env = _git_env(sc)
    for d in dirs:
        subprocess.run(["git", "worktree", "remove", "--force", str(d)], cwd=repo, env=env,
                       capture_output=True)
        shutil.rmtree(d, ignore_errors=True)
    subprocess.run(["git", "worktree", "prune"], cwd=repo, env=env, capture_output=True)


def _wt_argv(fx: dict, tc: C.Toolchain, template: str | None, wt: Path) -> list[str]:
    return C.command_for(fx, {**tc.values(), **_values(fx, wt)}, template)


def run_worktree_group(sc: dict, fx: dict, tc: C.Toolchain, template: str | None, repo: Path,
                       base: Path, n: int, concurrent: bool, rep: int,
                       counter_dir: Path | None = None, sample: bool = False,
                       stores: "C.StoreSet | None" = None) -> dict:
    """Create N worktrees of the one commit, apply each worktree's edit, run the
    first check in each (concurrently or one after the other), then remove them.
    Store policy: ONE store per (N, schedule, rep) group, shared by all N worktrees in it
    (so a content-addressed store may hit across the worktrees of a group, never across groups)."""
    stores = stores or C.StoreSet(base.parent / "stores")
    store = stores.new(f"b5-{sc['id']}-n{n}-{'conc' if concurrent else 'seq'}-rep{rep}")
    tc = tc.with_store(store)
    dirs = add_worktrees(sc, repo, base, n, rep)
    try:
        for i, d in enumerate(dirs):
            apply_slot(sc, d, i, rep)
        timeout = C.fixture_timeout(fx)
        env = tc.env()
        if counter_dir is not None:
            counter_dir.mkdir(parents=True, exist_ok=True)
        sinks = []
        jobs = []
        for i, d in enumerate(dirs):
            e = dict(env)
            sink = None
            if counter_dir is not None:
                sink = counter_dir / f"{sc['id']}.n{n}.wt{i:02d}.jsonl"
                sink.unlink(missing_ok=True)
                e.update(C.counter_env(sink))
            sinks.append(sink)
            jobs.append((_wt_argv(fx, tc, template, d), d, e))
        load_start = os.getloadavg()[0]
        if concurrent:
            if counter_dir is not None:
                raise C.ToolError("counter passes run sequentially")
            group = C.run_measured_group([(a, c) for a, c, _ in jobs], env=env, timeout=timeout,
                                         sample_tree_rss=sample)
            results = group["results"]
            totals = {"group_tree_rss_peak_kib": group.get("group_tree_rss_peak_kib"),
                      "group_wall_ms": group["group_wall_ms"], "total_cpu_ms": group["total_cpu_ms"],
                      "sum_rss_kib": group["sum_rss_kib"], "max_rss_kib": group["max_rss_kib"],
                      "timed_out": group["timed_out"]}
        else:
            results = [C.run_measured(a, cwd=c, env=e, timeout=timeout, sample_tree_rss=sample)
                       for a, c, e in jobs]
            totals = {"group_tree_rss_peak_kib": max((r.get("tree_rss_peak_kib") or 0) for r in results)
                      if sample else None,
                      "group_wall_ms": sum(r["wall_ms"] for r in results),
                      "total_cpu_ms": sum(r["cpu_ms"] for r in results),
                      "sum_rss_kib": sum((r["rss_kib"] or 0) for r in results),
                      "max_rss_kib": max((r["rss_kib"] or 0) for r in results),
                      "timed_out": any(r["timed_out"] for r in results)}
        statuses = []
        for r, d in zip(results, dirs):
            v = C.extract_verdict(r, [(str(d), "<WT>")])
            statuses.append(C.fixture_ok({"expected_status": "pass"}, v))
        counters = None
        if counter_dir is not None:
            counters = [C.summarize_counters(C.read_counter_records(s)) for s in sinks]
        return {"n": n, "concurrent": concurrent, "first_check_ms": [r["wall_ms"] for r in results],
                "per_cpu_ms": [r["cpu_ms"] for r in results],
                "per_rss_kib": [r["rss_kib"] for r in results],
                "per_tree_rss_peak_kib": [r.get("tree_rss_peak_kib") for r in results], "all_ok": all(statuses),
                "loadavg_start": load_start, "store_bytes": C.dir_size(store)["bytes"],
                "store_files": C.dir_size(store)["files"], **totals, "counters": counters}
    finally:
        remove_worktrees(sc, repo, dirs)
        stores.release(store)


def measure_worktrees(sc: dict, fx: dict, tc: C.Toolchain, template: str | None, mode: str,
                      reps: int, run_dir: Path, warmup: bool = True,
                      stores: "C.StoreSet | None" = None) -> list[dict]:
    """Rows per (N, schedule): medians over reps of total CPU, summed peak RSS,
    group wall and each worktree's first-check time."""
    base_dir = run_dir / "work" / f"{mode}-{sc['id']}"
    repo = base_dir / "repo"
    stores = stores or C.StoreSet(run_dir / "stores")
    commit = make_fixture_repo(sc, fx, repo)
    wt_root = base_dir / "wt"
    try:
        if warmup:  # page in the binary and the project; discarded
            run_worktree_group(sc, fx, tc, template, repo, wt_root, 1, False, 99, stores=stores)
        collected: dict[tuple[int, str], list[dict]] = {}
        for rep in range(reps):
            for n in sc["counts"]:
                for concurrent in (True, False):
                    g = run_worktree_group(sc, fx, tc, template, repo, wt_root, n, concurrent, rep, stores=stores)
                    collected.setdefault((n, "concurrent" if concurrent else "sequential"), []).append(g)
        sampled: dict[tuple[int, str], list[dict]] = {}
        if mode == "cold":   # separate, untimed passes: the /proc sampler perturbs the machine
            for rep in range(3):
                for n in sc["counts"]:
                    for concurrent in (True, False):
                        g = run_worktree_group(sc, fx, tc, template, repo, wt_root, n, concurrent,
                                               50 + rep, sample=True, stores=stores)
                        sampled.setdefault((n, "concurrent" if concurrent else "sequential"), []).append(g)
        counters = None
        if mode == "cold":
            nmax = max(sc["counts"])
            counters = run_worktree_group(sc, fx, tc, template, repo, wt_root, nmax, False, 98,
                                          counter_dir=run_dir / "counters", stores=stores)["counters"]
    finally:
        # Remove every worktree registration and the fixture repository itself.
        subprocess.run(["git", "worktree", "prune"], cwd=repo, env=_git_env(sc), capture_output=True)
        shutil.rmtree(base_dir, ignore_errors=True)
    rows = []
    for (n, schedule), groups in collected.items():
        firsts = [sorted(g["first_check_ms"]) for g in groups]
        by_index = [C.median([g["first_check_ms"][i] for g in groups]) for i in range(n)]
        row = {
            "scenario": sc["id"], "base_fixture": fx["id"], "mode": mode, "n": n,
            "schedule": schedule, "reps": reps, "commit": commit,
            "status_ok": all(g["all_ok"] and not g["timed_out"] for g in groups),
            "group_wall_ms_median": C.median([g["group_wall_ms"] for g in groups]),
            "group_wall_ms_min": min(g["group_wall_ms"] for g in groups),
            "group_wall_ms_max": max(g["group_wall_ms"] for g in groups),
            "total_cpu_ms_median": C.median([g["total_cpu_ms"] for g in groups]),
            "sum_rss_kib_median": C.median([g["sum_rss_kib"] for g in groups]),
            "max_rss_kib_median": C.median([g["max_rss_kib"] for g in groups]),
            "first_check_ms_by_worktree": by_index,
            "first_check_ms_wt0": by_index[0],
            "first_check_ms_later_median": C.median(by_index[1:]) if n > 1 else None,
            "first_check_ms_max": max(by_index),
            "loadavg_start": groups[0]["loadavg_start"],
            "store_bytes": groups[-1].get("store_bytes"), "store_files": groups[-1].get("store_files"),
            "cpus": os.cpu_count(),
        }
        sg = sampled.get((n, schedule))
        if sg:
            row["sampled_group_tree_rss_peak_mib"] = C.median([g["group_tree_rss_peak_kib"] for g in sg]) / 1024
            row["sampled_per_tree_peak_mib"] = C.median(
                [C.median(g["per_tree_rss_peak_kib"]) for g in sg]) / 1024
        if counters:
            for name, _ in C.WORK_COUNTERS:
                row[name] = sum(int(c[name] or 0) for c in counters[:n])
            row["checker_runs"] = sum(int(c["checker_runs"] or 0) for c in counters[:n])
        rows.append(row)
    rows.sort(key=lambda r: (r["n"], r["schedule"]))
    return rows


def validate_worktrees(sc: dict, fx: dict, tc: C.Toolchain, run_dir: Path) -> list[str]:
    """Structural validation of a worktree scenario: the repository builds, two
    real worktrees come from the same commit, slot edits apply and both checks
    pass; nothing is left behind (no worktree registration, no directory)."""
    base_dir = run_dir / "work" / f"validate-{sc['id']}"
    repo = base_dir / "repo"
    failures = []
    try:
        commit = make_fixture_repo(sc, fx, repo)
        g = run_worktree_group(sc, fx, tc, None, repo, base_dir / "wt", 2, False, 0)
        if not g["all_ok"]:
            failures.append(f"{sc['id']}: a worktree check did not pass")
        left = _git(["worktree", "list", "--porcelain"], repo, _git_env(sc)).count("worktree ")
        if left != 1:
            failures.append(f"{sc['id']}: {left - 1} worktree(s) left registered")
        if len(commit) != 40:
            failures.append(f"{sc['id']}: bad commit id {commit!r}")
    except C.ToolError as exc:
        failures.append(f"{sc['id']}: {exc}")
    finally:
        shutil.rmtree(base_dir, ignore_errors=True)
    return failures
