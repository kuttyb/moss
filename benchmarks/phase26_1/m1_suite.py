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
                 f"m1_consumer_{i}", "import m1_hub\nexport fn value() -> Int:\n  return m1_hub.value()\n")
                 for i in range(8)}
    fan_deps = {f"m1_consumer_{i}": f"../consumer_{i}" for i in range(8)}
    fan_imports = "\n".join(f"import m1_consumer_{i}" for i in range(8))
    fan_calls = "\n".join(f"  echo m1_consumer_{i}.value()" for i in range(8))
    fan = package("m1_fan_app", fan_deps, "m1_fan_app",
                  f"{fan_imports}\nfn main():\n{fan_calls}\n")

    generic = package("m1_generic", {}, "m1_generic",
                      "export fn identity(value):\n  return value\n" + anchors())
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
    function_checks = inference_visits = 0
    checked_packages: set[str] = set()
    for path in sorted(logdir.glob("*.stderr")):
        log_text = path.read_text()
        if "M1_CWD=" not in log_text:
            continue
        checked_packages.add(log_text.split("M1_CWD=", 1)[1].splitlines()[0])
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
            "inference_function_visits": inference_visits}


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
        text = text.replace("export fn inferred_anchor():\n  return 1",
                            "export fn inferred_anchor():\n  return 1 + 0", 1)
    elif scenario == "E2":
        text = text.replace("export fn inferred_anchor():\n  return 1",
                            'export fn inferred_anchor():\n  return "changed"', 1)
    elif scenario == "E3":
        text = text.replace("export fn explicit_anchor() -> Int:\n  return 1",
                            "export fn explicit_anchor() -> Float:\n  return 1.0", 1)
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
    run_id = time.strftime("%Y%m%d-%H%M%S")
    out = ROOT / "tmp/compiler-perf" / f"m1-{run_id}"
    out.mkdir(parents=True, exist_ok=False)
    rows: list[dict] = []
    strict_targets = os.environ.get("MOSS_M1_ENFORCE_TARGETS", "").lower() in {"1", "true", "yes"}
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
    probe_status, probe_output = invoke([str(MARGO), "build", "--check", "--json"],
                                        probe_roots["app"], probe_env, "build-check-probe")
    if probe_status == 0:
        margo_check_supported = True
    elif re.search(r"unrecognized arguments: .*--check", probe_output):
        margo_check_supported = False
    else:
        raise RuntimeError("margo build --check probe failed for a reason other than an unsupported option:\n"
                           + probe_output[-4000:])

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
        target = roots[package_key] / "src" / f"{graph[package_key]['module']}.moss"
        original_source = target.read_text()
        for scenario in ("E0", "E1", "E2", "E3"):
            mutate_scenario(target, scenario)
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

    with (out / "m1_work_counters.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader(); writer.writerows(rows)
    lines = ["# Phase 26.1 M1 package work counters", "", f"- Main SHA: `{subprocess.check_output(['git','rev-parse','origin/main'], cwd=ROOT, text=True).strip()}`", f"- Measurement revision SHA: `{subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()}`",
             "- The harness checks whether `margo build --check` is supported. On the pre-feature baseline it records the rejection and measures `margo build` as a full-work fallback.",
             f"- Target enforcement: `{'on' if strict_targets else 'off'}` (`MOSS_M1_ENFORCE_TARGETS=1` enables hard assertions). `expected_packages` is always the R-rule target; `packages_checked` is observed work.",
             "- Counter source: one `MOSS_PROFILE_COMPILER=1` compiler invocation per checked package; modules count source files in checked packages (these fixtures use one source module per file). `functions_checked` counts source function checker entries, summed across Checker instances. `inference_function_visits` is total inference visits, not unique functions.",
             "- The Margo rows are package-graph totals. The file-check rows profile the edited file with the built dependency interfaces on `MOSS_MODULE_PATH`.", "",
             "## Package graph fixtures", "", "| Fixture | Packages | Baseline packages checked | Modules | Functions checked | Inference visits |", "|---|---:|---:|---:|---:|---:|"]
    for row in rows[:4]:
        lines.append(f"| `{row['fixture']}` | {package_counts[row['fixture']]} | {row['packages_checked']} | {row['modules_checked']} | {row['functions_checked']} | {row['inference_function_visits']} |")
    lines += ["", "## Edit and revert scenarios", "", "| Fixture / position | Edit | Command | Actual | R-rule target | Target met | Modules | Functions | Inference visits |", "|---|---|---|---:|---:|:---:|---:|---:|---:|"]
    for row in rows[4:]:
        lines.append(f"| {row['fixture']} / {row['position']} | {row['scenario']} | `{row['command']}` | {row['packages_checked']} | {row['expected_packages']} | {row['target_met']} | {row['modules_checked']} | {row['functions_checked']} | {row['inference_function_visits']} |")
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
