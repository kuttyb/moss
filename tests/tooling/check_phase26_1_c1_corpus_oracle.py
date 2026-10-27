#!/usr/bin/env python3
"""Compare file-scoped and package-scoped diagnostic sets for tracked Moss.

The oracle CLI is intentionally opt-in. This gate requires every query to
return a complete diagnostic enumeration; parser, dependency, and other
uncomparable failures fail the run instead of being treated as parity.
"""
from __future__ import annotations

from collections import Counter, defaultdict
import json
import importlib.util
import importlib.machinery
import os
from pathlib import Path
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
SCHEMA = "moss-c1-oracle-1"
ORDINARY_SCHEMA = "moss-agent-1"
MARGO_SPEC = importlib.util.spec_from_loader(
    "margo_for_c1_oracle", importlib.machinery.SourceFileLoader(
        "margo_for_c1_oracle", str(MARGO)))
assert MARGO_SPEC and MARGO_SPEC.loader
MARGO_API = importlib.util.module_from_spec(MARGO_SPEC)
MARGO_SPEC.loader.exec_module(MARGO_API)


def run(args: list[object], cwd: Path, env: dict[str, str]) -> dict:
    try:
        completed = subprocess.run(
            [str(arg) for arg in args], cwd=cwd, env=env, text=True,
            capture_output=True, timeout=180,
        )
    except subprocess.TimeoutExpired as error:
        return {"_fatal": f"timeout after {error.timeout}s for {args}"}
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        return {"_fatal":
                f"no oracle JSON ({completed.returncode}) for {args}: "
                f"stdout={completed.stdout[-1000:]!r}, "
                f"stderr={completed.stderr[-1000:]!r}"}
    if payload.get("schema_version") == ORDINARY_SCHEMA and not payload.get("ok"):
        error = payload.get("error", {})
        message = error.get("message", "")
        code = error.get("code", "")
        if (code in {"MALFORMED_TYPE_EXPRESSION",
                     "INVALID_BARE_IDENTIFIER_STATEMENT",
                     "MODULE_IDENTITY_UNRESOLVED"} or
                (code == "MOSS_COMPILE_ERROR" and
                 ("interface for " in message and
                  ("missing or stale" in message)) ) or
                (code == "UNKNOWN_SYMBOL_OR_TYPE" and
                 any(text in message for text in
                     ("unknown parameter type", "unknown return type",
                      "unknown result type")))):
            phase = ("module-loading" if code == "MOSS_COMPILE_ERROR"
                     else "declaration-parsing")
            return {"_terminal": {
                "code": code, "message": message,
                "source_file": error.get("source_file") or "",
                "line": error.get("line", 0), "phase": phase},
                "diagnostics": [], "_trace": set()}
        return {"_fatal": f"unexpected non-oracle error for {args}: {payload}"}
    if payload.get("schema_version") != SCHEMA:
        return {"_fatal": f"unexpected oracle schema for {args}: {payload}"}
    result = payload.get("result", {})
    terminal = result.get("incomplete_error")
    if result.get("complete") is not True:
        # A pre-body failure such as a missing imported interface is a
        # terminal diagnostic, not an enumerated body-diagnostic set. Keep it
        # separate and let the caller accept it only when file and package
        # modes match exactly and neither produced body diagnostics.
        if not (terminal and not result.get("diagnostics")):
            return {"_fatal": f"uncomparable oracle result for {args}: {payload}"}
    else:
        if payload.get("ok") is not True:
            return {"_fatal": f"oracle result not ok for {args}: {payload}"}
    result["_terminal"] = terminal
    result["_trace"] = {
        line.split("|", 3)[3]
        for line in completed.stderr.splitlines()
        if line.startswith("MOSS_C1_SCOPE_ID|") and
        len(line.split("|", 3)) == 4
    }
    return result


def diag_key(item: dict) -> tuple[str, str, int, str, str]:
    source = Path(item["source_file"])
    try:
        normalized = source.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        normalized = source.as_posix()
    return (item["code"], item["message"], item["line"],
            normalized, item["semantic_identity"])


def terminal_key(item: dict) -> tuple[str, str, int, str, str]:
    source = Path(item["source_file"])
    try:
        normalized = source.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        normalized = source.as_posix()
    phase = item.get("phase", "module-loading" if
                     item["code"] == "MOSS_COMPILE_ERROR" else
                     "declaration-parsing")
    return (phase, item["code"], item["message"], item["line"], normalized)


def main() -> int:
    package_filter = (Path(sys.argv[2]).resolve().relative_to(ROOT)
                      if len(sys.argv) > 2 else None)
    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env["MARGO_HOME"] = str(ROOT / "tmp/phase26-1-c1-oracle/margo-home")
    for key in ("MOSS_MODULE_PATH", "MOSS_FAST_DEBUG_SOURCE_ROOTS",
                "MOSS_SOURCE_ROOTS"):
        env.pop(key, None)

    tracked = [Path(line) for line in subprocess.check_output(
        ["git", "ls-files", "*.moss"], cwd=ROOT, text=True).splitlines()]
    manifests = [path for path in ROOT.rglob("Moss.toml")
                 if path.is_relative_to(ROOT) and
                 not ({".git", "tmp", "build"} &
                      set(path.relative_to(ROOT).parts))]
    manifests.sort(key=lambda path: len(path.parts), reverse=True)

    groups: dict[Path | None, list[Path]] = defaultdict(list)
    for path in tracked:
        absolute = ROOT / path
        root = next((manifest.parent.relative_to(ROOT) for manifest in manifests
                     if absolute.is_relative_to(manifest.parent)), None)
        groups[root].append(path)

    checked_packages = 0
    checked_files = 0
    checked_diagnostics = 0
    failures: list[str] = []
    standalone_files: list[Path] = list(groups.get(None, []))
    # Package targets enumerate the reference set. Build checks are run first
    # to seed current dependency interfaces; expected package rejections are
    # retained as diagnostic data, while oracle incompleteness remains fatal.
    for package_root, files in sorted(
            ((root, files) for root, files in groups.items() if root),
            key=lambda pair: pair[0].as_posix()):
        assert package_root is not None
        if package_filter is not None and package_root != package_filter:
            continue
        absolute_root = ROOT / package_root
        print(f"oracle package {package_root}", flush=True)
        with (absolute_root / "Moss.toml").open("rb") as stream:
            manifest = tomllib.load(stream)
        source_dir = Path(manifest.get("build", {}).get("source", "src"))
        app_files, test_files, bench_files = [], [], []
        for path in files:
            relative = path.relative_to(package_root)
            if relative.parts[0] == "tests":
                test_files.append(path)
            elif relative.parts[0] == "benches":
                bench_files.append(path)
            elif relative.parts[0] == source_dir.parts[0] or len(relative.parts) == 1:
                app_files.append(path)
            else:
                standalone_files.append(path)
        # Seed dependency .mossi files without making their ordinary verdict
        # part of this oracle. R2 still makes a missing/stale interface fatal.
        subprocess.run([str(MARGO), "build", "--check"], cwd=absolute_root,
                       env=env, text=True, capture_output=True, timeout=600)
        try:
            root_package, ordered = MARGO_API.resolve_graph(absolute_root)
        except MARGO_API.MargoError as error:
            failures.append(f"{package_root}: cannot resolve Margo graph: {error}")
            continue
        package_env = MARGO_API.package_environment(
            root_package, ordered, release=False, check_only=True)

        target_diagnostics: dict[str, Counter] = {}
        for target, target_files, command in (
            ("application", app_files,
             [COMPILER, "build", "--check", "--phase26-c1-oracle", "--json"]),
            ("tests", test_files,
             [COMPILER, "test", "--check", "--phase26-c1-oracle", "--json"]),
            ("benches", bench_files,
             [COMPILER, "bench", "--check", "--phase26-c1-oracle", "--json"]),
        ):
            if not target_files:
                continue
            print(f"  target {target}", flush=True)
            package = run(command, absolute_root, package_env)
            if "_fatal" in package:
                failures.append(f"{package_root} [{target}]: {package['_fatal']}")
                continue
            package_terminal = package.pop("_terminal")
            if package_terminal:
                package_key = terminal_key(package_terminal)
                matched_terminal = False
                for relative_file in target_files:
                    file_result = run(
                        [COMPILER, "check", ROOT / relative_file,
                         "--phase26-c1-oracle", "--json"], absolute_root,
                        package_env | {"MOSS_PHASE26_C1_SCOPE_TRACE": "1"})
                    if "_fatal" in file_result:
                        failures.append(
                            f"{relative_file} [{target}]: {file_result['_fatal']}")
                        continue
                    file_terminal = file_result.pop("_terminal")
                    file_body_diags = file_result["diagnostics"]
                    if file_terminal:
                        if terminal_key(file_terminal) != package_key:
                            failures.append(
                                f"{package_root} [{target}] {relative_file}: "
                                "package/file terminal diagnostics differ: "
                                f"{package_terminal} vs {file_terminal}")
                        elif file_body_diags:
                            failures.append(
                                f"{relative_file}: terminal file query also "
                                f"enumerated body diagnostics {file_body_diags}")
                        else:
                            matched_terminal = True
                            checked_diagnostics += 1
                    else:
                        if file_body_diags:
                            failures.append(
                                f"{relative_file}: file reached body checking "
                                "while package target was terminal: "
                                f"{file_body_diags}")
                    checked_files += 1
                if not matched_terminal:
                    failures.append(
                        f"{package_root} [{target}]: package terminal has no "
                        f"matching file check: {package_terminal}")
                else:
                    checked_packages += 1
                continue
            package_diags = [d for d in package["diagnostics"]
                             if not d.get("package_only", False)]
            target_diagnostics[target] = Counter(map(diag_key, package_diags))
            checked_diagnostics += len(package_diags)
            for relative_file in target_files:
                file_result = run(
                    [COMPILER, "check", ROOT / relative_file,
                     "--phase26-c1-oracle", "--json"], absolute_root,
                    package_env | {"MOSS_PHASE26_C1_SCOPE_TRACE": "1"})
                if "_fatal" in file_result:
                    failures.append(f"{relative_file}: {file_result['_fatal']}")
                    continue
                file_terminal = file_result.pop("_terminal")
                if file_terminal:
                    failures.append(
                        f"{relative_file}: file has terminal pre-body error "
                        f"absent from complete package target: {file_terminal}")
                    continue
                reachable = file_result.pop("_trace")
                file_diags = [d for d in file_result["diagnostics"]
                              if not d.get("package_only", False)]
                file_counts = Counter(map(diag_key, file_diags))
                package_counts = target_diagnostics[target]
                missing_from_package = file_counts - package_counts
                if missing_from_package:
                    failures.append(
                        f"{relative_file}: file diagnostic absent from "
                        f"package target: {missing_from_package}")
                reachable_package = Counter(
                    {key: count for key, count in package_counts.items()
                     if key[4] in reachable})
                missing_from_file = reachable_package - file_counts
                if missing_from_file:
                    failures.append(
                        f"{relative_file}: reachable package diagnostic "
                        f"absent from file check: {missing_from_file}")
                checked_files += 1
            checked_packages += 1

    # A source outside the package's declared targets is checked as its own
    # compilation unit. In that case the file query is the package oracle too;
    # there is no distinct package target from which to derive another set.
    for relative_file in ([] if package_filter else standalone_files):
        package_env = env
        package_root = next((manifest.parent.relative_to(ROOT)
                             for manifest in manifests
                             if (ROOT / relative_file).is_relative_to(
                                 manifest.parent)), None)
        if package_root is not None:
            try:
                root_package, ordered = MARGO_API.resolve_graph(
                    ROOT / package_root)
                package_env = MARGO_API.package_environment(
                    root_package, ordered, release=False, check_only=True)
            except MARGO_API.MargoError as error:
                failures.append(
                    f"{relative_file}: cannot resolve Margo graph: {error}")
                continue
        result = run([COMPILER, "check", ROOT / relative_file,
                      "--phase26-c1-oracle", "--json"], ROOT,
                     package_env | {"MOSS_PHASE26_C1_SCOPE_TRACE": "1"})
        if "_fatal" in result:
            failures.append(f"{relative_file}: {result['_fatal']}")
            continue
        terminal = result.pop("_terminal")
        if terminal:
            checked_files += 1
            checked_diagnostics += 1
            continue
        result.pop("_trace")
        checked_diagnostics += len(result["diagnostics"])
        checked_files += 1

    print("C1 corpus oracle " + ("passed" if not failures else "failed") + ": "
          f"{checked_files} files, {checked_packages} package targets, "
          f"{checked_diagnostics} package/terminal diagnostics, "
          f"{len(tracked)} tracked Moss sources, "
          f"{len(failures)} uncomparable rows")
    for failure in failures:
        print("UNCOMPARABLE: " + failure)
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
