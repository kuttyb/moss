"""Report sections for Team 5: the Rust comparison and the B3 error-path edit."""
from __future__ import annotations

from collections import defaultdict

EDITS = ("a", "b", "c", "d", "e", "f")
# (label, Moss B3 scenario prefix, Rust scenario ids by layout)
SHAPES = (
    ("DAG D20 W4 K2", "b3_dag", {"single": "rust_dag_single", "workspace": "rust_dag_workspace"}),
    ("Fan-in D50 W4 K2", "b3_fanin", {"single": "rust_fanin_single"}),
)


def f(v, d=1):
    if v is None:
        return "n/a"
    return f"{v:.{d}f}" if isinstance(v, float) else str(v)


def mib(kib):
    return "n/a" if kib is None else f"{kib / 1024:.0f}"


def _rows(rows):
    out = defaultdict(dict)
    for r in rows:
        if r.get("scenario") and r["mode"] == "cold":
            out[r["scenario"]][r["step"]] = r
    return out


def _cell(r):
    """wall ms / CPU ms / peak RSS MiB of the whole process tree (sampled in separate untimed
    passes; Moss and Rust alike) of one measured step."""
    if r is None:
        return "n/a"
    return f"{f(r['wall_ms_median'])} / {f(r['cpu_ms_median'])} / {mib(r.get('tree_rss_peak_kib_median'))}"


def _flag(r):
    if r is None:
        return ""
    out = ""
    if r.get("spread_flagged"):
        out += " **(spread flagged)**"
    if not r["status_ok"]:
        out += " **(UNEXPECTED)**"
    return out


def sections(rows: list[dict], moss_scs: list[dict], rust_scs: list[dict], gate: dict | None, meta: dict) -> list[str]:
    by = _rows(rows)
    rust_ids = {s["id"] for s in rust_scs}
    moss_ids = {s["id"] for s in moss_scs}
    has_error_moss = any(i in by for i in ("b3_dag_f", "b3_fanin_f"))
    if not rust_ids and not has_error_moss:
        return []
    lines: list[str] = ["## 5. Rust comparison and the B3 error path (Team 5)", ""]
    if rust_ids:
        rv = meta.get("rust") or {}
        lines += ["**Not apples-to-apples**: a different language, checker and feature set (see README). "
                  "Rust figures are `cargo check` on a std-only crate with the same generic DAG shape; "
                  "Moss figures are `margo --check build` of the B3 package from the same run. "
                  "Cells are wall ms / CPU ms / peak RSS MiB of the WHOLE process tree (sampled every ~3 ms in "
                  "separate untimed passes, median of 3; for both languages: margo + python + moss, cargo + rustc). "
                  "`/usr/bin/time %M` (largest single process) is in the all-steps table and results.csv; for Moss "
                  "it probably reads margo's Python rather than the compiler (not established). Wall and CPU are medians over the timed reps.", "",
                  f"Toolchain: `{rv.get('cargo')}`, `{rv.get('rustc')}`. {meta.get('rust_env')}. "
                  "`CARGO_INCREMENTAL` is the default for `cargo check`.", ""]
        # ---------------- per-edit comparison (after-edit step), per shape
        for label, moss_prefix, layouts in SHAPES:
            present = {lay: pre for lay, pre in layouts.items() if f"{pre}_a" in by}
            if not present:
                continue
            lines += [f"### {label}: step after each edit (Moss vs Rust)", "",
                      "| Edit | Moss `margo --check build` | " + " | ".join(
                          f"Rust `cargo check` ({lay})" for lay in present) + " | Rust units rechecked / reused (" +
                          " / ".join(present) + ") |",
                      "|---|---|" + "---|" * len(present) + "---|"]
            for e in EDITS:
                moss = by.get(f"{moss_prefix}_{e}", {}).get("edit")
                cells = [_cell(by[f"{pre}_{e}"].get("edit")) + _flag(by[f"{pre}_{e}"].get("edit"))
                         if f"{pre}_{e}" in by else "n/a" for pre in present.values()]
                units = " / ".join(
                    (f"{by[f'{pre}_{e}']['edit']['units_checked']} / {by[f'{pre}_{e}']['edit']['units_fresh']}"
                     if f"{pre}_{e}" in by else "n/a") for pre in present.values())
                lines.append(f"| `{e}`{' (fails)' if e == 'f' else ''} | {_cell(moss)}{_flag(moss)} | "
                             + " | ".join(cells) + f" | {units} |")
            lines.append("")
        # ---------------- first / second / revert
        lines += ["### First check (empty target / build dir), no-change second check, and revert", "",
                  "Rust `first` = empty target dir; `second` = identical tree (cargo's freshness check); `revert` = "
                  "after the byte-identical revert. Moss `first`/`revert` from the B3 `a` sequence; Moss `second` "
                  "from the B4 sequence of the same package (margo skips an unchanged package).", "",
                  "| Project | Step | Moss | Rust (single crate) | Rust (workspace) | Rust units rechecked / reused (single / ws) |",
                  "|---|---|---|---|---|---|"]
        b4 = by.get("b4_seq_dag_d20_w4_k02", {})
        for label, moss_prefix, layouts in SHAPES:
            sa = by.get(f"{layouts['single']}_a")
            wa = by.get(f"{layouts['workspace']}_a") if "workspace" in layouts else None
            ma = by.get(f"{moss_prefix}_a", {})
            if sa is None:
                continue
            for step, mstep in (("first", ma.get("base")), ("second", b4.get("second") if moss_prefix == "b3_dag" else None),
                                ("revert", ma.get("revert"))):
                su = sa.get(step)
                wu = wa.get(step) if wa else None
                u = f"{su['units_checked']} / {su['units_fresh']}" + (
                    f" ; {wu['units_checked']} / {wu['units_fresh']}" if wu else "")
                lines.append(f"| {label} | {step} | {_cell(mstep)} | {_cell(su)}{_flag(su)} | "
                             f"{_cell(wu) if wu else 'n/a'}{_flag(wu)} | {u} |")
        floor = by.get("rust_floor_edit")
        if floor:
            lines += ["", "Startup floor (a `main` that only prints a literal): Rust `cargo check` "
                      f"first {_cell(floor.get('first'))}, second {_cell(floor.get('second'))}, "
                      f"after a one-line edit {_cell(floor.get('edit'))}; Moss `margo --check build` floor "
                      "is in the B4 table above."]
        lines.append("")
        # ---------------- whole-tree RSS
        lines += ["### Peak RSS of the whole process tree (sampled in separate untimed passes, MiB)", "",
                  "| Sequence | first | second | edit | revert |", "|---|---:|---:|---:|---:|"]
        for sid in sorted(rust_ids & set(by)):
            if not sid.endswith(("_a", "_f", "floor_edit")):
                continue
            st = by[sid]
            lines.append(f"| `{sid}` | " + " | ".join(
                mib(st[s]["tree_rss_peak_kib_median"]) if s in st else "n/a"
                for s in ("first", "second", "edit", "revert")) + " |")
        lines.append("")
        # ---------------- measurement quality
        flagged = [r for r in rows if r.get("kind") == "cargo" and r.get("spread_flagged")]
        retried = [r for r in rows if r.get("kind") == "cargo" and (r.get("attempts") or 1) > 1]
        noisy = [r for r in rows if r.get("kind") == "cargo" and r.get("block_noisy")]
        unstable = [r for r in rows if r.get("kind") == "cargo" and not r.get("bookkeeping_stable_across_reps", True)]
        lines += ["Measurement quality (Rust): per-scenario spread guard limit 1.3 (max/min wall over reps), "
                  f"at most 3 attempts; steps flagged after the last attempt: {len(flagged)}; rows measured "
                  f"more than once: {len(retried)}; rows in a block still noisy after the retries: {len(noisy)}; "
                  f"rows whose cargo bookkeeping differed between reps: {len(unstable)}.", ""]
        if flagged:
            lines += ["Flagged (spread above the limit after 3 attempts): " + ", ".join(
                f"`{r['fixture']}` ({f(r['spread'], 2)})" for r in flagged), ""]
        # ---------------- full per-step table
        lines += ["### All Rust steps", "",
                  "| Sequence | Step | Status (exit) | Wall ms (min-max) | CPU ms | RSS MiB | Units rechecked / reused | Spread | Attempts |",
                  "|---|---|---|---:|---:|---:|---|---:|---:|"]
        for sid in sorted(rust_ids & set(by)):
            for step, r in by[sid].items():
                lines.append(f"| `{sid}` | {step} | {r['status']} ({r['exit_code']}) | "
                             f"{f(r['wall_ms_median'])} ({f(r['wall_ms_min'])}-{f(r['wall_ms_max'])}) | "
                             f"{f(r['cpu_ms_median'])} | {mib(r['rss_kib_median'])} | "
                             f"{r['units_checked']} / {r['units_fresh']} | {f(r['spread'], 2)} | "
                             f"{r.get('attempts', 1)} |{_flag(r)}")
        lines.append("")
    # -------------------------------------------------------------------- error path
    err = [(i, by[i]) for i in ("b3_dag_f", "b3_fanin_f") if i in by]
    rerr = [(i, by[i]) for i in sorted(rust_ids) if i.endswith("_f") and i in by]
    if err or rerr:
        lines += ["### The error path: edit `f` (a type error in the top node of the edited chain)", "",
                  "Moss: `+ \"one\"` in `n000_0` (DAG) / `e0` (fan-in); the same text and position in the Rust "
                  "crates (`i64 + &str`). Probed alternatives (leaf breaks) gave no answer within 20 s on "
                  "Moss: `probes/f_edit_probe.txt`. Expected: `edit` fails (Moss margo exit 1, cargo exit 101), "
                  "`revert` passes. Counters for a failing Moss check are whatever the compiler reports "
                  "before the failure (functions analyzed shown).", "",
                  "| Sequence | Step | Status (exit) | Wall ms | CPU ms | RSS MiB | Fns analyzed (Moss) / units rechecked (Rust) | Diagnostic |",
                  "|---|---|---|---:|---:|---:|---|---|"]
        for sid, st in err + rerr:
            for step in ("base", "first", "edit", "revert"):
                if step not in st:
                    continue
                r = st[step]
                work = (f"{r.get('units_checked')} rechecked / {r.get('units_fresh')} reused"
                        if r.get("kind") == "cargo" else f(r.get("functions_analyzed")))
                lines.append(f"| `{sid}` | {step} | {r['status']} ({r['exit_code']}) | {f(r['wall_ms_median'])} | "
                             f"{f(r['cpu_ms_median'])} | {mib(r['rss_kib_median'])} | {work} | "
                             f"{(r.get('diagnostic') or '')[:110]} |{_flag(r)}")
        lines.append("")
    # -------------------------------------------------------------------- gate
    if gate and gate.get("rust_skipped"):
        lines += ["### Rust gate", "", "**FAIL-OPEN: " + "; ".join(gate["rust_skipped"]) +
                  " The gate result above does NOT cover Rust.**", ""]
    if gate and gate.get("rust"):
        lines += ["### Rust correctness gate (reference vs candidate cargo)", "",
                  f"**Result: {'PASS' if all(r['passed'] for r in gate['rust']) else 'FAIL'}** over "
                  f"{len(gate['rust'])} sequences. Compared per check step: exit status, `build-finished` "
                  "success and every diagnostic (level, code, message, primary-span file/line/column; "
                  "paths normalized, `rendered` text and cargo's fresh/dirty counts excluded).", "",
                  "| Sequence | Steps identical | Reference matches expectation |", "|---|---|---|"]
        for sg in gate["rust"]:
            lines.append(f"| `{sg['scenario']}` | {'yes' if all(x['identical'] for x in sg['steps']) else '**NO**'} | "
                         f"{'yes' if all(x['reference_matches_expected'] for x in sg['steps']) else '**NO**'} |")
        lines.append("")
    return lines
