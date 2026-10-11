#!/usr/bin/env python3
"""Permanent reconstructed P21-EXT-03/04 counterexamples."""

from pathlib import Path
import json
import re
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
out = repo / "tmp/phase21-peer-effects"
out.mkdir(parents=True, exist_ok=True)
effects_source = repo / "tests/tooling/fixtures/phase21_peer_effects.moss"
spec_source = repo / "tests/tooling/fixtures/phase21_peer_speculation.moss"
rustc = shutil.which("rustc")
assert rustc, "rustc required for native effect regression"


def run(*args, expected=0):
    result = subprocess.run([str(a) for a in args], cwd=repo, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


def query(source, name):
    doc = json.loads(run(compiler, "effects", name, "--source", source,
                         "--json").stdout)
    assert doc["ok"], doc
    return doc["result"]["observable_effects"]


assert json.loads(run(compiler, "check", effects_source, "--json").stdout)["ok"]
for name in ("grouped", "listed", "method_exit", "duck_call", "pipeline_duck",
             "mapped", "filtered", "any_checked", "all_checked"):
    effects = query(effects_source, name)
    assert effects["raise_set"] == ["enum:EffectFault:Bad"], (name, effects)
    assert not effects["unresolved"], (name, effects)
root_facts = query(effects_source, "EffectDomain.Run")
assert root_facts["raise_set"] == ["enum:EffectFault:Bad"], root_facts

# The unspecialized helper remains unresolved; its concrete caller must carry
# the exact variant. The negative program checks that main totality sees it.
negative = out / "unhandled_duck.moss"
negative.write_text(effects_source.read_text().split("fn main():")[0] +
                    "fn main():\n  echo duck_call()\n")
doc = json.loads(run(compiler, "check", negative, "--json", expected=1).stdout)
assert not doc["ok"] and "main may not let typed errors escape" in doc["error"]["message"], doc

rust = out / "effects.rs"
exe = out / "effects"
run(compiler, effects_source, "-o", rust)
run(rustc, "--edition=2021", "-Dwarnings", rust, "-o", exe)
expected = "grouped\nlisted\nmethod\nduck\npipeline\nmap\nfilter\nany\nall\n7\n"
assert run(exe).stdout == expected
assert run(compiler, "run", "--interp", effects_source).stdout == expected
optimized = out / "effects-optimized"
run(rustc, "--edition=2021", "-Dwarnings", "-O", rust, "-o", optimized)
assert run(optimized).stdout == expected

# The first 4096-byte chunk raises Early. The one-byte suffix would panic in
# either mapper if a later speculative Branch ran before that ordered raise.
data = repo / "tmp/phase21-peer-speculation.data"
data.write_bytes(bytes([0]) + bytes([42]) * 4095 + bytes([1]))
assert json.loads(run(compiler, "check", spec_source, "--json").stdout)["ok"]
for name in ("paren_panic", "case_shadow", "for_shadow"):
    facts = query(spec_source, name)
    assert facts["may_panic"] and facts["raise_set"] == ["enum:MapFault:Early"], (name, facts)
spec_rust = out / "speculation.rs"
ir = run(compiler, spec_source, "--dump-functional-ir", "-o", spec_rust).stdout
for name in ("paren_panic", "case_shadow", "for_shadow"):
    assert re.search(r"callable=" + name + r"[\s\S]*?chunk-plan:[^\n]*eligible=no", ir), (name, ir)
spec_exe = out / "speculation"
run(rustc, "--edition=2021", "-Dwarnings", spec_rust, "-o", spec_exe)
assert run(spec_exe).stdout == "paren early\ncase early\nfor early\n"
spec_optimized = out / "speculation-optimized"
run(rustc, "--edition=2021", "-Dwarnings", "-O", spec_rust,
    "-o", spec_optimized)
assert run(spec_optimized).stdout == "paren early\ncase early\nfor early\n"

print("Phase 21 peer effect and speculative panic probes passed.")
