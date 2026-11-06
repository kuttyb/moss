"""Report sections for the generic-DAG families (B1, B2, torture_ext, diamond).

Pure functions over runner result rows (cold mode only); no compiler access.
`analysis(rows)` is also stored in results.json so later runs can be diffed.
"""
from __future__ import annotations


def _cold(rows, family):
    return sorted((r for r in rows if r.get("mode") == "cold" and
                   (r.get("metadata") or {}).get("family") == family),
                  key=lambda r: ((r["metadata"].get("depth") or 0),
                                 (r["metadata"].get("width") or 0),
                                 (r["metadata"].get("types") or 0)))


def linfit(xs, ys):
    """Least-squares slope/intercept/R^2; None when fewer than 2 usable points."""
    pts = [(x, y) for x, y in zip(xs, ys) if y is not None]
    if len(pts) < 2:
        return None
    n = len(pts)
    mx = sum(p[0] for p in pts) / n
    my = sum(p[1] for p in pts) / n
    sxx = sum((p[0] - mx) ** 2 for p in pts)
    if sxx == 0:
        return None
    sxy = sum((p[0] - mx) * (p[1] - my) for p in pts)
    slope = sxy / sxx
    syy = sum((p[1] - my) ** 2 for p in pts)
    r2 = (sxy * sxy / (sxx * syy)) if syy else 1.0
    return {"slope": slope, "intercept": my - slope * mx, "r2": r2, "points": n}


METRICS = (("wall_ms_median", "wall ms"), ("cpu_ms_median", "CPU ms"),
           ("functions_analyzed", "functions analyzed"),
           ("specialization_keys", "spec keys"),
           ("specialization_evaluations", "spec evals"))


def analysis(rows) -> dict:
    out = {}
    b2 = _cold(rows, "b2_equiv")
    if b2:
        xs = [r["metadata"]["types"] for r in b2]
        out["b2_marginal_per_type"] = {
            key: linfit(xs, [r.get(key) for r in b2]) for key, _ in METRICS}
    per = {}
    for r in _cold(rows, "b1_leak"):
        m = r["metadata"]
        per.setdefault((m["depth"], m["width"]), []).append(r)
    if per:
        out["b1_marginal_per_type"] = {
            f"d{d:03d}_w{w}": {key: linfit([r["metadata"]["types"] for r in rs],
                                           [r.get(key) for r in rs]) for key, _ in METRICS}
            for (d, w), rs in sorted(per.items())}
    return out


def _f(v, digits=1):
    if v is None:
        return "n/a"
    return f"{v:.{digits}f}" if isinstance(v, float) else str(v)


def _fit_cell(fit, digits=2):
    return "n/a" if not fit else f"{fit['slope']:.{digits}f} (R2 {fit['r2']:.3f})"


def sections(rows) -> list[str]:
    lines: list[str] = []
    dag_rows = [r for r in rows if r.get("mode") == "cold" and
                (r.get("metadata") or {}).get("predicted_spec_keys") is not None]
    if not dag_rows:
        return lines
    lines += ["## 1b. Generic-DAG families (Phase 28.5 Team 2)", "",
              "Cold mode only. Counters come from the separate counter run. "
              "For package fixtures (B1) the provider `lib` is built first "
              "(`margo --check build`, its own series below) and the consumer is then "
              "measured with an up-to-date provider `.mossi`, so consumer numbers "
              "exclude provider build work.", "",
              "### Shape check: measured vs predicted", "",
              "Prediction: nodes = callables in the DAG (D*W+1 for DAG fixtures, "
              "depth+1 for chains); functions analyzed = nodes; distinct specialization "
              "keys = nodes x K (K = distinct concrete types; K=0 chain = nodes).", "",
              "| Fixture | Status | Nodes | K | Fns pred | Fns meas | Keys pred | Keys meas | Keys delta |",
              "|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for r in dag_rows:
        m = r["metadata"]
        keys, pred = r.get("specialization_keys"), m["predicted_spec_keys"]
        delta = "n/a" if keys is None else f"{keys - pred:+d}"
        lines.append(f"| `{r['fixture']}` | {r['status']}{'' if r['status_ok'] else ' **(UNEXPECTED)**'} | "
                     f"{m['nodes']} | {m['types']} | {m['predicted_functions_analyzed']} | "
                     f"{_f(r.get('functions_analyzed'))} | {pred} | {_f(keys)} | {delta} |")
    lines.append("")

    b1 = _cold(rows, "b1_leak")
    if b1:
        lines += ["### B1 (Mossi Leak): consumer check vs provider build", "",
                  "Consumer = `margo --check build` of `app` with `lib` already built. "
                  "Provider = `margo --check build` of `lib` alone (the cost a `.mossi` "
                  "firewall would be expected to absorb). `Fns (consumer)` counts "
                  "functions the consumer's checker analyzed; all of them live in package "
                  "`lib` because `app` defines none of the DAG.", "",
                  "| D | W | K | Nodes | Cons wall ms | Cons CPU ms | Cons RSS KiB | Fns (consumer) | "
                  "Spec keys | Spec hits | Prov wall ms | Prov CPU ms | Prov fns |",
                  "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for r in b1:
            m = r["metadata"]
            lines.append(
                f"| {m['depth']} | {m['width']} | {m['types']} | {m['nodes']} | "
                f"{_f(r['wall_ms_median'])} | {_f(r['cpu_ms_median'])} | "
                f"{_f(r['rss_kib_median'], 0)} | {_f(r.get('functions_analyzed'))} | "
                f"{_f(r.get('specialization_keys'))} | {_f(r.get('specialization_hits'))} | "
                f"{_f(r.get('provider_wall_ms_median'))} | {_f(r.get('provider_cpu_ms_median'))} | "
                f"{_f(r.get('provider_functions_analyzed'))} |")
        lines += ["", "#### B1 boundary-crossing evidence", "",
                  "`app` contains zero DAG functions (`dag_functions_in_consumer` = 0 in the "
                  "manifest), yet the consumer's checker run analyzes `nodes` functions and "
                  "creates `nodes x K` specializations. Those bodies can only come from "
                  "package `lib` via its `.mossi`. The provider build is a separate checker "
                  "run (it analyzes the same functions once, with no specializations).", "",
                  "| Fixture | Consumer checker runs | Consumer fns analyzed | DAG fns defined in app | "
                  "Provider checker runs | Provider fns analyzed |",
                  "|---|---:|---:|---:|---:|---:|"]
        for r in b1:
            lines.append(f"| `{r['fixture']}` | {_f(r.get('checker_runs'))} | "
                         f"{_f(r.get('functions_analyzed'))} | "
                         f"{r['metadata']['dag_functions_in_consumer']} | "
                         f"{_f(r.get('provider_checker_runs'))} | "
                         f"{_f(r.get('provider_functions_analyzed'))} |")
        an = analysis(rows)
        lines += ["", "#### B1 marginal cost per additional type (least squares over K = 1, 8, 32)", "",
                  "| D | W | wall ms/type | CPU ms/type | fns/type | spec keys/type |",
                  "|---:|---:|---:|---:|---:|---:|"]
        for name, fits in an.get("b1_marginal_per_type", {}).items():
            d, w = name.split("_")
            lines.append(f"| {int(d[1:])} | {w[1:]} | {_fit_cell(fits['wall_ms_median'])} | "
                         f"{_fit_cell(fits['cpu_ms_median'])} | "
                         f"{_fit_cell(fits['functions_analyzed'])} | "
                         f"{_fit_cell(fits['specialization_keys'])} |")
        lines.append("")

    b2 = _cold(rows, "b2_equiv")
    if b2:
        an = analysis(rows)["b2_marginal_per_type"]
        m0 = b2[0]["metadata"]
        lines += ["### B2 (equivalence in one package): marginal cost per new type", "",
                  f"Same DAG as B1 (D={m0['depth']}, W={m0['width']}, {m0['nodes']} nodes), no "
                  "package boundary, K = 1..32 user types, one fixture per K. Least-squares "
                  "fit of each metric against K (slope = marginal cost of one more type).", "",
                  "| Metric | Slope per type | Intercept | R2 | Points |", "|---|---:|---:|---:|---:|"]
        for key, label in METRICS:
            fit = an.get(key)
            if fit:
                lines.append(f"| {label} | {fit['slope']:.3f} | {fit['intercept']:.3f} | "
                             f"{fit['r2']:.4f} | {fit['points']} |")
        lines += ["", "| K | Wall ms | CPU ms | RSS KiB | Fns analyzed | Spec evals | Spec hits | Spec keys |",
                  "|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for r in b2:
            lines.append(f"| {r['metadata']['types']} | {_f(r['wall_ms_median'], 2)} | "
                         f"{_f(r['cpu_ms_median'], 2)} | {_f(r['rss_kib_median'], 0)} | "
                         f"{_f(r.get('functions_analyzed'))} | {_f(r.get('specialization_evaluations'))} | "
                         f"{_f(r.get('specialization_hits'))} | {_f(r.get('specialization_keys'))} |")
        lines.append("")

    for family, title, note in (
            ("torture_ext_w4", "Torture extension 1.1.0-ext: W4 deep chains",
             "Untyped chain `step_i(x) = step_{i-1}(x) + 1` instantiated once with Int."),
            ("torture_ext_cross", "Torture extension 1.1.0-ext: W4 x W2 cross",
             "Deep chain whose root is instantiated with K distinct user types. A fixture "
             "with expected status `fail` is a documented compiler defect, not a pass."),
            ("dag_diamond", "Shared-callee (diamond) DAG, K = 1",
             "Node (l,i) calls (l+1,i) and (l+1,(i+1)%W). Keys stay linear in nodes, but "
             "wall time grows super-linearly with D despite memo hits.")):
        fam = _cold(rows, family)
        if not fam:
            continue
        lines += [f"### {title}", "", note, "",
                  "| Fixture | Depth | K | Status (expected) | Exit | Timed out | Wall ms | CPU ms | "
                  "RSS KiB | Fns | Spec keys | Spec hits |",
                  "|---|---:|---:|---|---:|---|---:|---:|---:|---:|---:|---:|"]
        for r in fam:
            m = r["metadata"]
            lines.append(f"| `{r['fixture']}` | {m['depth']} | {m['types']} | "
                         f"{r['status']} ({r['expected_status']}) | {r.get('exit_code')} | "
                         f"{'yes' if r.get('timed_out') else 'no'} | {_f(r['wall_ms_median'])} | "
                         f"{_f(r['cpu_ms_median'])} | {_f(r['rss_kib_median'], 0)} | "
                         f"{_f(r.get('functions_analyzed'))} | {_f(r.get('specialization_keys'))} | "
                         f"{_f(r.get('specialization_hits'))} |")
        lines.append("")
    return lines
