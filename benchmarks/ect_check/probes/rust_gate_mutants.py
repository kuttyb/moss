#!/usr/bin/env python3
"""Mutation check of the Rust gate selftests (Team 5 review). Copies the suite under tmp/mut/,
breaks rust_cmp.py in one way per mutant (drop spans, drop message, drop the byte-identical
revert check, ignore the exit code, drop only the line number) and runs the Rust selftests
(selftest.rust_tests + rust_gate_strength_tests): every mutant must make at least one fail,
and none may crash. Run from the repository root; output: probes/rust_gate_mutants.txt."""
import shutil, subprocess, sys
from pathlib import Path

root = Path("tmp/mut")
src = Path("benchmarks/ect_check")
muts = {
    "M1_drop_spans": ('"spans": [{"file": _norm(sp["file_name"], repl), "line": sp["line_start"],\n                                     "column": sp["column_start"], "primary": sp["is_primary"]}\n                                    for sp in m.get("spans", [])],', '"spans": [],'),
    "M2_drop_message": ('"message": _norm(m.get("message", ""), repl),', '"message": "",'),
    "M3_drop_revert_check": ('            if C.tree_sha256(work) != fx["sha256"]:\n                raise C.ToolError(f"scenario {sc[\'id\']}: tree after reverting {step[\'patch\']} is not "\n                                  "byte-identical to the frozen fixture")\n        else:\n            raise C.ToolError(f"scenario {sc[\'id\']}: unknown step op {op!r}")\n    return results\n\n\ndef measure_once', '        else:\n            raise C.ToolError(f"scenario {sc[\'id\']}: unknown step op {op!r}")\n    return results\n\n\ndef measure_once'),
    "M5_ignore_exit_code": ('return {"exit_code": v["exit_code"], "ok": v["ok"], "document": v["document"]}', 'return {"ok": v["ok"], "document": v["document"]}'),
    "M6_drop_line_only": ('"line": sp["line_start"],', '"line": 0,'),
}
RUNNER = '''
import sys, shutil
sys.path.insert(0, "benchmarks/ect_check")
import ect_common as C, selftest, run as R
results = []
manifest = C.load_manifest()
scratch = C.TMP_ROOT / "mutselftest"
shutil.rmtree(scratch, ignore_errors=True); scratch.mkdir(parents=True)
selftest.rust_tests(R, None, manifest, scratch, results)
selftest.rust_gate_strength_tests(R, manifest, scratch, results)
print("RESULT", sum(results), "/", len(results))
sys.exit(0 if all(results) else 1)
'''
bad = False
for name, (old, new) in [("M0_unmutated", (None, None))] + list(muts.items()):
    d = root / name
    shutil.rmtree(d, ignore_errors=True)
    (d / "benchmarks").mkdir(parents=True)
    shutil.copytree(src, d / "benchmarks" / "ect_check", ignore=shutil.ignore_patterns("results", "__pycache__"))
    if old:
        p = d / "benchmarks/ect_check/rust_cmp.py"
        t = p.read_text()
        assert t.count(old) == 1, name
        p.write_text(t.replace(old, new))
    (d / "runner.py").write_text(RUNNER)
    out = subprocess.run([sys.executable, "runner.py"], cwd=d, capture_output=True, text=True)
    fails = [l[:100] for l in out.stdout.splitlines() if l.startswith("[FAIL]")]
    res = [l for l in out.stdout.splitlines() if l.startswith("RESULT")]
    verdict = ("clean" if name == "M0_unmutated" and out.returncode == 0 else
               "CAUGHT" if out.returncode == 1 and name != "M0_unmutated" else "UNEXPECTED")
    bad |= verdict == "UNEXPECTED"
    print(f"{name}: {verdict} rc={out.returncode} {res} {fails[:2]}")
    shutil.rmtree(d, ignore_errors=True)
sys.exit(1 if bad else 0)
