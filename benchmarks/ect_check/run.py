#!/usr/bin/env python3
"""ECT check benchmark runner: bench, correctness gate, M1/M2 metrics.

  run.py bench     [--reps N] [--ids ...] [--warm-client-cmd TEMPLATE]
  run.py gate      [--reference-moss P] [--candidate-moss P] [--candidate-cmd T]
  run.py metrics
  run.py validate

`bench` (the default) measures every fixture, runs the gate and the metrics,
and writes results.csv, results.json and LOCAL_REPORT.md into
tmp/ect_check/<run-id>/.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ect_common as C  # noqa: E402
import edit_evidence  # noqa: E402
import report_dag  # noqa: E402
import report_replay  # noqa: E402
import report_rust  # noqa: E402
import report_scenarios  # noqa: E402
import rust_cmp as RC  # noqa: E402
import replay as RP  # noqa: E402
import churn as CH  # noqa: E402
import scenarios as S  # noqa: E402

MIN_REPS = 3
WARM_ENV = "ECT_WARM_CLIENT_CMD"
CANDIDATE_ENV = "ECT_CANDIDATE_CMD"
POSITION_MAP_ENV = "ECT_CANDIDATE_POSITION_MAP"


# --------------------------------------------------------------------------
# Measurement
# --------------------------------------------------------------------------

def measure_fixture(fx: dict, tc: C.Toolchain, template: str | None, mode: str,
                    reps: int, primes: int, run_dir: Path, warmup: bool = True,
                    stores: "C.StoreSet | None" = None) -> dict:
    """Median-of-reps wall/CPU/RSS for one fixture in one mode.

    cold: a fresh staged copy per rep (full, from-scratch check), each rep (the discarded
          warm-up included) against a fresh, empty store ($MOSS_STORE_DIR).
    warm: one staged copy, `primes` unmeasured runs, then reps in place, so a resident
          client keeps whatever state it keeps between requests; ONE store per fixture,
          kept across the primes and the timed reps.
    A provider package is built against the same store as its consumer series.
    """
    work = run_dir / "work" / f"{mode}-{fx['id']}"
    stores = stores or C.StoreSet(run_dir / "stores")
    samples, provider_samples, verdict = [], [], None
    repl = C.path_replacements(work, tc)
    timeout = C.fixture_timeout(fx)
    provider = bool(fx.get("provider"))
    last_store: dict = {"bytes": None, "files": None}

    def one(tcs: C.Toolchain, stage: bool, extra_env: dict | None = None, provider_sink: Path | None = None,
            record_provider: bool = False):
        """Optionally stage; for fixtures with a package boundary build the
        provider first (its own measured series, never part of the consumer
        sample), then run the measured consumer command."""
        values = C.stage_fixture(fx, work) if stage else \
            {"root": str(work), "path": str(work / fx["entry"]), "id": fx["id"],
             "kind": fx["kind"]}
        if stage and provider:
            pres = C.build_provider(
                fx, tcs, work, C.counter_env(provider_sink) if provider_sink else None)
            if record_provider:
                provider_samples.append(pres)
        argv = C.command_for(fx, {**tcs.values(), **values}, template)
        env = tcs.env()
        env.update(extra_env or {})
        return C.run_measured(argv, cwd=work, env=env, timeout=timeout,
                              stack_kib=C.fixture_stack(fx))

    def cold_one(**kw):
        st = stores.new(f"cold-{fx['id']}")
        try:
            return one(tc.with_store(st), **kw)
        finally:
            rec = stores.release(st)
            last_store.update(bytes=rec["bytes"], files=rec["files"])

    counters = None
    provider_counters = None
    counters_by_request = None
    if mode == "warm":
        wst = stores.new(f"warm-{fx['id']}")
        wtc = tc.with_store(wst)

        def warm_series(tcs, counting=False):
            C.stage_fixture(fx, work)
            if provider:
                C.build_provider(fx, tcs, work)
            recs = []
            for i in range(primes + reps):
                sink = run_dir / "counters" / f"{fx['id']}.warm{i}.jsonl" if counting else None
                if sink is not None:
                    sink.parent.mkdir(parents=True, exist_ok=True)
                    sink.unlink(missing_ok=True)
                res = one(tcs, stage=False, extra_env=C.counter_env(sink) if sink else None)
                if sink is not None:
                    recs.append(C.summarize_counters(C.read_counter_records(sink)))
                elif i >= primes:
                    samples.append(res)
            return recs

        warm_series(wtc)
        verdict = C.extract_verdict(samples[-1], repl)
        rec = stores.release(wst)
        last_store.update(bytes=rec["bytes"], files=rec["files"])
        if C.counters_available(template):
            # the SAME series (primes + timed requests) in its own store, so it evolves like the timed one
            cst = stores.new(f"warm-counters-{fx['id']}")
            counters_by_request = warm_series(tc.with_store(cst), counting=True)
            counters = counters_by_request[-1]
            stores.release(cst)
    else:
        if warmup:
            cold_one(stage=True)  # discarded: first run pays binary/page-cache page-in
        for _ in range(reps):
            res = cold_one(stage=True, record_provider=True)
            samples.append(res)
            verdict = C.extract_verdict(res, repl)
        sink = run_dir / "counters" / f"{fx['id']}.jsonl"
        psink = run_dir / "counters" / f"{fx['id']}.provider.jsonl"
        sink.parent.mkdir(parents=True, exist_ok=True)
        sink.unlink(missing_ok=True)
        psink.unlink(missing_ok=True)
        cold_one(stage=True, extra_env=C.counter_env(sink), provider_sink=psink)
        counters = C.summarize_counters(C.read_counter_records(sink))
        if provider:
            provider_counters = C.summarize_counters(C.read_counter_records(psink))
    shutil.rmtree(work, ignore_errors=True)

    status = C.status_of(verdict)
    # A timed-out rep carries its timeout as wall time; it must not feed a median.
    good = [s for s in samples if not s.get("timed_out")]
    n_timeout = len(samples) - len(good)
    row = {
        "fixture": fx["id"], "kind": fx["kind"], "mode": mode, "reps": reps,
        "expected_status": fx["expected_status"], "status": status,
        "expected_exit_code": fx.get("expected_exit_code"),
        # status_ok: manifest status AND (when set) exit code, and no rep timed out.
        "status_ok": n_timeout == 0 and all(
            C.fixture_ok(fx, C.extract_verdict(s, repl)) for s in samples),
        "timed_out_reps": n_timeout,
        "wall_ms_median": C.median([s["wall_ms"] for s in good]),
        "wall_ms_min": min((s["wall_ms"] for s in good), default=None),
        "wall_ms_max": max((s["wall_ms"] for s in good), default=None),
        "cpu_ms_median": C.median([s["cpu_ms"] for s in good]),
        "rss_kib_median": C.median([s["rss_kib"] for s in good]),
        "wall_ms_samples": [round(s["wall_ms"], 3) for s in samples],
        "category": fx.get("category"), "metadata": fx.get("metadata"),
        "fixture_path": fx["path"], "stack_kib": fx.get("stack_kib"),
        "exit_code": verdict.get("exit_code"),
        "timed_out": n_timeout > 0,
        "has_provider": provider, "store_bytes": last_store["bytes"], "store_files": last_store["files"],
        "counters_by_request": counters_by_request,
        "provider_wall_ms_median": C.median([s["wall_ms"] for s in provider_samples]),
        "provider_cpu_ms_median": C.median([s["cpu_ms"] for s in provider_samples]),
        "provider_rss_kib_median": C.median([s["rss_kib"] for s in provider_samples]),
        "provider_functions_analyzed": (provider_counters or {}).get("functions_analyzed"),
        "provider_checker_runs": (provider_counters or {}).get("checker_runs"),
        # M2 facts of the package's own source (provider for B1/fan-in packages).
        "local_internal_functions": ((provider_counters or counters or {}).get("internal_functions")),
        "local_complete_summaries": ((provider_counters or counters or {}).get("internal_complete")),
    }
    row.update(counters or {"checker_runs": None,
                            **{name: None for name, _ in C.WORK_COUNTERS}})
    return row


# --------------------------------------------------------------------------
# Correctness gate
# --------------------------------------------------------------------------

def run_gate(fixtures: list[dict], reference: C.Toolchain, candidate: C.Toolchain,
             candidate_template: str | None, run_dir: Path,
             scenarios: list[dict] | None = None, stores: "C.StoreSet | None" = None,
             position_map: str | None = None) -> dict:
    """Reference vs candidate: identical verdict and diagnostics on every fixture.

    Each side runs against its OWN store (a fresh one per fixture; the scenario gates keep
    each side's store across the steps of a sequence, the replay gate across the commits).
    `position_map` rewrites the candidate's verdict positions to the reference layout first.
    A provider build that fails on either side is a verdict: both provider verdicts are
    compared, the consumer step is marked skipped, and the gate finishes."""
    stores = stores or C.StoreSet(run_dir / "stores")
    results = []
    for fx in fixtures:
        verdicts, provider_verdicts, skipped = {}, {}, False
        for side, tc, template in (("reference", reference, None),
                                   ("candidate", candidate, candidate_template)):
            work = run_dir / "work" / f"gate-{side}-{fx['id']}"
            st = stores.new(f"gate-{side}-{fx['id']}")
            tcs = tc.with_store(st)
            pm = position_map if side == "candidate" else None
            try:
                values = C.stage_fixture(fx, work)
                provider_ok = True
                if fx.get("provider"):
                    pres, pv = C.build_provider_checked(fx, tcs, work)
                    provider_verdicts[side] = C.extract_verdict(pres, C.path_replacements(work, tc), pm,
                                                                {"fixture": fx["id"], "step": "provider"})
                    provider_ok = pres["rc"] == 0
                if provider_ok:
                    argv = C.command_for(fx, {**tcs.values(), **values}, template)
                    res = C.run_measured(argv, cwd=work, env=tcs.env(),
                                         timeout=C.fixture_timeout(fx), stack_kib=C.fixture_stack(fx))
                    verdicts[side] = C.extract_verdict(res, C.path_replacements(work, tc), pm,
                                                       {"fixture": fx["id"], "step": "check"})
                else:
                    verdicts[side] = None
                    skipped = True
            finally:
                stores.release(st)
                shutil.rmtree(work, ignore_errors=True)
        if skipped:
            # a provider failed on at least one side: the provider verdicts ARE the comparison
            pr, pc = provider_verdicts.get("reference"), provider_verdicts.get("candidate")
            identical = (verdicts["reference"] is None and verdicts["candidate"] is None and
                         S.gate_view(pr) == S.gate_view(pc))
            expected = verdicts["reference"] is not None and C.fixture_ok(fx, verdicts["reference"])
            note = ("PROVIDER BUILD DIFFERS: " + ", ".join(
                f"{side} {'failed' if v is None else 'passed'}" for side, v in verdicts.items())
                    if not identical else None)
            results.append({"fixture": fx["id"], "identical": identical,
                            "reference_matches_expected": expected, "candidate_matches_expected": False,
                            "candidate_note": note, "consumer_skipped": True,
                            "provider_reference": pr, "provider_candidate": pc,
                            "status": "fail" if verdicts["reference"] is None else C.status_of(verdicts["reference"]),
                            "reference": verdicts["reference"], "candidate": verdicts["candidate"]})
            continue
        identical = verdicts["reference"] == verdicts["candidate"]
        # `ok` is None for non-JSON output (e.g. a compiler crash with empty
        # stdout); fixture_ok also rejects timeouts and a wrong exit code.
        expected = C.fixture_ok(fx, verdicts["reference"])
        cand_ok = C.fixture_ok(fx, verdicts["candidate"])
        note = None
        if not cand_ok and not verdicts["candidate"].get("timed_out") and \
                verdicts["candidate"].get("ok") and fx["expected_status"] == "fail":
            note = "IMPROVED: candidate passes a fixture the manifest expects to fail"
        elif not cand_ok and expected:
            note = "REGRESSED: candidate no longer satisfies the manifest"
        results.append({"fixture": fx["id"], "identical": identical,
                        "reference_matches_expected": expected,
                        "candidate_matches_expected": cand_ok,
                        "candidate_note": note, "consumer_skipped": False,
                        "status": C.status_of(verdicts["reference"]),
                        "reference": verdicts["reference"],
                        "candidate": verdicts["candidate"]})
    # The gate is an equivalence check (candidate verdict == reference verdict)
    # plus reference-vs-manifest; manifest drift on the candidate is a note.
    sc_results = []
    if scenarios:
        manifest = C.load_manifest()
        sc_results = [S.gate_scenario(sc, S.fixture_of(manifest, sc), reference, candidate,
                                      candidate_template, run_dir, stores, position_map)
                      for sc in scenarios if sc["kind"] == "edit_sequence"]
        # replay gate: one store per side for the whole replay, kept across the commits
        replay_stores = {"reference": stores.new("gate-reference-b6"), "candidate": stores.new("gate-candidate-b6")}
        try:
            sc_results += [RP.gate_step(sc, reference, candidate, candidate_template, run_dir, replay_stores,
                                        position_map)
                           for sc in scenarios if sc["kind"] == RP.KIND]
        finally:
            for side, st in replay_stores.items():
                stores.release(st, f"gate-{side}-b6")
    passed = all(r["identical"] and r["reference_matches_expected"] for r in results) and \
        all(r["passed"] for r in sc_results)
    return {"passed": passed, "fixtures": len(results), "scenarios": sc_results,
            "candidate_command": candidate_template or "built-in cold command",
            "candidate_moss": str(candidate.moss),
            "reference_moss": str(reference.moss),
            "results": results}


# --------------------------------------------------------------------------
# M1 / M2
# --------------------------------------------------------------------------

def _package_dirs(root: Path) -> list[Path]:
    found = []
    for toml in sorted(root.rglob("Moss.toml")):
        rel = toml.relative_to(root).parts
        if any(p in ("build", "target", "__pycache__", "tests", "examples", "benches")
               for p in rel):
            continue
        found.append(toml.parent)
    return found


def _topo_order(pkgs: list[Path]) -> list[Path]:
    by_dir = {p.resolve(): p for p in pkgs}
    deps = {}
    for p in pkgs:
        data = tomllib.loads((p / "Moss.toml").read_text())
        deps[p.resolve()] = [(p / d["path"]).resolve()
                             for d in (data.get("dependencies") or {}).values()
                             if isinstance(d, dict) and "path" in d]
    order, seen = [], set()

    def visit(node):
        if node in seen:
            return
        seen.add(node)
        for dep in deps.get(node, []):
            visit(dep)
        if node in by_dir:
            order.append(by_dir[node])

    for p in by_dir:
        visit(p)
    return order


def run_metrics(tc: C.Toolchain, run_dir: Path, mode: str = "check",
                roots: list[Path] | None = None) -> dict:
    """M1: exported untyped generics reaching scheme_complete (sdk/, packages/).
    M2: internal (non-exported) functions whose local summary is complete.

    Works on a scratch copy of sdk/ and packages/ (margo writes build dirs),
    building packages in dependency order so each package's own checker run is
    isolated in its own counter sink.
    """
    scratch = run_dir / "metrics_work"
    if scratch.exists():
        shutil.rmtree(scratch)
    for root in roots or [C.REPO_ROOT / "sdk", C.REPO_ROOT / "packages"]:
        shutil.copytree(Path(root).resolve(), scratch / Path(root).resolve().name,
                        ignore=C.copy_ignore)
    pkgs = _topo_order(_package_dirs(scratch))
    rows, failed = [], []
    by_pkg: dict[Path, list[dict]] = {p: [] for p in pkgs}
    ran_ok: dict[Path, bool] = {}
    resolved = sorted(pkgs, key=lambda p: -len(str(p.resolve())))
    for pkg in pkgs:
        rel = pkg.relative_to(scratch).as_posix()
        sink = run_dir / "counters" / f"metrics-{mode}-{rel.replace('/', '_')}.jsonl"
        sink.parent.mkdir(parents=True, exist_ok=True)
        sink.unlink(missing_ok=True)
        env = tc.env()
        env.update(C.counter_env(sink))
        flags = ["--check"] if mode == "check" else []
        res = C.run_measured([str(tc.margo), "--root", str(pkg), *flags, "--json", "build"],
                             cwd=pkg, env=env, timeout=1200)
        ran_ok[pkg] = res["rc"] == 0
        # A run may (re)build dependencies; attribute every checker record to
        # the package that owns its source file. Records with no package-local
        # source (a main-only unit) fall back to the package being built.
        for rec in C.read_counter_records(sink):
            owner = pkg
            unit = rec.get("unit_source") or ""
            for cand in resolved:
                if unit and unit.startswith(str(cand.resolve()) + os.sep):
                    owner = cand
                    break
            if ran_ok[pkg] or owner is not pkg:
                by_pkg[owner].append(rec)
    for pkg in pkgs:
        rel = pkg.relative_to(scratch).as_posix()
        rec = C.metric_record(by_pkg[pkg])
        if rec is None or not ran_ok[pkg]:
            failed.append(rel)
            continue
        rows.append({
            "package": rel, "checker_runs": len(by_pkg[pkg]),
            "m1_exported_generics": rec["ect_m1_exported_generics"],
            "m1_scheme_complete": rec["ect_m1_scheme_complete"],
            "m2_internal_functions": rec["ect_m2_internal_functions"],
            "m2_internal_with_summary": rec["ect_m2_internal_with_summary"],
            "m2_internal_complete": rec["ect_m2_internal_complete"]})
    shutil.rmtree(scratch, ignore_errors=True)

    def total(key):
        return sum(r[key] for r in rows)

    def share(num, den):
        return (num / den) if den else None

    m1n, m1d = total("m1_scheme_complete"), total("m1_exported_generics")
    m2n, m2d = total("m2_internal_complete"), total("m2_internal_functions")
    eligible = total("m2_internal_with_summary")
    return {"mode": mode, "packages_measured": len(rows), "packages_failed": failed,
            "roots": [str(Path(x)) for x in (roots or ["sdk", "packages"])],
            "m1": {"scheme_complete": m1n, "exported_generics": m1d, "share": share(m1n, m1d)},
            # Headline M2: only internal functions with an untyped parameter
            # carry an open summary, so they are the only ones that can be
            # `complete`. Fully annotated functions are reported separately.
            "m2": {"complete": m2n, "eligible": eligible, "share": share(m2n, eligible),
                   "annotated_internal": m2d - eligible, "internal_functions": m2d},
            "per_package": rows}


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def git(*args: str) -> str:
    try:
        return subprocess.run(["git", *args], cwd=C.REPO_ROOT, text=True,
                              capture_output=True).stdout.strip()
    except OSError:
        return "unknown"


def fmt(value, digits=2):
    if value is None:
        return "n/a"
    return f"{value:.{digits}f}" if isinstance(value, float) else str(value)


def pct(share):
    return "n/a" if share is None else f"{share * 100:.1f}%"


CSV_FIELDS = ["compiler_path", "compiler_sha256", "compiler_flavor", "fixture", "scenario", "step",
              "base_fixture", "kind", "mode", "reps", "expected_status", "status",
              "status_ok", "wall_ms_median", "wall_ms_min", "wall_ms_max", "wall_ms_first",
              "cpu_ms_median", "rss_kib_median", "tree_rss_peak_kib_median", "checker_runs",
              "store_bytes", "store_files", "units_checked", "units_fresh", "spread", "attempts", "spread_flagged", "block_attempts",
              "block_noisy", "diagnostic",
              "exit_code", "timed_out", "provider_wall_ms_median", "provider_cpu_ms_median",
              "provider_rss_kib_median", "provider_functions_analyzed", "provider_checker_runs",
              *[name for name, _ in C.WORK_COUNTERS]]


WT_CSV_FIELDS = ["compiler_flavor", "scenario", "base_fixture", "mode", "n", "schedule", "reps",
                 "commit", "status_ok", "group_wall_ms_median", "group_wall_ms_min",
                 "group_wall_ms_max", "total_cpu_ms_median", "sum_rss_kib_median",
                 "max_rss_kib_median", "first_check_ms_wt0", "first_check_ms_later_median",
                 "first_check_ms_max", "first_check_ms_by_worktree", "functions_analyzed",
                 "source_function_checks", "checker_runs", "loadavg_start", "cpus"]


def write_outputs(out: Path, meta: dict, rows: list[dict], gate: dict | None,
                  metrics: dict | None, scenarios: list[dict] | None = None,
                  evidence: list[dict] | None = None, wt_rows: list[dict] | None = None,
                  churn_records: list[dict] | None = None, rust_scs: list[dict] | None = None) -> None:
    out.mkdir(parents=True, exist_ok=True)
    scenarios = scenarios or []
    wt_rows = wt_rows or []
    for row in wt_rows:
        row["compiler_flavor"] = meta["moss_flavor"]
    if wt_rows:
        with (out / "results_worktrees.csv").open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=WT_CSV_FIELDS, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(wt_rows)
    for row in rows:
        row.update(compiler_path=meta["moss"], compiler_sha256=meta["moss_sha256"],
                   compiler_flavor=meta["moss_flavor"])
    with (out / "results.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    replay_stats = report_replay.analysis([r for r in rows if r.get("kind") != "cargo"], scenarios, churn_records)
    if replay_stats:
        report_replay.write_csv(out / "results_replay.csv", replay_stats)
    (out / "results.json").write_text(json.dumps(
        {"meta": meta, "rows": rows, "worktree_rows": wt_rows, "evidence": evidence,
         "gate": gate, "metrics": metrics, "replay": replay_stats, "churn": churn_records,
         "dag_analysis": report_dag.analysis(rows)}, indent=2) + "\n")

    lines = ["# ECT Check Benchmark - Local Report", "",
             f"**Suite:** `{meta['suite']}` {meta['suite_version']} "
             f"({meta['fixtures']} fixtures, seed {meta['seed']})  ",
             f"**Run:** `{meta['run_id']}` ({meta['timestamp']})  ",
             f"**Compiler:** `{meta['moss']}`  ",
             f"**Compiler SHA256:** `{meta['moss_sha256']}`  ",
             f"**Build flavor:** `{meta['moss_flavor']}`"
             f"{'' if meta['moss_flavor'] == 'opt' else '  **WARNING: not an optimized build; timings are NOT comparable to compiler-perf/package-perf baselines**'}  ",
             f"**Repo:** `{meta['git_branch']}` @ `{meta['git_sha']}`"
             f"{' (dirty tree)' if meta['git_dirty'] else ''}  ",
             f"**Platform:** `{meta['platform']}` ({meta['cpus']} CPUs)  ",
             f"**Reps:** {meta['reps']} timed runs per fixture/mode (median reported), "
             "plus one separate counter run (counters are not collected during timed runs)  ",
             f"**Warm client:** {meta['warm_client'] or 'not configured'}  ",
             f"**Stores:** every series ran against its own `$MOSS_STORE_DIR` under the run directory "
             f"({len(meta.get('stores') or [])} stores, {sum(x['bytes'] for x in meta.get('stores') or [])} bytes "
             f"in total at release; {'kept' if meta.get('keep_stores') else 'deleted after their series'}); "
             "today's compiler ignores the variable, so every size is 0 unless a store-using candidate runs.  ",
             f"**Position map:** {meta.get('position_map') or 'none (identity)'}  ", ""]
    nz = meta.get("noise")
    if nz:
        lines += [f"**Noise probe:** loadavg start {nz['start']['loadavg']}, end {nz['end']['loadavg']}; "
                  f"calibration `{nz['start']['calibration_fixture']}` median wall "
                  f"{nz['start']['calibration_wall_ms_median']:.1f} ms at start, "
                  f"{nz['end']['calibration_wall_ms_median']:.1f} ms after the timed loop "
                  f"(drift {nz['drift_pct']}%).  "]
        if not nz["quiet"]:
            lines += ["", f"**WARNING: NOT a quiet run (calibration drift {nz['drift_pct']}%, limit "
                      f"{NOISE_WARN_PCT:.0f}%; 1-min loadavg at start "
                      f"{nz['start']['loadavg'][0]:.2f}, limit {LOAD_WARN}). Other work or "
                      "throttling was present, so the wall/CPU figures below are not "
                      "trustworthy baselines.**"]
        lines.append("")
    lines += [
             "## 1. Timing, memory and compiler counters", "",
             "Wall, CPU and RSS are medians over the timed reps. CPU is user+system "
             "of the whole process tree. RSS is the largest single process in the tree "
             "(not a sum). Counters are summed over the checker runs one command starts.", ""]
    for mode in ("cold", "warm"):
        mode_rows = [r for r in rows if r["mode"] == mode and not r.get("scenario")]
        if not mode_rows:
            continue
        lines += [f"### Mode: {mode}", "",
                  "| Fixture | Kind | Status | Wall ms | CPU ms | RSS KiB | Checker runs | "
                  "Fns analyzed | Spec evals | Spec hits | Spec keys |",
                  "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for r in mode_rows:
            status = r["status"] + ("" if r["status_ok"] else (
                " **(UNEXPECTED)**" if mode == "cold" else " **(candidate differs from manifest: see gate notes)**"))
            if r.get("timed_out_reps"):
                status += f" **TIMED OUT x{r['timed_out_reps']} (excluded from medians)**"
            lines.append(
                f"| `{r['fixture']}` | {r['kind']} | {status} | "
                f"{fmt(r['wall_ms_median'])} ({fmt(r['wall_ms_min'], 1)}-{fmt(r['wall_ms_max'], 1)}) | "
                f"{fmt(r['cpu_ms_median'])} | {fmt(r['rss_kib_median'], 0)} | "
                f"{fmt(r['checker_runs'])} | {fmt(r['functions_analyzed'])} | "
                f"{fmt(r['specialization_evaluations'])} | {fmt(r['specialization_hits'])} | "
                f"{fmt(r['specialization_keys'])} |")
        lines.append("")
    lines += ["Specialization counters: evals = `specialization_requests`, hits = "
              "`reused_specialization_results`, keys = `distinct_specialization_keys` "
              "(the compiler's own `MOSS_PROFILE` names; today's memo is in-process, so "
              "hits are intra-run reuse only).", ""]

    lines += report_dag.sections([r for r in rows if not r.get("scenario")])
    lines += report_scenarios.sections([r for r in rows if r.get("kind") != "cargo"], scenarios, evidence, wt_rows, meta)
    lines += report_replay.sections(replay_stats, meta)
    lines += report_rust.sections(rows, scenarios or [], rust_scs or [], gate, meta)

    lines += ["## 2. Correctness gate (reference vs candidate)", ""]
    if gate is None:
        lines += ["Not run.", ""]
    else:
        lines += [f"**Result: {'PASS' if gate['passed'] else 'FAIL'}** over "
                  f"{gate['fixtures']} fixtures. Reference: `{gate['reference_moss']}`. "
                  f"Candidate: `{gate['candidate_moss']}` via {gate['candidate_command']}.", ""]
        lines += ["The gate requires candidate verdict == reference verdict and reference == "
                  "manifest (status, expected exit code, no timeout). A candidate that "
                  "differs from the manifest is annotated IMPROVED/REGRESSED.", "",
                  "| Fixture | Status | Verdicts identical | Reference matches manifest | Candidate note |",
                  "|---|---|---|---|---|"]
        for r in gate["results"]:
            lines.append(f"| `{r['fixture']}` | {r['status']} | "
                         f"{'yes' if r['identical'] else '**NO**'} | "
                         f"{'yes' if r['reference_matches_expected'] else '**NO**'} | "
                         f"{r.get('candidate_note') or ''} |")
        if gate.get("scenarios"):
            lines += ["", "Edit sequences (every check step compared; reference == step expectation). "
                      "margo's `checked` / `skipped` package lists are volatile here (a client may "
                      "legitimately answer a repeat from a store) and shown as information only; "
                      "everything else, including diagnostics and exit codes, is strict:", "",
                      "| Scenario | Steps identical | Reference matches expectation | checked/skipped differ (info) |",
                      "|---|---|---|---|"]
            for sg in gate["scenarios"]:
                lines.append(f"| `{sg['scenario']}` | "
                             f"{'yes' if all(x['identical'] for x in sg['steps']) else '**NO**'} | "
                             f"{'yes' if all(x['reference_matches_expected'] for x in sg['steps']) else '**NO**'} | "
                             f"{'yes' if any(x['bookkeeping_reference'] != x['bookkeeping_candidate'] for x in sg['steps']) else 'no'} |")
        for sg in gate["scenarios"]:
            pin = sg.get("reference_pin")
            if pin:
                lines += ["", f"B6 reference compiler sha256 `{pin['sha256'][:12]}`: " +
                          ("the pinned binary the expectations were recorded with." if pin["matches"]
                           else "**NOT the pinned binary** - " + str(pin["note"]))]
                break
        same = gate["reference_moss"] == gate["candidate_moss"] and \
            gate["candidate_command"] == "built-in cold command"
        if same:
            lines += ["", "Reference and candidate are the same compiler today, so this "
                      "pass only shows the gate is deterministic and wired; it becomes "
                      "meaningful once the candidate differs."]
        lines.append("")

    lines += ["## 3. Metrics M1 / M2 (sdk/ and packages/)", ""]
    if metrics is None:
        lines += ["Not run.", ""]
    else:
        lines += ["`scheme_complete` and `complete` are checker facts read at the end of "
                  "each package's source check. Their values depend on the margo mode "
                  "(`--check` interface check vs full `build`), so each mode is reported "
                  "separately; neither is hidden.", ""]
        for mode, mres in metrics.items():
            m1, m2 = mres["m1"], mres["m2"]
            lines += [
                f"### margo mode: {mode}", "",
                f"Roots: {', '.join(mres['roots'])}. Packages measured: {mres['packages_measured']}; "
                f"failed and dropped from every figure below: {len(mres['packages_failed'])} "
                f"{mres['packages_failed'] or ''}", "",
                "| Metric | Numerator | Denominator | Share |", "|---|---:|---:|---:|",
                f"| **M1** exported generics reaching `scheme_complete` | {m1['scheme_complete']} | "
                f"{m1['exported_generics']} | {pct(m1['share'])} |",
                f"| **M2** eligible internal functions (open summary) that are `complete` | "
                f"{m2['complete']} | {m2['eligible']} | {pct(m2['share'])} |",
                f"| (not in M2) fully annotated internal functions, never eligible | "
                f"{m2['annotated_internal']} | {m2['internal_functions']} total internal | n/a |",
                "",
                "| Package | Exp. generics | scheme_complete | Internal fns | eligible (open summary) | complete |",
                "|---|---:|---:|---:|---:|---:|"]
            for r_ in mres["per_package"]:
                if r_["m1_exported_generics"] or r_["m2_internal_functions"]:
                    lines.append(f"| `{r_['package']}` | {r_['m1_exported_generics']} | "
                                 f"{r_['m1_scheme_complete']} | {r_['m2_internal_functions']} | "
                                 f"{r_['m2_internal_with_summary']} | {r_['m2_internal_complete']} |")
            lines.append("")
        lines += ["Denominators differ between modes because a package whose margo run "
                  "fails in one mode is dropped from that mode only.", ""]
        lines += ["Definitions: an *exported generic* is a package-local `export fn` with at "
                  "least one untyped (inferred) parameter. An *internal function* is a "
                  "package-local non-exported function (imported interface functions are "
                  "excluded). **M2 is only meaningful over eligible functions**: only "
                  "callables with an untyped parameter carry an open summary. `complete` "
                  "is also leaf-biased (callers of inferred callees are not complete; "
                  "open arithmetic such as `i = i + a` is incomplete), so M2 is a weak "
                  "proxy for how much a fact store could skip. These are counts of "
                  "checker facts; nothing is recomputed. Per-package numbers come from "
                  "the checker run that saw the package's own source "
                  "(`ect_common.metric_record`).", ""]

    lines += ["## 4. Caveats", "",
              "- Cold mode starts a fresh process (package fixtures: `margo --check build` from "
              "an empty build directory) per rep; no daemon, no persistent store.",
              "- Wall times for sub-10 ms single-file fixtures are dominated by process "
              "startup and are noisy on WSL2; compare medians and ranges, not single digits.",
              "- Counters come from a separate run with `MOSS_PROFILE_COMPILER` enabled, "
              "so they never perturb the timed reps.",
              "- Counters are `n/a` for warm mode: a future client is not required to "
              "expose them.", ""]
    (out / "LOCAL_REPORT.md").write_text("\n".join(lines))


def run_churn(b6: list[dict], tc: C.Toolchain, run_dir: Path) -> list[dict]:
    """Summary churn for each replayed commit (untimed; runs after every timed
    measurement so its many query processes cannot disturb a timing)."""
    records = []
    for sc in b6:
        rec = RP.churn_step(sc, tc, run_dir / "work" / f"churn-{sc['id']}")
        records.append(rec)
        print(f"churn {sc['id']}: " + (
            f"{rec['n_changed']} summaries changed (own {rec['n_changed_own']}, other {rec['n_changed_other']}), "
            f"+{len(rec['added'])}/-{len(rec['removed'])} functions" if rec["available"]
            else f"unavailable ({rec['unavailable_reason'][:90]})"), flush=True)
    return records


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def add_common(ap):
    ap.add_argument("--moss", default=os.environ.get("MOSS") or str(C.DEFAULT_MOSS))
    ap.add_argument("--margo", default=str(C.DEFAULT_MARGO))
    ap.add_argument("--ids", nargs="*", help="restrict to these fixture ids (scenarios are then "
                    "skipped unless --scenario-ids is given)")
    ap.add_argument("--scenario-ids", nargs="*", help="scenario ids (B3 edits, B4 sequences, B5 "
                    "worktrees), or 'all'; default: every scenario when --ids is not given")
    ap.add_argument("--out", help="output dir (default tmp/ect_check/<run-id>)")


def add_gate_args(ap):
    ap.add_argument("--reference-moss", help="reference compiler (default --moss)")
    ap.add_argument("--candidate-moss", help="candidate compiler (default --moss)")
    ap.add_argument("--candidate-position-map", default=os.environ.get(POSITION_MAP_ENV),
                    help="command that rewrites the CANDIDATE verdict's positions to the reference layout "
                         "before the gate compares (Phase 28.6 anchors diagnostics to declarations); stdin: "
                         "{kind, context, verdict} JSON, stdout: the rewritten verdict JSON. Identity when "
                         f"absent. Env: {POSITION_MAP_ENV}")
    ap.add_argument("--keep-stores", action="store_true",
                    help="keep the per-series stores ($MOSS_STORE_DIR) under the run directory instead of "
                         "deleting them after their series (their final sizes are always recorded)")
    ap.add_argument("--require-rust", action="store_true",
                    help="fail instead of skipping when cargo is unavailable (default: a loud FAIL-OPEN "
                         "SKIPPED line, recorded in the gate result and the report)")
    ap.add_argument("--rust-candidate-cmd", default=os.environ.get(RC.CANDIDATE_ENV),
                    help="candidate command template for the Rust (cargo) sequences; placeholders "
                         "{cargo} {path} {root} {id}; must print cargo --message-format=json. "
                         f"Env: {RC.CANDIDATE_ENV}. Default: the built-in cargo check command.")
    ap.add_argument("--candidate-cmd", default=os.environ.get(CANDIDATE_ENV),
                    help="candidate command template; placeholders {moss} {margo} "
                         "{path} {root} {id}. Must print moss-agent-1 or margo-1 JSON. "
                         f"Env: {CANDIDATE_ENV}. Default: the built-in cold command.")


def print_metrics(metrics: dict) -> None:
    for mode, m in metrics.items():
        print(f"[margo {mode}] M1 {pct(m['m1']['share'])} "
              f"({m['m1']['scheme_complete']}/{m['m1']['exported_generics']}); "
              f"M2 {pct(m['m2']['share'])} ({m['m2']['complete']}/{m['m2']['eligible']} eligible; "
              f"{m['m2']['annotated_internal']} annotated internal fns not eligible); "
              f"{m['packages_measured']} packages, {len(m['packages_failed'])} failed")


CALIBRATION_FIXTURE = "b2_k04"   # ~0.5 s, single file, no provider
NOISE_WARN_PCT = 10.0
LOAD_WARN = 1.0   # 1-minute loadavg at start above this = other work was running


def noise_probe(tc: C.Toolchain, manifest: dict, run_dir: Path, label: str) -> dict:
    """Machine-noise probe: 1/5/15-minute loadavg plus the median wall of a
    small calibration fixture (5 cold reps). Taken before and after the timed
    loop; a drift above ~10% means the run was not on a quiet machine."""
    fx = next(f for f in manifest["fixtures"] if f["id"] == CALIBRATION_FIXTURE)
    work = run_dir / "work" / f"calib-{label}"
    walls = []
    for _ in range(5):
        values = C.stage_fixture(fx, work)
        argv = C.command_for(fx, {**tc.values(), **values}, None)
        walls.append(C.run_measured(argv, cwd=work, env=tc.env(),
                                    timeout=C.fixture_timeout(fx))["wall_ms"])
    shutil.rmtree(work, ignore_errors=True)
    return {"loadavg": list(os.getloadavg()), "calibration_fixture": fx["id"],
            "calibration_wall_ms_median": C.median(walls),
            "calibration_wall_ms_samples": [round(w, 2) for w in walls]}


def f_(v):
    return "n/a" if v is None else f"{v:.2f}"


def block_probe(tc: C.Toolchain, manifest: dict, run_dir: Path) -> float:
    """Mid-run probe: one number combining a single-process calibration
    (b2_k04, 3 cold reps) and a multi-process one (b4_floor_pkg through margo,
    5 reps; interference in process trees shows there): k04 median + 10 x
    floor_pkg median, in ms. Compared with the same probe taken before the loop."""
    total = 0.0
    for fid, reps, weight in (("b2_k04", 3, 1.0), ("b4_floor_pkg", 5, 10.0)):
        fx = next(f for f in manifest["fixtures"] if f["id"] == fid)
        work = run_dir / "work" / f"blockprobe-{fid}"
        walls = []
        for _ in range(reps):
            values = C.stage_fixture(fx, work)
            walls.append(C.run_measured(C.command_for(fx, {**tc.values(), **values}, None), cwd=work,
                                        env=tc.env(), timeout=C.fixture_timeout(fx))["wall_ms"])
        shutil.rmtree(work, ignore_errors=True)
        total += weight * C.median(walls)
    return total


def noise_summary(start: dict, end: dict) -> dict:
    a, b = start["calibration_wall_ms_median"], end["calibration_wall_ms_median"]
    drift = abs(b - a) / min(a, b) * 100.0
    busy_start = start["loadavg"][0] > LOAD_WARN
    return {"start": start, "end": end, "drift_pct": round(drift, 1),
            "busy_at_start": busy_start,
            "quiet": drift <= NOISE_WARN_PCT and not busy_start}


def make_run_dir(args) -> tuple[str, Path]:
    run_id = C.time.strftime("%Y%m%d-%H%M%S")
    return run_id, (Path(args.out) if args.out else C.TMP_ROOT / run_id).resolve()


def startup_decomposition(tc: C.Toolchain, manifest: dict, run_dir: Path, reps: int = 11) -> list[dict]:
    """Where the ~45 ms of a package check goes: median wall/CPU of bare process
    starts, so margo's Python/driver overhead is its own number."""
    floor_file = next(f for f in manifest["fixtures"] if f["id"] == "b4_floor_file")
    floor_pkg = next(f for f in manifest["fixtures"] if f["id"] == "b4_floor_pkg")
    wf, wp = run_dir / "work" / "startup-file", run_dir / "work" / "startup-pkg"
    vf, vp = C.stage_fixture(floor_file, wf), None
    cases = [("python3 -c pass", ["python3", "-c", "pass"], wf, None),
             ("margo --help (Python start + imports + argparse)", [str(tc.margo), "--help"], wf, None),
             ("moss check floor file (no margo)", C.command_for(floor_file, {**tc.values(), **vf}, None), wf, None),
             ("margo --check build floor package (empty build dir)", None, wp, floor_pkg)]
    out = []
    for label, argv, cwd, fx in cases:
        walls, cpus = [], []
        for _ in range(reps + 1):
            if fx is not None:
                vp = C.stage_fixture(fx, cwd)
                argv = C.command_for(fx, {**tc.values(), **vp}, None)
            r = C.run_measured(argv, cwd=cwd, env=tc.env(), timeout=60)
            walls.append(r["wall_ms"])
            cpus.append(r["cpu_ms"])
        out.append({"case": label, "wall_ms_median": C.median(walls[1:]), "cpu_ms_median": C.median(cpus[1:]),
                    "reps": reps})
    shutil.rmtree(wf, ignore_errors=True)
    shutil.rmtree(wp, ignore_errors=True)
    return out


def pick_scenarios(manifest: dict, args) -> list[dict]:
    """Moss scenarios (the Team 5 Rust sequences are picked by rust_cmp.pick)."""
    ids = getattr(args, "scenario_ids", None)
    if ids:
        chosen = S.select_scenarios(manifest, None if ids == ["all"] else ids)
    elif getattr(args, "ids", None):
        return []
    else:
        chosen = manifest.get("scenarios", [])
    return [sc for sc in chosen if sc["kind"] != RC.SEQ_KIND]


RUST_SKIPPED: list[str] = []     # reasons the Rust sequences did not run (FAIL-OPEN for Rust)


def rust_toolchains(args, scs: list[dict]):
    """(reference, candidate) cargo toolchains, or (None, None) with a loud
    SKIPPED line when cargo is not usable (never a crash). A skip is recorded in
    RUST_SKIPPED so the gate result and the report say the Rust part did not run;
    `--require-rust` turns it into a failure."""
    if not scs:
        return None, None
    try:
        ref = RC.find_rust()
    except RC.RustUnavailable as exc:
        msg = f"Rust comparison SKIPPED ({len(scs)} sequences NOT RUN): {exc}"
        RUST_SKIPPED.append(msg)
        print(f"*** FAIL-OPEN: {msg} ***", file=sys.stderr)
        if getattr(args, "require_rust", False):
            raise C.ToolError(msg + " (--require-rust)")
        return None, None
    return ref, ref


def run_rust_gate(scs: list[dict], manifest: dict, ref, cand, template: str | None, run_dir: Path,
                  position_map: str | None = None) -> list[dict]:
    return [RC.gate_scenario(sc, RC.fixture_by_id(manifest, sc["fixture"]), ref, cand, template, run_dir,
                             position_map)
            for sc in scs]


def gate_toolchains(args):
    ref = C.Toolchain(Path(args.reference_moss or args.moss), Path(args.margo))
    cand = C.Toolchain(Path(args.candidate_moss or args.moss), Path(args.margo))
    return ref, cand


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    b = sub.add_parser("bench")
    add_common(b)
    add_gate_args(b)
    b.add_argument("--reps", type=int, default=MIN_REPS)
    b.add_argument("--warm-client-cmd", default=os.environ.get(WARM_ENV),
                   help="warm-client command template (same placeholders); measured as "
                        f"mode 'warm' after --warm-primes unmeasured runs. Env: {WARM_ENV}")
    b.add_argument("--warm-primes", type=int, default=1)
    b.add_argument("--warm-store", action="store_true",
                   help="Phase 28.6 warm series: the SAME built-in commands as cold, but one store "
                        "($MOSS_STORE_DIR) kept across the primes and timed reps instead of a fresh one per rep "
                        "(no socket client). Combine with --moss <candidate>. Counters are reported.")
    b.add_argument("--no-warmup", action="store_true",
                   help="do not discard one cold warm-up run per fixture before the timed reps")
    b.add_argument("--no-gate", action="store_true")
    b.add_argument("--no-evidence", action="store_true",
                   help="skip the B3 query evidence (CPU heavy, runs after the timed loop)")
    b.add_argument("--no-churn", action="store_true",
                   help="skip the B6 summary-churn pass (untimed; many query processes, runs after the timed loop)")
    b.add_argument("--no-metrics", action="store_true")
    b.add_argument("--metrics-modes", default="check,build",
                   help="comma list of margo modes: check (--check) and/or build")
    b.add_argument("--allow-non-opt", action="store_true",
                   help="permit timing a non-optimized compiler (loud warning in report)")
    g = sub.add_parser("gate")
    add_common(g)
    add_gate_args(g)
    ev = sub.add_parser("evidence", help="B3 query evidence only (moss inspect before/after each edit)")
    add_common(ev)
    m = sub.add_parser("metrics")
    add_common(m)
    m.add_argument("--metrics-modes", default="check,build",
                   help="comma list of margo modes: check (--check) and/or build")
    m.add_argument("--root", nargs="+", help="package-tree roots to measure instead of "
                   "sdk/ and packages/ (e.g. benchmarks/ect_check/fixtures/trivial/pkg_pass)")
    v = sub.add_parser("validate")
    add_common(v)
    rr = sub.add_parser("replay-record", help="record the reference expectations of every B6 step "
                        "(writes replay_expectations.json; run before generate.py --regenerate)")
    add_common(rr)
    rc = sub.add_parser("replay-check", help="B6 determinism: run every selected step twice; "
                        "classification and counters must repeat exactly (timing excluded)")
    add_common(rc)
    rc.add_argument("--with-churn", action="store_true", help="also repeat the summary-churn pass")
    ch = sub.add_parser("churn", help="B6 summary churn only (untimed)")
    add_common(ch)
    s = sub.add_parser("selftest", help="RSS-independence and gate self-tests")
    add_common(s)

    args = ap.parse_args(sys.argv[1:] or ["bench"])
    cmd = args.cmd or "bench"
    manifest = C.load_manifest()
    fixtures = C.select_fixtures(manifest, getattr(args, "ids", None))

    if cmd == "selftest":
        import selftest
        return selftest.run(Path(args.moss), Path(args.margo))

    if cmd == "validate":
        return subprocess.call([sys.executable, str(C.SUITE_DIR / "generate.py"),
                                "--validate-only", "--moss", args.moss, "--margo", args.margo])

    if cmd == "replay-record":
        tc = C.Toolchain(Path(args.moss), Path(args.margo))
        run_id, run_dir = make_run_dir(args)
        run_dir.mkdir(parents=True, exist_ok=True)
        exp = RP.record_expectations(tc, run_dir)
        RP.EXPECTATIONS_PATH.write_text(json.dumps(exp, indent=1, sort_keys=True) + "\n")
        print(f"recorded {len(exp)} steps -> {RP.EXPECTATIONS_PATH}")
        return 0

    S.verify_frozen(manifest, fixtures, pick_scenarios(manifest, args))
    tc = C.Toolchain(Path(args.moss), Path(args.margo))
    run_id, run_dir = make_run_dir(args)
    run_dir.mkdir(parents=True, exist_ok=True)
    # Every series runs against its own store under the run directory ($MOSS_STORE_DIR; see
    # ect_common.Toolchain); an invocation without an explicit series store gets a fresh one too.
    stores = C.StoreSet(run_dir / "stores", keep=getattr(args, "keep_stores", False))
    C.STORES_ADHOC = run_dir / "stores" / "adhoc"

    if cmd == "replay-check":
        b6 = [sc for sc in pick_scenarios(manifest, args) if sc["kind"] == RP.KIND]
        bad = []
        for sc in b6:
            a = RP.fingerprint_step(sc, tc, run_dir)
            b = RP.fingerprint_step(sc, tc, run_dir)
            exp_ok = all(RP.expect_ok_obs(RP.expectation_for(sc, l), o) for l, o in a["observed"].items())
            same = a == b
            print(f"{sc['id']}: repeat {'identical' if same else 'DIFFERS'}; frozen expectations "
                  f"{'met' if exp_ok else 'NOT MET'}", flush=True)
            if not (same and exp_ok):
                bad.append(sc["id"])
        if args.with_churn:
            ra, rb = run_churn(b6, tc, run_dir), run_churn(b6, tc, run_dir)
            for x, y in zip(ra, rb):
                if x != y:
                    bad.append(f"churn:{x['scenario']}")
        print("replay-check:", "PASS" if not bad else f"FAIL {bad}", f"({len(b6)} steps)")
        return 0 if not bad else 1

    if cmd == "churn":
        b6 = [sc for sc in pick_scenarios(manifest, args) if sc["kind"] == RP.KIND]
        recs = run_churn(b6, tc, run_dir)
        (run_dir / "churn.json").write_text(json.dumps(recs, indent=2, sort_keys=True) + "\n")
        print(run_dir / "churn.json")
        return 0

    if cmd == "gate":
        ref, cand = gate_toolchains(args)
        gate = run_gate(fixtures, ref, cand, args.candidate_cmd, run_dir,
                        pick_scenarios(manifest, args), stores, args.candidate_position_map)
        gate["stores"] = stores.summary()
        gate["position_map"] = args.candidate_position_map
        rust_scs = RC.pick(manifest, args)
        rref, rcand = rust_toolchains(args, rust_scs)
        if rref is not None:
            RC.verify_frozen(manifest, rust_scs)
            gate["rust"] = run_rust_gate(rust_scs, manifest, rref, rcand, args.rust_candidate_cmd, run_dir,
                                         args.candidate_position_map)
            gate["passed"] = gate["passed"] and all(r["passed"] for r in gate["rust"])
        (run_dir / "gate.json").write_text(json.dumps(gate, indent=2) + "\n")
        for sg in gate["scenarios"]:
            if sg.get("reference_note"):
                print(f"NOTE {sg['scenario']}: {sg['reference_note']}")
                break
        gate["rust_skipped"] = list(RUST_SKIPPED)
        (run_dir / "gate.json").write_text(json.dumps(gate, indent=2) + "\n")
        print(f"gate: {'PASS' if gate['passed'] else 'FAIL'} ({gate['fixtures']} fixtures, "
              f"{len(gate['scenarios'])} Moss sequences, {len(gate.get('rust', []))} Rust sequences)"
              + (" *** Rust part SKIPPED (FAIL-OPEN for Rust: cargo unavailable) ***" if RUST_SKIPPED else "")
              + f"; {run_dir / 'gate.json'}")
        return 0 if gate["passed"] else 1

    if cmd == "evidence":
        scs = [sc for sc in (pick_scenarios(manifest, args) if (args.scenario_ids or args.ids) else
                             manifest.get("scenarios", [])) if sc["family"] == "b3_edits"]
        evidence = [edit_evidence.evidence_for(sc, S.fixture_of(manifest, sc), tc.moss, run_dir / "evidence")
                    for sc in scs]
        (run_dir / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
        for e in evidence:
            print(f"{e['scenario']}: {e['summary']} (ideal recheck {len(e['ideal_recheck'])})")
        print(run_dir / "evidence.json")
        return 0

    if cmd == "metrics":
        roots = [Path(x) for x in args.root] if args.root else None
        metrics = {m: run_metrics(tc, run_dir, m, roots) for m in args.metrics_modes.split(",")}
        (run_dir / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
        print_metrics(metrics)
        print(f"{run_dir / 'metrics.json'}")
        return 0

    # bench
    if tc.flavor != "opt":
        msg = (f"{tc.moss} has build flavor '{tc.flavor}', not 'opt'. Timing/RSS "
               "from -O0/test builds are not comparable to compiler-perf baselines.")
        if not args.allow_non_opt:
            raise SystemExit("refusing to time: " + msg +
                             " Build with `make moss-opt` or pass --allow-non-opt.")
        print("WARNING: " + msg, file=sys.stderr)
    if args.reps < MIN_REPS:
        raise SystemExit(f"--reps must be >= {MIN_REPS}")
    rows = []
    manifest_all = C.load_manifest()
    noise_start = noise_probe(tc, manifest_all, run_dir, "start") \
        if any(f["id"] == CALIBRATION_FIXTURE for f in manifest_all["fixtures"]) else None
    modes = ["cold"] + (["warm"] if args.warm_client_cmd or args.warm_store else [])
    for fx in fixtures:
        for mode in modes:
            template = args.warm_client_cmd if mode == "warm" else None
            row = measure_fixture(fx, tc, template, mode, args.reps,
                                  args.warm_primes, run_dir, not args.no_warmup, stores)
            rows.append(row)
            print(f"{mode:4} {fx['id']}: wall {row['wall_ms_median']:.2f} ms, "
                  f"status {row['status']}", flush=True)
    chosen_scenarios = pick_scenarios(manifest, args)
    wt_rows: list[dict] = []
    block_probes: list[dict] = []
    b6_steps = [sc for sc in chosen_scenarios if sc["kind"] == RP.KIND]
    for mode in modes:
        if not b6_steps:
            break
        template = args.warm_client_cmd if mode == "warm" else None
        base_probe = C.median([block_probe(tc, manifest_all, run_dir) for _ in range(3)])

        def log_probe(p, mode=mode):
            print(f"  probe {mode} block@{p['first_step']} try {p['attempt']}: drift {p['drift_pct']}%"
                  f"{' NOISY: block re-measured' if p['noisy'] else ''}", flush=True)
        # B6 store policy: one store per replay run, kept across the commits (see replay.ReplayStores)
        rs = RP.ReplayStores(stores, mode, args.reps)
        step_rows, probes = RP.run_blocks(
            b6_steps,
            lambda sc: RP.measure_step(sc, tc, template, mode, args.reps, args.warm_primes, run_dir,
                                       not args.no_warmup, rs),
            lambda: block_probe(tc, manifest_all, run_dir), base_probe, log=log_probe,
            snapshot=rs.snapshot, restore=rs.restore, drop=rs.drop_snapshot)
        rs.finish()
        block_probes += [{**p, "mode": mode, "baseline_probe_ms": base_probe} for p in probes]
        for sc, new in zip(b6_steps, step_rows):
            rows.extend(new)
            print(f"{mode:4} {sc['id']}: " + ", ".join(
                f"{r['step']} {r['wall_ms_median']:.1f} ms ({r['status']}, spread {f_(r['spread'])}, "
                f"attempts {r['attempts']})" for r in new), flush=True)
    for sc in chosen_scenarios:
        if sc["kind"] == RP.KIND:
            continue
        fx = S.fixture_of(manifest, sc)
        for mode in modes:
            template = args.warm_client_cmd if mode == "warm" else None
            if sc["kind"] == "edit_sequence":
                new = S.measure_scenario(sc, fx, tc, template, mode, args.reps, args.warm_primes,
                                         run_dir, not args.no_warmup, stores)
                rows.extend(new)
                print(f"{mode:4} {sc['id']}: " + ", ".join(
                    f"{r['step']} {r['wall_ms_median']:.1f} ms" for r in new), flush=True)
            else:
                new = S.measure_worktrees(sc, fx, tc, template, mode, args.reps, run_dir,
                                          not args.no_warmup, stores)
                wt_rows.extend(new)
                print(f"{mode:4} {sc['id']}: " + ", ".join(
                    f"N={r['n']} {r['schedule'][:4]} {r['group_wall_ms_median']:.0f} ms"
                    for r in new), flush=True)
    rust_scs = RC.pick(manifest, args)
    rref, rcand = rust_toolchains(args, rust_scs)
    rust_scs = rust_scs if rref is not None else []
    if rust_scs:
        RC.verify_frozen(manifest, rust_scs)
        rbase = C.median([block_probe(tc, manifest_all, run_dir) for _ in range(3)])

        def log_rprobe(p):
            print(f"  probe rust block@{p['first_step']} try {p['attempt']}: drift {p['drift_pct']}%"
                  f"{' NOISY: block re-measured' if p['noisy'] else ''}", flush=True)
        rrows, rprobes = RP.run_blocks(
            rust_scs,
            lambda sc: RC.measure_scenario(sc, RC.fixture_by_id(manifest, sc["fixture"]), rref, args.reps,
                                           run_dir, not args.no_warmup),
            lambda: block_probe(tc, manifest_all, run_dir), rbase, log=log_rprobe)
        block_probes += [{**p, "mode": "rust", "baseline_probe_ms": rbase} for p in rprobes]
        for sc, new in zip(rust_scs, rrows):
            rows.extend(new)
            print(f"rust {sc['id']}: " + ", ".join(
                f"{r['step']} {r['wall_ms_median']:.1f} ms ({r['status']}, spread {f_(r['spread'])}, "
                f"attempts {r['attempts']})" for r in new), flush=True)
    startup = startup_decomposition(tc, manifest_all, run_dir) \
        if any(sc["family"] == "b4_cold_warm" for sc in chosen_scenarios) else None
    noise = noise_summary(noise_start, noise_probe(tc, manifest_all, run_dir, "end")) \
        if noise_start else None
    # Query evidence for the B3 edits runs AFTER the timed loop and the end noise
    # probe (it is CPU heavy and must not perturb any timing).
    evidence = None
    b3 = [sc for sc in chosen_scenarios if sc["family"] == "b3_edits"]
    if b3 and not args.no_evidence:
        evidence = [edit_evidence.evidence_for(sc, S.fixture_of(manifest, sc), tc.moss,
                                               run_dir / "evidence") for sc in b3]
        (run_dir / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    churn_records = None
    b6 = [sc for sc in chosen_scenarios if sc["kind"] == RP.KIND]
    if b6 and not args.no_churn:
        churn_records = run_churn(b6, tc, run_dir)
        (run_dir / "churn.json").write_text(json.dumps(churn_records, indent=2, sort_keys=True) + "\n")
    gate = None
    if not args.no_gate:
        ref, cand = gate_toolchains(args)
        gate = run_gate(fixtures, ref, cand, args.candidate_cmd or args.warm_client_cmd, run_dir,
                        chosen_scenarios, stores, args.candidate_position_map)
        gate["position_map"] = args.candidate_position_map
        if rust_scs:
            gate["rust"] = run_rust_gate(rust_scs, manifest, rref, rcand, args.rust_candidate_cmd, run_dir,
                                         args.candidate_position_map)
            gate["passed"] = gate["passed"] and all(r["passed"] for r in gate["rust"])
    metrics = None if args.no_metrics else {
        m: run_metrics(tc, run_dir, m) for m in args.metrics_modes.split(",")}
    if gate is not None:
        gate["rust_skipped"] = list(RUST_SKIPPED)
    meta = {"suite": manifest["suite_name"], "suite_version": manifest["version"],
            "seed": manifest["seed"], "fixtures": len(fixtures), "run_id": run_id,
            "timestamp": C.now_iso(), "moss": str(tc.moss),
            "moss_sha256": C.sha256_file(tc.moss), "moss_flavor": tc.flavor,
            "git_sha": git("rev-parse", "--short=12", "HEAD"),
            "git_branch": git("rev-parse", "--abbrev-ref", "HEAD"),
            "git_dirty": bool(git("status", "--porcelain")),
            "platform": platform.platform(), "cpus": os.cpu_count(),
            "reps": args.reps, "noise": noise,
            "warm_client": args.warm_client_cmd or ("built-in commands with a persistent per-series store (--warm-store)"
                                                    if args.warm_store else None),
            "block_probes": block_probes, "rust": ({k: v for k, v in rref.versions.items()
                                                    if k != "rustc_verbose"} if rust_scs else None),
            "rust_env": ("CARGO_INCREMENTAL unset (default); CARGO_HOME and CARGO_TARGET_DIR under tmp/; "
                         "--offline --locked; std only") if rust_scs else None,
            "reference_pin": RP.reference_pin(tc) if b6_steps else None,
            "startup": startup, "stores": stores.summary(), "keep_stores": args.keep_stores,
            "position_map": args.candidate_position_map}
    write_outputs(run_dir, meta, rows, gate, metrics, chosen_scenarios, evidence, wt_rows, churn_records,
                  rust_scs)
    # Cold rows are the reference compiler: manifest mismatch is a hard failure.
    # Warm rows (a candidate client) only produce IMPROVED/REGRESSED notes.
    ok = all(r["status_ok"] for r in rows if r["mode"] == "cold") and \
        all(r["status_ok"] for r in wt_rows if r["mode"] == "cold") and \
        (gate is None or gate["passed"])
    print(f"report: {run_dir / 'LOCAL_REPORT.md'}")
    if metrics:
        print_metrics(metrics)
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except C.ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(2)
