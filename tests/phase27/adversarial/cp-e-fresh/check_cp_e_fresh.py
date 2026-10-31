#!/usr/bin/env python3
"""Paired CP-E probes against Phase 27.2 and the frozen CP-E tree."""
import json, os, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parents[4]
CANDIDATE = ROOT / "build/compiler/moss-dev"
BASELINE = ROOT / "tmp/cp-e-base/build/compiler/moss-dev"
CASES = {
  "parenthesized_enum_constructor": '''enum Choice:\n  Value(value: Int)\n  Empty\nfn unwrap(choice) -> Int:\n  match choice:\n    case Value(value):\n      return value\n    case Empty:\n      return 0\nfn main():\n  result = unwrap((Choice.Value(value: 7)))\n  echo result\n''',
  "dotted_string_member_path": '''fn main():\n  text = "AbC"\n  n = text.ascii_lower().length()\n  assertEqual(n, 3)\n''',
  "invalid_dotted_member_path": '''fn main():\n  text = "AbC"\n  n = text.ascii_lower().not_a_method()\n  echo n\n''',
  "invalid_if_expression": '''fn main():\n  x = if 1 == 1: 1 else: 2\n  echo 0\n''',
  "checked_closure_unreachable": '''fn dormant():\n  echo "dormant"\nfn main():\n  echo "closure-ok"\n''',
  "checked_closure_reachable": '''fn dormant():\n  echo "dormant"\nfn main():\n  dormant()\n''',
}

def run(binary, mode, name, source):
  path = ROOT / "tmp" / f"cp_e_{name}.moss"
  path.write_text(source)
  env = dict(os.environ, MOSS_EST=mode)
  p = subprocess.run([str(binary), "check", "--json", str(path)], cwd=ROOT,
                     env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  check = {"status": p.returncode, "stdout": p.stdout, "stderr": p.stderr}
  # Run only valid programs via deterministic interpreter to compare checked closure.
  execution = None
  if name in ("parenthesized_enum_constructor", "dotted_string_member_path",
              "checked_closure_unreachable", "checked_closure_reachable"):
    q = subprocess.run([str(binary), "run", "--interp", str(path)], cwd=ROOT,
                       env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    execution = {"status": q.returncode, "stdout": q.stdout, "stderr": q.stderr}
  return {"check": check, "execution": execution}

if not CANDIDATE.exists() or not BASELINE.exists():
  raise SystemExit("missing built candidate or baseline compiler")
results = {}
for name, source in CASES.items():
  results[name] = {"baseline": run(BASELINE, "off", name, source),
                   "candidate_off": run(CANDIDATE, "off", name, source),
                   "candidate_shadow": run(CANDIDATE, "shadow", name, source)}
# Verdict/diagnostic parity and candidate mode parity are exact for check status,
# structured diagnostic code sequences, and execution status/output.
def diag_codes(result):
  try:
    root=json.loads(result["stdout"])
    error=root.get("error")
    if error:
      return [(error.get("code"), error.get("message"), error.get("line"))]
    return [(d.get("code"), d.get("message"), d.get("line"))
            for d in root.get("result", {}).get("diagnostics", [])]
  except Exception:
    return [("unparseable", result["stdout"], None)]
for name, triple in results.items():
  keys=("baseline", "candidate_off", "candidate_shadow")
  signatures=[]
  for key in keys:
    value=triple[key]
    signatures.append((value["check"]["status"], tuple(diag_codes(value["check"])),
       None if value["execution"] is None else
       (value["execution"]["status"],value["execution"]["stdout"],value["execution"]["stderr"])))
  if signatures[0] != signatures[1] or signatures[1] != signatures[2]:
    print("PARITY DIFFERENCE", name, signatures, file=sys.stderr)
print(json.dumps(results, indent=2))
if any(results[name]["candidate_shadow"]["check"]["status"] < 0 or
       (results[name]["candidate_shadow"]["execution"] and
        results[name]["candidate_shadow"]["execution"]["status"] < 0)
       for name in results):
  print("BLOCKER: shadow assertion failure captured; see parenthesized_enum_constructor")
  sys.exit(1)
print("PASS paired phase27-2-m1/frozen EST-off/shadow checks and checked-closure probes")
