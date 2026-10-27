#!/usr/bin/env python3
"""C1 probes for file-reachable checks and package-only diagnostics.

Run with the compiler-under-test as argv[1]. Fixtures are generated beneath
tmp/ so the test leaves no source artifacts in the checkout.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp/phase26-1-c1-scope"


def run(argv: list[object], cwd: Path, env: dict[str, str], *, ok: bool):
    result = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env,
                            text=True, capture_output=True, timeout=180)
    assert (result.returncode == 0) == ok, (
        argv, result.returncode, result.stdout, result.stderr)
    return result


def new_package(name: str) -> tuple[Path, Path, dict[str, str]]:
    root = SCRATCH / name
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env["MARGO_HOME"] = str(SCRATCH / "margo-home")
    for key in ("MOSS_MODULE_PATH", "MOSS_FAST_DEBUG_SOURCE_ROOTS",
                "MOSS_SOURCE_ROOTS"):
        env.pop(key, None)
    return root, root / "src/main.moss", env


def check_file(source: Path, env: dict[str, str], *, ok: bool):
    return run([COMPILER, "check", source, "--json"], source.parent.parent,
               env, ok=ok)


def check_package(root: Path, env: dict[str, str], *, ok: bool):
    check = run([MARGO, "build", "--check"], root, env, ok=ok)
    # The package-wide semantic rules remain active in native build mode too.
    native = run([MARGO, "build"], root, env, ok=ok)
    return check, native


def main() -> int:
    if SCRATCH.exists():
        shutil.rmtree(SCRATCH)
    SCRATCH.mkdir(parents=True)

    # File mode includes bodies reached from the requested file, but a broken
    # sibling helper remains outside that closure. Package mode still catches it.
    root, entry, env = new_package("c1_unreachable_body")
    entry.write_text(
        "module c1_unreachable_body\n"
        "fn main():\n  echo good()\n"
        "fn good() -> Int:\n  return 42\n")
    (root / "src/helpers.moss").write_text(
        "module c1_unreachable_body\n"
        "fn unrelated() -> Int:\n  return absent()\n")
    check_file(entry, env, ok=True)
    assert not (root / "build").exists(), "moss check must not emit Rust or create build outputs"
    assert not list(root.rglob("*.rs")), "moss check unexpectedly emitted generated Rust"
    check_package(root, env, ok=False)

    # The same error moves into the closure when a function body is reachable.
    entry.write_text(
        "module c1_reachable_body\n"
        "fn main():\n  echo broken()\n")
    (root / "Moss.toml").write_text(
        '[package]\nname = "c1_reachable_body"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (root / "src/helpers.moss").write_text(
        "module c1_reachable_body\n"
        "fn broken() -> Int:\n  return absent()\n")
    result = check_file(entry, env, ok=False)
    assert "absent" in result.stdout + result.stderr, (
        result.stdout, result.stderr)

    # Name mentions are not call edges. A local value called `broken` and a
    # string containing `read()` must not select same-named sibling callables.
    # Unrelated object and enum declaration errors are also package-only for
    # this file check, while both package build modes must still diagnose them.
    root, entry, env = new_package("c1_non_call_mentions")
    entry.write_text(
        "module c1_non_call_mentions\n"
        "fn main():\n"
        "  broken = 1\n"
        "  echo broken\n"
        '  method_text = "read()"\n'
        "  echo method_text\n")
    (root / "src/unrelated.moss").write_text(
        "module c1_non_call_mentions\n"
        "fn broken() -> Int:\n  return absent()\n"
        "type UnusedObject:\n  value: MissingObjectType\n"
        "enum UnusedEnum:\n  Value(item: MissingEnumPayload)\n"
        "type UnusedMethod:\n  value: Int\n"
        "  fn read() -> Int:\n    return absent\n")
    check_file(entry, env, ok=True)
    check_package(root, env, ok=False)

    # Duplicate function names are package-wide. A file check with no edge to
    # either duplicate stays usable; both package build modes reject the package.
    root, entry, env = new_package("c1_duplicate_functions")
    entry.write_text("module c1_duplicate_functions\nfn main():\n  pass\n")
    (root / "src/left.moss").write_text(
        "module c1_duplicate_functions\nfn clash() -> Int:\n  return 1\n")
    (root / "src/right.moss").write_text(
        "module c1_duplicate_functions\nfn clash() -> Int:\n  return 2\n")
    check_file(entry, env, ok=True)
    check_package(root, env, ok=False)

    # Method reachability is conservative by method name: a standalone member
    # call checks all same-named methods. `message_text` must not make this
    # look like a domain message and suppress the method edge.
    root, entry, env = new_package("c1_same_named_methods")
    entry.write_text(
        "module c1_same_named_methods\n"
        "type One:\n  value: Int\n"
        "  fn read() -> Int:\n    return value\n"
        "fn main():\n  message_text = One(value = 1)\n  message_text.read()\n")
    (root / "src/other.moss").write_text(
        "module c1_same_named_methods\n"
        "type Two:\n  value: Int\n"
        "  fn read() -> Int:\n    return absent\n")
    result = check_file(entry, env, ok=False)
    assert "absent" in result.stdout + result.stderr, (
        result.stdout, result.stderr)

    # Declarations physically present in the requested file are in scope even
    # when no function references them.
    root, entry, env = new_package("c1_root_declarations")
    entry.write_text(
        "module c1_root_declarations\n"
        "type RootObject:\n  value: MissingRootFieldType\n"
        "fn main():\n  pass\n")
    result = check_file(entry, env, ok=False)
    assert "MissingRootFieldType" in result.stdout + result.stderr, (
        result.stdout, result.stderr)
    entry.write_text(
        "module c1_root_declarations\n"
        "enum RootEnum:\n  Value(item: MissingRootEnumType)\n"
        "fn main():\n  pass\n")
    result = check_file(entry, env, ok=False)
    assert "MissingRootEnumType" in result.stdout + result.stderr, (
        result.stdout, result.stderr)

    # Trait coherence and domain-route conflicts are package analyses. Keep
    # them in sibling source so file mode can omit them, while package mode
    # must still reject. The duplicate trait method is a coherence conflict;
    # the duplicated route name exercises route namespace validation.
    root, entry, env = new_package("c1_package_rules")
    entry.write_text("module c1_package_rules\nfn main():\n  pass\n")
    (root / "src/coherence.moss").write_text(
        "module c1_package_rules\n"
        "trait Repeated:\n"
        "  fn score() -> Int\n"
        "  fn score() -> Int\n")
    (root / "src/routes.moss").write_text(
        "module c1_package_rules\n"
        "domain Worker:\n  fn run():\n    pass\n"
        "domain Owner:\n"
        "  domainroutes(worker: Worker, worker: Worker)\n"
        "  fn run():\n    pass\n")
    check_file(entry, env, ok=True)
    check_package(root, env, ok=False)

    # `margo build --check` validates the complete Tests target without
    # executing tests or emitting Rust, and includes test sources in its fingerprint.
    root, entry, env = new_package("c1_test_source_check")
    entry.write_text("module c1_test_source_check\nfn main():\n  pass\n")
    test_source = root / "tests" / "broken_test.moss"
    test_source.parent.mkdir(parents=True)
    test_source.write_text(
        "module broken_test\nfn broken() -> Int:\n  return absent()\n")
    package_check = run([MARGO, "build", "--check"], root, env, ok=False)
    assert str(test_source) in package_check.stdout + package_check.stderr, (
        package_check.stdout, package_check.stderr)
    assert "absent" in package_check.stdout + package_check.stderr, (
        package_check.stdout, package_check.stderr)

    # Whole-target test mode retains package-wide duplicate declarations
    # across separate test files; file-by-file scoped checks cannot prove this.
    root, entry, env = new_package("c1_test_target_duplicates")
    entry.write_text("module c1_test_target_duplicates\nfn main():\n  pass\n")
    tests = root / "tests"
    tests.mkdir()
    for name, value in (("left", 1), ("right", 2)):
        (tests / f"{name}.moss").write_text(
            "module c1_test_target_duplicates\n"
            f"fn shared() -> Int:\n  return {value}\n")
    duplicate_check = run([MARGO, "build", "--check"], root, env, ok=False)
    duplicate_output = duplicate_check.stdout + duplicate_check.stderr
    assert "shared" in duplicate_output, duplicate_output

    root, entry, env = new_package("c1_test_target_clean")
    entry.write_text("module c1_test_target_clean\nfn main():\n  pass\n")
    tests = root / "tests"
    tests.mkdir()
    (tests / "ok.moss").write_text(
        'module c1_test_target_clean\ntest "works":\n  assert(true)\n')
    semantic_check = run([MARGO, "build", "--check"], root, env, ok=True)
    assert "Checked 1 package" in semantic_check.stdout, semantic_check.stdout
    assert not list(root.rglob("*.rs")), "semantic test-target check emitted Rust"

    print("Phase 26.1 C1 reachability and package-rule probes passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
