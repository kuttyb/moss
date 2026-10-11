#!/usr/bin/env python3
"""External peer review regressions for typed test and recovery routing."""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
margo = compiler.with_name("margo")
scratch = repo / "tmp/phase21_peer_recovery"
scratch.mkdir(parents=True, exist_ok=True)
fixtures = repo / "tests/tooling/fixtures"


def run(*args, cwd=repo):
    return subprocess.run([str(item) for item in args], cwd=cwd,
                          text=True, capture_output=True, timeout=120)


for name, function in (("re_raise", "relay"),
                       ("re_raise_exact", "relay"),
                       ("raise_enum_value", "forward"),
                       ("recover_binding", "inspect_error"),
                       ("grouped_reraise", "regroup")):
    source = fixtures / f"phase21_peer_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    assert json.loads(checked.stdout)["ok"], checked.stdout
    effects = run(compiler, "effects", f"fn:{function}", "--source", source,
                  "--json")
    assert effects.returncode == 0, (name, effects.stdout, effects.stderr)
    raised = json.loads(effects.stdout)["result"]["observable_effects"]["raise_set"]
    expected_raises = ([], ["enum:Failure:First"],
                       ["enum:Failure:First", "enum:Failure:Second"])
    assert raised == (expected_raises[0] if name == "recover_binding" else
                      expected_raises[1] if name == "re_raise_exact" else
                      expected_raises[2]), (name, raised)
    interpreted = run(compiler, "run", "--interp", source)
    assert interpreted.returncode == 0, (name, interpreted.stdout,
                                          interpreted.stderr)
    expected_stdout = ("first\n" if name == "re_raise_exact" else
                       "first\nsecond\n")
    assert interpreted.stdout == expected_stdout, (name, interpreted.stdout)
    native = scratch / f"{name}.rs"
    binary = scratch / name
    generated = run(compiler, source, "-o", native)
    assert generated.returncode == 0, (name, generated.stdout,
                                        generated.stderr)
    compiled = run("rustc", "--edition=2021", "-D", "warnings", native,
                   "-o", binary)
    assert compiled.returncode == 0, (name, compiled.stdout, compiled.stderr)
    executed = run(binary)
    assert executed.returncode == 0, (name, executed.stdout, executed.stderr)
    assert executed.stdout == interpreted.stdout, (name, executed.stdout)


for name, expected in (("keyword_prefix", "3\n"),
                       ("on_fail_parity", "first 7\nsecond 9\n")):
    source = fixtures / f"phase21_peer_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    interpreted = run(compiler, "run", "--interp", source)
    assert interpreted.returncode == 0, (name, interpreted.stdout,
                                          interpreted.stderr)
    assert interpreted.stdout == expected, (name, interpreted.stdout)
    native = scratch / f"{name}.rs"
    binary = scratch / name
    generated = run(compiler, source, "-o", native)
    assert generated.returncode == 0, (name, generated.stdout,
                                        generated.stderr)
    compiled = run("rustc", "--edition=2021", "-D", "warnings", native,
                   "-o", binary)
    assert compiled.returncode == 0, (name, compiled.stdout, compiled.stderr)
    executed = run(binary)
    assert executed.returncode == 0, (name, executed.stdout, executed.stderr)
    assert executed.stdout == expected, (name, executed.stdout)


source = fixtures / "phase21_peer_raise_scalar_call.moss"
checked = run(compiler, "check", source, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
effects = run(compiler, "effects", "fn:forward", "--source", source,
              "--json")
assert effects.returncode == 0, (effects.stdout, effects.stderr)
assert json.loads(effects.stdout)["result"]["observable_effects"]["raise_set"] == [
    "enum:Failure:Bad", "scalar:int"]
interpreted = run(compiler, "run", "--interp", source)
assert interpreted.returncode == 0, (interpreted.stdout, interpreted.stderr)
assert interpreted.stdout == "bad\n7\n", interpreted.stdout
native = scratch / "raise_scalar_call.rs"
binary = scratch / "raise_scalar_call"
generated = run(compiler, source, "-o", native)
assert generated.returncode == 0, (generated.stdout, generated.stderr)
compiled = run("rustc", "--edition=2021", "-D", "warnings", native,
               "-o", binary)
assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)
executed = run(binary)
assert executed.returncode == 0, (executed.stdout, executed.stderr)
assert executed.stdout == interpreted.stdout, executed.stdout


test_source = fixtures / "phase21_unhandled_interpreter_test.moss"
checked = run(compiler, "check", test_source, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
interpreted = run(compiler, "test", "--interp", test_source)
assert interpreted.returncode == 1, (interpreted.stdout, interpreted.stderr)
assert "PASS subsequent test still runs" in interpreted.stdout
assert "1 passed, 1 failed" in interpreted.stdout

package = scratch / "test_package"
(package / "src").mkdir(parents=True, exist_ok=True)
(package / "src/main.moss").write_text(test_source.read_text())
(package / "Moss.toml").write_text(
    '[project]\nname = "phase21_peer_test_raise"\nversion = "0.1.0"\n'
    '[build]\nsource = "src"\n')
native_tests = run(margo, "test", "--json", cwd=package)
assert native_tests.returncode == 1, (native_tests.stdout,
                                      native_tests.stderr)
result = json.loads(native_tests.stdout)["result"]
assert result["summary"] == {"passed": 1, "failed": 1, "total": 2}, result
by_name = {test["name"]: test for test in result["tests"]}
assert by_name["subsequent test still runs"]["status"] == "pass", result
failed = by_name["unhandled typed raise"]
assert failed["status"] == "fail", result
assert failed["diagnostic"]["message"] == (
    "unhandled typed raise enum:Failure:Bad"), result

print("Phase 21 peer recovery native and Fast Debug regressions passed.")
