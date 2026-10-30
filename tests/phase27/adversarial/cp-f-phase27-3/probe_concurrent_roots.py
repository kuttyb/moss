#!/usr/bin/env python3
"""Repeat CP-F native runs with the concurrent FileIO fixture inputs present."""
import hashlib,json,os,pathlib,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[4]
SOURCE=ROOT/"tests/tooling/fixtures/phase21c_concurrent_roots.moss"
BASELINE=pathlib.Path(sys.argv[1]).resolve();CANDIDATE=pathlib.Path(sys.argv[2]).resolve()
EXPECTED=[1260,2001,3003,4065]
rows=[]
def run(argv,cwd,env,timeout=120):
 p=subprocess.run(argv,cwd=cwd,env=env,text=True,capture_output=True,timeout=timeout)
 return {"command":argv,"exit":p.returncode,"stdout":p.stdout,"stderr":p.stderr}
for label,compiler,mode in (("baseline",BASELINE,"off"),("candidate_off",CANDIDATE,"off"),("candidate_shadow",CANDIDATE,"shadow")):
 for profile in ("default","optimized"):
  env=os.environ.copy();env["MOSS_EST"]=mode
  with tempfile.TemporaryDirectory(prefix="cpf-concurrent-roots-",dir=ROOT/"tmp/cpf-phase27-3/evidence") as td:
   work=pathlib.Path(td);(work/"tmp").mkdir()
   for suffix,payload in (("a",b"AAAA"),("b",b"ABCD"),("c",b"ABCD"),("d",b"A")):
    (work/"tmp"/f"phase21c-concurrent-{suffix}.data").write_bytes(payload)
   rust=work/"program.rs";binary=work/"program"
   emit=run([str(compiler)]+(["-O"] if profile=="optimized" else [])+[str(SOURCE),"-o",str(rust)],work,env)
   compiled=run(["rustc","--edition=2021",str(rust),"-o",str(binary)],work,env) if emit["exit"]==0 else None
   repeats=[]
   if compiled and compiled["exit"]==0:
    for _ in range(8): repeats.append(run([str(binary)],work,env))
   actual=[sorted(int(line) for line in p["stdout"].splitlines()) if p["exit"]==0 else None for p in repeats]
   rows.append({"compiler":label,"profile":profile,"emit":emit,"rustc":compiled,"runs":repeats,"expected_sorted":EXPECTED,"all_expected":bool(actual) and all(x==EXPECTED for x in actual),"unique_schedules":len({p["stdout"] for p in repeats})})
result={"source":"tests/tooling/fixtures/phase21c_concurrent_roots.moss","source_sha256":hashlib.sha256(SOURCE.read_bytes()).hexdigest(),"baseline_compiler_sha256":hashlib.sha256(BASELINE.read_bytes()).hexdigest(),"candidate_compiler_sha256":hashlib.sha256(CANDIDATE.read_bytes()).hexdigest(),"repeats_per_profile":8,"results":rows}
out=ROOT/"tests/phase27/adversarial/cp-f-phase27-3/concurrent-roots-followup.json"
out.write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
print(json.dumps([{k:r.get(k) for k in ("compiler","profile","all_expected","unique_schedules")} for r in rows],indent=2))
