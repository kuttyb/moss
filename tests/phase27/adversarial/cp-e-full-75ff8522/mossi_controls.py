#!/usr/bin/env python3
"""Run the existing source-free .mossi package controls on both compilers."""
import hashlib,json,os,pathlib,subprocess,sys
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[3]
BASE=pathlib.Path(sys.argv[1]).resolve()
rows=[]
for revision,compiler in (("baseline",BASE/"moss"),("candidate",ROOT/"moss")):
  for scenario in (8,9):
    for mode in ("off","shadow"):
      env=os.environ.copy();env["MOSS"]=str(compiler);env["MOSS_EST"]=mode
      env["MARGO_HOME"]=str(ROOT/"tmp/cp-e-full-75ff8522/mossi-margo-home")
      env["TMPDIR"]=str(ROOT/"tmp")
      p=subprocess.run([sys.executable,str(ROOT/"tests/phase25/integration/cross_crate_harness.py"),
                        "--scenario",str(scenario),"--json"],cwd=ROOT,env=env,
                       text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
      try: payload=json.loads(p.stdout)
      except Exception: payload=None
      row={"revision":revision,"mode":mode,"scenario":scenario,"exit":p.returncode,
           "summary":payload.get("summary") if payload else None,
           "results":[{"id":r.get("scenario_id"),"status":r.get("status"),
                       "diagnostic":r.get("details",{}).get("diagnostic")}
                      for r in payload.get("results",[])] if payload else [],
           "stdout_sha256":hashlib.sha256(p.stdout.encode()).hexdigest(),
           "stderr":p.stderr}
      rows.append(row)
      (HERE/f"mossi-{revision}-{mode}-scenario-{scenario}.json").write_text(
         json.dumps(payload if payload is not None else row,indent=2,sort_keys=True)+"\n")
      if p.returncode: print(p.stderr,file=sys.stderr)
(HERE/"mossi-controls-summary.json").write_text(json.dumps(rows,indent=2,sort_keys=True)+"\n")
print(json.dumps([{k:r[k] for k in ("revision","mode","scenario","exit","summary","results")} for r in rows],sort_keys=True))
