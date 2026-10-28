#!/usr/bin/env python3
"""Phase 26.1.1 X2 Fast Debug scope, snapshot, and source-boundary gates."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]


def run(command, *, cwd, env):
    return subprocess.run(command, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False)


def require_ok(result, label):
    if result.returncode:
        raise AssertionError(f"{label} failed ({result.returncode})\n"
                             f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")


def write_package(root, name, main, dependencies=""):
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        f'[build]\nsource = "src"\n{dependencies}', encoding="utf-8")
    (root / "src" / "main.moss").write_text(main, encoding="utf-8")


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path-to-moss-compiler>", file=sys.stderr)
        return 2
    moss = Path(sys.argv[1]).resolve()
    if not moss.is_file():
        raise SystemExit(f"compiler not found: {moss}")
    margo = moss.parent / "margo"
    with tempfile.TemporaryDirectory(prefix="phase26-x2-", dir=REPO / "tmp") as temp:
        root = Path(temp)
        dep = root / "dep"
        app = root / "app"
        write_package(dep, "dep", '''module dep
export fn value() -> Int:
  return 40

export fn not_reached() -> Int:
  return 0
''')
        write_package(app, "app", '''module app
import dep

fn main():
  echo used()
  empty = typed_empty()

fn typed_empty() -> Vector[Int]:
  return Vector [Int]()

fn used() -> Int:
  return dep.value() + 2

fn unreachable() -> Int:
  return definitely_missing()

bench "unselected sibling bench":
  definitely_missing()
''', '\n[dependencies]\ndep = { path = "../dep" }\n')
        (app / "tests").mkdir()
        test_a = app / "tests" / "a.moss"
        test_a.write_text('''module tests_a
test "first":
  echo 1
''', encoding="utf-8")
        test_b = app / "tests" / "b.moss"
        test_b.write_text('''module tests_b
test "second":
  echo 2
''', encoding="utf-8")

        env = dict(os.environ)
        env["MOSS"] = str(moss)
        env["RUSTC"] = "/definitely/not/rustc"
        env["MOSS_PHASE26_C1_SCOPE_TRACE"] = "1"
        debug = run([str(margo), "debug"], cwd=app, env=env)
        require_ok(debug, "Margo source-free Fast Debug with unreachable root error")
        if debug.stdout.strip() != "42":
            raise AssertionError(f"main execution produced {debug.stdout!r}")
        if "unreachable" in debug.stderr or "definitely_missing" in debug.stderr:
            raise AssertionError("unreachable root sibling entered Fast Debug scope")
        interfaces = list((dep / "build" / "check-debug").glob("*.mossi"))
        if not interfaces:
            raise AssertionError("margo debug did not refresh the dependency .mossi")
        if list((dep / "build" / "check-debug").glob("*.rlib")):
            raise AssertionError("Margo Fast Debug unexpectedly invoked rustc")
        dep_source = dep / "src" / "main.moss"
        dep_source.write_text('''module dep
export fn value() -> Int:
  return 41

export fn not_reached() -> Int:
  return 0
''', encoding="utf-8")
        refreshed = run([str(margo), "debug"], cwd=app, env=env)
        require_ok(refreshed, "Margo debug refresh after dependency edit")
        if refreshed.stdout.strip() != "43":
            raise AssertionError(f"Margo debug used stale dependency body: {refreshed.stdout!r}")
        env["MOSS_MODULE_PATH"] = str(dep / "build" / "check-debug")

        # A filtered file run checks only the selected test body. Package mode
        # with no file checks every test across the package.
        selected = run([str(moss), "test", "--interp", str(test_a), "first"],
                       cwd=app, env=env)
        require_ok(selected, "selected test Fast Debug")
        if selected.stdout != "1\nPASS first\n1 passed\n":
            raise AssertionError(f"filter selected an unexpected test set: {selected.stdout!r}")
        empty_filter = run(
            [str(moss), "test", "--interp", str(test_a), ""],
            cwd=app, env=env)
        require_ok(empty_filter, "empty-filter test Fast Debug")
        if empty_filter.stdout != selected.stdout:
            raise AssertionError(
                f"empty filter changed selected tests: {empty_filter.stdout!r}")
        all_tests = run([str(moss), "test", "--interp"], cwd=app, env=env)
        require_ok(all_tests, "all-package-tests Fast Debug")
        if all_tests.stdout != "1\nPASS first\n2\nPASS second\n2 passed\n":
            raise AssertionError(f"no-file test run selected an unexpected set: {all_tests.stdout!r}")
        margo_tests = run([str(margo), "test", "--interp"], cwd=app, env=env)
        require_ok(margo_tests, "Margo all-package Fast Debug tests")
        if margo_tests.stdout != all_tests.stdout:
            raise AssertionError("Margo test --interp did not select all package tests")

        # Source-free dependency interfaces resolve imports from an implicit
        # root module too; this package intentionally has no `module` line.
        implicit_app = root / "implicit-app"
        write_package(implicit_app, "implicit_app", '''import dep
fn main():
  echo dep.value() + 2
''', '\n[dependencies]\ndep = { path = "../dep" }\n')
        implicit_debug = run([str(margo), "debug"], cwd=implicit_app, env=env)
        require_ok(implicit_debug, "implicit-module source-free dependency import")
        if implicit_debug.stdout.strip() != "43":
            raise AssertionError(f"implicit-module dependency execution produced {implicit_debug.stdout!r}")

        implicit_main = implicit_app / "src" / "main.moss"
        without_module_path = dict(env)
        without_module_path.pop("MOSS_MODULE_PATH", None)
        check_without_path = run(
            [str(moss), "check", str(implicit_main), "--json"],
            cwd=implicit_app, env=without_module_path)
        try:
            without_path_payload = json.loads(check_without_path.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError(
                "moss check without MOSS_MODULE_PATH returned invalid JSON:\n" +
                check_without_path.stdout + check_without_path.stderr) from error
        if (check_without_path.returncode == 0 or
                without_path_payload.get("ok") is not False or
                without_path_payload.get("error", {}).get("code") !=
                "UNKNOWN_SYMBOL_OR_TYPE"):
            raise AssertionError(
                "implicit dependency import unexpectedly checked without "
                "MOSS_MODULE_PATH:\n" + check_without_path.stdout)
        check_with_path = run(
            [str(moss), "check", str(implicit_main), "--json"],
            cwd=implicit_app, env=env)
        require_ok(check_with_path,
                   "implicit-module moss check with MOSS_MODULE_PATH")

        implicit_tests = implicit_app / "tests"
        implicit_tests.mkdir()
        (implicit_tests / "dep_test.moss").write_text('''module implicit_tests
import dep
test "implicit dependency import":
  assertEqual(dep.value(), 41)
''', encoding="utf-8")
        implicit_check = run(
            [str(margo), "build", "--check", "--json"],
            cwd=implicit_app, env=env)
        require_ok(implicit_check,
                   "implicit-module package semantic check with dependency interface")

        native_env = dict(env)
        native_env.pop("RUSTC", None)
        native_build = run([str(margo), "build", "--json"],
                           cwd=implicit_app, env=native_env)
        require_ok(native_build,
                   "native build of implicit-module dependency import")
        native_test = run([str(margo), "test", "--json"],
                          cwd=implicit_app, env=native_env)
        require_ok(native_test,
                   "native test of implicit-module dependency import")
        try:
            native_test_payload = json.loads(native_test.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError(
                "native margo test returned invalid JSON:\n" +
                native_test.stdout) from error
        native_tests = native_test_payload.get("result", {}).get("tests", [])
        if not any(test.get("name") == "implicit dependency import" and
                   test.get("status") == "pass" for test in native_tests):
            raise AssertionError(
                "native margo test did not execute the implicit dependency test:\n" +
                native_test.stdout)

        main_source = app / "src" / "main.moss"
        main_source.write_text(main_source.read_text() +
            '\ntest "unselected sibling test":\n  echo definitely_missing()\n',
            encoding="utf-8")
        main_after_test = run([str(margo), "debug"], cwd=app, env=env)
        require_ok(main_after_test, "main Fast Debug excludes colocated test body")
        if main_after_test.stdout.strip() != "43":
            raise AssertionError("colocated test body changed selected main execution")
        test_b.write_text('''module tests_b
import definitely_missing_module
test "second":
  echo definitely_missing()
''', encoding="utf-8")
        selected_with_unreachable_error = run(
            [str(moss), "test", "--interp", str(test_a), "first"],
            cwd=app, env=env)
        require_ok(selected_with_unreachable_error,
                   "filtered test excludes unselected error")
        if selected_with_unreachable_error.stdout != "1\nPASS first\n1 passed\n":
            raise AssertionError("filtered test checked an unselected test body")
        all_with_error = run([str(moss), "test", "--interp"], cwd=app, env=env)
        if (all_with_error.returncode == 0 or
                "interface for definitely_missing_module is missing or stale"
                not in all_with_error.stderr):
            raise AssertionError("no-file test run failed to resolve every package test module")

        # The removed source-root variable cannot supply dependency source.
        # Once the checked interface becomes stale, Moss must stop at that
        # boundary instead of parsing the invalid source through the variable.
        source = dep / "src" / "main.moss"
        source.write_text("module dep\nthis is not valid moss\n", encoding="utf-8")
        contaminated = dict(env)
        contaminated["MOSS_FAST_DEBUG_SOURCE_ROOTS"] = str(dep)
        stale = run([str(moss), "debug", str(app)], cwd=app, env=contaminated)
        combined = stale.stdout + stale.stderr
        if stale.returncode == 0 or "missing or stale" not in combined or "MALFORMED" in combined:
            raise AssertionError("Fast Debug read dependency source despite stale .mossi:\n" + combined)

    print("Phase 26.1.1 X2 selected main, filtered tests, package tests, current dependency snapshot, no-rustc, and source-boundary gates passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
