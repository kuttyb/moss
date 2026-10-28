#!/usr/bin/env python3
"""One-way fail-fast corpus gates for Phase 26.1 C1.

G-a checks every tracked source in a package accepted by Margo.
G-b checks the first package diagnostic against files that trace its identity.
"""
from __future__ import annotations

from collections import defaultdict
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
SPEC = importlib.util.spec_from_loader(
    "margo_for_c1_scope_gates",
    importlib.machinery.SourceFileLoader("margo_for_c1_scope_gates", str(MARGO)))
assert SPEC and SPEC.loader
MARGO_API = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MARGO_API)

# These first-package failures were individually reproduced on clean main at
# d109b8f with `margo build`. They predate the fail-fast C1 gates and are
# catalogued with reproducer commands in docs/phase26/C1_RESULTS.md.
KNOWN_BASELINE_PACKAGE_ERRORS = {
    "examples/swarm/Surface/Shapes": "unresolved callable argument",
    "examples/swarm/modules/algo_chain/repro/trait_export/single":
        "MODULE_IDENTITY_UNRESOLVED",
    "examples/swarm/modules/algo_chain/repro/trait_export/split":
        "unresolved state layout",
    "examples/swarm/modules/data_pipeline/repro/trait_across/split":
        "unresolved state layout",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/single":
        "empty compiler payload",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/split":
        "empty compiler payload",
    "examples/swarm/modules/domain_services/repro/natural_system/app":
        "unresolved state layout",
    "examples/swarm/modules/domain_services/repro/natural_system/common":
        "unresolved state layout",
    "examples/swarm/modules/domain_services/repro/natural_system/services":
        "unresolved state layout",
    "examples/swarm/modules/domain_services/repro/trait_param/split":
        "unresolved state layout",
    "examples/swarm/modules/lib_and_app/repro/generic_pipeline_method_placeholder/single":
        "empty compiler payload",
    "examples/swarm/modules/lib_and_app/repro/trait_param_export/split":
        "unresolved state layout",
}


def invoke(args: list[object], cwd: Path, env: dict[str, str],
           timeout: int = 180) -> tuple[subprocess.CompletedProcess[str], dict | None]:
    completed = subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                               text=True, capture_output=True, timeout=timeout)
    try:
        return completed, json.loads(completed.stdout)
    except json.JSONDecodeError:
        return completed, None


def tracked_sources() -> list[Path]:
    result = subprocess.check_output(
        ["git", "ls-files", "*.moss"], cwd=ROOT, text=True)
    return [ROOT / row for row in result.splitlines()]


def package_roots(sources: list[Path]) -> tuple[dict[Path, list[Path]], list[Path]]:
    grouped: dict[Path, list[Path]] = defaultdict(list)
    standalone = []
    for source in sources:
        # Match nearest_moss_project_root: walk outward from the source's
        # directory and stop at the first directory containing either spelling.
        # load_project_manifest prefers Moss.toml if both are present there.
        current = source.parent.resolve()
        package_root = None
        while current == ROOT or ROOT in current.parents:
            if ((current / "Moss.toml").is_file() or
                    (current / "moss.toml").is_file()):
                package_root = current
                break
            if current == ROOT:
                break
            current = current.parent
        if package_root:
            grouped[package_root].append(source)
        else:
            standalone.append(source)
    return grouped, standalone


def check_file(path: Path, cwd: Path, env: dict[str, str], trace: bool):
    run_env = dict(env)
    if trace:
        run_env["MOSS_PHASE26_C1_SCOPE_TRACE"] = "1"
    completed, payload = invoke([COMPILER, "check", path, "--json"], cwd,
                               run_env)
    identities = {
        row.split("|", 3)[3]
        for row in completed.stderr.splitlines()
        if row.startswith("MOSS_C1_SCOPE_ID|") and len(row.split("|", 3)) == 4
    }
    trace_complete = any(
        row.startswith("MOSS_C1_SCOPE_TRACE_COMPLETE|")
        for row in completed.stderr.splitlines())
    return completed, payload, identities, trace_complete


def package_diagnostic(payload: dict) -> dict | None:
    error = payload.get("error")
    if isinstance(error, dict):
        return error
    if not isinstance(error, str):
        return None
    decoder = json.JSONDecoder()
    for offset, character in enumerate(error):
        if character != "{":
            continue
        try:
            nested, _ = decoder.raw_decode(error[offset:])
        except json.JSONDecodeError:
            continue
        if nested.get("schema_version") == "moss-agent-1":
            return nested.get("error")
    if "internal synchronization invariant:" in error:
        return {"code": "MOSS_INTERNAL_OR_IO_ERROR", "message": error,
                "semantic_identity": None}
    return None


def main() -> int:
    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env["MARGO_HOME"] = str(ROOT / "tmp/phase26-1-c1-scope-gates/margo-home")
    os.environ["MARGO_HOME"] = env["MARGO_HOME"]
    for key in ("MOSS_MODULE_PATH", "MOSS_FAST_DEBUG_SOURCE_ROOTS",
                "MOSS_SOURCE_ROOTS"):
        env.pop(key, None)

    sources = tracked_sources()
    raw_packages, standalone = package_roots(sources)
    packages: dict[Path, list[Path]] = {}
    for root, files in raw_packages.items():
        try:
            root_package, _ = MARGO_API.resolve_graph(root)
        except Exception:
            # Keep it in the package list so the gate reports the unresolved
            # graph instead of silently treating malformed package data as a
            # standalone source.
            packages[root] = files
            continue
        build = root_package.data.get("build", {})
        source_dir = Path(build.get("source", "src"))
        target_files = []
        for source in files:
            relative = source.relative_to(root)
            if (relative.parts[0] in {"tests", "benches"} or
                    relative.parts[:len(source_dir.parts)] == source_dir.parts or
                    len(relative.parts) == 1):
                target_files.append(source)
            else:
                standalone.append(source)
        if target_files:
            packages[root] = target_files
    if len(sys.argv) > 2:
        selected = Path(sys.argv[2]).resolve()
        packages = {root: files for root, files in packages.items()
                    if root.resolve() == selected}
        standalone = []
    failures: list[str] = []
    checked_files = 0
    accepted_packages = rejected_packages = 0
    ga_required = ga_passed = gb_required = gb_passed = 0
    gb_no_hit_packages = 0
    gb_terminal_packages = 0
    baseline_known_packages = 0

    for root, files in sorted(packages.items(), key=lambda item: item[0].as_posix()):
        print(f"package {root.relative_to(ROOT)}", flush=True)
        try:
            root_package, ordered = MARGO_API.resolve_graph(root)
            package_env = MARGO_API.package_environment(
                root_package, ordered, release=False, check_only=True)
        except Exception as error:
            failures.append(f"{root.relative_to(ROOT)}: cannot resolve graph: {error}")
            continue
        package_env["MOSS"] = str(COMPILER)
        package_env["MARGO_HOME"] = env["MARGO_HOME"]
        # The graph-aware environment makes Margo's own check and each
        # source-file check consume exactly the same dependency roots.
        completed, payload = invoke(
            [MARGO, "build", "--check", "--json"], root, package_env,
            timeout=600)
        if payload is None:
            relative = root.relative_to(ROOT).as_posix()
            known = KNOWN_BASELINE_PACKAGE_ERRORS.get(relative)
            if known == "empty compiler payload" and completed.returncode != 0 and \
                    not completed.stdout and not completed.stderr:
                baseline_known_packages += 1
                print(f"  known baseline issue: {relative}: SIGSEGV (main d109b8f)",
                      flush=True)
                continue
            failures.append(f"{root.relative_to(ROOT)}: Margo returned no JSON: "
                            f"{completed.returncode} {completed.stdout[-300:]!r} "
                            f"{completed.stderr[-300:]!r}")
            continue
        package_ok = completed.returncode == 0 and payload.get("ok") is True
        if package_ok:
            accepted_packages += 1
            for source in sorted(files):
                ga_required += 1
                checked_files += 1
                file_result, file_payload, _, _ = check_file(
                    source, root, package_env, False)
                if file_result.returncode == 0 and file_payload and \
                        file_payload.get("ok") is True:
                    ga_passed += 1
                else:
                    failures.append(
                        f"G-a {source.relative_to(ROOT)}: package accepted, "
                        f"file rejected ({file_payload or file_result.stderr[-400:]})")
            continue

        rejected_packages += 1
        error = package_diagnostic(payload)
        if not isinstance(error, dict):
            relative = root.relative_to(ROOT).as_posix()
            raw_error = payload.get("error") if isinstance(payload, dict) else None
            if (KNOWN_BASELINE_PACKAGE_ERRORS.get(relative) ==
                    "empty compiler payload" and isinstance(raw_error, str) and
                    raw_error.endswith("): ")):
                primary = root / "src/main.moss"
                if primary.is_file():
                    direct, _ = invoke([COMPILER, "check", primary, "--json"],
                                       root, package_env)
                    if direct.returncode == -11 and not direct.stdout and not direct.stderr:
                        baseline_known_packages += 1
                        print(f"  known baseline issue: {relative}: SIGSEGV "
                              f"(main d109b8f; candidate direct repro)", flush=True)
                        continue
            failures.append(f"{root.relative_to(ROOT)}: malformed Margo error: {payload}")
            continue
        identity = error.get("semantic_identity")
        code = error.get("code")
        relative = root.relative_to(ROOT).as_posix()
        known = KNOWN_BASELINE_PACKAGE_ERRORS.get(relative)
        if known and known != "empty compiler payload":
            message = error.get("message", "")
            matches_known = (known == code or known in message)
            if matches_known:
                baseline_known_packages += 1
                print(f"  known baseline issue: {relative}: {code} {message}",
                      flush=True)
                continue
        if (code == "MOSS_COMPILE_ERROR" and
                "missing or stale" in error.get("message", "")):
            # R2's pre-semantic interface failure has no callable identity and
            # cannot participate in the reachable-body implication.
            gb_terminal_packages += 1
            continue
        if not code:
            failures.append(f"{root.relative_to(ROOT)}: first Margo error lacks "
                            f"semantic identity/code: {error}")
            continue
        if not identity:
            failures.append(f"{root.relative_to(ROOT)}: first Margo error lacks "
                            f"semantic identity: {error}")
            continue
        reached = 0
        for source in sorted(files):
            checked_files += 1
            file_result, file_payload, identities, trace_complete = check_file(
                source, root, package_env, True)
            if identity not in identities:
                continue
            if not trace_complete:
                failures.append(f"G-b {source.relative_to(ROOT)} reaches "
                                f"{identity!r} without a completed trace")
                continue
            reached += 1
            gb_required += 1
            file_error = (file_payload or {}).get("error", {})
            if file_result.returncode != 0 and file_error.get("code") == code:
                gb_passed += 1
            else:
                failures.append(
                    f"G-b {source.relative_to(ROOT)} reaches {identity!r} but "
                    f"did not reject with {code}: {file_payload or file_result.stderr[-400:]}")
        if reached == 0:
            gb_no_hit_packages += 1
            print(f"  no file trace reaches first package error: {code} {identity}",
                  flush=True)

    for source in standalone:
        completed, payload, _, _ = check_file(source, ROOT, env, False)
        checked_files += 1
        if completed.returncode != 0:
            # Standalone legacy fixtures may deliberately reject; both one-way
            # package implications are vacuous because they have no Margo root.
            continue
        if not payload or payload.get("ok") is not True:
            failures.append(f"standalone {source.relative_to(ROOT)}: malformed result")

    print(f"tracked_sources={len(sources)} packages={len(packages)} "
          f"accepted_packages={accepted_packages} rejected_packages={rejected_packages} "
          f"files_checked={checked_files}")
    print(f"G-a={ga_passed}/{ga_required} G-b={gb_passed}/{gb_required} "
          f"G-b-no-hit-packages={gb_no_hit_packages} "
          f"G-b-terminal-packages={gb_terminal_packages} "
                  f"known-baseline-packages={baseline_known_packages} "
          f"failures={len(failures)}")
    for failure in failures:
        print("FAIL " + failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
