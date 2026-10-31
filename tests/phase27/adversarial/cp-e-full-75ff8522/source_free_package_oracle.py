#!/usr/bin/env python3
"""Check the two corpus-differing source-free consumers against package-built .mossi."""
import hashlib,json,os,pathlib,subprocess
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[3]
BASE=ROOT/"tmp/cp-e-full-baseline"
SOURCES=("sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss",
         "tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss")
MATRIX=HERE/"package-matrix-isolated.jsonl"
rows=[json.loads(x) for x in MATRIX.read_text().splitlines()]
def normalized(payload):
    if payload.get("error"):
        d=payload["error"]
        return [{k:d.get(k) for k in ("code","severity","message","line","column")}]
    return [{k:d.get(k) for k in ("code","severity","message","line","column")}
            for d in payload.get("result",{}).get("diagnostics",[])]
outputs=[]
for src in SOURCES:
  for revision,binary in (("baseline",BASE/"moss"),("candidate",ROOT/"moss")):
    for mode in ("off","shadow"):
      stage=ROOT/"tmp/cp-e-full-75ff8522/package-stages"/f"{revision}-{mode}"
      row=next(r for r in rows if r["revision"]==revision and r["mode"]==mode and r["root"]=="sdk/advanced-data-acceptance")
      dirs=[]
      for p in row["packages"]:
        base=p.get("source",{}).get("path")
        if not base:base=str(stage/"sdk/advanced-data-acceptance")
        artifact=pathlib.Path(base)/"build/check-debug"
        if any(artifact.glob("*.mossi")):dirs.append(str(artifact))
      env=os.environ.copy();env["MOSS_EST"]=mode;env["MOSS_MODULE_PATH"]=os.pathsep.join(sorted(set(dirs)))
      proc=subprocess.run([str(binary),"check",str(ROOT/src),"--json"],cwd=ROOT,env=env,
                          text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
      try:payload=json.loads(proc.stdout)
      except Exception:payload={"ok":False,"parse_error":proc.stdout}
      result={"source":src,"revision":revision,"mode":mode,"exit":proc.returncode,
        "verdict":payload.get("ok"),"diagnostics":normalized(payload),
        "module_interface_directories":sorted(set(dirs)),
        "module_interface_count":sum(len(list(pathlib.Path(d).glob("*.mossi"))) for d in set(dirs)),
        "stdout_sha256":hashlib.sha256(proc.stdout.encode()).hexdigest(),
        "stderr_sha256":hashlib.sha256(proc.stderr.encode()).hexdigest()}
      outputs.append(result)
      name=pathlib.Path(src).parent.name
      (HERE/f"source-free-{name}-{revision}-{mode}.json").write_text(json.dumps(payload,indent=2,sort_keys=True)+"\n")
(HERE/"source-free-package-oracle.json").write_text(json.dumps(outputs,indent=2,sort_keys=True)+"\n")
diffs=[]
for src in SOURCES:
  selected=[r for r in outputs if r["source"]==src]
  signatures={(r["verdict"],json.dumps(r["diagnostics"],sort_keys=True)) for r in selected}
  if len(signatures)>1:diffs.append(src)
print(json.dumps({"runs":len(outputs),"source_count":len(SOURCES),"four_way_diagnostic_differences":diffs,
                  "results":[{k:r[k] for k in ("source","revision","mode","verdict","diagnostics","module_interface_count")} for r in outputs]},sort_keys=True))
