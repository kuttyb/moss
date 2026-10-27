#!/usr/bin/env python3
"""M1 package graph and edit/revert work-counter baseline.

The fixtures are generated under tmp so each scenario starts from a clean,
isolated graph. Margo currently rebuilds every package on every build; the
report records that baseline and separately measures file-mode moss check.
"""
from __future__ import annotations

import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MARGO = ROOT / "margo"
MOSS = ROOT / "build/compiler/moss-opt"
TMP = ROOT / "tmp/phase26-1-m1"
COUNTER_RE = re.compile(r"^MOSS_PROFILE\|([^|]+)\|([0-9]+)$", re.M)
IDENTITY_RE = re.compile(r"^MOSS_PROFILE_ID\|source_function\|(.+)$", re.M)

# C1 partial-reach fixture: the edited file reaches only part of its package.
# Reachable function identities (`<file>:<function>`) are listed explicitly;
# the set, not the raw checker-entry count, is the pass/fail target.
PARTIAL_REACH_ENTRY = "m1_partial.moss"
PARTIAL_REACH_FILES = {
    PARTIAL_REACH_ENTRY: (
        "module m1_partial\n"
        "fn main():\n  echo reached_top(3)\n"
        "fn reached_top(x: Int) -> Int:\n  return reached_leaf(x) + 1\n"
        "fn reached_leaf(x: Int) -> Int:\n  return x\n"),
    "helpers.moss": (
        "module m1_partial\n"
        "fn unreached_a(x: Int) -> Int:\n  return unreached_b(x)\n"
        "fn unreached_b(x: Int) -> Int:\n  return unreached_c(x) + 1\n"
        "fn unreached_c(x: Int) -> Int:\n  return x\n"),
}
PARTIAL_REACH_EXPECTED = {
    "m1_partial.moss:main", "m1_partial.moss:m1_partial__reached_top",
    "m1_partial.moss:m1_partial__reached_leaf"}
PARTIAL_REACH_PACKAGE = PARTIAL_REACH_EXPECTED | {
    "helpers.moss:m1_partial__unreached_a",
    "helpers.moss:m1_partial__unreached_b",
    "helpers.moss:m1_partial__unreached_c"}


def package(name: str, deps: dict[str, str], module: str, body: str) -> dict:
    return {"name": name, "deps": deps, "module": module, "body": body}


# Two-package Fast Debug fixture. The first selected test reaches one library
# chain; the second test stays entirely in the app. Identities below are the
# X0 target for R11 and make dependency versus package-wide work measurable.
FAST_DEBUG_GRAPH = {
    "app": package("m1_fd_app", {"m1_fd_lib": "../library"}, "m1_fd_app_api",
                    "import m1_fd_lib\n"
                    "export fn reaches_library():\n  return m1_fd_lib.helper()\n"
                    "export fn reaches_explicit():\n  return m1_fd_lib.explicit_value()\n"
                    "export fn app_only() -> Int:\n  return 7\n"
                    "fn app_unused() -> Int:\n  return 8\n"),
    "library": package("m1_fd_lib", {}, "m1_fd_lib",
                       "export fn target_value():\n  return 1\n"
                       "export fn helper():\n  return target_value()\n"
                       "export fn explicit_value() -> Int:\n  return 2\n"
                       "export fn unrelated() -> Int:\n  return 9\n"),
}
FAST_DEBUG_TESTS = (
    "module m1_fd_tests\nimport m1_fd_app_api\n"
    'test "reaches library":\n  echo m1_fd_app_api.reaches_library()\n'
    'test "reaches explicit":\n  echo m1_fd_app_api.reaches_explicit()\n'
    'test "does not reach library":\n  echo m1_fd_app_api.app_only()\n'
)


def anchors() -> str:
    """Stable declarations used to model inferred and explicit interface edits."""
    return ("export fn inferred_anchor():\n  return 1\n"
            "export fn explicit_anchor() -> Int:\n  return 1\n")


def graphs() -> dict[str, dict[str, dict]]:
    lib = package("m1_library", {}, "m1_library", "export fn value(x: Int) -> Int:\n  return x\n")
    app = package("m1_app", {"m1_library": "../library"}, "m1_app",
                  "import m1_library\nfn main():\n  echo m1_library.value(7)\n" + anchors())

    core = package("m1_core", {}, "m1_core",
                   "export fn value(x: Int):\n  return x\n"
                   "export fn explicit_value(x: Int) -> Int:\n  return x\n"
                   "fn unused_a(x: Int) -> Int:\n  return x + 1\n"
                   "fn unused_b(x: Int) -> Int:\n  return x + 2\n" + anchors())
    middle = package("m1_middle", {"m1_core": "../core"}, "m1_middle",
                     "import m1_core\nexport fn value(x: Int):\n  return m1_core.explicit_value(x)\n")
    service = package("m1_service", {"m1_middle": "../middle"}, "m1_service",
                      "import m1_middle\nexport fn value(x: Int):\n  return m1_middle.value(x)\n")
    chain_app = package("m1_app", {"m1_service": "../service"}, "m1_app",
                        "import m1_service\nfn main():\n  echo m1_service.value(7)\n" + anchors())

    hub = package("m1_hub", {}, "m1_hub", "export fn value() -> Int:\n  return 1\n" + anchors())
    consumers = {f"consumer_{i}": package(f"m1_consumer_{i}", {"m1_hub": "../hub"},
                 f"m1_consumer_{i}", "import m1_hub\nexport fn value():\n  return m1_hub.value()\n")
                 for i in range(8)}
    fan_deps = {f"m1_consumer_{i}": f"../consumer_{i}" for i in range(8)}
    fan_imports = "\n".join(f"import m1_consumer_{i}" for i in range(8))
    fan_calls = "\n".join(f"  echo m1_consumer_{i}.value()" for i in range(8))
    fan = package("m1_fan_app", fan_deps, "m1_fan_app",
                  f"{fan_imports}\nfn main():\n{fan_calls}\n")

    generic = package("m1_generic", {}, "m1_generic",
                      "export fn identity(value):\n  return value\n" + anchors()
                      + "export fn forwarded_explicit() -> Int:\n  return 1\n")
    int_consumer = package("m1_int_consumer", {"m1_generic": "../generic"}, "m1_int_consumer",
                           "import m1_generic\nexport fn value() -> Int:\n  return m1_generic.identity(17)\n"
                           "export fn forwarded():\n  return m1_generic.forwarded_explicit()\n")
    text_consumer = package("m1_text_consumer", {"m1_generic": "../generic"}, "m1_text_consumer",
                            'import m1_generic\nexport fn value() -> String:\n  return m1_generic.identity("moss")\n'
                            "export fn forwarded():\n  return m1_generic.forwarded_explicit()\n")
    generic_app = package("m1_generic_app", {
        "m1_int_consumer": "../int_consumer",
        "m1_text_consumer": "../text_consumer",
    }, "m1_generic_app", "import m1_int_consumer\nimport m1_text_consumer\nfn main():\n"
       "  echo m1_int_consumer.value()\n  echo m1_text_consumer.value()\n"
       "export fn forwarded():\n  return m1_int_consumer.forwarded()\n")

    return {
        "application_library": {"app": app, "library": lib},
        "dependency_chain": {"app": chain_app, "service": service, "middle": middle, "core": core},
        "fan_in_eight": {"app": fan, "hub": hub, **consumers},
        "generic_cross_package": {"app": generic_app, "int_consumer": int_consumer,
                                  "text_consumer": text_consumer, "generic": generic},
    }

def write_graph(root: Path, graph: dict[str, dict]) -> dict[str, Path]:
    roots: dict[str, Path] = {}
    for key, row in graph.items():
        pkg_root = root / key
        roots[key] = pkg_root
        src = pkg_root / "src"
        src.mkdir(parents=True, exist_ok=True)
        manifest = ["[project]", f'name = "{row["name"]}"', 'version = "0.1.0"', ""]
        if row["deps"]:
            manifest += ["[dependencies]"]
            manifest += [f'{dep} = {{ path = "{path}" }}' for dep, path in sorted(row["deps"].items())]
        (pkg_root / "Moss.toml").write_text("\n".join(manifest) + "\n")
        module_name = row["module"]
        (src / f"{module_name}.moss").write_text(f"module {module_name}\n{row['body']}")
    return roots


def make_wrapper(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("""#!/usr/bin/env python3
import hashlib, os, subprocess, sys
from pathlib import Path
real = os.environ["M1_REAL_MOSS"]
logdir = Path(os.environ["M1_PROFILE_DIR"])
logdir.mkdir(parents=True, exist_ok=True)
cwd = str(Path.cwd())
tag = hashlib.sha1(cwd.encode()).hexdigest()[:10]
result = subprocess.run([real, *sys.argv[1:]], stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, text=True)
(logdir / f"{Path(cwd).name}-{tag}.stderr").write_text("M1_CWD=" + cwd + "\\n" + result.stderr)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
raise SystemExit(result.returncode)
""")
    path.chmod(0o755)


def clear_logs(logdir: Path) -> None:
    logdir.mkdir(parents=True, exist_ok=True)
    for path in logdir.glob("*.stderr"):
        path.unlink()


def invoke(cmd: list[str], cwd: Path, env: dict[str, str],
           label: str) -> tuple[int, str, float]:
    started = time.perf_counter()
    result = subprocess.run(cmd, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    wall_ms = (time.perf_counter() - started) * 1000
    if result.returncode:
        (TMP / f"{label}.failed.log").write_text(result.stdout)
    return result.returncode, result.stdout, wall_ms


def summarize_logs(logdir: Path, package_roots: dict[str, Path]) -> dict:
    function_checks = inference_visits = 0
    identities: set[str] = set()
    checked_packages: set[str] = set()
    for path in sorted(logdir.glob("*.stderr")):
        log_text = path.read_text()
        if "M1_CWD=" not in log_text:
            continue
        checked_packages.add(log_text.split("M1_CWD=", 1)[1].splitlines()[0])
        identities.update(IDENTITY_RE.findall(log_text))
        for key, value in COUNTER_RE.findall(log_text):
            if key == "source_function_checks":
                function_checks += int(value)
            elif key == "inference_reinferred_functions":
                inference_visits += int(value)
    roots_by_path = {str(root.resolve()): root for root in package_roots.values()}
    modules = sum(len(list((root / "src").rglob("*.moss")))
                  for checked in checked_packages
                  if (root := roots_by_path.get(checked)) is not None)
    return {"packages_checked": len(checked_packages), "modules_checked": modules,
            "functions_checked": function_checks,
            "inference_function_visits": inference_visits,
            "function_identities": sorted(identities)}


def run_profiled_build(root: Path, package_roots: dict[str, Path], label: str,
                       check_mode: bool = False) -> dict:
    logs = TMP / "logs" / label
    clear_logs(logs)
    wrapper = TMP / "bin/moss-profile-wrapper"
    make_wrapper(wrapper)
    env = dict(os.environ)
    env.update({"MOSS": str(wrapper), "M1_REAL_MOSS": str(MOSS),
                "M1_PROFILE_DIR": str(logs), "MOSS_PROFILE_COMPILER": "1",
                # Keep Margo's state stable across clean/edit/revert for this
                # graph so incremental build behavior is observable.
                "MARGO_HOME": str(TMP / "margo-home" / str(abs(hash(str(root.resolve())))))})
    args = [str(MARGO), "build"] + (["--check"] if check_mode else []) + ["--json"]
    status, output, wall_ms = invoke(args, root, env, label)
    if status:
        raise RuntimeError(f"margo build failed for {label}:\n{output[-4000:]}")
    return {**summarize_logs(logs, package_roots), "wall_ms": round(wall_ms, 3)}


def run_profiled_check(root: Path, package_roots: dict[str, Path], target: Path,
                       label: str) -> dict:
    logs = TMP / "logs" / label
    clear_logs(logs)
    wrapper = TMP / "bin/moss-profile-wrapper"
    make_wrapper(wrapper)
    env = dict(os.environ)
    env.update({"MOSS": str(wrapper), "M1_REAL_MOSS": str(MOSS),
                "M1_PROFILE_DIR": str(logs), "MOSS_PROFILE_COMPILER": "1"})
    target_root = target.parent.parent
    module_paths = [str(path / "build/debug") for path in package_roots.values()
                    if path != target_root]
    env["MOSS_MODULE_PATH"] = os.pathsep.join(module_paths)
    status, output, wall_ms = invoke([str(wrapper), "check", str(target)],
                                     target_root, env, label)
    if status:
        raise RuntimeError(f"moss check failed for {label}:\n{output[-4000:]}")
    counts = summarize_logs(logs, {"target": target_root})
    if counts["packages_checked"] != 1:
        raise RuntimeError(f"moss check should check only the edited package, got {counts['packages_checked']}")
    return {**counts, "wall_ms": round(wall_ms, 3)}


def run_partial_reach(run_id: str, rows: list[dict], enforce: bool,
                      check_mode: bool) -> None:
    """C1 target: file check reaches 3 of 6 functions; package mode all 6."""
    root = TMP / "graphs" / run_id / "partial_reach"
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "Moss.toml").write_text(
        '[project]\nname = "m1_partial"\nversion = "0.1.0"\n')
    for name, text in PARTIAL_REACH_FILES.items():
        (root / "src" / name).write_text(text)
    roots = {"app": root}
    entry = root / "src" / PARTIAL_REACH_ENTRY
    package = run_profiled_build(root, roots, "partial_reach-check",
                                 check_mode=check_mode)
    checked = run_profiled_check(root, roots, entry, "partial_reach-moss-check")
    for command, counts, expected in (
            ("margo build --check", package, PARTIAL_REACH_PACKAGE),
            ("moss check <edited file>", checked, PARTIAL_REACH_EXPECTED)):
        identities = set(counts["function_identities"])
        met = identities == expected
        rows.append({"fixture": "partial_reach", "position": "entry",
                     "scenario": "reach", "command": command,
                     **{k: v for k, v in counts.items()
                        if k != "function_identities"},
                     "expected_packages": 1,
                     "target_met": met and counts["packages_checked"] == 1,
                     "function_identities": " ".join(sorted(identities)),
                     "expected_functions": len(expected)})
        if not met and enforce:
            raise RuntimeError(
                f"partial_reach {command}: reachable functions "
                f"{sorted(identities)}; expected {sorted(expected)}")


def package_artifact_hashes(package_roots: dict[str, Path]) -> dict[str, str]:
    """Hash `.mossi` interfaces and Margo's existing semantic-cache files."""
    import hashlib
    result = {}
    for key, root in package_roots.items():
        paths = sorted({*root.rglob("*.mossi"),
                        *root.rglob(".moss/semantic-cache-v1/*.snapshot")})
        digest = hashlib.sha256()
        for path in paths:
            digest.update(path.relative_to(root).as_posix().encode())
            digest.update(path.read_bytes())
        result[key] = digest.hexdigest() if paths else ""
    return result


def run_profiled_fast_debug(package_roots: dict[str, Path], test_file: Path,
                            selected_test: str, label: str) -> dict:
    logs = TMP / "logs" / label
    clear_logs(logs)
    wrapper = TMP / "bin/moss-profile-wrapper"
    make_wrapper(wrapper)
    env = dict(os.environ)
    env.update({"M1_REAL_MOSS": str(MOSS), "M1_PROFILE_DIR": str(logs),
                "MOSS_PROFILE_COMPILER": "1",
                "MOSS_FAST_DEBUG_SOURCE_ROOTS": os.pathsep.join(
                    str(root) for key, root in package_roots.items()
                    if key != "app")})
    status, output, wall_ms = invoke(
        [str(wrapper), "test", "--interp", str(test_file), selected_test],
        package_roots["app"], env, label)
    if status:
        raise RuntimeError(f"Fast Debug failed for {label}:\n{output[-4000:]}")
    passed_tests = re.findall(r"^PASS (.+)$", output, re.M)
    if passed_tests != [selected_test]:
        raise RuntimeError(
            f"Fast Debug filter {selected_test!r} selected {passed_tests!r}; expected exactly one test")
    counts = summarize_logs(logs, package_roots)
    identities = set(counts["function_identities"])
    dep_roots = {key: root for key, root in package_roots.items() if key != "app"}
    dep_identity_sets = {
        key: {identity for identity in identities
              if identity.split(":", 1)[0] in {
                  path.name for path in root.rglob("*.moss")}}
        for key, root in dep_roots.items()}
    full_dep_identity_sets = {
        key: {identity for identity in (
            "m1_fd_lib.moss:m1_fd_lib__target_value",
            "m1_fd_lib.moss:m1_fd_lib__helper",
            "m1_fd_lib.moss:m1_fd_lib__explicit_value",
            "m1_fd_lib.moss:m1_fd_lib__unrelated")}
        for key in dep_roots}
    counts.update({
        "dependency_packages_with_functions": sum(bool(v) for v in dep_identity_sets.values()),
        "dependency_packages_fully_checked": sum(
            bool(full_dep_identity_sets[key]) and
            full_dep_identity_sets[key].issubset(dep_identity_sets[key])
            for key in dep_roots),
        "dependency_functions_checked": sum(map(len, dep_identity_sets.values())),
        "wall_ms": round(wall_ms, 3),
    })
    return counts


def run_fast_debug_scenarios(run_id: str, rows: list[dict], enforce: bool) -> None:
    fixture = "fast_debug_two_package"
    root = TMP / "graphs" / run_id / fixture
    roots = write_graph(root, FAST_DEBUG_GRAPH)
    tests = roots["app"] / "tests"
    tests.mkdir(parents=True, exist_ok=True)
    test_file = tests / "fast_debug.moss"
    test_file.write_text(FAST_DEBUG_TESTS)
    lib = roots["library"] / "src" / "m1_fd_lib.moss"
    app = roots["app"] / "src" / "m1_fd_app_api.moss"
    original_lib, original_app = lib.read_text(), app.read_text()

    def record(scenario: str, selected: str, expected: set[str],
               before: dict[str, str], expected_changed: int) -> None:
        observed = run_profiled_fast_debug(roots, test_file, selected,
                                           f"{fixture}-{scenario}-{selected}")
        identities = set(observed["function_identities"])
        after = package_artifact_hashes(roots)
        changed_packages = [key for key in roots
                            if before.get(key) != after.get(key)]
        exact = identities == expected
        row = {"fixture": fixture, "position": scenario, "scenario": selected,
               "command": "moss test --interp <file> <filter>", **{
                   key: value for key, value in observed.items()
                   if key != "function_identities"},
               "expected_dependency_packages_fully_checked": 0,
               "target_met": exact and not observed["dependency_packages_fully_checked"]
                   and len(changed_packages) == expected_changed,
               "function_identities": " ".join(sorted(identities)),
               "expected_functions": len(expected),
               "package_artifacts_changed": len(changed_packages),
               "package_artifact_changed_packages": " ".join(changed_packages),
               "expected_package_artifacts_changed": expected_changed}
        rows.append(row)
        if enforce and not row["target_met"]:
            raise RuntimeError(
                f"{fixture}/{scenario}/{selected}: functions={sorted(identities)}, "
                f"expected={sorted(expected)}, fully_checked_dependencies="
                f"{observed['dependency_packages_fully_checked']}, "
                f"artifact_changes={changed_packages}, expected changes={expected_changed}")

    # Establish the initial checked package artifacts. In the target design,
    # these include the package snapshot paired with each .mossi interface.
    run_profiled_build(roots["app"], roots,
                       f"{fixture}-initial", check_mode=True)
    base_hashes = package_artifact_hashes(roots)
    if set(base_hashes.values()) == {""}:
        raise RuntimeError("Fast Debug fixture did not produce package artifacts")
    # The exact identity sets are explicit by file and semantic function.
    reach = {
        "m1_fd_app_api.moss:m1_fd_app_api__reaches_library",
        "m1_fd_lib.moss:m1_fd_lib__helper",
        "m1_fd_lib.moss:m1_fd_lib__target_value",
    }
    reach_explicit = {
        "m1_fd_app_api.moss:m1_fd_app_api__reaches_explicit",
        "m1_fd_lib.moss:m1_fd_lib__explicit_value",
    }
    local = {
        "m1_fd_app_api.moss:m1_fd_app_api__app_only",
    }

    # Fast Debug selected-entry reach rows before and after a dependency edit.
    record("baseline_reach", "reaches library", reach, base_hashes, 0)
    record("baseline_local", "does not reach library", local, base_hashes, 0)

    for scenario in ("E0", "E1", "E2", "E3"):
        lib.write_text(original_lib)
        if scenario == "E0":
            os.utime(lib, None)
            expected_checks = 0
            expected_artifact_packages = []
        elif scenario == "E1":
            lib.write_text(original_lib.replace(
                "export fn target_value():\n  return 1",
                "export fn target_value():\n  return 1 + 0", 1))
            expected_checks = 1
            expected_artifact_packages = ["library"]
        elif scenario == "E2":
            lib.write_text(original_lib.replace(
                "export fn target_value():\n  return 1",
                'export fn target_value():\n  return "changed"', 1))
            expected_checks = 2  # provider and app are directly dependent
            expected_artifact_packages = ["app", "library"]
        else:
            lib.write_text(original_lib.replace(
                "export fn explicit_value() -> Int:\n  return 2",
                "export fn explicit_value() -> Float:\n  return 2.0", 1))
            expected_checks = 2
            expected_artifact_packages = ["app", "library"]
        if lib.read_text() == original_lib and scenario != "E0":
            raise RuntimeError(f"{scenario} did not mutate the Fast Debug provider")
        before = package_artifact_hashes(roots)
        changed = run_profiled_build(roots["app"], roots,
                                     f"{fixture}-{scenario}-check", check_mode=True)
        after_build = package_artifact_hashes(roots)
        changed_artifacts = [key for key in roots
                             if before.get(key) != after_build.get(key)]
        # Existing G1 counters establish package propagation. Until X1 exists,
        # artifact hashes below are only a proxy for output regeneration.
        rows.append({"fixture": fixture, "position": "dependency", "scenario": scenario,
                     "command": "margo build --check (Fast Debug fixture)", **changed,
                     "expected_packages": expected_checks,
                     "target_met": changed["packages_checked"] == expected_checks
                         and changed_artifacts == expected_artifact_packages,
                     "package_artifacts_changed": len(changed_artifacts),
                     "package_artifact_changed_packages": " ".join(changed_artifacts),
                     "expected_artifact_changed_packages": " ".join(expected_artifact_packages),
                     "expected_package_artifacts_changed": expected_checks})
        # The edit's selected test reaches the edited provider; the other
        # selected test must remain independent of every library body.
        after = after_build
        selected, expected = (("reaches explicit", reach_explicit)
                              if scenario == "E3" else ("reaches library", reach))
        record(scenario + "_reach", selected, expected, after, 0)
        record(scenario + "_local", "does not reach library", local, after, 0)
        lib.write_text(original_lib)
        run_profiled_build(roots["app"], roots,
                           f"{fixture}-{scenario}-restore", check_mode=True)
        base_hashes = package_artifact_hashes(roots)

    # App-only source edit must not regenerate a dependency artifact.
    app.write_text(original_app.replace(
        "fn app_only() -> Int:\n  return 7",
        "fn app_only() -> Int:\n  return 7 + 0", 1))
    before = package_artifact_hashes(roots)
    app_build = run_profiled_build(roots["app"], roots,
                                   f"{fixture}-app-only-check", check_mode=True)
    after = package_artifact_hashes(roots)
    changed_app_artifacts = [key for key in roots
                             if before.get(key) != after.get(key)]
    rows.append({"fixture": fixture, "position": "app", "scenario": "app-only",
                 "command": "margo build --check (Fast Debug fixture)", **app_build,
                 "expected_packages": 1,
                 "target_met": app_build["packages_checked"] == 1
                     and changed_app_artifacts == ["app"],
                 "package_artifacts_changed": len(changed_app_artifacts),
                 "package_artifact_changed_packages": " ".join(changed_app_artifacts),
                 "expected_artifact_changed_packages": "app",
                 "expected_package_artifacts_changed": 1})
    record("app_only", "does not reach library", local, after, 0)
    if enforce and any(before[key] != after[key] for key in roots if key != "app"):
        raise RuntimeError("app-only Fast Debug edit changed a dependency artifact")
    app.write_text(original_app)


def mutate_scenario(source: Path, scenario: str, position: str) -> None:
    text = source.read_text()
    if scenario == "E0":
        os.utime(source, None)
    elif scenario == "E1":
        text = text.replace("export fn inferred_anchor():\n  return 1",
                            "export fn inferred_anchor():\n  return 1 + 0", 1)
    elif scenario == "E2":
        text = text.replace("export fn inferred_anchor():\n  return 1",
                            'export fn inferred_anchor():\n  return "changed"', 1)
    elif scenario == "E3":
        if position == "app":
            text = text.replace("export fn explicit_anchor() -> Int:\n  return 1",
                                "export fn explicit_anchor() -> Float:\n  return 1.0", 1)
        elif "m1_core" in text:
            text = text.replace("export fn explicit_value(x: Int) -> Int:\n  return x",
                                "export fn explicit_value(x: Int) -> Float:\n  return 1.0", 1)
        elif "m1_hub" in text:
            text = text.replace("export fn value() -> Int:\n  return 1",
                                "export fn value() -> Float:\n  return 1.0", 1)
        elif "m1_generic" in text:
            text = text.replace("export fn forwarded_explicit() -> Int:\n  return 1",
                                "export fn forwarded_explicit() -> Float:\n  return 1.0", 1)
    else:
        raise ValueError(scenario)
    if scenario != "E0":
        if text == source.read_text():
            raise RuntimeError(f"{scenario} mutation did not match an anchor in {source}")
        source.write_text(text)


def expected_target(fixture: str, position: str, scenario: str, graph_size: int) -> int:
    """R5/R6 work target: changed package plus the required dependency closure."""
    if scenario == "E0":
        return 0
    if position == "app":
        return 1
    if scenario == "E1":
        return 1
    if scenario == "E2":
        if fixture == "dependency_chain":
            return 2  # core and its direct dependent middle
        if fixture == "fan_in_eight":
            return 9  # hub and eight direct consumers
        if fixture == "generic_cross_package":
            return 3  # generic provider and its two direct consumers
    if scenario == "E3":
        return graph_size  # transitive interface propagation/checkstop closure
    raise ValueError((fixture, position, scenario))


def main() -> int:
    TMP.mkdir(parents=True, exist_ok=True)
    run_id = os.environ.get("M1_RUN_ID") or time.strftime("%Y%m%d-%H%M%S")
    out = ROOT / "tmp/compiler-perf" / f"m1-{run_id}"
    out.mkdir(parents=True, exist_ok=False)
    rows: list[dict] = []
    force_strict_targets = os.environ.get("MOSS_M1_ENFORCE_TARGETS", "").lower() in {"1", "true", "yes"}
    package_counts = {}
    for fixture, graph in graphs().items():
        root = TMP / "graphs" / run_id / fixture
        roots = write_graph(root, graph)
        root_pkg = roots["app"]
        clean = run_profiled_build(root_pkg, roots, f"{fixture}-clean")
        package_counts[fixture] = len(graph)
        if clean["packages_checked"] != len(graph):
            raise RuntimeError(f"clean build of {fixture} checked {clean['packages_checked']} packages; expected {len(graph)}")
        target = len(graph)
        rows.append({"fixture": fixture, "position": "clean", "scenario": "clean_build", "command": "margo build",
                     **clean, "expected_packages": target, "target_met": clean["packages_checked"] == target})

    # Probe the optional Margo mode once. The current baseline may not support it.
    probe_root = TMP / "graphs" / run_id / "probe-build-check"
    probe_roots = write_graph(probe_root, graphs()["dependency_chain"])
    probe_env = dict(os.environ)
    probe_env.update({"MOSS": str(MOSS), "MARGO_HOME": str(TMP / "margo-home" / "build-check-probe")})
    probe_status, probe_output, _ = invoke(
        [str(MARGO), "build", "--check", "--json"], probe_roots["app"],
        probe_env, "build-check-probe")
    if probe_status == 0:
        margo_check_supported = True
    elif re.search(r"unrecognized arguments: .*--check", probe_output):
        margo_check_supported = False
    else:
        raise RuntimeError("margo build --check probe failed for a reason other than an unsupported option:\n"
                           + probe_output[-4000:])
    # Once Margo supports the incremental --check contract, targets become a
    # hard gate by default. The env flag enables the same gate during rollout.
    strict_targets = force_strict_targets or margo_check_supported

    scenarios = [
        ("dependency_chain", "core", "core"),
        ("dependency_chain", "app", "app"),
        ("fan_in_eight", "hub", "hub"),
        ("generic_cross_package", "generic", "generic"),
    ]
    for fixture, position, package_key in scenarios:
        graph = graphs()[fixture]
        root = TMP / "graphs" / run_id / f"edits-{fixture}-{position}"
        roots = write_graph(root, graph)
        app_root = roots["app"]
        run_profiled_build(app_root, roots, f"{fixture}-{position}-initial")
        if margo_check_supported:
            # Seed the semantic-check fingerprint state independently from
            # the native build state before measuring edit/revert work.
            run_profiled_build(app_root, roots,
                               f"{fixture}-{position}-check-initial",
                               check_mode=True)
        target = roots[package_key] / "src" / f"{graph[package_key]['module']}.moss"
        original_source = target.read_text()
        for scenario in ("E0", "E1", "E2", "E3"):
            mutate_scenario(target, scenario, position)
            expected = expected_target(fixture, position, scenario, len(graph))
            command = ("margo build --check" if margo_check_supported else
                       "margo build (build --check unsupported)")
            changed = run_profiled_build(app_root, roots, f"{fixture}-{position}-{scenario}-edit",
                                        check_mode=margo_check_supported)
            row = {"fixture": fixture, "position": position, "scenario": scenario,
                   "command": command, **changed, "expected_packages": expected,
                   "target_met": changed["packages_checked"] == expected}
            rows.append(row)
            if strict_targets and not row["target_met"]:
                raise RuntimeError(f"{fixture}/{position}/{scenario}: checked {changed['packages_checked']} packages; R-rule target {expected}")

            check_counts = run_profiled_check(app_root, roots, target,
                                               f"{fixture}-{position}-{scenario}-moss-check")
            check_row = {"fixture": fixture, "position": position, "scenario": scenario,
                         "command": "moss check <edited file>", **check_counts,
                         "expected_packages": 1, "target_met": check_counts["packages_checked"] == 1}
            rows.append(check_row)
            if strict_targets and not check_row["target_met"]:
                raise RuntimeError(f"{fixture}/{position}/{scenario}: moss check checked {check_counts['packages_checked']} packages; target 1")

            target.write_text(original_source)
            reverted = run_profiled_build(app_root, roots, f"{fixture}-{position}-{scenario}-revert",
                                          check_mode=margo_check_supported)
            revert_row = {"fixture": fixture, "position": position, "scenario": f"{scenario}-revert",
                          "command": command + " (reverted)", **reverted,
                          "expected_packages": expected, "target_met": reverted["packages_checked"] == expected}
            rows.append(revert_row)
            if strict_targets and not revert_row["target_met"]:
                raise RuntimeError(f"{fixture}/{position}/{scenario} revert: checked {reverted['packages_checked']} packages; R-rule target {expected}")
            revert_check = run_profiled_check(app_root, roots, target,
                                              f"{fixture}-{position}-{scenario}-moss-check-revert")
            rows.append({"fixture": fixture, "position": position, "scenario": f"{scenario}-revert",
                         "command": "moss check <reverted file>", **revert_check,
                         "expected_packages": 1, "target_met": revert_check["packages_checked"] == 1})
            if strict_targets and rows[-1]["target_met"] is False:
                raise RuntimeError(f"{fixture}/{position}/{scenario} revert: moss check target 1 not met")
            # Ensure each E scenario starts from the original bytes.
            target.write_text(original_source)

    run_partial_reach(run_id, rows, strict_targets, margo_check_supported)
    run_fast_debug_scenarios(run_id, rows, force_strict_targets)

    fieldnames = list(dict.fromkeys(key for row in rows for key in row))
    with (out / "m1_work_counters.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, restval="",
                                lineterminator="\n")
        writer.writeheader(); writer.writerows(rows)
    lines = ["# Phase 26.1 M1 package work counters", "", f"- Main SHA: `{subprocess.check_output(['git','rev-parse','origin/main'], cwd=ROOT, text=True).strip()}`", f"- Measurement revision SHA: `{subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()}`",
             "- The harness checks whether `margo build --check` is supported. On the pre-feature baseline it records the rejection and measures `margo build` as a full-work fallback.",
             f"- Target enforcement: `{'on' if strict_targets else 'off'}` (automatic when `margo build --check` is supported; `MOSS_M1_ENFORCE_TARGETS=1` enables it during rollout). `expected_packages` is always the R-rule target; `packages_checked` is observed work.",
             "- Counter source: one `MOSS_PROFILE_COMPILER=1` compiler invocation per checked package; modules count source files in checked packages (these fixtures use one source module per file). `functions_checked` counts source function checker entries, summed across Checker instances. `inference_function_visits` is total inference visits, not unique functions.",
             "- The Margo rows are package-graph totals. The file-check rows profile the edited file with the built dependency interfaces on `MOSS_MODULE_PATH`.", "",
             "## Package graph fixtures", "", "| Fixture | Packages | Baseline packages checked | Modules | Functions checked | Inference visits |", "|---|---:|---:|---:|---:|---:|"]
    for row in rows[:4]:
        lines.append(f"| `{row['fixture']}` | {package_counts[row['fixture']]} | {row['packages_checked']} | {row['modules_checked']} | {row['functions_checked']} | {row['inference_function_visits']} |")
    lines += ["", "## Edit and revert scenarios", "", "Wall time is the elapsed command time on this machine, including Margo orchestration. `moss check` rows include one file check. These are single-run observations, not a timing certification.", "", "| Fixture / position | Edit | Command | Wall ms | Actual | R-rule target | Target met | Modules | Functions | Inference visits |", "|---|---|---|---:|---:|:---:|---:|---:|---:|---:|"]
    for row in rows[4:]:
        if row["fixture"] == "fast_debug_two_package":
            continue
        lines.append(f"| {row['fixture']} / {row['position']} | {row['scenario']} | `{row['command']}` | {row.get('wall_ms', 0):.3f} | {row['packages_checked']} | {row['expected_packages']} | {row['target_met']} | {row['modules_checked']} | {row['functions_checked']} | {row['inference_function_visits']} |")
    lines += ["", "## Partial-reach fixture (C1 target)", "",
              "The edited file reaches 3 of the package's 6 functions. The target is the exact reachable set by identity (`<file>:<function>`), not a raw entry count.", "",
              "| Command | Functions | Expected | Target met | Identities |", "|---|---:|---:|:---:|---|"]
    for row in rows:
        if row["fixture"] == "partial_reach":
            lines.append(f"| `{row['command']}` | {row['functions_checked']} | {row['expected_functions']} | {row['target_met']} | {row['function_identities']} |")
    lines += ["", "## Fast Debug count gates (X0)", "",
              "Targets are selected-entry function identities and package artifact changes, not elapsed time. `dependency_packages_fully_checked` is zero when Fast Debug checks only reached dependency functions. `package_artifacts_changed` hashes `.mossi` interfaces and Margo's existing semantic-cache snapshots. These are a baseline proxy for check/build artifact churn; Phase 26.1.1's checked-program snapshot does not exist yet, so these rows do not claim to measure it.",
              "", "| Scenario | Selected test | Functions / target | Dependency functions | Dependency packages fully checked | Artifact packages changed / target | Target met | Identities |", "|---|---|---:|---:|---:|---:|:---:|---|"]
    for row in rows:
        if row["fixture"] != "fast_debug_two_package":
            continue
        lines.append(f"| {row['position']} | {row['scenario']} | {row['functions_checked']} / {row.get('expected_functions', 'package target')} | {row.get('dependency_functions_checked', 0)} | {row.get('dependency_packages_fully_checked', 0)} | {row.get('package_artifacts_changed', 0)} / {row.get('expected_package_artifacts_changed', row.get('expected_packages', 0))} | {row['target_met']} | {row.get('function_identities', '')} |")
    lines += ["", "Scenario definitions: E0 touches the source without changing bytes; E1 changes an exported function body while preserving its interface; E2 changes an inferred exported result type; E3 changes an explicit exported result type. Core/hub/provider E2 targets include direct dependents; E3 targets the full transitive closure. Top-app E1–E3 targets are one package. Each scenario is edited and reverted independently. `MOSS_M1_ENFORCE_TARGETS=1` turns target mismatches into hard failures; it is intended for after G1 implements incremental checks. The pre-G1 baseline reports mismatches as data.", "",
              "Functions checked counts source function checker entry occurrences, not unique reachable identities. R3 still requires its diagnostic oracle; any work attribution by reachable function should record identities, not infer them from this aggregate count. Full rows: `benchmarks/phase26_1/results/m1_phase26_main.csv`.", ""]
    (out / "M1_BASELINE.md").write_text("\n".join(lines))
    print(f"M1 baseline: {out / 'M1_BASELINE.md'}")
    print(f"M1 counters: {out / 'm1_work_counters.csv'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"M1 suite: {error}", file=sys.stderr)
        raise SystemExit(1)
