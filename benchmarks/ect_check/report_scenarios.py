"""Report sections for the Team 3 benchmarks: B3 edits, B4 cold/warm, B5 worktrees."""
from __future__ import annotations

from collections import defaultdict

import report_dag


def f(v, d=1):
    if v is None:
        return "n/a"
    return f"{v:.{d}f}" if isinstance(v, float) else str(v)


def _by_scenario(rows, mode):
    out = defaultdict(dict)
    for r in rows:
        if r.get("scenario") and r["mode"] == mode:
            out[r["scenario"]][r["step"]] = r
    return out


def _status(r):
    return "" if r["status_ok"] else " **(UNEXPECTED)**"


def sections(rows: list[dict], scenarios: list[dict], evidence: list[dict] | None,
             wt_rows: list[dict], meta: dict) -> list[str]:
    sc_by_id = {s["id"]: s for s in scenarios}
    lines: list[str] = []
    cold = _by_scenario(rows, "cold")
    warm = _by_scenario(rows, "warm")
    ev_by = {e["scenario"]: e for e in (evidence or [])}

    # ---------------------------------------------------------------- B3
    b3 = [sid for sid in cold if sc_by_id[sid]["family"] == "b3_edits"]
    if b3:
        lines += ["## B3. Inference Ripple: edits at a leaf (cold process per request)", "",
                  "Each sequence runs `check` (empty build dir: the unedited baseline), applies one frozen "
                  "leaf patch, `check` (edit), reverts it (byte-identical tree verified), `check` (revert). "
                  "The package is checked by `margo --check build`; margo itself skips an unchanged "
                  "package, so every edit here is a whole-package recheck by construction. "
                  "Counters come from a separate run of the same sequence. 'Ideal' is the callable "
                  "count a perfect incremental checker would recheck, derived from the SOURCE call graph "
                  "(reverse reachability from the edited callable) and the edit class, not from generator "
                  "metadata; 'observed' is the number of callables whose queried facts (`moss inspect`) "
                  "changed.", ""]
        for shape in ("dag", "fanin"):
            ids = [s for s in b3 if sc_by_id[s]["id"].startswith(f"b3_{shape}_")]
            if not ids:
                continue
            fx = sc_by_id[ids[0]]["fixture"]
            lines += [f"### {fx}", "",
                      "| Edit | Class | Step | Wall ms | CPU ms | Fns analyzed | Source fn checks | "
                      "Spec evals | Spec keys | Checker runs | Callables (src) | Ideal recheck | Ideal spec keys | "
                      "Observed summary change | Analyzed / ideal |",
                      "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
            base_done = False
            for sid in ids:
                steps = cold[sid]
                sc = sc_by_id[sid]
                ev = ev_by.get(sid)
                if not base_done:
                    r = steps["base"]
                    shape = report_dag.source_shape(r)
                    callables = shape[0] if shape else None
                    lines.append(f"| (none) | unedited baseline | base | {f(r['wall_ms_median'])} | {f(r['cpu_ms_median'])} | "
                                 f"{f(r['functions_analyzed'])} | {f(r['source_function_checks'])} | "
                                 f"{f(r['specialization_evaluations'])} | {f(r['specialization_keys'])} | "
                                 f"{f(r['checker_runs'])} | {f(callables)} | n/a | n/a | n/a | n/a |{_status(r)}")
                    base_done = True
                for step in ("edit", "revert"):
                    r = steps[step]
                    ideal = obs = ratio = ikeys = ""
                    if step == "edit" and ev:
                        ideal = str(len(ev["ideal_recheck"]))
                        ikeys = str(len(ev["ideal_recheck"]) * (shape[1] if shape else 1))
                        obs = str(len(ev["summary_changed"]))
                        ratio = (f"{r['functions_analyzed'] / len(ev['ideal_recheck']):.1f}x"
                                 if ev["ideal_recheck"] and r["functions_analyzed"] is not None
                                 else "n/a (ideal 0)")
                    lines.append(f"| `{sid.split('_', 2)[2]}` | {sc['classification']} | {step} | "
                                 f"{f(r['wall_ms_median'])} | {f(r['cpu_ms_median'])} | {f(r['functions_analyzed'])} | "
                                 f"{f(r['source_function_checks'])} | {f(r['specialization_evaluations'])} | "
                                 f"{f(r['specialization_keys'])} | {f(r['checker_runs'])} | {f(callables)} | {ideal} | {ikeys} | {obs} | {ratio} |{_status(r)}")
            lines.append("")
        if evidence:
            lines += ["### B3 query evidence (what the compiler says changed)", "",
                      "| Scenario | Edit class | Evidence |", "|---|---|---|"]
            for e in evidence:
                lines.append(f"| `{e['scenario']}` | {e['classification']} | {e['summary']} |")
            lines.append("")
        if warm:
            lines += ["### B3 warm client", ""]
            for sid in [s for s in warm if sc_by_id[s]["family"] == "b3_edits"]:
                for step, r in warm[sid].items():
                    lines.append(f"- `{sid}:{step}` {f(r['wall_ms_median'])} ms (first rep {f(r['wall_ms_first'])} ms)")
            lines.append("")

    # ---------------------------------------------------------------- B4
    b4 = [sid for sid in cold if sc_by_id[sid]["family"] == "b4_cold_warm"]
    if b4:
        lines += ["## B4. Cold vs warm", "",
                  "Sequence per project: `first` (empty build dir, fresh process), `second` (identical "
                  "tree, fresh process, build dir left by `first`), apply a one-line edit, `edit`, revert. "
                  f"Warm client: **{meta['warm_client'] or 'not configured'}**.", ""]
        lines += ["| Project | Step | Wall ms | CPU ms | RSS KiB | Checker runs | Fns analyzed | Spec keys |",
                  "|---|---|---:|---:|---:|---:|---:|---:|"]
        for sid in b4:
            for step in ("first", "second", "edit"):
                r = cold[sid][step]
                lines.append(f"| `{sc_by_id[sid]['fixture']}` | {step} | {f(r['wall_ms_median'])} | {f(r['cpu_ms_median'])} | "
                             f"{f(r['rss_kib_median'], 0)} | {f(r['checker_runs'])} | {f(r['functions_analyzed'])} | "
                             f"{f(r['specialization_keys'])} |{_status(r)}")
        lines.append("")
        if meta.get("startup"):
            lines += ["Startup decomposition (median of 11 bare starts, untimed-state independent):", "",
                      "| Command | Wall ms | CPU ms |", "|---|---:|---:|"]
            for u in meta["startup"]:
                lines.append(f"| {u['case']} | {f(u['wall_ms_median'])} | {f(u['cpu_ms_median'])} |")
            lines += ["", "margo is a Python script: the package rows of every table include this "
                      "interpreter and driver overhead, which a resident client also removes.", ""]
        floor = [r for r in rows if r["mode"] == "cold" and r.get("fixture", "").startswith("b4_floor") and not r.get("scenario")]
        if floor:
            lines += ["Startup floor (checking a `main` that only echoes a literal), plain cold fixtures:", "",
                      "| Fixture | Wall ms | CPU ms | RSS KiB |", "|---|---:|---:|---:|"]
            for r in floor:
                lines.append(f"| `{r['fixture']}` | {f(r['wall_ms_median'])} | {f(r['cpu_ms_median'])} | {f(r['rss_kib_median'], 0)} |")
            lines.append("")
        if warm:
            lines += ["Warm client rows (`first` is the first request after the priming passes; "
                      "'first rep' is the very first timed sequence):", "",
                      "| Project | Step | Warm median ms | first rep ms | Cold ms |", "|---|---|---:|---:|---:|"]
            for sid in b4:
                for step in ("first", "second", "edit"):
                    if sid in warm:
                        r = warm[sid][step]
                        lines.append(f"| `{sc_by_id[sid]['fixture']}` | {step} | {f(r['wall_ms_median'])} | "
                                     f"{f(r['wall_ms_first'])} | {f(cold[sid][step]['wall_ms_median'])} |")
            lines.append("")

    # ---------------------------------------------------------------- B5
    if wt_rows:
        lines += ["## B5. Many worktrees", "",
                  "N real `git worktree` checkouts of ONE commit of a deterministic fixture repository "
                  "(built under `tmp/`, committed once), each with a distinct small uncommitted edit, "
                  "each checked from an empty build dir. `concurrent` starts all N checks together; "
                  "`sequential` runs them one after another. Total CPU is the sum of per-process "
                  "user+system; the `time %M` column is the sum over the N trees of the LARGEST SINGLE "
                  "PROCESS in each tree (margo's Python, not moss) and is NOT a footprint or an upper "
                  "bound; the sampled columns are the real figure: a /proc scanner (every ~3 ms, "
                  "untimed separate passes, 3 reps) sums the RSS of every process of the trees, "
                  "giving the peak simultaneous RSS of the whole group (concurrent) or of the "
                  "largest tree (sequential: trees do not overlap); group wall is first launch "
                  f"to last finish (sequential: the sum). **{meta['cpus']} CPUs: at N=16 concurrent the "
                  "processes (plus the margo Python processes and their children) contend for the same "
                  "CPUs and the runner polls; per-worktree wall times and CPU totals when concurrent are "
                  "inflated and are not first-check cost (the cause is not established, see README).**", ""]
        for sid in sorted({r["scenario"] for r in wt_rows}):
            for mode in ("cold", "warm"):
                sel = [r for r in wt_rows if r["scenario"] == sid and r["mode"] == mode]
                if not sel:
                    continue
                lines += [f"### {sid} ({mode}), base fixture `{sel[0]['base_fixture']}`, commit `{sel[0]['commit'][:12]}`", "",
                          "| N | Schedule | Group wall ms | Total CPU ms | Sum of per-tree largest-process peaks MiB (time %M) | "
                          "Sampled peak simultaneous RSS, whole trees MiB | Sampled peak per tree MiB | "
                          "wt0 first ms | later wt median ms | slowest wt ms | Fns analyzed (sum) | Source fn checks (sum) |",
                          "|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
                for r in sel:
                    lines.append(f"| {r['n']} | {r['schedule']} | {f(r['group_wall_ms_median'])} | "
                                 f"{f(r['total_cpu_ms_median'])} | {f(r['sum_rss_kib_median'] / 1024)} | "
                                 f"{f(r.get('sampled_group_tree_rss_peak_mib'))} | {f(r.get('sampled_per_tree_peak_mib'))} | "
                                 f"{f(r['first_check_ms_wt0'])} | "
                                 f"{f(r['first_check_ms_later_median'])} | {f(r['first_check_ms_max'])} | "
                                 f"{f(r.get('functions_analyzed'))} | {f(r.get('source_function_checks'))} |"
                                 f"{'' if r['status_ok'] else ' **(UNEXPECTED)**'}")
                lines.append("")
        lines += ["Counters (functions analyzed, source function checks) come from one separate sequential "
                  "pass over 16 worktrees; the per-N sums are prefix sums of that pass.", ""]
    return lines
