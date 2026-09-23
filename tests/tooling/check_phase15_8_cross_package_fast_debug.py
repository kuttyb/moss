#!/usr/bin/env python3
"""Phase 15.8 cross-package Fast Debug regression coverage.

Margo resolves package graphs. Moss sees only resolved source-provider roots and
remains responsible for module resolution, checking, and interpretation.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "phase15_8"
PKG_GRAPHPKG = FIXTURES / "pkg_graphpkg"
PKG_MAIN = FIXTURES / "pkg_main"
SOURCE_ROOT_ENV = "MOSS_FAST_DEBUG_SOURCE_ROOTS"


def run(command, *, cwd, env=None):
    return subprocess.run(command, cwd=cwd, env=env or dict(os.environ), text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def env_for(compiler, **extra):
    env = dict(os.environ)
    env["MOSS"] = str(compiler)
    env.pop(SOURCE_ROOT_ENV, None)
    env.pop("MOSS_SOURCE_ROOTS", None)
    env.update(extra)
    return env


def require_ok(result, context):
    if result.returncode:
        raise AssertionError(f"{context} failed (rc={result.returncode}):\n"
                             f"stdout: {result.stdout}\nstderr: {result.stderr}")


def write_package(root, name, source, dependencies=""):
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        f'[build]\nsource = "src"\n{dependencies}', encoding="utf-8")
    (root / "src" / "main.moss").write_text(source, encoding="utf-8")


def test_path_source_parity_and_no_rustc(compiler):
    margo = compiler.parent / "margo"
    native_env = env_for(compiler)
    require_ok(run([str(margo), "build"], cwd=PKG_MAIN, env=native_env),
               "fixture native package build")
    native = run([str(margo), "run"], cwd=PKG_MAIN, env=native_env)
    require_ok(native, "fixture native package run")
    interpreted = run([str(margo), "debug"], cwd=PKG_MAIN,
                      env=env_for(compiler, RUSTC="/definitely/not/rustc"))
    require_ok(interpreted, "source-backed margo debug without rustc")
    if native.stdout != interpreted.stdout or native.stdout.strip() != "42":
        raise AssertionError(f"native/interpreted output mismatch: {native.stdout!r} vs "
                             f"{interpreted.stdout!r}")
    direct = run([str(compiler), "debug", str(PKG_MAIN)], cwd=PKG_MAIN,
                 env=env_for(compiler, **{SOURCE_ROOT_ENV: str(PKG_GRAPHPKG),
                                           "RUSTC": "/definitely/not/rustc"}))
    require_ok(direct, "direct source-provider Fast Debug")
    if direct.stdout != native.stdout:
        raise AssertionError("direct source-provider output differed from native output")
    print("  [PASS] path source closure, no-rustc debug, and native/interpreter parity")


def test_real_source_free_provider_rejection(compiler):
    """Build a real interface, resolve it, then require the specific rejection."""
    margo = compiler.parent / "margo"
    require_ok(run([str(margo), "build"], cwd=PKG_MAIN, env=env_for(compiler)),
               "build real graphpkg .mossi provider")
    provider_dir = PKG_GRAPHPKG / "build" / "debug"
    if not list(provider_dir.glob("*.mossi")):
        raise AssertionError(f"native build did not emit graphpkg .mossi in {provider_dir}")
    result = run([str(compiler), "debug", str(PKG_MAIN)], cwd=PKG_MAIN,
                 env=env_for(compiler, MOSS_MODULE_PATH=str(provider_dir),
                             RUSTC="/definitely/not/rustc"))
    combined = result.stdout + result.stderr
    if result.returncode == 0 or "FAST_DEBUG_NATIVE_DEPENDENCY" not in combined or "graphpkg" not in combined:
        raise AssertionError("expected semantic .mossi selection followed by "
                             f"FAST_DEBUG_NATIVE_DEPENDENCY for graphpkg:\n{combined}")
    print("  [PASS] real source-free .mossi provider is rejected before interpreter preparation")


def trace_events(result, context):
    require_ok(result, context)
    events = []
    for line in result.stderr.splitlines():
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise AssertionError(f"trace was not NDJSON: {line!r}") from error
    return events


def test_trace_has_dependency_execution_event(compiler):
    result = run([str(compiler), "debug", "--trace", str(PKG_MAIN)], cwd=PKG_MAIN,
                 env=env_for(compiler, **{SOURCE_ROOT_ENV: str(PKG_GRAPHPKG),
                                           "RUSTC": "/definitely/not/rustc"}))
    events = trace_events(result, "source-backed trace")
    dependency_file = str((PKG_GRAPHPKG / "src" / "graphpkg.moss").resolve())
    if not any(event.get("source_file") == dependency_file and
               event.get("event") == "FunctionEnter" and
               event.get("semantic_identity") == "fn:graphpkg__double@3"
               for event in events):
        raise AssertionError(f"trace lacked semantic execution in graphpkg source: {events}")
    print("  [PASS] structured trace includes semantic execution in dependency source")


def test_invalid_root_fails_closed(compiler):
    missing = run([str(compiler), "debug", str(PKG_MAIN)], cwd=PKG_MAIN,
                  env=env_for(compiler, **{SOURCE_ROOT_ENV: str(PKG_MAIN / "absent")}))
    if missing.returncode == 0 or "PROJECT_SOURCE_NOT_FOUND" not in (missing.stdout + missing.stderr):
        raise AssertionError("missing supplied source root was silently ignored")
    print("  [PASS] invalid Fast Debug source roots fail closed")


def test_source_provider_selection_parity(compiler):
    """Unused duplicates are legal; root source wins; reachable deps conflict."""
    with tempfile.TemporaryDirectory(prefix="moss-phase158-", dir=REPO / "tmp") as tmp:
        root = Path(tmp)
        write_package(root / "a", "a", "module Utils\n\nexport fn value() -> Int:\n  return 1\n")
        write_package(root / "b", "b", "module Utils\n\nexport fn value() -> Int:\n  return 2\n")
        dependencies = "\n[dependencies]\na = { path = \"../a\" }\nb = { path = \"../b\" }\n"
        # Source candidates are not disambiguated until an import reaches one.
        write_package(root / "unused", "unused", "module app\n\nfn main():\n  echo 7\n",
                      dependencies)
        unused = run([str(compiler.parent / "margo"), "debug"], cwd=root / "unused",
                     env=env_for(compiler, RUSTC="/definitely/not/rustc"))
        require_ok(unused, "unused duplicate source providers")
        if unused.stdout.strip() != "7":
            raise AssertionError(f"unused duplicate output was {unused.stdout!r}")

        # Root/local source intentionally beats an external source provider.
        write_package(root / "local", "local", "module app\nimport Utils\n\nfn main():\n  echo Utils.value()\n",
                      "\n[dependencies]\na = { path = \"../a\" }\n")
        (root / "local" / "src" / "utils.moss").write_text(
            "module Utils\n\nexport fn value() -> Int:\n  return 42\n", encoding="utf-8")
        local = run([str(compiler.parent / "margo"), "debug"], cwd=root / "local",
                    env=env_for(compiler, RUSTC="/definitely/not/rustc"))
        require_ok(local, "root source precedence")
        if local.stdout.strip() != "42":
            raise AssertionError(f"root source did not win: {local.stdout!r}")

        write_package(root / "app", "app", "module app\nimport Utils\n\nfn main():\n  echo Utils.value()\n",
                      dependencies)
        result = run([str(compiler.parent / "margo"), "debug"], cwd=root / "app",
                     env=env_for(compiler, RUSTC="/definitely/not/rustc"))
        text = result.stdout + result.stderr
        providers = sorted(str((root / package).resolve()) for package in ("a", "b"))
        if (result.returncode == 0 or "MODULE_IMPORT_AMBIGUOUS" not in text or
                "Utils" not in text or not all(provider in text for provider in providers) or
                text.index(providers[0]) > text.index(providers[1])):
            raise AssertionError(f"duplicate source providers were not rejected:\n{text}")
    print("  [PASS] lazy source-provider selection matches native precedence and ambiguity")


def test_transitive_source_closure_and_identity(compiler):
    with tempfile.TemporaryDirectory(prefix="moss-phase158-", dir=REPO / "tmp") as tmp:
        root = Path(tmp)
        write_package(root / "leaf", "leaf", "module leaf\n\nexport fn value() -> Int:\n  return 40\n")
        write_package(root / "middle", "middle", "module middle\nimport leaf\n\nexport fn value() -> Int:\n  return leaf.value() + 2\n",
                      "\n[dependencies]\nleaf = { path = \"../leaf\" }\n")
        write_package(root / "app", "app", "module app\nimport middle\n\nfn main():\n  echo middle.value()\n",
                      "\n[dependencies]\nmiddle = { path = \"../middle\" }\n")
        debug = run([str(compiler.parent / "margo"), "debug"], cwd=root / "app",
                    env=env_for(compiler, RUSTC="/definitely/not/rustc"))
        require_ok(debug, "transitive source-backed margo debug")
        if debug.stdout.strip() != "42":
            raise AssertionError(f"transitive output was {debug.stdout!r}")
        trace = run([str(compiler.parent / "margo"), "debug", "--trace"], cwd=root / "app",
                    env=env_for(compiler, RUSTC="/definitely/not/rustc"))
        sources = {event.get("source_file") for event in trace_events(trace, "transitive source trace")}
        # The root main itself is not a traced function frame, but both
        # dependency packages deliberately use the same local filename.
        expected = {str((root / package / "src" / "main.moss").resolve())
                    for package in ("middle", "leaf")}
        if not expected <= sources:
            raise AssertionError(f"same local filenames collapsed provenance: {sources}")
    print("  [PASS] transitive source closure preserves package-aware source identities")


def test_git_source_closure_and_cached_lock(compiler):
    with tempfile.TemporaryDirectory(prefix="moss-phase158-", dir=REPO / "tmp") as tmp:
        root = Path(tmp)
        remote = root / "remote"
        write_package(remote, "gitleaf", "module gitleaf\n\nexport fn value() -> Int:\n  return 42\n")
        require_ok(run(["git", "init", "--quiet", "--initial-branch=main"], cwd=remote), "initialize local Git dependency")
        require_ok(run(["git", "add", "."], cwd=remote), "stage local Git dependency")
        require_ok(run(["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                        "commit", "--quiet", "-m", "initial"], cwd=remote), "commit local Git dependency")
        write_package(root / "app", "app", "module app\nimport gitleaf\n\nfn main():\n  echo gitleaf.value()\n",
                      f"\n[dependencies]\ngitleaf = {{ git = \"{remote}\", branch = \"main\" }}\n")
        environment = env_for(compiler, MARGO_HOME=str(root / "margo-cache"),
                              RUSTC="/definitely/not/rustc")
        first = run([str(compiler.parent / "margo"), "debug"], cwd=root / "app", env=environment)
        require_ok(first, "Git dependency Fast Debug")
        if first.stdout.strip() != "42" or not (root / "app" / "Moss.lock").is_file():
            raise AssertionError("Git debug did not execute the locked source dependency")
        remote.rename(root / "remote-offline")
        second = run([str(compiler.parent / "margo"), "debug"], cwd=root / "app", env=environment)
        require_ok(second, "cached locked Git Fast Debug")
        if second.stdout != first.stdout:
            raise AssertionError("cached locked Git debug output differed")
    print("  [PASS] resolved and cached locked Git dependency source executes without Moss Git access")


def test_margo_cli_and_environment_are_exact(compiler):
    margo = compiler.parent / "margo"
    for arguments in (("debug", "target"), ("debug", "--release"),
                      ("debug", "--json"), ("debug", "--typo")):
        if run([str(margo), *arguments], cwd=PKG_MAIN, env=env_for(compiler)).returncode == 0:
            raise AssertionError(f"margo {' '.join(arguments)} was silently accepted")
    with tempfile.TemporaryDirectory(prefix="moss-phase158-", dir=REPO / "tmp") as tmp:
        root = Path(tmp)
        write_package(root, "solo", "module solo\n\nfn main():\n  echo 7\n")
        result = run([str(margo), "debug"], cwd=root,
                     env=env_for(compiler, **{SOURCE_ROOT_ENV: "/not/a/source/root",
                                              "RUSTC": "/definitely/not/rustc"}))
        require_ok(result, "dependency-free margo debug with contaminated environment")
        if result.stdout.strip() != "7":
            raise AssertionError("margo debug inherited an unrelated source-root environment")
    print("  [PASS] margo debug accepts only --trace and supplies an exact source universe")


def test_standalone_debug_unchanged(compiler):
    with tempfile.TemporaryDirectory(prefix="moss-phase158-", dir=REPO / "tmp") as tmp:
        root = Path(tmp)
        write_package(root, "standalone", "module standalone\n\nfn main():\n  echo 99\n")
        result = run([str(compiler), "debug", str(root)], cwd=root,
                     env=env_for(compiler, RUSTC="/definitely/not/rustc"))
        require_ok(result, "standalone moss debug")
        if result.stdout.strip() != "99":
            raise AssertionError(f"unexpected standalone output: {result.stdout!r}")
    print("  [PASS] standalone/same-project moss debug remains unchanged")


def test_build_planner_dogfood(compiler):
    graphlib = REPO / "projects" / "build_planner" / "graphlib"
    planner = graphlib.parent / "planner"
    margo = compiler.parent / "margo"
    native_env = env_for(compiler)
    for package, command in ((graphlib, "build"), (graphlib, "test"),
                             (planner, "build"), (planner, "test")):
        require_ok(run([str(margo), command], cwd=package, env=native_env),
                   f"Build Planner {package.name} {command}")
    native = run([str(margo), "run"], cwd=planner, env=native_env)
    require_ok(native, "Build Planner native run")
    debug_env = env_for(compiler, RUSTC="/definitely/not/rustc")
    debug = run([str(margo), "debug"], cwd=planner, env=debug_env)
    require_ok(debug, "Build Planner source Fast Debug")
    if native.stdout != debug.stdout:
        raise AssertionError(f"Build Planner mismatch: {native.stdout!r} vs {debug.stdout!r}")
    trace = run([str(margo), "debug", "--trace"], cwd=planner, env=debug_env)
    sources = {event.get("source_file") for event in trace_events(trace, "Build Planner source trace")}
    if not any(source and source.startswith(str(planner.resolve()) + os.sep) for source in sources):
        raise AssertionError("Build Planner trace lacked planner semantic events")
    if not any(source and source.startswith(str(graphlib.resolve()) + os.sep) for source in sources):
        raise AssertionError("Build Planner trace lacked graphlib semantic events")
    print("  [PASS] Build Planner uses graphlib source with parity, trace, and no rustc")


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path-to-moss-compiler>", file=sys.stderr)
        return 2
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"Compiler not found: {compiler}", file=sys.stderr)
        return 2
    (REPO / "tmp").mkdir(exist_ok=True)
    print("Phase 15.8 — Cross-Package Fast Debug Source Convergence")
    tests = [test_path_source_parity_and_no_rustc, test_real_source_free_provider_rejection,
             test_trace_has_dependency_execution_event, test_invalid_root_fails_closed,
             test_source_provider_selection_parity,
             test_transitive_source_closure_and_identity, test_git_source_closure_and_cached_lock,
             test_margo_cli_and_environment_are_exact, test_standalone_debug_unchanged,
             test_build_planner_dogfood]
    failures = 0
    for test in tests:
        try:
            test(compiler)
        except AssertionError as error:
            failures += 1
            print(f"  [FAIL] {test.__name__}: {error}")
    if failures:
        print(f"\n{failures}/{len(tests)} tests FAILED")
        return 1
    print(f"\nAll {len(tests)} Phase 15.8 tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
