"""Report section, statistics and CSV for B6 (agent-loop replay) and summary churn."""
from __future__ import annotations

import csv
from pathlib import Path
import statistics

KIND = "replay_step"
COUNTER_KEYS = ("functions_analyzed", "source_function_checks", "specialization_evaluations",
                "specialization_keys", "checker_runs")
BUCKETS = ((50, "< 50 ms"), (100, "50-100 ms"), (250, "100-250 ms"), (500, "250-500 ms"),
           (1000, "0.5-1 s"), (2000, "1-2 s"), (5000, "2-5 s"), (float("inf"), ">= 5 s"))


def f(v, d=1):
    if v is None:
        return "n/a"
    return f"{v:.{d}f}" if isinstance(v, float) else str(v)


def percentile(values: list[float], q: float) -> float | None:
    """Linear interpolation between order statistics (numpy 'linear' /
    statistics.quantiles 'inclusive'): p(q) at rank q*(n-1)."""
    v = sorted(x for x in values if x is not None)
    if not v:
        return None
    if len(v) == 1:
        return v[0]
    pos = q * (len(v) - 1)
    lo = int(pos)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (pos - lo)


def dist(values: list[float]) -> dict:
    v = [x for x in values if x is not None]
    return {"n": len(v), "min": min(v, default=None), "p50": percentile(v, 0.5), "p95": percentile(v, 0.95),
            "max": max(v, default=None), "mean": statistics.fmean(v) if v else None}


def histogram(values: list[float]) -> list[tuple[str, int]]:
    out, prev = [], 0.0
    for hi, label in BUCKETS:
        out.append((label, sum(1 for x in values if prev <= x < hi)))
        prev = hi
    return out


def wilson(k: int, n: int, z: float = 1.96) -> tuple[float, float] | None:
    if n == 0:
        return None
    p = k / n
    d = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / d
    half = z * ((p * (1 - p) / n + z * z / (4 * n * n)) ** 0.5) / d
    return max(0.0, centre - half), min(1.0, centre + half)


def analysis(rows: list[dict], scenarios: list[dict], churn: list[dict] | None) -> dict | None:
    b6 = [s for s in scenarios if s.get("kind") == KIND]
    if not b6:
        return None
    sc_by = {s["id"]: s for s in b6}
    commits = []
    for mode in ("cold", "warm"):
        for sc in b6:
            pre = next((r for r in rows if r.get("scenario") == sc["id"] and r["mode"] == mode and r["step"] == "pre"), None)
            post = next((r for r in rows if r.get("scenario") == sc["id"] and r["mode"] == mode and r["step"] == "post"), None)
            if pre is None and post is None:
                continue
            rec = {"mode": mode, "scenario": sc["id"], "index": sc["index"], "commit": sc["commit"],
                   "subject": sc["subject"], "entries": len(sc["entries"]),
                   "new_package": sc["new_package"], "effectively_new": sc["effectively_new"],
                   "touches_source": sc["touches_source"],
                   "spread_pre": pre and pre.get("spread"), "spread_post": post and post.get("spread"),
                   "attempts": max((r.get("attempts", 1) for r in (pre, post) if r), default=1),
                   "spread_flagged": any(r.get("spread_flagged") for r in (pre, post) if r),
                   "block_noisy": any(r.get("block_noisy") for r in (pre, post) if r),
                   "block_attempts": max((r.get("block_attempts", 1) for r in (pre, post) if r), default=1),
                   "pre_ms": pre and pre["wall_ms_median"], "post_ms": post and post["wall_ms_median"],
                   "pre_status": pre and pre["status"], "post_status": post and post["status"],
                   "pre_ok": pre and pre["status_ok"], "post_ok": post and post["status_ok"]}
            for ph, r in (("pre", pre), ("post", post)):
                for k in COUNTER_KEYS:
                    rec[f"{ph}_{k}"] = r.get(k) if r else None
            fails = []
            for ph, r in (("pre", pre), ("post", post)):
                for e in ((r or {}).get("metadata") or {}).get("entries", []):
                    if e["status"] != "pass":
                        fails.append(f"{ph} {e['id']}: {(e.get('diag') or '')[:140]}")
            rec["failures"] = fails
            commits.append(rec)
    out = {"commits": commits, "populations": {}, "churn": None}
    for mode in ("cold", "warm"):
        cs = [c for c in commits if c["mode"] == mode]
        if not cs:
            continue
        pops = {
            "all": [c for c in cs if c["post_ms"] is not None],
            "post_pass": [c for c in cs if c["post_status"] == "pass"],
            "post_pass_existing": [c for c in cs if c["post_status"] == "pass" and not c["effectively_new"]],
            # margo no-op posts (no checker ran at all) are not a check of the edit
            "clean": [c for c in cs if c["post_status"] == "pass" and c["pre_status"] == "pass"
                      and not c["effectively_new"] and c["post_checker_runs"]],
        }
        stats = {}
        for name, sel in pops.items():
            stats[name] = {"commits": len(sel),
                           "post": dist([c["post_ms"] for c in sel]),
                           "pre": dist([c["pre_ms"] for c in sel if c["pre_ms"] is not None]),
                           "post_hist": histogram([c["post_ms"] for c in sel]),
                           "counters_post_p50": {k: percentile([c[f"post_{k}"] for c in sel if c[f"post_{k}"] is not None], .5)
                                                 for k in COUNTER_KEYS},
                           "counters_post_n": {k: sum(1 for c in sel if c[f"post_{k}"] is not None) for k in COUNTER_KEYS},
                           "counters_post_sum": {k: sum(c[f"post_{k}"] or 0 for c in sel) for k in COUNTER_KEYS},
                           "counters_pre_p50": {k: percentile([c[f"pre_{k}"] for c in sel if c[f"pre_{k}"] is not None], .5)
                                                for k in COUNTER_KEYS}}
        out["populations"][mode] = stats
    if churn:
        out["churn"] = churn_stats(churn)
        out["churn_records"] = [{k: v for k, v in c.items() if k not in ("changed", "changed_own", "changed_other")}
                                for c in churn]
    return out


def churn_class(c: dict) -> str:
    """Why an analysable commit is (not) informative about summary churn."""
    if c["new_package"] or c["effectively_new"]:
        return "new_package"          # every function is added; nothing can change
    if not c["touches_source"]:
        return "tests_only"           # nothing under package src changed: vacuous by construction
    if c["edited"]:
        return "edits_existing"       # the text of at least one existing function changed
    return "additions_only"           # only functions were added (or other non-function text changed)


def churn_stats(churn: list[dict]) -> dict:
    avail = [c for c in churn if c["available"]]
    unavailable = [c for c in churn if not c["available"]]
    classes: dict[str, list[dict]] = {k: [] for k in ("edits_existing", "additions_only", "tests_only", "new_package")}
    for c in avail:
        classes[churn_class(c)].append(c)
    edits = [c for c in avail if churn_class(c) != "new_package"]
    n = len(edits)
    inf = classes["edits_existing"]

    def count(pred, sel=None):
        return sum(1 for c in (edits if sel is None else sel) if pred(c))
    headline = {"any": count(lambda c: c["any_change"]), "own": count(lambda c: c["own_change"]),
                "other": count(lambda c: c["other_change"]),
                "none": count(lambda c: not c["any_change"]),
                "membership_only": count(lambda c: c["membership_only"])}
    by_cat = {}
    for cat in ("types", "access", "raises", "effects"):
        by_cat[cat] = {"any": count(lambda c: cat in c["categories_any"]),
                       "own": count(lambda c: cat in c["categories_own"]),
                       "other": count(lambda c: cat in c["categories_other"])}
    informative = {"commits": len(inf), "any": count(lambda c: c["any_change"], inf),
                   "own": count(lambda c: c["own_change"], inf), "other": count(lambda c: c["other_change"], inf),
                   "functions_edited": sum(len(c["edited"]) for c in inf),
                   "edited_changed": sum(c["n_changed_own"] for c in inf),
                   "unedited_changed": sum(c["n_changed_other"] for c in inf),
                   "wilson_any": wilson(count(lambda c: c["any_change"], inf), len(inf)),
                   "wilson_other": wilson(count(lambda c: c["other_change"], inf), len(inf)),
                   "own_changed_by_commit": {c["index"]: c["n_changed_own"] for c in inf if c["n_changed_own"]},
                   "ids": [c["index"] for c in inf]}
    # coverage: commits that modify an existing source file of an existing package
    modifying = [c for c in churn if c.get("edits_existing_source") and not (c["new_package"] or c["effectively_new"])]
    cover = {"modifying_existing_source": len(modifying),
             "analysable": sum(1 for c in modifying if c["available"])}
    per_commit_changed = [c["n_changed"] for c in edits]
    return {"total": len(churn), "available": len(avail), "unavailable": len(unavailable),
            "classes": {k: [c["index"] for c in v] for k, v in classes.items()},
            "new_package_commits": len(classes["new_package"]), "edit_commits": n,
            "headline": headline, "by_category": by_cat, "informative": informative, "coverage": cover,
            "changed_per_commit": dist(per_commit_changed),
            "wilson": {k: wilson(v, n) for k, v in headline.items() if k in ("any", "own", "other")},
            "unavailable_list": [{"scenario": c["scenario"], "reason": c["unavailable_reason"]} for c in unavailable]}


def write_csv(path: Path, stats: dict) -> None:
    cols = ["mode", "scenario", "index", "commit", "entries", "new_package", "touches_source",
            "pre_status", "post_status", "pre_ms", "post_ms",
            *[f"pre_{k}" for k in COUNTER_KEYS], *[f"post_{k}" for k in COUNTER_KEYS], "subject"]
    with path.open("w", newline="") as h:
        w = csv.DictWriter(h, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(stats["commits"])


def sections(stats: dict | None, meta: dict) -> list[str]:
    if not stats:
        return []
    lines = ["## B6. Agent-loop replay (last 50 commits touching sdk/packages `.moss`)", "",
             "Each commit's PARENT tree is materialized (`git archive`), every entry point is checked "
             "(`pre`: the baseline, cold), the commit's `.moss` / `Moss.toml` changes are applied and "
             "the same entry points are checked again (`post`: the per-edit latency; margo's package "
             "fingerprint cache skips unchanged dependencies). A phase's latency is the sum over its "
             "entry points of the median over reps. Old trees are checked with TODAY's compiler, so many "
             "legitimately fail: failing checks end at the first diagnostic and are NOT representative "
             "latencies; the populations below separate them. See README for the entry-point definition "
             "and limits.", ""]
    pin = meta.get("reference_pin")
    if pin:
        lines += [f"Reference compiler sha256 `{pin['sha256'][:12]}`: " +
                  ("the pinned binary the expectations were recorded with." if pin["matches"] else
                   "**NOT the pinned binary. " + pin["note"] + "**"), ""]
    probes = meta.get("block_probes") or []
    cold_rows = [c for c in stats["commits"] if c["mode"] == "cold"]
    if probes:
        noisy = [p for p in probes if p["noisy"]]
        lines += [f"Measurement guards: {len(probes)} mid-run calibration probes (every 5 steps; combined single- and "
                  f"multi-process probe), {len(noisy)} flagged noisy (the block was re-measured, up to 2 times); "
                  f"max drift {max(p['drift_pct'] for p in probes)}%. Steps re-measured because a phase's rep max/min "
                  f"spread exceeded 1.3: {sum(1 for c in cold_rows if c['attempts'] > 1)} of {len(cold_rows)} "
                  f"(kept attempt = lowest worst-phase spread, never chosen by median); still flagged after 3 attempts: "
                  f"{sum(1 for c in cold_rows if c['spread_flagged'])}; blocks left noisy after retries: "
                  f"{sum(1 for c in cold_rows if c['block_noisy'])} steps. Median max/min spread over steps and phases: "
                  f"{f(statistics.median([x for c in cold_rows for x in (c['spread_pre'], c['spread_post']) if x]), 2)}.", ""]
    for mode, pops in stats["populations"].items():
        lines += [f"### Latency per edit ({mode})", "",
                  "| Population | Commits | post p50 ms | post p95 ms | post min-max ms | pre p50 ms | pre p95 ms |",
                  "|---|---:|---:|---:|---:|---:|---:|"]
        names = {"all": "all commits (incl. failing checks)", "post_pass": "post-edit check passes",
                 "post_pass_existing": "post passes, package not new and had sources (pre may fail)",
                 "clean": "clean edits: pre and post pass, not new, a checker ran"}
        for k, label in names.items():
            p = pops[k]
            small = p["commits"] < 10        # a p95 of fewer than 10 values is not a result
            lines.append(f"| {label} | {p['commits']} | {f(p['post']['p50'])} | "
                         f"{'n/a (n<10)' if small else f(p['post']['p95'])} | "
                         f"{f(p['post']['min'])}-{f(p['post']['max'])} | {f(p['pre']['p50'])} | "
                         f"{'n/a (n<10)' if small else f(p['pre']['p95'])} |")
        lines += ["", "Percentiles: linear interpolation between order statistics (`statistics.quantiles` "
                  "'inclusive' convention); with this few commits p95 sits close to the maximum.", ""]
        lines += ["Distribution of post-edit latency (commits per bucket):", "",
                  "| Bucket | " + " | ".join(names.values()) + " |", "|---|" + "---:|" * len(names)]
        for i, (label, _) in enumerate(pops["all"]["post_hist"]):
            lines.append(f"| {label} | " + " | ".join(str(pops[k]["post_hist"][i][1]) for k in names) + " |")
        lines.append("")
        lines += ["Compiler counters of the post-edit check (median over the commits that HAVE counters, n shown: a "
                  "failing step that stops before the checker finishes reports none; per-commit sum over "
                  "entry points; margo skips unchanged packages, so these count only what was rechecked):", "",
                  "| Population | Fns analyzed p50 (n) | Source fn checks p50 | Spec evals p50 | Spec keys p50 | Checker runs p50 | Fns analyzed (sum) |",
                  "|---|---:|---:|---:|---:|---:|---:|"]
        for k, label in names.items():
            c = pops[k]["counters_post_p50"]
            nn = pops[k]["counters_post_n"]["functions_analyzed"]
            lines.append(f"| {label} | {f(c['functions_analyzed'], 1)} (n={nn} of {pops[k]['commits']}) | {f(c['source_function_checks'], 1)} | "
                         f"{f(c['specialization_evaluations'], 1)} | {f(c['specialization_keys'], 1)} | "
                         f"{f(c['checker_runs'], 1)} | {pops[k]['counters_post_sum']['functions_analyzed']} |")
        lines.append("")
        noop = [c for c in stats["commits"] if c["mode"] == mode and c["post_status"] == "pass"
                and c["post_checker_runs"] == 0]
        lines += [f"Passing post-edit checks in which no checker ran (margo reported every package as skipped; "
                  f"the changed files were not package sources or tests): {len(noop)} commits "
                  f"({', '.join('#' + str(c['index']) for c in noop) or 'none'}).", ""]
    cold = [c for c in stats["commits"] if c["mode"] == "cold"]
    lines += ["### Per commit (cold)", "",
              "| # | Commit | Entries | Pre | Post | Pre ms | Post ms | Spread pre/post | Tries | Post fns analyzed | Post checker runs | Subject |",
              "|---:|---|---:|---|---|---:|---:|---|---:|---:|---:|---|"]
    for c in cold:
        flag = ("new pkg; " if c["new_package"] else "first-source pkg; " if c["effectively_new"] else "") + \
            ("" if c["touches_source"] else "no src; ") + ("spread-flagged; " if c["spread_flagged"] else "")
        lines.append(f"| {c['index']} | `{c['commit'][:8]}` | {c['entries']} | {c['pre_status'] or '-'} | "
                     f"{c['post_status'] or '-'} | {f(c['pre_ms'])} | {f(c['post_ms'])} | "
                     f"{f(c['spread_pre'], 2)}/{f(c['spread_post'], 2)} | {c['attempts']} | "
                     f"{c['post_functions_analyzed'] if c['post_functions_analyzed'] is not None else 'n/a'} | "
                     f"{c['post_checker_runs'] if c['post_checker_runs'] is not None else 'n/a'} | "
                     f"{flag}{c['subject'][:60]} |")
    lines.append("")
    failing = [c for c in cold if c["failures"]]
    lines += [f"### Checks that fail today ({len(failing)} of {len(cold)} commits have at least one failing "
              "entry point; each is the frozen expectation, not a harness error)", "",
              "| # | Commit | First failing entry points (diagnostic) |", "|---:|---|---|"]
    for c in failing:
        shown = "<br>".join(x.replace("|", "\\|") for x in c["failures"][:3])
        more = f"<br>(+{len(c['failures']) - 3} more)" if len(c["failures"]) > 3 else ""
        lines.append(f"| {c['index']} | `{c['commit'][:8]}` | {shown}{more} |")
    lines.append("")
    ch = stats.get("churn")
    if ch:
        h, n, e = ch["headline"], ch["edit_commits"], ch["informative"]
        cl = ch["classes"]
        lines += ["## Summary churn (does an edit move function summaries, and others' summaries?)", "",
                  f"Commits: {ch['total']} replayed; {ch['available']} answered the queries on every touched "
                  f"unit on both sides ({ch['unavailable']} did not: the tree does not check today). Of the "
                  f"{ch['available']}: {len(cl['new_package'])} add a package or give a scaffold its first source "
                  "(every function is added: nothing can change), "
                  f"{len(cl['tests_only'])} change nothing under any package's src (tests/benches/fixtures only: "
                  "vacuous, the churn pass reads package src), "
                  f"{len(cl['additions_only'])} only add functions, and **{e['commits']} change the text of an "
                  "existing function: the informative edits.**", "",
                  "**Headline (informative edits only):** "
                  f"any function summary changed in {e['any']} of {e['commits']} "
                  f"(95% Wilson {e['wilson_any'][0] * 100:.0f}-{e['wilson_any'][1] * 100:.0f}%); an edited function's own "
                  f"summary changed in {e['own']} of {e['commits']}; another, unedited function's summary changed "
                  f"(ripple) in {e['other']} of {e['commits']} "
                  f"(95% Wilson {e['wilson_other'][0] * 100:.0f}-{e['wilson_other'][1] * 100:.0f}%). "
                  f"{e['functions_edited']} functions were edited, {e['edited_changed']} of them changed summary, "
                  f"and {e['unedited_changed']} functions that were NOT edited changed summary. "
                  f"Own-changed functions by commit: {e['own_changed_by_commit']}.", "",
                  f"Secondary figure, over all {n} analysable commits that are not new packages (diluted by the "
                  "vacuous and addition-only commits):", "",
                  "| Question | Commits | Share | 95% Wilson |", "|---|---:|---:|---|"]
        w = ch["wilson"]
        for label, k in (("any function summary changed", "any"), ("an edited function's own summary changed", "own"),
                         ("some other (not edited) function's summary changed", "other")):
            ci = w.get(k)
            lines.append(f"| {label} | {h[k]} / {n} | {(h[k] / n * 100 if n else 0):.0f}% | "
                         f"{ci[0] * 100:.0f}-{ci[1] * 100:.0f}% |" if ci else f"| {label} | 0 / 0 | n/a | |")
        lines += [f"| no summary changed at all | {h['none']} / {n} | {(h['none'] / n * 100 if n else 0):.0f}% | |", "",
                  "By category over those commits (commits with at least one changed function in it; any / edited / other):", "",
                  "| Category | any function | edited function | other function (ripple) |", "|---|---:|---:|---:|"]
        for cat, v in ch["by_category"].items():
            lines.append(f"| {cat} | {v['any']} / {n} | {v['own']} / {n} | {v['other']} / {n} |")
        d, cv = ch["changed_per_commit"], ch["coverage"]
        lines += ["", f"Commit classes (commit numbers): informative edits {cl['edits_existing']}; additions only "
                  f"{cl['additions_only']}; tests/benches/fixtures only {cl['tests_only']}; new package or first source "
                  f"{cl['new_package']}.", "",
                  f"Coverage: of the {cv['modifying_existing_source']} commits that modify an existing source file of an "
                  f"existing package, {cv['analysable']} are analysable today. Functions whose summary changed per "
                  f"analysable non-new commit: p50 {f(d['p50'])}, p95 {f(d['p95'])}, max {f(d['max'], 0)}, mean {f(d['mean'])}.", "",
                  "Per commit:", ""] + churn_table(stats["churn_records"]) + [""]
        if ch["unavailable_list"]:
            lines += ["Unavailable (queries could not run on at least one side):", ""]
            lines += [f"- `{u['scenario']}`: {u['reason'][:160]}" for u in ch["unavailable_list"]]
            lines.append("")
    return lines


def churn_table(churn: list[dict]) -> list[str]:
    rows = ["| # | Commit | Fns pre | Fns post | Added | Removed | Edited | Summary changed | Own | Other | Categories |",
            "|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---|"]
    for c in churn:
        if not c["available"]:
            rows.append(f"| {c['index']} | `{c['commit'][:8]}` | unavailable | | | | | | | | |")
            continue
        rows.append(f"| {c['index']} | `{c['commit'][:8]}` | {c['functions_pre']} | {c['functions_post']} | "
                    f"{len(c['added'])} | {len(c['removed'])} | {len(c['edited'])} | {c['n_changed']} | "
                    f"{c['n_changed_own']} | {c['n_changed_other']} | {', '.join(c['categories_any']) or '-'} |")
    return rows
