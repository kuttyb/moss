#!/usr/bin/env python3
"""Run each corpus-differing source file as an actual Margo package consumer."""
import hashlib,json,os,pathlib,shutil,subprocess,sys,re
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[3]
BASE=ROOT/"tmp/cp-e-full-baseline"
SOURCES=("sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss",
         "tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss")
def norm(p):
    if p.get("error"):
        d=p["error"]
        if isinstance(d,str):
            m=re.search(r'(\{\s*"protocol_version"[\s\S]*)',d)
            if m:
                try:
                    q=json.loads(m.group(1));d=q.get("error",{})
                except Exception:pass
            if isinstance(d,str):
                code=re.search(r'"code"\s*:\s*"([A-Z0-9_]+)"',d)
                return [{"code":code.group(1) if code else "MARGO_ERROR","severity":None,
                         "message":d.rsplit("): ",1)[-1].splitlines()[0][:240],"line":None,"column":None}]
        return [{k:d.get(k) for k in ("code","severity","message","line","column")}]
    return [{k:d.get(k) for k in ("code","severity","message","line","column")}
            for d in p.get("result",{}).get("diagnostics",[])]
rows=[]
for src in SOURCES:
 for revision,compiler in (("baseline",BASE/"moss"),("candidate",ROOT/"moss")):
  for mode in ("off","shadow"):
   stage=ROOT/"tmp/cp-e-full-75ff8522/package-stages"/f"{revision}-{mode}"
   consumer=stage/"cp-e-source-free-consumer"
   if consumer.exists():shutil.rmtree(consumer)
   (consumer/"src").mkdir(parents=True)
   shutil.copy2(ROOT/src,consumer/"src/main.moss")
   (consumer/"Moss.toml").write_text(
     '[package]\nname = "cp-e-source-free-consumer"\nversion = "0.1.0"\n\n'
     '[build]\nsource = "src"\n\n[dependencies]\n'
     '"moss.ndarray" = { path = "../sdk/ndarray" }\n'
     '"moss.datastructures" = { path = "../sdk/datastructures" }\n'
     '"moss.tables" = { path = "../sdk/tables" }\n'
     '"moss.binary" = { path = "../sdk/binary" }\n'
     '"moss.cbor" = { path = "../sdk/cbor" }\n'
     '"moss.csv" = { path = "../sdk/csv" }\n'
     '"moss.linalg" = { path = "../packages/linalg" }\n')
   env=os.environ.copy();env["MOSS"]=str(compiler);env["MOSS_EST"]=mode
   env["MARGO_HOME"]=str(ROOT/"tmp/cp-e-full-75ff8522/margo-home-sourcefree-{revision}-{mode}")
   env["TMPDIR"]=str(ROOT/"tmp");env.pop("MOSS_MODULE_PATH",None)
   p=subprocess.run([str(ROOT/"margo"),"--root",str(consumer),"build","--check","--json"],
                    cwd=stage,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   try:payload=json.loads(p.stdout)
   except Exception:payload={"ok":False,"parse_error":p.stdout}
   row={"source":src,"revision":revision,"mode":mode,"exit":p.returncode,
        "verdict":payload.get("ok"),"diagnostics":norm(payload),
        "checked":payload.get("checked",[]),"skipped":payload.get("skipped",[]),
        "packages":payload.get("packages",[]),"stderr":p.stderr,
        "stdout_sha256":hashlib.sha256(p.stdout.encode()).hexdigest()}
   rows.append(row)
   label=pathlib.Path(src).parent.name
   (HERE/f"margo-source-free-{label}-{revision}-{mode}.json").write_text(json.dumps(payload,indent=2,sort_keys=True)+"\n")
(HERE/"source-free-margo-oracle.json").write_text(json.dumps(rows,indent=2,sort_keys=True)+"\n")
diffs=[]
for src in SOURCES:
 ss=[r for r in rows if r["source"]==src]
 signatures={(r["verdict"],json.dumps(r["diagnostics"],sort_keys=True)) for r in ss}
 if len(signatures)>1:diffs.append(src)
print(json.dumps({"runs":len(rows),"sources":len(SOURCES),"four_way_diagnostic_differences":diffs,
 "results":[{k:r[k] for k in ("source","revision","mode","exit","verdict","diagnostics","checked","skipped")} for r in rows]},sort_keys=True))
