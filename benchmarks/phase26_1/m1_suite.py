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


def package(name: str, deps: dict[str, str], module: str, body: str) -> dict:
    return {"name": name, "deps": deps, "module": module, "body": body}


def graphs() -> dict[str, dict[str, dict]]:
    lib = package("m1_library", {}, "m1_library", "export fn value(x: Int) -> Int:\n  return x\n")
    app = package("m1_app", {"m1_library": "../library"}, "m1_app",
                  "import m1_library\nfn main():\n  echo m1_library.value(7)\n")

    core = package("m1_core", {}, "m1_core",
                   "export fn value(x: Int):\n  return x\n"
                   "export fn explicit_value(x: Int) -> Int:\n  return x\n"
                   "fn unused_a(x: Int) -> Int:\n  return x + 1\n"
                   "fn unused_b(x: Int) -> Int:\n  return x + 2\n")
    middle = package("m1_middle", {"m1_core": "../core"}, "m1_middle",
                     "import m1_core\nexport fn value(x: Int):\n  return m1_core.explicit_value(x)\n")
    service = package("m1_service", {"m1_middle": "../middle"}, "m1_service",
                      "import m1_middle\nexport fn value(x: Int):\n  return m1_middle.value(x)\n")
    chain_app = package("m1_app", {"m1_service": "../service"}, "m1_app",
                        "import m1_service\nfn main():\n  echo m1_service.value(7)\n")

    hub = package("m1_hub", {}, "m1_hub", "export fn value() -> Int:\n  return 1\n")
    consumers = {f"consumer_{i}": package(f"m1_consumer_{i}", {"m1_hub": "../hub"},
                 f"m1_consumer_{i}", "import m1_hub\nexport fn value() -> Int:\n  return m1_hub.value()\n")
                 for i in range(8)}
    fan_deps = {f"m1_consumer_{i}": f"../consumer_{i}" for i in range(8)}
    fan_imports = "\n".join(f"import m1_consumer_{i}" for i in range(8))
    fan_calls = "\n".join(f"  echo m1_consumer_{i}.value()" for i in range(8))
    fan = package("m1_fan_app", fan_deps, "m1_fan_app",
                  f"{fan_imports}\nfn main():\n{fan_calls}\n")

    generic = package("m1_generic", {}, "m1_generic",
                      "export fn identity(value):\n  return value\n")
    int_consumer = package("m1_int_consumer", {"m1_generic": "../generic"}, "m1_int_consumer",
                           "import m1_generic\nexport fn value() -> Int:\n  return m1_generic.identity(17)\n")
    text_consumer = package("m1_text_consumer", {"m1_generic": "../generic"}, "m1_text_consumer",
                            'import m1_generic\nexport fn value() -> String:\n  return m1_generic.identity("moss")\n')
    generic_app = package("m1_generic_app", {
        "m1_int_consumer": "../int_consumer",
        "m1_text_consumer": "../text_consumer",
    }, "m1_generic_app", "import m1_int_consumer\nimport m1_text_consumer\nfn main():\n"
       "  echo m1_int_consumer.value()\n  echo m1_text_consumer.value()\n")

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


def invoke(cmd: list[str], cwd: Path, env: dict[str, str], label: str) -> tuple[int, str]:
    result = subprocess.run(cmd, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        (TMP / f"{label}.failed.log").write_text(result.stdout)
    return result.returncode, result.stdout


def summarize_logs(logdir: Path, package_roots: dict[str, Path]) -> dict:
    functions = reinferred = 0
    checked_packages: set[str] = set()
    for path in sorted(logdir.glob("*.stderr")):
        log_text = path.read_text()
        if "M1_CWD=" not in log_text:
            continue
        checked_packages.add(log_text.split("M1_CWD=", 1)[1].splitlines()[0])
        for key, value in COUNTER_RE.findall(log_text):
            if key == "functions_analyzed":
                functions += int(value)
            elif key == "inference_reinferred_functions":
                reinferred += int(value)
    roots_by_path = {str(root.resolve()): root for root in package_roots.values()}
    modules = sum(len(list((root / "src").rglob("*.moss")))
                  for checked in checked_packages
                  if (root := roots_by_path.get(checked)) is not None)
    return {"packages_checked": len(checked_packages), "modules_checked": modules,
            "functions_checked": functions, "functions_reinferred": reinferred}


def run_profiled_build(root: Path, package_roots: dict[str, Path], label: str,
                       check_mode: bool = False) -> dict:
    logs = TMP / "logs" / label
    clear_logs(logs)
    wrapper = TMP / "bin/moss-profile-wrapper"
    make_wrapper(wrapper)
    env = dict(os.environ)
    env.update({"MOSS": str(wrapper), "M1_REAL_MOSS": str(MOSS),
                "M1_PROFILE_DIR": str(logs), "MOSS_PROFILE_COMPILER": "1",
                "MARGO_HOME": str(TMP / "margo-home" / label)})
    args = [str(MARGO), "build"] + (["--check"] if check_mode else []) + ["--json"]
    status, output = invoke(args, root, env, label)
    if status:
        raise RuntimeError(f"margo build failed for {label}:\n{output[-4000:]}")
    return summarize_logs(logs, package_roots)


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
    status, output = invoke([str(wrapper), "check", str(target)], target_root, env, label)
    if status:
        raise RuntimeError(f"moss check failed for {label}:\n{output[-4000:]}")
    counts = summarize_logs(logs, {"target": target_root})
    if counts["packages_checked"] != 1:
        raise RuntimeError(f"moss check should check only the edited package, got {counts['packages_checked']}")
    return counts


def mutate_scenario(source: Path, scenario: str) -> None:
    text = source.read_text()
    if scenario == "E0":
        os.utime(source, None)
    elif scenario == "E1":
        text = text.replace("return x", "return x + 0", 1)
    elif scenario == "E2":
        text = ("module m1_core\nexport fn value(x: Int):\n  return \"changed\"\n"
                "export fn explicit_value(x: Int) -> Int:\n  return x\n"
                "fn unused_a(x: Int) -> Int:\n  return x + 1\n"
                "fn unused_b(x: Int) -> Int:\n  return x + 2\n")
    elif scenario == "E3":
        text = ("module m1_core\nexport fn value(x: Int):\n  return x\n"
                "export fn explicit_value(x: Int) -> Float:\n  return 1.0\n"
                "fn unused_a(x: Int) -> Int:\n  return x + 1\n"
                "fn unused_b(x: Int) -> Int:\n  return x + 2\n")
    else:
        raise ValueError(scenario)
    if scenario != "E0":
        source.write_text(text)


def main() -> int:
    TMP.mkdir(parents=True, exist_ok=True)
    run_id = time.strftime("%Y%m%d-%H%M%S")
    out = ROOT / "tmp/compiler-perf" / f"m1-{run_id}"
    out.mkdir(parents=True, exist_ok=False)
    rows: list[dict] = []
    package_counts = {}
    for fixture, graph in graphs().items():
        root = TMP / "graphs" / run_id / fixture
        roots = write_graph(root, graph)
        root_pkg = roots["app"]
        clean = run_profiled_build(root_pkg, roots, f"{fixture}-clean")
        package_counts[fixture] = len(graph)
        if clean["packages_checked"] != len(graph):
            raise RuntimeError(f"clean build of {fixture} checked {clean['packages_checked']} packages; expected {len(graph)}")
        rows.append({"fixture": fixture, "scenario": "clean_build", "command": "margo build",
                     "packages_checked": clean["packages_checked"], "modules_checked": clean["modules_checked"],
                     "functions_checked": clean["functions_checked"], "functions_reinferred": clean["functions_reinferred"],
                     "expected_packages": len(graph)})

    scenario_graph = graphs()["dependency_chain"]
    for scenario in ("E0", "E1", "E2", "E3"):
        root = TMP / "graphs" / run_id / f"edits-{scenario}"
        roots = write_graph(root, scenario_graph)
        app_root, core_root = roots["app"], roots["core"]
        run_profiled_build(app_root, roots, f"{scenario}-initial")
        probe_root = TMP / "graphs" / run_id / f"probe-{scenario}"
        probe_roots = write_graph(probe_root, scenario_graph)
        probe_env = dict(os.environ)
        probe_env.update({"MOSS": str(MOSS),
                          "MARGO_HOME": str(TMP / "margo-home" / f"{scenario}-probe")})
        probe_status, _ = invoke([str(MARGO), "build", "--check", "--json"],
                                 probe_roots["app"], probe_env,
                                 f"{scenario}-margo-check-probe")
        margo_check_supported = probe_status == 0
        target = core_root / "src/m1_core.moss"
        original_source = target.read_text()
        mutate_scenario(target, scenario)

        # Baseline Margo has no --check mode; preserve its rejection as part of
        # the M1 report and measure its current full-build behavior as control.
        margo_counts = run_profiled_build(app_root, roots, f"{scenario}-after-margo-check",
                                          check_mode=margo_check_supported)
        expected_edit_packages = (len(roots) if not margo_check_supported else
                                  {"E0": 0, "E1": 1, "E2": 2, "E3": 4}[scenario])
        if margo_counts["packages_checked"] != expected_edit_packages:
            raise RuntimeError(f"{scenario} edit checked {margo_counts['packages_checked']} packages; expected {expected_edit_packages}")
        rows.append({"fixture": "dependency_chain", "scenario": scenario,
                     "command": ("margo build --check" if margo_check_supported else
                                 "margo build (build --check unsupported)"),
                     "packages_checked": margo_counts["packages_checked"],
                     "modules_checked": margo_counts["modules_checked"],
                     "functions_checked": margo_counts["functions_checked"],
                     "functions_reinferred": margo_counts["functions_reinferred"],
                     "expected_packages": len(roots)})

        check_counts = run_profiled_check(app_root, roots, target, f"{scenario}-after-moss-check")
        rows.append({"fixture": "dependency_chain", "scenario": scenario,
                     "command": "moss check <edited core file>",
                     "packages_checked": check_counts["packages_checked"],
                     "modules_checked": check_counts["modules_checked"],
                     "functions_checked": check_counts["functions_checked"],
                     "functions_reinferred": check_counts["functions_reinferred"],
                     "expected_packages": 1})
        target.write_text(original_source)
        revert_counts = run_profiled_build(app_root, roots, f"{scenario}-revert-margo-check",
                                           check_mode=margo_check_supported)
        expected_revert_packages = (len(roots) if not margo_check_supported else
                                    ({"E0": 0, "E1": 1, "E2": 2, "E3": 4}[scenario]))
        if revert_counts["packages_checked"] != expected_revert_packages:
            raise RuntimeError(f"{scenario} revert checked {revert_counts['packages_checked']} packages; expected {expected_revert_packages}")
        rows.append({"fixture": "dependency_chain", "scenario": f"{scenario}-revert",
                     "command": ("margo build --check (reverted)" if margo_check_supported else
                                 "margo build (reverted; --check unsupported)"),
                     "packages_checked": revert_counts["packages_checked"],
                     "modules_checked": revert_counts["modules_checked"],
                     "functions_checked": revert_counts["functions_checked"],
                     "functions_reinferred": revert_counts["functions_reinferred"],
                     "expected_packages": len(roots)})
        revert_check = run_profiled_check(app_root, roots, target, f"{scenario}-revert-moss-check")
        rows.append({"fixture": "dependency_chain", "scenario": f"{scenario}-revert",
                     "command": "moss check <reverted core file>",
                     "packages_checked": revert_check["packages_checked"],
                     "modules_checked": revert_check["modules_checked"],
                     "functions_checked": revert_check["functions_checked"],
                     "functions_reinferred": revert_check["functions_reinferred"],
                     "expected_packages": 1})

    with (out / "m1_work_counters.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader(); writer.writerows(rows)
    lines = ["# Phase 26.1 M1 package work counters", "", f"- Main SHA: `{subprocess.check_output(['git','rev-parse','origin/main'], cwd=ROOT, text=True).strip()}`",
             "- The harness checks whether `margo build --check` is supported. On the pre-feature baseline it records the rejection and measures `margo build` as a full-work fallback.",
             "- Counter source: one `MOSS_PROFILE_COMPILER=1` compiler invocation per checked package; module counts are source modules in checked packages. Function and reinference counters sum all checker-instance profile records.",
             "- The Margo rows are package-graph totals. The file-check rows profile the edited file with the built dependency interfaces on `MOSS_MODULE_PATH`.", "",
             "## Package graph fixtures", "", "| Fixture | Packages | Baseline packages checked | Modules | Functions checked | Re-inferred functions |", "|---|---:|---:|---:|---:|---:|"]
    for row in rows[:4]:
        lines.append(f"| `{row['fixture']}` | {package_counts[row['fixture']]} | {row['packages_checked']} | {row['modules_checked']} | {row['functions_checked']} | {row['functions_reinferred']} |")
    lines += ["", "## Edit and revert scenarios", "", "| Edit | Command | Packages checked | Modules checked | Functions checked | Functions re-inferred |", "|---|---|---:|---:|---:|---:|"]
    for row in rows[4:]:
        lines.append(f"| {row['scenario']} | `{row['command']}` | {row['packages_checked']} | {row['modules_checked']} | {row['functions_checked']} | {row['functions_reinferred']} |")
    lines += ["", "Scenario definitions: E0 touches the source without changing bytes; E1 changes one function body while preserving its interface; E2 changes another function body so its inferred result type changes; E3 changes an explicit result signature whose type is forwarded through the dependency chain. Each starts from a fresh chain, measures both commands after the edit, restores the original source, and measures both commands after revert.", "",
              "Full rows: `m1_work_counters.csv`.", ""]
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
