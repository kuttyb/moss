#!/usr/bin/env python3
"""Minimize the five frozen tree_split_binary shadow crashes by expression."""
import json,os,pathlib,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[4]
COMPILER=pathlib.Path(sys.argv[1]).resolve()
OUT=ROOT/"tests/phase27/adversarial/cp-f-phase27-3/tree-split-fragments.json"
CASES={
 "pipe-count-comparison": "fn main():\n  v = [1, 2]\n  if v |> count > 0:\n    echo \\\"y\\\"\n",
 "while-vector-count": "fn main():\n  tasks = [1, 2]\n  var index = 0\n  while index < tasks |> count:\n    index = index + 1\n",
 "while-vector-count-typed": "fn total(values: Vector[Int]) -> Int:\n  var index = 0\n  while index < values |> count:\n    index = index + 1\n  return index\nfn main():\n  echo total([3, 4])\n",
 "pipeline-comparison-stage": "fn has_large_shift(energies: Vector[Int]):\n  return energies |> filter(_ >= 100) |> sum >= 100\nfn main():\n  echo has_large_shift([100])\n"
}
results=[]
for name,text in CASES.items():
 with tempfile.TemporaryDirectory(prefix="cpf-fragment-",dir=ROOT/"tmp/cpf-phase27-3/evidence") as td:
  src=pathlib.Path(td)/f"{name}.moss"; src.write_text(text)
  env=os.environ.copy();env["MOSS_EST"]="shadow"
  p=subprocess.run([str(COMPILER),"check",str(src),"--json"],cwd=ROOT,env=env,text=True,capture_output=True,timeout=30)
  try: payload=json.loads(p.stdout)
  except json.JSONDecodeError: payload=None
  results.append({"probe":name,"source":text,"exit":p.returncode,"payload":payload,"stdout":p.stdout,"stderr":p.stderr})
OUT.write_text(json.dumps({"candidate_sha256":__import__("hashlib").sha256(COMPILER.read_bytes()).hexdigest(),"results":results},indent=2)+"\n")
print(json.dumps([{k:r.get(k) for k in ("probe","exit","stderr")} for r in results],indent=2))
