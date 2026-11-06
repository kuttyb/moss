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
                   "new_package": sc["new_package"], "touches_source": sc["touches_source"],
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
            "clean": [c for c in cs if c["post_status"] == "pass" and c["pre_status"] == "pass"
                      and not c["new_package"]],
        }
        stats = {}
        for name, sel in pops.items():
            stats[name] = {"commits": len(sel),
                           "post": dist([c["post_ms"] for c in sel]),
                           "pre": dist([c["pre_ms"] for c in sel if c["pre_ms"] is not None]),
                           "post_hist": histogram([c["post_ms"] for c in sel]),
                           "counters_post_p50": {k: percentile([c[f"post_{k}"] for c in sel if c[f"post_{k}"] is not None], .5)
                                                 for k in COUNTER_KEYS},
                           "counters_post_sum": {k: sum(c[f"post_{k}"] or 0 for c in sel) for k in COUNTER_KEYS},
                           "counters_pre_p50": {k: percentile([c[f"pre_{k}"] for c in sel if c[f"pre_{k}"] is not None], .5)
                                                for k in COUNTER_KEYS}}
        out["populations"][mode] = stats
    if churn:
        out["churn"] = churn_stats(churn)
        out["churn_records"] = [{k: v for k, v in c.items() if k not in ("changed", "changed_own", "changed_other")}
                                for c in churn]
    return out


def churn_stats(churn: list[dict]) -> dict:
    avail = [c for c in churn if c["available"]]
    unavailable = [c for c in churn if not c["available"]]
    newpkg = [c for c in avail if c["new_package"]]
    edits = [c for c in avail if not c["new_package"]]
    n = len(edits)

    def count(pred):
        return sum(1 for c in edits if pred(c))
    headline = {"any": count(lambda c: c["any_change"]), "own": count(lambda c: c["own_change"]),
                "other": count(lambda c: c["other_change"]),
                "none": count(lambda c: not c["any_change"]),
                "membership_only": count(lambda c: c["membership_only"]),
                "no_change_no_membership": count(lambda c: not c["any_change"] and not c["added"] and not c["removed"])}
    by_cat = {}
    for cat in ("types", "access", "raises", "effects"):
        by_cat[cat] = {"any": count(lambda c: cat in c["categories_any"]),
                       "own": count(lambda c: cat in c["categories_own"]),
                       "other": count(lambda c: cat in c["categories_other"])}
    per_commit_changed = [c["n_changed"] for c in edits]
    return {"total": len(churn), "available": len(avail), "unavailable": len(unavailable),
            "new_package_commits": len(newpkg), "edit_commits": n,
            "headline": headline, "by_category": by_cat,
            "changed_per_commit": dist(per_commit_changed),
            "wilson": {k: wilson(v, n) for k, v in headline.items() if k in ("any", "own", "other")},
            "unavailable_list": [{"scenario": c["scenario"], "reason": c["unavailable_reason"]} for c in unavailable],
            "newpkg_list": [c["scenario"] for c in newpkg],
            "partial_note": "a commit is available only when every touched unit answered the queries on both sides"}


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
    for mode, pops in stats["populations"].items():
        lines += [f"### Latency per edit ({mode})", "",
                  "| Population | Commits | post p50 ms | post p95 ms | post max ms | pre p50 ms | pre p95 ms |",
                  "|---|---:|---:|---:|---:|---:|---:|"]
        names = {"all": "all commits (incl. failing checks)", "post_pass": "post-edit check passes",
                 "clean": "clean edits: pre and post pass, package existed"}
        for k, label in names.items():
            p = pops[k]
            lines.append(f"| {label} | {p['commits']} | {f(p['post']['p50'])} | {f(p['post']['p95'])} | "
                         f"{f(p['post']['max'])} | {f(p['pre']['p50'])} | {f(p['pre']['p95'])} |")
        lines += ["", "Percentiles: linear interpolation between order statistics (`statistics.quantiles` "
                  "'inclusive' convention); with this few commits p95 sits close to the maximum.", ""]
        lines += ["Distribution of post-edit latency (commits per bucket):", "",
                  "| Bucket | " + " | ".join(names.values()) + " |", "|---|" + "---:|" * len(names)]
        for i, (label, _) in enumerate(pops["all"]["post_hist"]):
            lines.append(f"| {label} | " + " | ".join(str(pops[k]["post_hist"][i][1]) for k in names) + " |")
        lines.append("")
        lines += ["Compiler counters of the post-edit check (median over commits of the per-commit sum over "
                  "entry points; margo skips unchanged packages, so these count only what was rechecked):", "",
                  "| Population | Fns analyzed p50 | Source fn checks p50 | Spec evals p50 | Spec keys p50 | Checker runs p50 | Fns analyzed (sum) |",
                  "|---|---:|---:|---:|---:|---:|---:|"]
        for k, label in names.items():
            c = pops[k]["counters_post_p50"]
            lines.append(f"| {label} | {f(c['functions_analyzed'], 0)} | {f(c['source_function_checks'], 0)} | "
                         f"{f(c['specialization_evaluations'], 0)} | {f(c['specialization_keys'], 0)} | "
                         f"{f(c['checker_runs'], 0)} | {pops[k]['counters_post_sum']['functions_analyzed']} |")
        lines.append("")
    cold = [c for c in stats["commits"] if c["mode"] == "cold"]
    lines += ["### Per commit (cold)", "",
              "| # | Commit | Entries | Pre | Post | Pre ms | Post ms | Post fns analyzed | Post checker runs | Subject |",
              "|---:|---|---:|---|---|---:|---:|---:|---:|---|"]
    for c in cold:
        flag = ("new pkg; " if c["new_package"] else "") + ("" if c["touches_source"] else "no src; ")
        lines.append(f"| {c['index']} | `{c['commit'][:8]}` | {c['entries']} | {c['pre_status'] or '-'} | "
                     f"{c['post_status'] or '-'} | {f(c['pre_ms'])} | {f(c['post_ms'])} | "
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
        h, n = ch["headline"], ch["edit_commits"]
        lines += ["## Summary churn (does an edit move function summaries, and others' summaries?)", "",
                  f"Commits: {ch['total']} replayed; {ch['available']} answered the queries on every touched "
                  f"unit on both sides ({ch['unavailable']} did not: the tree does not check today); of "
                  f"those, {ch['new_package_commits']} add a package (pure membership change, no before "
                  f"state) and **{n} are edits of existing packages: the denominator below.**", "",
                  "| Question | Commits | Share of edits | 95% Wilson interval |", "|---|---:|---:|---|"]
        w = ch["wilson"]

        def share(k, key):
            ci = w.get(key)
            return (f"{h[k] / n * 100:.0f}%" if n else "n/a",
                    f"{ci[0] * 100:.0f}-{ci[1] * 100:.0f}%" if ci else "n/a")
        for label, k, key in (("any function summary changed (types, access, raises or effects)", "any", "any"),
                              ("an EDITED function's own summary changed", "own", "own"),
                              ("some OTHER (not edited) function's summary changed: ripple", "other", "other")):
            s1, s2 = share(k, key)
            lines.append(f"| {label} | {h[k]} / {n} | {s1} | {s2} |")
        lines += [f"| no summary changed at all | {h['none']} / {n} | "
                  f"{(h['none'] / n * 100 if n else 0):.0f}% | |",
                  f"| ... of which only functions were added/removed | {h['membership_only']} / {n} | | |", "",
                  "By category (commits in which at least one function's summary changed in that category):", "",
                  "| Category | any function | edited function | other function (ripple) |", "|---|---:|---:|---:|"]
        for cat, v in ch["by_category"].items():
            lines.append(f"| {cat} | {v['any']} / {n} | {v['own']} / {n} | {v['other']} / {n} |")
        d = ch["changed_per_commit"]
        lines += ["", f"Functions whose summary changed per edit commit: p50 {f(d['p50'])}, p95 {f(d['p95'])}, "
                  f"max {f(d['max'], 0)}, mean {f(d['mean'])}.", ""]
        lines += ["Per commit:", ""] + churn_table(stats["churn_records"]) + [""]
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
