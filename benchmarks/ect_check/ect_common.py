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
import resource
import signal
import shlex
import shutil
import statistics
import subprocess
import tempfile
import time

SUITE_NAME = "moss_ect_check"
SUITE_VERSION = "0.3.0"
SUITE_SEED = 20261105

SUITE_DIR = Path(__file__).resolve().parent
REPO_ROOT = SUITE_DIR.parent.parent
FIXTURES_DIR = SUITE_DIR / "fixtures"
MANIFEST_PATH = SUITE_DIR / "manifest.json"
TMP_ROOT = REPO_ROOT / "tmp" / "ect_check"

DEFAULT_MOSS = REPO_ROOT / "build" / "compiler" / "moss-opt"
DEFAULT_MARGO = REPO_ROOT / "margo"

# Directories that are build products and never part of a fixture's identity.
# Only a package's own top-level `build/` (a directory next to a Moss.toml) is a
# build output. Any other directory named build/target is fixture content, and
# generators reject such names outright (see generate.reject_reserved_names).
RESERVED_NAMES = {"build", "target", "__pycache__"}


class ToolError(Exception):
    """A configuration/usage problem reported as one line, not a traceback."""


def is_build_output(directory: Path, name: str) -> bool:
    return name == "build" and (Path(directory) / "Moss.toml").exists()


def copy_ignore(directory, names):
    return [n for n in names
            if is_build_output(Path(directory), n) or n == "__pycache__"]

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
    ("source_function_checks", "source_function_checks"),
    ("specialization_evaluations", "specialization_requests"),
    ("specialization_hits", "reused_specialization_results"),
    ("specialization_keys", "distinct_specialization_keys"),
    ("specialization_effect_evaluations", "specialization_effect_function_evaluations"),
    ("specialization_effect_hits", "specialization_effect_cache_hits"),
)
# The complete list of fields ignored when comparing verdicts. Everything else
# in the JSON document must match exactly between reference and candidate.
VOLATILE_KEYS = {"duration_ms", "elapsed_ms", "time_ms", "timings", "pid",
                 "scheduling_events", "compiler_version"}


class Generator:
    """One family of fixtures. `generate()` must be a pure function of rng."""
    name = "base"
    version = "1.0.0"
    category = "base"

    def generate(self, rng: "random.Random", out_dir: Path) -> list[dict]:
        """Write fixture directories under out_dir; return manifest rows
        (without sha256/generator fields; those are added by the framework).
        Row keys: id, path (relative to out_dir), kind (file|package),
        entry (relative to path), expected_status, metadata."""
        raise NotImplementedError


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
        skip = False
        for i, part in enumerate(rel.parts[:-1] if item.is_file() else rel.parts):
            if part == "__pycache__" or is_build_output(path.joinpath(*rel.parts[:i]), part):
                skip = True
                break
        if skip or not item.is_file():
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
                    ignore=copy_ignore)
    return {"root": str(dest), "path": str(dest / fixture["entry"]),
            "id": fixture["id"], "kind": fixture["kind"]}


DEFAULT_TIMEOUT_S = 600.0
# A provider package (generic-DAG fixtures with a package boundary) is built
# before the consumer is measured, so the consumer check sees an up-to-date
# provider `.mossi` and checks only its own package. `margo --check` writes the
# interface without rustc, same stage the consumer series uses.
PROVIDER_COMMAND = "{margo} --root {provider_root} --check --json build"


def fixture_timeout(fixture: dict) -> float:
    """Per-fixture timeout (manifest `timeout_s`); large generated fixtures set it."""
    return float(fixture.get("timeout_s") or DEFAULT_TIMEOUT_S)


def build_provider(fixture: dict, tc: "Toolchain", work: Path,
                   extra_env: dict | None = None) -> dict:
    """Build the fixture's provider package (manifest `provider`, a directory
    relative to the staged fixture root). Returns the measured result; raises
    ToolError if it fails, since the consumer series would be meaningless."""
    root = Path(work) / fixture["provider"]
    values = {**tc.values(), "provider_root": str(root)}
    env = tc.env()
    env.update(extra_env or {})
    res = run_measured(expand_command(PROVIDER_COMMAND, values), cwd=root, env=env,
                       timeout=fixture_timeout(fixture), stack_kib=fixture_stack(fixture))
    if res["rc"] != 0:
        raise ToolError(f"provider build of {fixture['id']} failed: "
                        f"{(res['stdout'] or res['stderr'])[:300]}")
    return res


def expand_command(template: str, values: dict[str, str]) -> list[str]:
    try:
        return [token.format_map(values) for token in shlex.split(template)]
    except KeyError as exc:
        raise ToolError(f"command template {template!r} uses unknown placeholder "
                        f"{{{exc.args[0]}}}; known: {', '.join('{'+k+'}' for k in sorted(values))}")
    except (ValueError, IndexError) as exc:
        raise ToolError(f"bad command template {template!r}: {exc}")


def command_for(fixture: dict, values: dict[str, str], template: str | None) -> list[str]:
    return expand_command(template or DEFAULT_COMMANDS[fixture["kind"]], values)


def is_instrumentation_var(name: str) -> bool:
    return (name.startswith(("MOSS_PROFILE", "MOSS_ECT_")) or
            (name.startswith("MOSS_") and name.endswith("_TRACE")) or
            name in ("MOSS_ECT_COUNTERS",))


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
        """Clean environment for timed and gate runs: no profiling/counter or
        trace variables inherited from the caller. Only the separate counter
        run adds them back (counter_env)."""
        env = {k: v for k, v in os.environ.items() if not is_instrumentation_var(k)}
        env["MOSS"] = str(self.moss)
        return env


# --------------------------------------------------------------------------
# Measured execution
# --------------------------------------------------------------------------

TIME_BIN = "/usr/bin/time"


def _stack_preexec(stack_kib):
    if not stack_kib:
        return None

    def apply():
        _, hard = resource.getrlimit(resource.RLIMIT_STACK)
        want = int(stack_kib) * 1024
        if hard != resource.RLIM_INFINITY:
            want = min(want, hard)
        resource.setrlimit(resource.RLIMIT_STACK, (want, hard))
    return apply


def fixture_stack(fixture: dict):
    """Per-fixture stack limit in KiB (manifest `stack_kib`), or None."""
    return fixture.get("stack_kib")


def fixture_ok(fixture: dict, verdict: dict, timed_out: bool = False) -> bool:
    """Does a run satisfy the manifest? Status must match, a timeout never
    counts, and `expected_exit_code` (when set) must match exactly, so a crash,
    abort or hang cannot stand in for a clean diagnostic."""
    if timed_out or verdict.get("timed_out"):
        return False
    if status_of(verdict) != fixture["expected_status"]:
        return False
    want = fixture.get("expected_exit_code")
    return want is None or verdict.get("exit_code") == want


class _Launch:
    """One launched `/usr/bin/time`-wrapped command (see run_measured)."""

    def __init__(self, argv, cwd, env, stack_kib):
        if not Path(TIME_BIN).exists():
            raise ToolError(f"{TIME_BIN} is required for peak-RSS measurement")
        self.argv = argv
        self.out = tempfile.TemporaryFile()
        self.err = tempfile.TemporaryFile()
        self.rss_file = tempfile.NamedTemporaryFile("r")
        wrapped = [TIME_BIN, "-f", "%M", "-o", self.rss_file.name, *argv]
        self.start = time.perf_counter()
        try:
            self.proc = subprocess.Popen(wrapped, cwd=cwd, env=env, stdout=self.out,
                                         stderr=self.err, stdin=subprocess.DEVNULL,
                                         start_new_session=True,
                                         preexec_fn=_stack_preexec(stack_kib))
        except OSError as exc:
            self.close()
            raise ToolError(f"cannot run {argv[0]!r}: {exc.strerror or exc}")
        self.end = None

    def close(self):
        for f in (self.out, self.err, self.rss_file):
            try:
                f.close()
            except OSError:
                pass

    def kill(self):
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)
        except OSError:
            self.proc.kill()

    def result(self, status, usage, timed_out, end=None) -> dict:
        wall = (end or time.perf_counter()) - self.start
        rc = os.waitstatus_to_exitcode(status)
        self.out.seek(0)
        self.err.seek(0)
        rss_text = self.rss_file.read()
        # `time` writes "Command exited with non-zero status N" before %M when
        # the command fails; the last line is always the %M value.
        lines = [l for l in rss_text.splitlines() if l.strip()]
        try:
            rss = int(lines[-1])
        except (IndexError, ValueError):
            rss = None
        stderr = self.err.read().decode(errors="replace")
        if rc in (126, 127) and "cannot run" in stderr.splitlines()[:1].__str__():
            raise ToolError(f"cannot run {self.argv[0]!r}: {stderr.strip().splitlines()[0].split(': ', 2)[-1]}")
        res = {"rc": rc, "stdout": self.out.read().decode(errors="replace"),
               "stderr": stderr, "wall_ms": wall * 1000.0,
               "cpu_ms": (usage.ru_utime + usage.ru_stime) * 1000.0,
               "rss_kib": rss, "timed_out": timed_out}
        self.close()
        return res


def run_measured(argv: list[str], *, cwd: Path, env: dict[str, str],
                 timeout: float = 600.0, stack_kib: int | None = None) -> dict:
    """Run argv once; return rc, stdout, stderr, wall/CPU ms and peak RSS (KiB).

    Peak RSS is read from a /usr/bin/time wrapper (-f %M), NOT from this
    process's wait4() rusage: on Linux ru_maxrss of a forked+exec'd child
    includes the parent's pre-exec high-water mark, so a large Python runner
    would inflate every child. `time` is tiny, forks the real command and
    reports that command's tree maximum (largest single process). Wall is the
    runner's monotonic clock around the whole wrapped command (adds ~1 ms of
    `time` startup); CPU is user+system from wait4 (unaffected by mm history).

    `stack_kib` sets RLIMIT_STACK (soft, clamped to the hard limit) in the child
    before exec, so a deep-recursion checker does not depend on the machine's
    default 8 MiB. On timeout the whole process group is killed (the command
    runs in its own session) so no compiler is left burning CPU.
    """
    launch = _Launch(argv, cwd, env, stack_kib)
    deadline = launch.start + timeout
    timed_out = False
    while True:
        pid, status, usage = os.wait4(launch.proc.pid, os.WNOHANG)
        if pid:
            break
        if time.perf_counter() > deadline:
            launch.kill()
            timed_out = True
            pid, status, usage = os.wait4(launch.proc.pid, 0)
            break
        time.sleep(0.0005)
    return launch.result(status, usage, timed_out)


def run_measured_group(jobs: list[tuple[list[str], Path]], *, env: dict[str, str],
                       timeout: float = 600.0) -> dict:
    """Launch every job at (nearly) the same time and wait for all of them.

    jobs: (argv, cwd) pairs. Every process is wrapped exactly like run_measured,
    so per-process peak RSS comes from /usr/bin/time and CPU from wait4. Returns
    the per-job results (each with its own wall, measured from the group start
    to the moment the runner reaped it) and the group totals: group wall (first
    launch to last reap), total CPU (sum), summed peak RSS (sum of per-process
    peaks, an upper bound on the simultaneous footprint). Timeouts kill every
    unfinished process group.
    """
    launches = []
    try:
        for argv, cwd in jobs:
            launches.append(_Launch(argv, cwd, env, None))
    except Exception:
        for l in launches:
            l.kill()
            os.wait4(l.proc.pid, 0)
            l.close()
        raise
    start = min(l.start for l in launches)
    deadline = start + timeout
    pending = dict(enumerate(launches))
    results: dict[int, dict] = {}
    timed_out = False
    while pending:
        for i, l in list(pending.items()):
            pid, status, usage = os.wait4(l.proc.pid, os.WNOHANG)
            if pid:
                results[i] = l.result(status, usage, False, time.perf_counter())
                del pending[i]
        if pending and time.perf_counter() > deadline:
            timed_out = True
            for i, l in list(pending.items()):
                l.kill()
                pid, status, usage = os.wait4(l.proc.pid, 0)
                results[i] = l.result(status, usage, True, time.perf_counter())
                del pending[i]
        elif pending:
            time.sleep(0.0005)
    end = time.perf_counter()
    ordered = [results[i] for i in range(len(launches))]
    return {"results": ordered, "timed_out": timed_out,
            "group_wall_ms": (end - start) * 1000.0,
            "total_cpu_ms": sum(r["cpu_ms"] for r in ordered),
            "sum_rss_kib": sum((r["rss_kib"] or 0) for r in ordered),
            "max_rss_kib": max(((r["rss_kib"] or 0) for r in ordered), default=0)}


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
    """Reduce a command result to {exit_code, ok, diagnostics, document}.

    `document` is the WHOLE parsed JSON output with VOLATILE_KEYS dropped and
    paths normalized, so any extra or changed field is a gate failure.
    `diagnostics` is a convenience view of the same data for reports.
    Understands moss-agent-1 (`moss check --json`) and margo-1 (`margo --json`);
    a margo failure embeds the moss JSON in its `error` string, which is parsed
    so the compiler's diagnostic is compared structurally. Non-JSON output is
    compared as scrubbed raw text so a mismatch is visible, never hidden.
    """
    text = result["stdout"]
    try:
        doc = json.loads(text)
    except ValueError:
        return {"exit_code": result["rc"], "ok": None, "diagnostics": None,
                "timed_out": bool(result.get("timed_out")),
                "raw": _scrub(text, replacements)}
    ok = doc.get("ok")
    if ok:
        diagnostics = (doc.get("result") or {}).get("diagnostics", [])
    elif doc.get("schema_version") == "margo-1" and isinstance(doc.get("error"), str):
        inner = _embedded_error(doc["error"])
        diagnostics = [inner.get("error", inner)] if inner is not None else [doc["error"]]
    else:
        diagnostics = [doc.get("error", doc)]
    document = dict(doc)
    if not ok and doc.get("schema_version") == "margo-1" and isinstance(doc.get("error"), str):
        prefix_end = doc["error"].find("{")
        inner = _embedded_error(doc["error"])
        if inner is not None:
            document["error"] = {"prefix": doc["error"][:prefix_end], "embedded": inner}
    return {"exit_code": result["rc"], "ok": bool(ok),
            "timed_out": bool(result.get("timed_out")),
            "diagnostics": _scrub(diagnostics, replacements),
            "document": _scrub(document, replacements)}


def path_replacements(work: Path, tc: "Toolchain") -> list[tuple[str, str]]:
    """Normalize staged-dir and tool paths so reference/candidate documents
    (including margo's embedded error strings) compare equal."""
    return [(str(work), "<FIXTURE>"), (str(tc.moss), "<MOSS>"), (str(tc.margo), "<MARGO>")]


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
        return {"checker_runs": 0, **{name: None for name, _ in WORK_COUNTERS},
                "internal_functions": None, "internal_complete": None}
    summary = {"checker_runs": len(records)}
    for name, source in WORK_COUNTERS:
        summary[name] = sum(int(r.get(source, 0)) for r in records)
    # Checked-summary facts (M2): internal functions of the package's own source
    # and how many carry a `complete` local summary.
    summary["internal_functions"] = sum(int(r.get("ect_m2_internal_functions", 0)) for r in records)
    summary["internal_complete"] = sum(int(r.get("ect_m2_internal_complete", 0)) for r in records)
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


# --------------------------------------------------------------------------
# Frozen patch specs (edit scenarios)
# --------------------------------------------------------------------------

def text_sha256(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def canonical_json(obj) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


def apply_patch(root: Path, patch: dict, direction: str = "forward") -> None:
    """Apply a frozen patch (`edits`: [{file, old, new}] plus `before_sha256` /
    `after_sha256` maps of file -> SHA-256 of the whole file) under `root`.

    forward: replace `old` by `new`; backward (revert): replace `new` by `old`.
    Every replacement must match EXACTLY once, and the file hash before and
    after must equal the frozen value; otherwise ToolError (never a silent
    no-op, never an ad-hoc edit)."""
    if direction not in ("forward", "backward"):
        raise ToolError(f"bad patch direction {direction!r}")
    pre, post = (("before_sha256", "after_sha256") if direction == "forward"
                 else ("after_sha256", "before_sha256"))
    by_file: dict[str, list[dict]] = {}
    for edit in patch["edits"]:
        by_file.setdefault(edit["file"], []).append(edit)
    for rel, edits in by_file.items():
        path = Path(root) / rel
        if not path.is_file():
            raise ToolError(f"patch {patch['id']}: {rel} is missing under {root}")
        text = path.read_text()
        if text_sha256(text) != patch[pre][rel]:
            raise ToolError(f"patch {patch['id']} ({direction}): {rel} does not have the "
                            "frozen pre-image hash")
        for edit in edits:
            old, new = (edit["old"], edit["new"]) if direction == "forward" \
                else (edit["new"], edit["old"])
            if text.count(old) != 1:
                raise ToolError(f"patch {patch['id']} ({direction}): expected exactly one "
                                f"match in {rel}, found {text.count(old)}")
            text = text.replace(old, new)
        if text_sha256(text) != patch[post][rel]:
            raise ToolError(f"patch {patch['id']} ({direction}): {rel} does not match the "
                            "frozen post-image hash")
        path.write_text(text)


def make_patch(patch_id: str, fixture_dir: Path, edits: list[dict]) -> dict:
    """Generator side: build a frozen patch spec from the freshly generated
    fixture, computing the pre/post file hashes by applying the edits."""
    files = sorted({e["file"] for e in edits})
    before, after = {}, {}
    for rel in files:
        text = (Path(fixture_dir) / rel).read_text()
        before[rel] = text_sha256(text)
        for e in [e for e in edits if e["file"] == rel]:
            if text.count(e["old"]) != 1:
                raise ToolError(f"patch {patch_id}: old text must occur exactly once in {rel}")
            text = text.replace(e["old"], e["new"])
        after[rel] = text_sha256(text)
    return {"id": patch_id, "edits": edits, "before_sha256": before, "after_sha256": after}
