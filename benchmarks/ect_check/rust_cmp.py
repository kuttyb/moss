"""Phase 28.5 Team 5: the Rust comparison (`cargo check` on the same generic DAG).

An idiomatic, std-only Rust crate (see rust_generators.py) is checked with
`cargo check --offline --locked --message-format=json` and driven through the
SAME edit sequences, patch machinery, noise probe and spread guard as the Moss
B3 sequences. Offline and std-only: no crates.io access, nothing written to
`~/.cargo` (CARGO_HOME and CARGO_TARGET_DIR point under tmp/).

What is measured per step: wall, CPU, peak RSS (`/usr/bin/time %M`, the largest
single process, i.e. rustc; plus the whole process tree sampled in a separate
untimed pass), and cargo's own freshness bookkeeping, which is the only counter
cargo exposes on stable: how many units it ran rustc on (`Checking <crate>` lines
on stderr = dirty units) and how many it reused (`"fresh": true` in the
compiler-artifact messages). No counter is invented beyond those.

Correctness gate: exit status plus the normalized diagnostics (level, error
code, message, primary span file and position) of reference and candidate must be
identical; `rendered` text and cargo's fresh/dirty bookkeeping are information
only. Paths are normalized to <FIXTURE>; line numbers are NOT stripped (same
tree, same lines). See README for the Phase 28.6 position-mapping requirement.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import ect_common as C
import scenarios as S

KIND = "cargo"
SEQ_KIND = "rust_edit_sequence"
FAMILY = "rust_edits"
CARGO_CMD = "{cargo} check --offline --locked --message-format=json --manifest-path {path}/Cargo.toml"
CANDIDATE_ENV = "ECT_RUST_CANDIDATE_CMD"
SCRUB_PREFIXES = ("CARGO_", "RUSTC", "RUSTFLAGS", "RUSTDOC", "RUST_LOG")


class RustUnavailable(C.ToolError):
    """cargo is not installed or not usable offline: the Rust comparison is skipped."""


class Rust:
    """The cargo toolchain used for a run (resolved once, recorded in the report)."""

    def __init__(self, cargo: Path, bindir: Path, versions: dict):
        self.cargo, self.bindir, self.versions = cargo, bindir, versions

    def values(self) -> dict[str, str]:
        return {"cargo": str(self.cargo)}

    def env(self, work: Path, run_dir: Path) -> dict[str, str]:
        """Clean environment for timed and gate runs: no CARGO_*/RUSTC*/RUSTFLAGS
        or instrumentation variables inherited. CARGO_INCREMENTAL is NOT set (the
        default for `cargo check`); CARGO_HOME and the target dir are private."""
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(SCRUB_PREFIXES) and not C.is_instrumentation_var(k)}
        env["PATH"] = f"{self.bindir}{os.pathsep}{env.get('PATH', '')}"
        env["CARGO_HOME"] = str(run_dir / "cargo_home")
        env["CARGO_TARGET_DIR"] = str(target_dir(work))
        env["CARGO_TERM_PROGRESS_WHEN"] = "never"
        env["CARGO_TERM_COLOR"] = "never"
        return env


def target_dir(work: Path) -> Path:
    return work.parent / f"{work.name}.target"


def find_rust() -> Rust:
    """Locate cargo (through rustup's `which` when cargo is a rustup proxy, so the
    proxy's exec does not sit in every timed run) and record its version strings.
    Raises RustUnavailable with one clear line when cargo cannot be used."""
    cargo = shutil.which("cargo")
    if cargo is None:
        raise RustUnavailable("cargo not found on PATH: the Rust comparison is skipped "
                              "(install a stable toolchain; no network or crates are needed)")
    try:
        real = subprocess.run(["rustup", "which", "cargo"], capture_output=True, text=True, timeout=30)
        if real.returncode == 0 and real.stdout.strip():
            cargo = real.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    cargo_p = Path(cargo).resolve()
    bindir = Path(cargo).parent
    env = {**os.environ, "PATH": f"{bindir}{os.pathsep}{os.environ.get('PATH', '')}"}
    try:
        cv = subprocess.run([str(cargo), "--version"], capture_output=True, text=True, timeout=30, env=env)
        rv = subprocess.run(["rustc", "-vV"], capture_output=True, text=True, timeout=30, env=env)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RustUnavailable(f"cargo at {cargo} cannot be run ({exc}): the Rust comparison is skipped")
    if cv.returncode != 0 or rv.returncode != 0:
        raise RustUnavailable(f"cargo/rustc at {cargo} failed to report a version: the Rust comparison is skipped")
    return Rust(Path(cargo), bindir, {"cargo": cv.stdout.strip(),
                                      "rustc": rv.stdout.strip().splitlines()[0] if rv.stdout else "",
                                      "rustc_verbose": rv.stdout.strip(), "cargo_path": str(cargo_p)})


def rust_fixtures(manifest: dict) -> list[dict]:
    return manifest.get("rust_fixtures", [])


def rust_scenarios(manifest: dict) -> list[dict]:
    return [s for s in manifest.get("scenarios", []) if s["kind"] == SEQ_KIND]


def pick(manifest: dict, args) -> list[dict]:
    """Rust scenarios selected by --scenario-ids (family names, ids or `all`);
    with neither --ids nor --scenario-ids every one."""
    allr = rust_scenarios(manifest)
    ids = getattr(args, "scenario_ids", None)
    if ids:
        if ids == ["all"]:
            return allr
        wanted = set(ids)
        return [s for s in allr if s["id"] in wanted or any(
            s["family"] == w or s["family"].startswith(w + "_") or s["id"].startswith(w + "_") for w in wanted)]
    if getattr(args, "ids", None):
        return []
    return allr


def fixture_by_id(manifest: dict, fid: str) -> dict:
    return next(f for f in rust_fixtures(manifest) if f["id"] == fid)


def verify_frozen(manifest: dict, scs: list[dict]) -> None:
    by = {f["id"]: f for f in rust_fixtures(manifest)}
    for fid in {s["fixture"] for s in scs}:
        fx = by[fid]
        if C.tree_sha256(C.FIXTURES_DIR / fx["path"]) != fx["sha256"]:
            raise C.ToolError(f"Rust fixture {fid} does not match its frozen SHA-256; refusing to run")
    for sc in scs:
        body = {k: v for k, v in sc.items() if k != "sha256"}
        if C.text_sha256(C.canonical_json(body)) != sc["sha256"]:
            raise C.ToolError(f"scenario {sc['id']} does not match its frozen SHA-256; refusing to run")


# --------------------------------------------------------------------------
# verdict
# --------------------------------------------------------------------------

_CHECKING = re.compile(r"^\s*(?:Checking|Compiling) (\S+) v", re.M)


def _norm(text, repl):
    for old, new in repl:
        text = text.replace(old, new)
    return text


def extract_verdict(result: dict, repl: list[tuple[str, str]], position_map: str | None = None,
                    context: dict | None = None) -> dict:
    """`position_map` (candidate side only) rewrites the verdict's positions (span line/column) to
    the reference layout first, see ect_common.POSITION_MAP; identity when None."""
    v = _extract_verdict(result, repl)
    if position_map:
        v = C.apply_position_map(v, position_map, "rust", context)
    return v


def _extract_verdict(result: dict, repl: list[tuple[str, str]]) -> dict:
    """Reduce a cargo run to {exit_code, ok, timed_out, diagnostics, document, bookkeeping}.

    diagnostics: every compiler-message (level, code, message, spans (file, line,
    column, primary), paths normalized). `document` (what the gate compares) is
    exit status + build-finished.success + diagnostics. `rendered` text and the
    fresh/dirty counts are NOT compared (the latter is cargo's own bookkeeping).
    Output that is not cargo JSON at all is compared as scrubbed raw text."""
    diags, success, fresh, artifacts = [], None, 0, 0
    for line in result["stdout"].splitlines():
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        reason = msg.get("reason")
        if reason == "compiler-message":
            m = msg["message"]
            diags.append({"level": m.get("level"), "code": (m.get("code") or {}).get("code"),
                          "message": _norm(m.get("message", ""), repl),
                          "spans": [{"file": _norm(sp["file_name"], repl), "line": sp["line_start"],
                                     "column": sp["column_start"], "primary": sp["is_primary"]}
                                    for sp in m.get("spans", [])],
                          "rendered": _norm(m.get("rendered") or "", repl)})
        elif reason == "compiler-artifact":
            artifacts += 1
            fresh += 1 if msg.get("fresh") else 0
        elif reason == "build-finished":
            success = bool(msg.get("success"))
    checked = _CHECKING.findall(result["stderr"])
    book = {"units_checked": len(checked), "units_fresh": fresh, "units_with_artifacts": artifacts,
            "checked_units": checked}
    compare = [{k: v for k, v in d.items() if k != "rendered"} for d in diags]
    base = {"exit_code": result["rc"], "timed_out": bool(result.get("timed_out")), "bookkeeping": book}
    if success is None:
        raw = _norm(result["stderr"] or result["stdout"], repl)
        return {**base, "ok": None, "diagnostics": None, "raw": raw[:2000],
                "document": {"raw": raw[:2000]}, "rendered": []}
    errors = [d for d in compare if d["level"] == "error"]
    return {**base, "ok": success, "diagnostics": compare, "rendered": [d["rendered"] for d in diags],
            "n_errors": len(errors), "n_warnings": len(compare) - len(errors),
            "document": {"build_finished": success, "diagnostics": compare}}


def gate_view(v: dict) -> dict:
    return {"exit_code": v["exit_code"], "ok": v["ok"], "document": v["document"]}


def diag_summary(v: dict) -> str | None:
    if v.get("ok"):
        return None
    errs = [d for d in (v.get("diagnostics") or []) if d["level"] == "error"]
    if errs:
        d = errs[0]
        return f"{d['code'] or '?'}: {d['message'][:160]}"
    return (v.get("raw") or "")[:160] or None


def replacements(work: Path, rust: Rust, run_dir: Path) -> list[tuple[str, str]]:
    return [(str(target_dir(work)), "<TARGET>"), (str(work), "<FIXTURE>"),
            (str(rust.cargo), "<CARGO>"), (str(run_dir / "cargo_home"), "<CARGO_HOME>")]


# --------------------------------------------------------------------------
# sequences
# --------------------------------------------------------------------------

def stage(fx: dict, work: Path) -> dict[str, str]:
    shutil.rmtree(target_dir(work), ignore_errors=True)
    return C.stage_fixture(fx, work)


def argv_for(fx: dict, rust: Rust, values: dict, template: str | None = None) -> list[str]:
    return C.expand_command(template or CARGO_CMD, {**rust.values(), **values})


def run_sequence(sc: dict, fx: dict, rust: Rust, template: str | None, work: Path, run_dir: Path, *,
                 do_stage: bool = True, sample: bool = False) -> dict[str, dict]:
    values = stage(fx, work) if do_stage else {"root": str(work), "path": str(work / fx["entry"]),
                                                "id": fx["id"], "kind": fx["kind"]}
    results: dict[str, dict] = {}
    for step in sc["steps"]:
        op = step["op"]
        if op == "check":
            res = C.run_measured(argv_for(fx, rust, values, template), cwd=work, env=rust.env(work, run_dir),
                                 timeout=C.fixture_timeout(fx), sample_tree_rss=sample)
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


def measure_once(sc: dict, fx: dict, rust: Rust, reps: int, run_dir: Path, warmup: bool = True) -> list[dict]:
    """One row per check step: medians over `reps` complete sequences, each on a
    freshly staged tree with an empty target dir (cold = fresh processes, empty
    target dir at `first`; later steps see what earlier steps left, as in a real
    edit loop). One discarded sequence first. `spread` = max/min wall over reps."""
    work = run_dir / "work" / f"rust-{sc['id']}"
    repl = replacements(work, rust, run_dir)
    expects = {s["label"]: s["expect"] for s in sc["steps"] if s["op"] == "check"}
    exits = S.expect_exits(sc)
    codes = S.expect_codes(sc)
    samples: dict[str, list[dict]] = {label: [] for label in expects}
    if warmup:
        run_sequence(sc, fx, rust, None, work, run_dir)
    for _ in range(reps):
        for label, r in run_sequence(sc, fx, rust, None, work, run_dir).items():
            samples[label].append(r)
    sampled: dict[str, list[int]] = {label: [] for label in expects}
    for _ in range(3):      # separate, untimed passes: the /proc sampler perturbs the machine
        for label, r in run_sequence(sc, fx, rust, None, work, run_dir, sample=True).items():
            sampled[label].append(r.get("tree_rss_peak_kib") or 0)
    shutil.rmtree(work, ignore_errors=True)
    shutil.rmtree(target_dir(work), ignore_errors=True)
    rows = []
    for label, runs in samples.items():
        good = [r for r in runs if not r.get("timed_out")]
        verdicts = [extract_verdict(r, repl) for r in runs]
        walls = [r["wall_ms"] for r in good]
        books = {json.dumps({k: v for k, v in x["bookkeeping"].items() if k != "checked_units"}, sort_keys=True)
                 for x in verdicts}
        v = verdicts[-1]
        rows.append({
            "fixture": f"{sc['id']}:{label}", "scenario": sc["id"], "step": label, "base_fixture": fx["id"],
            "kind": KIND, "mode": "cold", "reps": reps, "expected_status": expects[label],
            "expected_exit_code": exits[label], "expected_code": codes[label], "status": C.status_of(v),
            "status_ok": len(good) == len(runs) and all(
                S._expect(expects[label], x, exits[label], codes[label]) for x in verdicts),
            "timed_out_reps": len(runs) - len(good), "timed_out": len(good) != len(runs),
            "wall_ms_median": C.median(walls), "wall_ms_min": min(walls, default=None),
            "wall_ms_max": max(walls, default=None), "wall_ms_first": walls[0] if walls else None,
            "cpu_ms_median": C.median([r["cpu_ms"] for r in good]),
            "rss_kib_median": C.median([r["rss_kib"] for r in good]),
            "tree_rss_peak_kib_median": C.median(sampled[label]),
            "wall_ms_samples": [round(r["wall_ms"], 3) for r in runs],
            "spread": (max(walls) / min(walls)) if walls and min(walls) > 0 else None,
            "exit_code": v["exit_code"], "category": sc["family"],
            "units_checked": v["bookkeeping"]["units_checked"], "units_fresh": v["bookkeeping"]["units_fresh"],
            "bookkeeping_stable_across_reps": len(books) == 1,
            "n_errors": v.get("n_errors"), "n_warnings": v.get("n_warnings"),
            "diagnostic": diag_summary(v),
            "diagnostic_codes": [d["code"] for d in (v.get("diagnostics") or []) if d["level"] == "error"],
            "metadata": {"classification": sc.get("classification"), "edited_function": sc.get("edited_function"),
                         "edit_count": sc.get("edit_count")},
            "fixture_path": fx["path"],
        })
    return rows


def measure_scenario(sc, fx, rust, reps, run_dir, warmup=True):
    import replay as RP
    return RP.guarded_measure(lambda: measure_once(sc, fx, rust, reps, run_dir, warmup))


# --------------------------------------------------------------------------
# gate and validation
# --------------------------------------------------------------------------

def gate_scenario(sc: dict, fx: dict, ref: Rust, cand: Rust, cand_template: str | None, run_dir: Path,
                  position_map: str | None = None) -> dict:
    """Reference vs candidate on every check step: identical exit status and
    normalized diagnostics, and reference == the step's expectation."""
    verdicts = {}
    for side, rust, template in (("reference", ref, None), ("candidate", cand, cand_template)):
        work = run_dir / "work" / f"rgate-{side}-{sc['id']}"
        res = run_sequence(sc, fx, rust, template, work, run_dir)
        repl = replacements(work, rust, run_dir)
        pm = position_map if side == "candidate" else None
        verdicts[side] = {label: extract_verdict(r, repl, pm, {"scenario": sc["id"], "step": label})
                          for label, r in res.items()}
        shutil.rmtree(work, ignore_errors=True)
        shutil.rmtree(target_dir(work), ignore_errors=True)
    expects = {s["label"]: s["expect"] for s in sc["steps"] if s["op"] == "check"}
    exits = S.expect_exits(sc)
    codes = S.expect_codes(sc)
    steps = []
    for label, exp in expects.items():
        r, c = verdicts["reference"][label], verdicts["candidate"][label]
        steps.append({"step": label, "identical": gate_view(r) == gate_view(c),
                      "bookkeeping_reference": r["bookkeeping"]["units_checked"],
                      "bookkeeping_candidate": c["bookkeeping"]["units_checked"],
                      "reference_matches_expected": S._expect(exp, r, exits[label], codes[label]),
                      "candidate_matches_expected": S._expect(exp, c, exits[label], codes[label]),
                      "status": C.status_of(r), "reference": gate_view(r), "candidate": gate_view(c)})
    return {"scenario": sc["id"], "fixture": fx["id"], "steps": steps,
            "passed": all(s["identical"] and s["reference_matches_expected"] for s in steps)}


def validate_scenario(sc: dict, fx: dict, rust: Rust, run_dir: Path) -> list[str]:
    work = run_dir / "work" / f"rvalidate-{sc['id']}"
    try:
        res = run_sequence(sc, fx, rust, None, work, run_dir)
    except C.ToolError as exc:
        return [f"{sc['id']}: {exc}"]
    finally:
        pass
    repl = replacements(work, rust, run_dir)
    bad = []
    exits = S.expect_exits(sc)
    codes = S.expect_codes(sc)
    for step in sc["steps"]:
        if step["op"] == "check":
            v = extract_verdict(res[step["label"]], repl)
            if not S._expect(step["expect"], v, exits[step["label"]], codes[step["label"]]):
                bad.append(f"{sc['id']}:{step['label']}: status {C.status_of(v)} (exit {v['exit_code']}) "
                           f"!= expected {step['expect']}")
    shutil.rmtree(work, ignore_errors=True)
    shutil.rmtree(target_dir(work), ignore_errors=True)
    return bad


def validate_fixture(fx: dict, rust: Rust, run_dir: Path) -> list[str]:
    """The unedited fixture passes `cargo check --locked` (also proves Cargo.lock is final)."""
    work = run_dir / "work" / f"rvalidate-fx-{fx['id']}"
    values = stage(fx, work)
    res = C.run_measured(argv_for(fx, rust, values), cwd=work, env=rust.env(work, run_dir),
                         timeout=C.fixture_timeout(fx))
    v = extract_verdict(res, replacements(work, rust, run_dir))
    shutil.rmtree(work, ignore_errors=True)
    shutil.rmtree(target_dir(work), ignore_errors=True)
    ok = C.fixture_ok(fx, v) and not v["diagnostics"] and C.tree_sha256(C.FIXTURES_DIR / fx["path"]) == fx["sha256"]
    return [] if ok else [f"{fx['id']}: cargo check exit {v['exit_code']} ok={v['ok']} diagnostics={v.get('diagnostics')}"]
