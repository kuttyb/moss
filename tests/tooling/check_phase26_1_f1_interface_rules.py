#!/usr/bin/env python3
"""F1 regressions for source-backed .mossi freshness and package boundaries."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp/phase26-1-f1-interface-rules"


def run(argv, cwd: Path, env: dict[str, str], *, ok: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=180)
    assert (result.returncode == 0) == ok, (
        argv, result.returncode, result.stdout, result.stderr)
    return result


def manifest(name: str, dependencies: str = "") -> str:
    return ('[project]\nname = "' + name + '"\nversion = "0.1.0"\n' + dependencies)


def make_provider(root: Path, *, body: str = "  return 42\n") -> Path:
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "Moss.toml").write_text(manifest("f1_provider"))
    (root / "src/lib.moss").write_text(
        "module f1_lib\nexport fn answer() -> Int:\n" + body)
    return root


def make_consumer(root: Path, provider: Path) -> Path:
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "tests").mkdir(parents=True, exist_ok=True)
    relative = os.path.relpath(provider, root).replace(os.sep, "/")
    (root / "Moss.toml").write_text(manifest(
        "f1_consumer", f'\n[dependencies]\nf1_provider = {{ path = "{relative}" }}\n'))
    (root / "src/main.moss").write_text(
        "module f1_app\nimport f1_lib\nfn main():\n  echo f1_lib.answer()\n")
    (root / "tests/test.moss").write_text(
        'module f1_tests\nimport f1_lib\ntest "provider source closure":\n'
        "  assertEqual(f1_lib.answer(), 42)\n")
    return root


def base_env() -> dict[str, str]:
    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env.pop("MOSS_MODULE_PATH", None)
    env.pop("MOSS_FAST_DEBUG_SOURCE_ROOTS", None)
    env.pop("MOSS_SOURCE_ROOTS", None)
    env["MARGO_HOME"] = str(SCRATCH / "margo-home")
    return env


def check(consumer: Path, provider: Path, env: dict[str, str], *, ok: bool = True):
    env = dict(env)
    env["MOSS_MODULE_PATH"] = str(provider / "build/debug")
    return run([COMPILER, "check", consumer / "src/main.moss", "--json"],
               consumer, env, ok=ok)


def assert_interface_error(result, package: str,
                           code: str = "MOSS_INTERFACE_STALE"):
    payload = json.loads(result.stdout)
    assert payload["error"]["code"] == code, payload
    assert payload["error"]["message"] == (
        f"interface for {package} is missing or stale; run `margo build`"), payload


def main() -> int:
    if SCRATCH.exists():
        shutil.rmtree(SCRATCH)
    SCRATCH.mkdir(parents=True)
    env = base_env()
    provider = make_provider(SCRATCH / "provider")
    consumer = make_consumer(SCRATCH / "consumer", provider)
    run([MARGO, "build"], provider, env)
    interface = provider / "build/debug/f1_lib.mossi"
    assert interface.is_file(), interface
    assert "package_source_hash sha256:" in interface.read_text(), interface

    # Freshness applies to every interface consumer, including direct native
    # compilation outside Margo. A body edit is enough to stale the interface;
    # the compiler must reject it before using the old signature or artifact.
    source = provider / "src/lib.moss"
    original = source.read_text()
    source.write_text(original.replace("return 42", "return 43"))
    build_env = dict(env)
    build_env["MOSS_MODULE_PATH"] = str(provider / "build/debug")
    stale_build = run([COMPILER, "build", "--json"], consumer, build_env,
                      ok=False)
    assert_interface_error(stale_build, "f1_provider")
    source.write_text(original)

    # Hash-only validation rejects changed source before attempting to parse it.
    source.write_text("module f1_lib\nthis is intentionally invalid syntax\n")
    stale = check(consumer, provider, env, ok=False)
    assert_interface_error(stale, "f1_provider")
    source.write_text(original)
    run([MARGO, "build"], provider, env)

    # A body/comment-only rebuild advances freshness metadata without changing
    # the semantic interface identity used by dependents.
    before_lines = dict(line.split(" ", 1) for line in interface.read_text().splitlines()
                        if " " in line)
    source.write_text(original + "# freshness-only edit\n")
    run([MARGO, "build"], provider, env)
    after_lines = dict(line.split(" ", 1) for line in interface.read_text().splitlines()
                       if " " in line)
    assert after_lines["package_source_hash"] != before_lines["package_source_hash"]
    assert after_lines["interface_hash"] == before_lines["interface_hash"]
    source.write_text(original)
    run([MARGO, "build"], provider, env)

    # No interface is a targeted build instruction, even though source exists.
    interface.unlink()
    missing = check(consumer, provider, env, ok=False)
    assert_interface_error(missing, "f1_lib", "MOSS_COMPILE_ERROR")
    run([MARGO, "build"], provider, env)

    # moss check must ignore the Fast Debug source-root handoff and consume the
    # current interface. A nonexistent root would fail if check indexed it.
    check_env = dict(env)
    check_env["MOSS_FAST_DEBUG_SOURCE_ROOTS"] = str(SCRATCH / "not-a-package")
    passed = check(consumer, provider, check_env)
    assert json.loads(passed.stdout)["ok"] is True, passed.stdout

    # The query/compiler path must ignore dependency source-root overrides.
    # Use an invalid source provider for the same module: if any query loads
    # that .moss as semantic input, it fails parsing. The current .mossi remains
    # the only dependency semantic input, after its required freshness check.
    decoy = SCRATCH / "query-source-root-decoy"
    (decoy / "src").mkdir(parents=True)
    (decoy / "Moss.toml").write_text(manifest("f1_decoy"))
    (decoy / "src/lib.moss").write_text(
        "module f1_lib\nthis dependency source must not be parsed\n")
    query_env = dict(env)
    query_env["MOSS_MODULE_PATH"] = str(provider / "build/debug")
    query_env["MOSS_FAST_DEBUG_SOURCE_ROOTS"] = str(decoy)
    query_source = consumer / "src/main.moss"
    query_specs = (
        ("type", "main"),
        ("effects", "main"),
        ("why", "main"),
        ("complete", "at:4:16"),
    )
    for command, target in query_specs:
        args = [COMPILER, command, target, "--source", query_source, "--json"]
        unset_env = dict(query_env)
        unset_env.pop("MOSS_FAST_DEBUG_SOURCE_ROOTS", None)
        unset = run(args, consumer, unset_env)
        overridden = run(args, consumer, query_env)
        assert json.loads(unset.stdout)["ok"] is True, (command, unset.stdout)
        assert json.loads(overridden.stdout)["ok"] is True, (
            command, overridden.stdout, overridden.stderr)
        assert json.loads(unset.stdout) == json.loads(overridden.stdout), (
            command, unset.stdout, overridden.stdout)
    direct_debug = run([COMPILER, "debug", consumer], consumer, check_env, ok=False)
    assert "interface for f1_lib is missing or stale; run `margo build`" in (
        direct_debug.stdout + direct_debug.stderr), (
            direct_debug.stdout, direct_debug.stderr)

    # Margo refreshes dependency .mossi files before Fast Debug execution.
    debug = run([MARGO, "debug"], consumer, check_env)
    assert "42" in debug.stdout, (debug.stdout, debug.stderr)
    interp = run([MARGO, "test", "--interp"], consumer, check_env)
    assert "PASS" in (interp.stdout + interp.stderr).upper(), (
        interp.stdout, interp.stderr)

    # A package must not resolve an orphaned interface from its own build tree.
    own = SCRATCH / "own-output"
    (own / "src").mkdir(parents=True)
    (own / "Moss.toml").write_text(manifest("f1_own_output"))
    (own / "src/main.moss").write_text(
        "module f1_own_app\nimport f1_ghost\nfn main():\n  echo f1_ghost.answer()\n")
    ghost = make_provider(SCRATCH / "ghost-provider")
    (ghost / "Moss.toml").write_text(manifest("f1_ghost_package"))
    (ghost / "src/lib.moss").write_text(
        "module f1_ghost\nexport fn answer() -> Int:\n  return 99\n")
    run([MARGO, "build"], ghost, env)
    own_build = own / "build/debug"
    own_build.mkdir(parents=True)
    shutil.copy2(ghost / "build/debug/f1_ghost.mossi", own_build / "f1_ghost.mossi")
    shutil.copy2(ghost / "build/debug/libf1_ghost.rlib", own_build / "libf1_ghost.rlib")
    own_env = dict(env)
    # Even an overly broad module root must not surface this package's build output.
    own_env["MOSS_MODULE_PATH"] = str(own)
    orphan = run([COMPILER, "check", own / "src/main.moss", "--json"],
                 own, own_env, ok=False)
    assert_interface_error(orphan, "f1_ghost", "MOSS_COMPILE_ERROR")

    # Vendored/path dependencies remain discoverable below the consumer root.
    nested_consumer_root = SCRATCH / "nested" / "application"
    nested_provider = make_provider(nested_consumer_root / "vendor/provider")
    nested_consumer = make_consumer(nested_consumer_root, nested_provider)
    run([MARGO, "build"], nested_provider, env)
    nested_check = check(nested_consumer, nested_provider, env)
    assert json.loads(nested_check.stdout)["ok"] is True, nested_check.stdout

    # Checking one package file retains its local import closure rather than
    # semantically checking unrelated sibling modules. This also guards
    # against using the raw project source list for `moss check <file>`.
    scope = SCRATCH / "single-file-scope"
    (scope / "src").mkdir(parents=True)
    (scope / "Moss.toml").write_text(manifest("f1_single_file_scope"))
    requested = scope / "src/library.moss"
    requested.write_text(
        "module f1_library\nexport fn answer() -> Int:\n  return 42\n")
    (scope / "src/main.moss").write_text(
        "module f1_scope_app\nimport f1_library\nfn main():\n"
        "  echo f1_library.missing()\n")
    scoped = run([COMPILER, "check", requested, "--json"], scope, env)
    assert json.loads(scoped.stdout)["ok"] is True, scoped.stdout

    # Staged ABI-v10 serialization also carries out-of-band freshness metadata.
    v10_env = dict(env)
    v10_env["MOSS_STAGED_ABI"] = "v10"
    v10_provider = make_provider(SCRATCH / "v10-provider")
    v10_consumer = make_consumer(SCRATCH / "v10-consumer", v10_provider)
    run([MARGO, "build"], v10_provider, v10_env)
    v10_interface = v10_provider / "build/debug/f1_lib.mossi"
    assert "moss-module-interface-v3\n" in v10_interface.read_text()
    assert "package_source_hash sha256:" in v10_interface.read_text()
    v10_check = check(v10_consumer, v10_provider, v10_env)
    assert json.loads(v10_check.stdout)["ok"] is True, v10_check.stdout
    v10_before = dict(line.split(" ", 1) for line in v10_interface.read_text().splitlines()
                      if " " in line)
    v10_source = v10_provider / "src/lib.moss"
    v10_original = v10_source.read_text()
    v10_source.write_text(v10_original + "# freshness-only edit\n")
    run([MARGO, "build"], v10_provider, v10_env)
    v10_after = dict(line.split(" ", 1) for line in v10_interface.read_text().splitlines()
                     if " " in line)
    assert v10_after["package_source_hash"] != v10_before["package_source_hash"]
    assert v10_after["semantic_contract_hash"] == v10_before["semantic_contract_hash"]
    v10_source.write_text(v10_original)
    run([MARGO, "build"], v10_provider, v10_env)
    v10_source.write_text(v10_original + "# stale source\n")
    v10_stale = check(v10_consumer, v10_provider, v10_env, ok=False)
    assert_interface_error(v10_stale, "f1_provider")

    print("Phase 26.1 F1 .mossi staleness, query source-root isolation, "
          "Fast Debug source-root, own-output, and missing-interface checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
