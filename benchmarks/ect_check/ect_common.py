"""Shared plumbing for the ECT check benchmark suite (Phase 28.5, Team 1).

Nothing here depends on a particular compiler build: every benchmark goes
through command templates, so a future resident client can be measured with
the same fixtures.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess
import tempfile
import time

SUITE_NAME = "moss_ect_check"
SUITE_VERSION = "0.1.0"
SUITE_SEED = 20261105

SUITE_DIR = Path(__file__).resolve().parent
REPO_ROOT = SUITE_DIR.parent.parent
FIXTURES_DIR = SUITE_DIR / "fixtures"
MANIFEST_PATH = SUITE_DIR / "manifest.json"
TMP_ROOT = REPO_ROOT / "tmp" / "ect_check"

DEFAULT_MOSS = REPO_ROOT / "build" / "compiler" / "moss-opt"
DEFAULT_MARGO = REPO_ROOT / "margo"

# Directories that are build products and never part of a fixture's identity.
IGNORED_DIRS = {"build", "target", "__pycache__"}

# Default cold commands. Tokens are formatted after shlex-splitting, so paths
# containing spaces stay intact.
#   {moss} {margo}  compiler / driver executables
#   {path}          file fixtures: the entry file; package fixtures: root package dir
#   {root}          the staged fixture directory
#   {id}            fixture id
DEFAULT_COMMANDS = {
    "file": "{moss} check {path} --json",
    "package": "{margo} --root {path} --check --json build",
}

# Counters summed across checker runs (a package check may start several
# checker processes). Names are the compiler's own MOSS_PROFILE counter names.
WORK_COUNTERS = (
    ("functions_analyzed", "functions_analyzed"),
    ("specialization_evaluations", "specialization_requests"),
    ("specialization_hits", "reused_specialization_results"),
    ("specialization_keys", "distinct_specialization_keys"),
    ("specialization_effect_evaluations", "specialization_effect_function_evaluations"),
    ("specialization_effect_hits", "specialization_effect_cache_hits"),
)
VOLATILE_KEYS = {"duration_ms", "elapsed_ms", "time_ms", "timings", "pid",
                 "scheduling_events", "compiler_version"}


# --------------------------------------------------------------------------
# Hashing / manifest
# --------------------------------------------------------------------------

def tree_sha256(path: Path) -> str:
    """SHA-256 of a file, or of a directory as sorted (relpath, filehash) rows."""
    if path.is_file():
        return hashlib.sha256(path.read_bytes()).hexdigest()
    rows = []
    for item in sorted(path.rglob("*")):
        rel = item.relative_to(path)
        if any(part in IGNORED_DIRS for part in rel.parts) or not item.is_file():
            continue
        rows.append(f"{rel.as_posix()}\0{hashlib.sha256(item.read_bytes()).hexdigest()}")
    return hashlib.sha256("\n".join(rows).encode()).hexdigest()


def load_manifest() -> dict:
    return json.loads(MANIFEST_PATH.read_text())


def select_fixtures(manifest: dict, ids: list[str] | None) -> list[dict]:
    fixtures = manifest["fixtures"]
    if not ids:
        return fixtures
    wanted = set(ids)
    chosen = [f for f in fixtures if f["id"] in wanted]
    missing = wanted - {f["id"] for f in chosen}
    if missing:
        raise SystemExit(f"unknown fixture id(s): {sorted(missing)}")
    return chosen


# --------------------------------------------------------------------------
# Staging and command templates
# --------------------------------------------------------------------------

def stage_fixture(fixture: dict, dest: Path) -> dict[str, str]:
    """Copy a fixture into `dest` and return the template placeholders."""
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(FIXTURES_DIR / fixture["path"], dest,
                    ignore=shutil.ignore_patterns(*IGNORED_DIRS))
    return {"root": str(dest), "path": str(dest / fixture["entry"]),
            "id": fixture["id"]}


def expand_command(template: str, values: dict[str, str]) -> list[str]:
    return [token.format_map(values) for token in shlex.split(template)]


def command_for(fixture: dict, values: dict[str, str], template: str | None) -> list[str]:
    return expand_command(template or DEFAULT_COMMANDS[fixture["kind"]], values)


def build_flavor(path: Path) -> str:
    """Build flavor from the Makefile's naming: moss-opt is -O2, moss-dev is
    -O0 with -DMOSS_TEST_BUILD=1, moss-release is the release build."""
    override = os.environ.get("ECT_COMPILER_FLAVOR")
    if override:
        return override
    name = Path(path).name
    for key, flavor in (("moss-opt", "opt"), ("moss-dev", "dev"),
                        ("moss-release", "release")):
        if name.startswith(key):
            return flavor
    return "unknown"


def sha256_file(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Toolchain:
    """Executables substituted into {moss} / {margo}; margo also reads $MOSS."""

    def __init__(self, moss: Path, margo: Path):
        self.moss = Path(moss).resolve()
        self.margo = Path(margo).resolve()

    @property
    def flavor(self) -> str:
        return build_flavor(self.moss)

    def values(self) -> dict[str, str]:
        return {"moss": str(self.moss), "margo": str(self.margo)}

    def env(self) -> dict[str, str]:
        env = dict(os.environ)
        env["MOSS"] = str(self.moss)
        return env


# --------------------------------------------------------------------------
# Measured execution
# --------------------------------------------------------------------------

def run_measured(argv: list[str], *, cwd: Path, env: dict[str, str],
                 timeout: float = 600.0) -> dict:
    """Run argv once; return rc, stdout, stderr, wall/CPU ms and peak RSS (KiB).

    CPU and RSS come from wait4() rusage, which covers every descendant the
    process waited for (margo -> moss included). RSS is the largest single
    process in the tree, not a sum.
    """
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        start = time.perf_counter()
        proc = subprocess.Popen(argv, cwd=cwd, env=env, stdout=out, stderr=err,
                                stdin=subprocess.DEVNULL)
        deadline = start + timeout
        while True:
            pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
            if pid:
                break
            if time.perf_counter() > deadline:
                proc.kill()
                pid, status, usage = os.wait4(proc.pid, 0)
                break
            time.sleep(0.0005)
        wall = time.perf_counter() - start
        proc.returncode = os.waitstatus_to_exitcode(status)
        out.seek(0)
        err.seek(0)
        return {"rc": proc.returncode,
                "stdout": out.read().decode(errors="replace"),
                "stderr": err.read().decode(errors="replace"),
                "wall_ms": wall * 1000.0,
                "cpu_ms": (usage.ru_utime + usage.ru_stime) * 1000.0,
                "rss_kib": int(usage.ru_maxrss)}


def median(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


# --------------------------------------------------------------------------
# Verdict extraction (reference/candidate comparison)
# --------------------------------------------------------------------------

def _scrub(value, replacements: list[tuple[str, str]]):
    if isinstance(value, dict):
        return {k: _scrub(v, replacements) for k, v in value.items()
                if k not in VOLATILE_KEYS}
    if isinstance(value, list):
        return [_scrub(v, replacements) for v in value]
    if isinstance(value, str):
        for old, new in replacements:
            value = value.replace(old, new)
        return value
    return value


def _embedded_error(text: str):
    """Margo wraps a failing moss JSON object inside its `error` string."""
    start = text.find("{")
    if start < 0:
        return None
    try:
        return json.loads(text[start:])
    except ValueError:
        return None


def extract_verdict(result: dict, replacements: list[tuple[str, str]]) -> dict:
    """Reduce a command result to {exit_code, ok, diagnostics}.

    Understands moss-agent-1 (`moss check --json`) and margo-1 (`margo --json`)
    documents. A client template must print one of those; anything else is
    compared as scrubbed raw text so a mismatch is visible, never hidden.
    """
    text = result["stdout"]
    try:
        doc = json.loads(text)
    except ValueError:
        return {"exit_code": result["rc"], "ok": None, "diagnostics": None,
                "raw": _scrub(text, replacements)}
    ok = doc.get("ok")
    if ok:
        diagnostics = (doc.get("result") or {}).get("diagnostics", [])
    elif doc.get("schema_version") == "margo-1" and isinstance(doc.get("error"), str):
        inner = _embedded_error(doc["error"])
        if inner is not None:
            diagnostics = [inner.get("error", inner)]
        else:
            diagnostics = [doc["error"]]
    else:
        diagnostics = [doc.get("error", doc)]
    return {"exit_code": result["rc"], "ok": bool(ok),
            "diagnostics": _scrub(diagnostics, replacements)}


def status_of(verdict: dict) -> str:
    return "pass" if verdict.get("ok") else "fail"


# --------------------------------------------------------------------------
# Compiler counters (MOSS_PROFILE_COMPILER + MOSS_ECT_COUNTERS sink)
# --------------------------------------------------------------------------

def read_counter_records(sink: Path) -> list[dict]:
    if not sink.exists():
        return []
    return [json.loads(line) for line in sink.read_text().splitlines() if line.strip()]


def summarize_counters(records: list[dict]) -> dict:
    """Sum work counters across checker runs; None when the compiler is silent."""
    if not records:
        return {"checker_runs": 0, **{name: None for name, _ in WORK_COUNTERS}}
    summary = {"checker_runs": len(records)}
    for name, source in WORK_COUNTERS:
        summary[name] = sum(int(r.get(source, 0)) for r in records)
    return summary


def metric_record(records: list[dict]) -> dict | None:
    """The checker run that saw the package's own source functions.

    A package build starts several checker runs (the source check, then runs
    that reload the emitted interface). Later runs see no local bodies, so the
    run with the most local functions is the source check.
    """
    best, best_size = None, -1
    for rec in records:
        size = int(rec.get("ect_m1_exported_generics", 0)) + \
            int(rec.get("ect_m2_internal_functions", 0))
        if size > best_size:
            best, best_size = rec, size
    return best


def counter_env(sink: Path) -> dict[str, str]:
    return {"MOSS_PROFILE_COMPILER": "1", "MOSS_ECT_COUNTERS": str(sink)}


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")
