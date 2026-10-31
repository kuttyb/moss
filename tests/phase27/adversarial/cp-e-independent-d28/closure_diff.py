#!/usr/bin/env python3
"""Compare checked-declaration closure and verdicts on common corpus roots."""
from __future__ import annotations
import concurrent.futures
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[4]
BASELINE=ROOT.parent/"cp-e-independent-d28-baseline"
OUT=ROOT/"tests/phase27/adversarial/cp-e-independent-d28/closure-diff.jsonl"
MODES=("baseline-off","baseline-shadow","candidate-off","candidate-shadow")

def tracked(root):
    return {x.decode() for x in subprocess.check_output(["git","ls-files","-z","*.moss"],cwd=root).split(b"\0") if x}

def provider_paths(root):
    p=root/"sdk/advanced-data-acceptance/check_source_free.py"
    spec=importlib.util.spec_from_file_location("source_free",p)
    m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return ":".join(str(package/"build/debug") for _name,package in m.PROVIDERS)

def normalize(value,root):
    if isinstance(value,str): return value.replace(str(root),"<ROOT>")
    if isinstance(value,list): return [normalize(v,root) for v in value]
    if isinstance(value,dict): return {k:normalize(v,root) for k,v in value.items()}
    return value

def run(root,mode,source):
    env=os.environ.copy(); env.update({"MOSS_EST":mode,"MOSS_PHASE26_C1_SCOPE_TRACE":"1","MOSS_MODULE_PATH":provider_paths(root)})
    binary=root/"build/compiler/moss-dev"
    try:
        p=subprocess.run([str(binary),"check",source,"--json"],cwd=root,env=env,capture_output=True,text=True,timeout=120)
        try: payload=json.loads(p.stdout)
        except json.JSONDecodeError: payload=None
        if isinstance(payload,dict):
            verdict={"ok":payload.get("ok"),"error":normalize(payload.get("error"),root),"diagnostics":normalize(payload.get("result",{}).get("diagnostics",[]),root)}
        else: verdict={"ok":"NOJSON","stdout":p.stdout[:500],"stderr":p.stderr[-1000:]}
        prefix=str(root)+"/"; checked=set(); edges=set(); complete=False
        for row in p.stderr.splitlines():
            if row.startswith("MOSS_C1_SCOPE_TRACE_COMPLETE|"): complete=True
            elif row.startswith("MOSS_C1_SCOPE_ID|"):
                _,kind,path,identity=row.split("|",3); checked.add((kind,path.replace(prefix,""),identity))
            elif row.startswith("MOSS_C1_SCOPE_EDGE|"):
                _,owner,kind,target=row.split("|",3); edges.add((owner.replace(prefix,""),kind,target))
        return {"returncode":p.returncode,"verdict":verdict,"complete":complete,"checked":sorted(checked),"edges":sorted(edges),"trace_error":normalize(p.stderr,root) if not complete else None}
    except subprocess.TimeoutExpired:
        return {"returncode":"TIMEOUT","verdict":{"ok":"TIMEOUT"},"complete":False,"checked":[],"edges":[]}

def main():
    base,cand=tracked(BASELINE),tracked(ROOT); common=sorted(base&cand); jobs=int(os.environ.get("CP_E_JOBS","8"))
    print(f"baseline={len(base)} candidate={len(cand)} common={len(common)} jobs={jobs}",flush=True)
    start=time.monotonic(); rows=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
      future={}
      for src in common:
        for mode,root,engine in (("baseline-off",BASELINE,"off"),("baseline-shadow",BASELINE,"shadow"),("candidate-off",ROOT,"off"),("candidate-shadow",ROOT,"shadow")):
          future[src,mode]=pool.submit(run,root,engine,src)
      for n,src in enumerate(common,1):
        result={m:future[src,m].result() for m in MODES}
        baseline=result["baseline-off"]; cand_off=result["candidate-off"]; cand_shadow=result["candidate-shadow"]
        cmp={"baseline_candidate_off_verdict":baseline["verdict"]!=cand_off["verdict"],
             "baseline_candidate_shadow_verdict":result["baseline-shadow"]["verdict"]!=result["candidate-shadow"]["verdict"],
             "off_shadow_candidate_verdict":cand_off["verdict"]!=cand_shadow["verdict"],
             "off_shadow_candidate_closure":cand_off["checked"]!=cand_shadow["checked"],
             "baseline_checked_missing_from_candidate_shadow":sorted(set(baseline["checked"])-set(cand_shadow["checked"])),
             "candidate_checked_added_vs_baseline":sorted(set(cand_shadow["checked"])-set(baseline["checked"])),
             "incomplete_trace":not all(result[m]["complete"] for m in MODES)}
        rows.append({"source":src,"modes":result,"comparison":cmp})
        if n%100==0: print(f"completed={n}/{len(common)} elapsed={time.monotonic()-start:.1f}s",flush=True)
    OUT.write_text("".join(json.dumps(r,sort_keys=True)+"\n" for r in rows))
    summary={"rows":len(rows),"incomplete_per_mode":{m:sum(not r["modes"][m]["complete"] for r in rows) for m in MODES},
             "verdict_deltas":{k:sum(bool(r["comparison"][k]) for r in rows) for k in ("baseline_candidate_off_verdict","baseline_candidate_shadow_verdict","off_shadow_candidate_verdict")},
             "off_shadow_candidate_closure_deltas":sum(bool(r["comparison"]["off_shadow_candidate_closure"]) for r in rows),
             "missing_baseline_checked_identities":sum(len(r["comparison"]["baseline_checked_missing_from_candidate_shadow"]) for r in rows),
             "added_candidate_checked_identities":sum(len(r["comparison"]["candidate_checked_added_vs_baseline"]) for r in rows),
             "seconds":round(time.monotonic()-start,1),"output":str(OUT.relative_to(ROOT))}
    print(json.dumps(summary,indent=2))

if __name__=="__main__": main()
