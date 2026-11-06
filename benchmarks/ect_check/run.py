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
import report_dag  # noqa: E402

MIN_REPS = 3
WARM_ENV = "ECT_WARM_CLIENT_CMD"
CANDIDATE_ENV = "ECT_CANDIDATE_CMD"


# --------------------------------------------------------------------------
# Measurement
# --------------------------------------------------------------------------

def measure_fixture(fx: dict, tc: C.Toolchain, template: str | None, mode: str,
                    reps: int, primes: int, run_dir: Path, warmup: bool = True) -> dict:
    """Median-of-reps wall/CPU/RSS for one fixture in one mode.

    cold: a fresh staged copy per rep (full, from-scratch check).
    warm: one staged copy, `primes` unmeasured runs, then reps in place, so a
          resident client keeps whatever state it keeps between requests.
    """
    work = run_dir / "work" / f"{mode}-{fx['id']}"
    samples, provider_samples, verdict = [], [], None
    repl = C.path_replacements(work, tc)
    timeout = C.fixture_timeout(fx)
    provider = bool(fx.get("provider"))

    def one(stage: bool, extra_env: dict | None = None, provider_sink: Path | None = None,
            record_provider: bool = False):
        """Optionally stage; for fixtures with a package boundary build the
        provider first (its own measured series, never part of the consumer
        sample), then run the measured consumer command."""
        values = C.stage_fixture(fx, work) if stage else \
            {"root": str(work), "path": str(work / fx["entry"]), "id": fx["id"],
             "kind": fx["kind"]}
        if stage and provider:
            pres = C.build_provider(
                fx, tc, work, C.counter_env(provider_sink) if provider_sink else None)
            if record_provider:
                provider_samples.append(pres)
        argv = C.command_for(fx, {**tc.values(), **values}, template)
        env = tc.env()
        env.update(extra_env or {})
        return C.run_measured(argv, cwd=work, env=env, timeout=timeout)

    if mode == "warm":
        C.stage_fixture(fx, work)
        if provider:
            C.build_provider(fx, tc, work)
        for _ in range(primes):
            one(stage=False)
    if mode == "cold" and warmup:
        one(stage=True)  # discarded: first run pays binary/page-cache page-in
    for _ in range(reps):
        res = one(stage=(mode == "cold"), record_provider=True)
        samples.append(res)
        verdict = C.extract_verdict(res, repl)

    counters = None
    provider_counters = None
    if mode == "cold":
        sink = run_dir / "counters" / f"{fx['id']}.jsonl"
        psink = run_dir / "counters" / f"{fx['id']}.provider.jsonl"
        sink.parent.mkdir(parents=True, exist_ok=True)
        sink.unlink(missing_ok=True)
        psink.unlink(missing_ok=True)
        one(stage=True, extra_env=C.counter_env(sink), provider_sink=psink)
        counters = C.summarize_counters(C.read_counter_records(sink))
        if provider:
            provider_counters = C.summarize_counters(C.read_counter_records(psink))
    shutil.rmtree(work, ignore_errors=True)

    status = C.status_of(verdict)
    row = {
        "fixture": fx["id"], "kind": fx["kind"], "mode": mode, "reps": reps,
        "expected_status": fx["expected_status"], "status": status,
        "status_ok": status == fx["expected_status"],
        "wall_ms_median": C.median([s["wall_ms"] for s in samples]),
        "wall_ms_min": min(s["wall_ms"] for s in samples),
        "wall_ms_max": max(s["wall_ms"] for s in samples),
        "cpu_ms_median": C.median([s["cpu_ms"] for s in samples]),
        "rss_kib_median": C.median([s["rss_kib"] for s in samples]),
        "wall_ms_samples": [round(s["wall_ms"], 3) for s in samples],
        "category": fx.get("category"), "metadata": fx.get("metadata"),
        "exit_code": verdict.get("exit_code"),
        "timed_out": any(s.get("timed_out") for s in samples),
        "has_provider": provider,
        "provider_wall_ms_median": C.median([s["wall_ms"] for s in provider_samples]),
        "provider_cpu_ms_median": C.median([s["cpu_ms"] for s in provider_samples]),
        "provider_rss_kib_median": C.median([s["rss_kib"] for s in provider_samples]),
        "provider_functions_analyzed": (provider_counters or {}).get("functions_analyzed"),
        "provider_checker_runs": (provider_counters or {}).get("checker_runs"),
    }
    row.update(counters or {"checker_runs": None,
                            **{name: None for name, _ in C.WORK_COUNTERS}})
    return row


# --------------------------------------------------------------------------
# Correctness gate
# --------------------------------------------------------------------------

def run_gate(fixtures: list[dict], reference: C.Toolchain, candidate: C.Toolchain,
             candidate_template: str | None, run_dir: Path) -> dict:
    """Reference vs candidate: identical verdict and diagnostics on every fixture."""
    results = []
    for fx in fixtures:
        verdicts = {}
        for side, tc, template in (("reference", reference, None),
                                   ("candidate", candidate, candidate_template)):
            work = run_dir / "work" / f"gate-{side}-{fx['id']}"
            values = C.stage_fixture(fx, work)
            if fx.get("provider"):
                C.build_provider(fx, tc, work)
            argv = C.command_for(fx, {**tc.values(), **values}, template)
            res = C.run_measured(argv, cwd=work, env=tc.env(),
                                 timeout=C.fixture_timeout(fx))
            verdicts[side] = C.extract_verdict(res, C.path_replacements(work, tc))
            shutil.rmtree(work, ignore_errors=True)
        identical = verdicts["reference"] == verdicts["candidate"]
        # `ok` is None for non-JSON output (e.g. a compiler crash with empty
        # stdout), which counts as a failure, never as "matches expected pass".
        expected = bool(verdicts["reference"]["ok"]) == (fx["expected_status"] == "pass")
        results.append({"fixture": fx["id"], "identical": identical,
                        "reference_matches_expected": expected,
                        "status": C.status_of(verdicts["reference"]),
                        "reference": verdicts["reference"],
                        "candidate": verdicts["candidate"]})
    passed = all(r["identical"] and r["reference_matches_expected"] for r in results)
    return {"passed": passed, "fixtures": len(results),
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


CSV_FIELDS = ["compiler_path", "compiler_sha256", "compiler_flavor", "fixture", "kind", "mode", "reps", "expected_status", "status",
              "status_ok", "wall_ms_median", "wall_ms_min", "wall_ms_max",
              "cpu_ms_median", "rss_kib_median", "checker_runs",
              "exit_code", "timed_out", "provider_wall_ms_median", "provider_cpu_ms_median",
              "provider_rss_kib_median", "provider_functions_analyzed", "provider_checker_runs",
              *[name for name, _ in C.WORK_COUNTERS]]


def write_outputs(out: Path, meta: dict, rows: list[dict], gate: dict | None,
                  metrics: dict | None) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for row in rows:
        row.update(compiler_path=meta["moss"], compiler_sha256=meta["moss_sha256"],
                   compiler_flavor=meta["moss_flavor"])
    with (out / "results.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    (out / "results.json").write_text(json.dumps(
        {"meta": meta, "rows": rows, "gate": gate, "metrics": metrics,
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
             f"**Warm client:** {meta['warm_client'] or 'not configured'}  ", "",
             "## 1. Timing, memory and compiler counters", "",
             "Wall, CPU and RSS are medians over the timed reps. CPU is user+system "
             "of the whole process tree. RSS is the largest single process in the tree "
             "(not a sum). Counters are summed over the checker runs one command starts.", ""]
    for mode in ("cold", "warm"):
        mode_rows = [r for r in rows if r["mode"] == mode]
        if not mode_rows:
            continue
        lines += [f"### Mode: {mode}", "",
                  "| Fixture | Kind | Status | Wall ms | CPU ms | RSS KiB | Checker runs | "
                  "Fns analyzed | Spec evals | Spec hits | Spec keys |",
                  "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for r in mode_rows:
            status = r["status"] + ("" if r["status_ok"] else " **(UNEXPECTED)**")
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

    lines += report_dag.sections(rows)

    lines += ["## 2. Correctness gate (reference vs candidate)", ""]
    if gate is None:
        lines += ["Not run.", ""]
    else:
        lines += [f"**Result: {'PASS' if gate['passed'] else 'FAIL'}** over "
                  f"{gate['fixtures']} fixtures. Reference: `{gate['reference_moss']}`. "
                  f"Candidate: `{gate['candidate_moss']}` via {gate['candidate_command']}.", ""]
        lines += ["| Fixture | Status | Verdicts identical | Reference matches manifest |",
                  "|---|---|---|---|"]
        for r in gate["results"]:
            lines.append(f"| `{r['fixture']}` | {r['status']} | "
                         f"{'yes' if r['identical'] else '**NO**'} | "
                         f"{'yes' if r['reference_matches_expected'] else '**NO**'} |")
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


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def add_common(ap):
    ap.add_argument("--moss", default=os.environ.get("MOSS") or str(C.DEFAULT_MOSS))
    ap.add_argument("--margo", default=str(C.DEFAULT_MARGO))
    ap.add_argument("--ids", nargs="*", help="restrict to these fixture ids")
    ap.add_argument("--out", help="output dir (default tmp/ect_check/<run-id>)")


def add_gate_args(ap):
    ap.add_argument("--reference-moss", help="reference compiler (default --moss)")
    ap.add_argument("--candidate-moss", help="candidate compiler (default --moss)")
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


def make_run_dir(args) -> tuple[str, Path]:
    run_id = C.time.strftime("%Y%m%d-%H%M%S")
    return run_id, (Path(args.out) if args.out else C.TMP_ROOT / run_id).resolve()


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
    b.add_argument("--no-warmup", action="store_true",
                   help="do not discard one cold warm-up run per fixture before the timed reps")
    b.add_argument("--no-gate", action="store_true")
    b.add_argument("--no-metrics", action="store_true")
    b.add_argument("--metrics-modes", default="check,build",
                   help="comma list of margo modes: check (--check) and/or build")
    b.add_argument("--allow-non-opt", action="store_true",
                   help="permit timing a non-optimized compiler (loud warning in report)")
    g = sub.add_parser("gate")
    add_common(g)
    add_gate_args(g)
    m = sub.add_parser("metrics")
    add_common(m)
    m.add_argument("--metrics-modes", default="check,build",
                   help="comma list of margo modes: check (--check) and/or build")
    m.add_argument("--root", nargs="+", help="package-tree roots to measure instead of "
                   "sdk/ and packages/ (e.g. benchmarks/ect_check/fixtures/trivial/pkg_pass)")
    v = sub.add_parser("validate")
    add_common(v)
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

    tc = C.Toolchain(Path(args.moss), Path(args.margo))
    run_id, run_dir = make_run_dir(args)
    run_dir.mkdir(parents=True, exist_ok=True)

    if cmd == "gate":
        ref, cand = gate_toolchains(args)
        gate = run_gate(fixtures, ref, cand, args.candidate_cmd, run_dir)
        (run_dir / "gate.json").write_text(json.dumps(gate, indent=2) + "\n")
        print(f"gate: {'PASS' if gate['passed'] else 'FAIL'} ({gate['fixtures']} fixtures); {run_dir / 'gate.json'}")
        return 0 if gate["passed"] else 1

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
    modes = ["cold"] + (["warm"] if args.warm_client_cmd else [])
    for fx in fixtures:
        for mode in modes:
            template = args.warm_client_cmd if mode == "warm" else None
            row = measure_fixture(fx, tc, template, mode, args.reps,
                                  args.warm_primes, run_dir, not args.no_warmup)
            rows.append(row)
            print(f"{mode:4} {fx['id']}: wall {row['wall_ms_median']:.2f} ms, "
                  f"status {row['status']}", flush=True)
    gate = None
    if not args.no_gate:
        ref, cand = gate_toolchains(args)
        gate = run_gate(fixtures, ref, cand, args.candidate_cmd or args.warm_client_cmd, run_dir)
    metrics = None if args.no_metrics else {
        m: run_metrics(tc, run_dir, m) for m in args.metrics_modes.split(",")}
    meta = {"suite": manifest["suite_name"], "suite_version": manifest["version"],
            "seed": manifest["seed"], "fixtures": len(fixtures), "run_id": run_id,
            "timestamp": C.now_iso(), "moss": str(tc.moss),
            "moss_sha256": C.sha256_file(tc.moss), "moss_flavor": tc.flavor,
            "git_sha": git("rev-parse", "--short=12", "HEAD"),
            "git_branch": git("rev-parse", "--abbrev-ref", "HEAD"),
            "git_dirty": bool(git("status", "--porcelain")),
            "platform": platform.platform(), "cpus": os.cpu_count(),
            "reps": args.reps, "warm_client": args.warm_client_cmd}
    write_outputs(run_dir, meta, rows, gate, metrics)
    ok = all(r["status_ok"] for r in rows) and (gate is None or gate["passed"])
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
