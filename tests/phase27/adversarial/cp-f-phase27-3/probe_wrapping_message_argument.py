#!/usr/bin/env python3
"""Probe wrapping Int arithmetic in a generated message argument."""
import hashlib, json, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[4]
SOURCE=ROOT/"tests/phase27/adversarial/cp-f-phase27-3/wrapping-message-argument.moss"
BASELINE=pathlib.Path(sys.argv[1]).resolve(); CANDIDATE=pathlib.Path(sys.argv[2]).resolve()
OUT=ROOT/"tests/phase27/adversarial/cp-f-phase27-3/wrapping-message-argument.json"
ARTIFACTS=ROOT/"tests/phase27/adversarial/cp-f-phase27-3/wrapping-rust"
ARTIFACTS.mkdir(parents=True,exist_ok=True)
def run(argv,env):
 p=subprocess.run(argv,cwd=ROOT,env=env,text=True,capture_output=True,timeout=90)
 return {"command":argv,"exit":p.returncode,"stdout":p.stdout,"stderr":p.stderr}
rows={}
for label,compiler in (("baseline",BASELINE),("candidate_off",CANDIDATE),("candidate_shadow",CANDIDATE)):
 mode="shadow" if label.endswith("shadow") else "off"
 env=os.environ.copy(); env["MOSS_EST"]=mode
 rows[label]={"check":run([str(compiler),"check",str(SOURCE),"--json"],env),"fast_debug":run([str(compiler),"run","--interp",str(SOURCE)],env)}
 for profile in ("default","optimized"):
  rust=ARTIFACTS/f"{label}-{profile}.rs"; binary=ARTIFACTS/f"{label}-{profile}.bin"
  emit=[str(compiler)]+(["-O"] if profile=="optimized" else [])+[str(SOURCE),"-o",str(rust)]
  emitted=run(emit,env)
  item={"emit":emitted,"rust_sha256":hashlib.sha256(rust.read_bytes()).hexdigest() if rust.exists() else None}
  if rust.exists():
   compiled=run(["rustc","--edition=2021",str(rust),"-o",str(binary)],env); item["rustc"]=compiled
   if compiled["exit"]==0: item["native"]=run([str(binary)],env)
  rows[label][profile]=item
result={"source":"tests/phase27/adversarial/cp-f-phase27-3/wrapping-message-argument.moss","sha256":hashlib.sha256(SOURCE.read_bytes()).hexdigest(),"baseline_compiler_sha256":hashlib.sha256(BASELINE.read_bytes()).hexdigest(),"candidate_compiler_sha256":hashlib.sha256(CANDIDATE.read_bytes()).hexdigest(),"results":rows}
OUT.write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
print(json.dumps({k:v for k,v in result.items() if k!="results"},indent=2))
for label,items in rows.items():
 print(label,"Fast Debug",items["fast_debug"]["exit"],repr(items["fast_debug"]["stdout"]),repr(items["fast_debug"]["stderr"][:200]))
 for profile in ("default","optimized"):
  print(label,profile,"emit",items[profile]["emit"]["exit"],"rustc",items[profile].get("rustc",{}).get("exit"),"native",items[profile].get("native",{}).get("exit"),repr(items[profile].get("native",{}).get("stdout")),repr(items[profile].get("native",{}).get("stderr","" )[:300]))
