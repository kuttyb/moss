#!/usr/bin/env python3
"""Run all package roots against one shared source manifest in isolated trees."""
import hashlib,json,os,pathlib,shutil,subprocess,sys
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[3]
BASE=pathlib.Path(sys.argv[1]).resolve();STAGING=ROOT/"tmp/cp-e-full-75ff8522/package-stages"
OUT=HERE/"package-matrix-isolated.jsonl"

def tracked():
    return [x.decode() for x in subprocess.check_output(["git","ls-files","-z"],cwd=ROOT).split(b"\0") if x]

def main():
    files=tracked()
    inputs=sorted(p for p in files if p.endswith((".moss",".mossi")) or pathlib.PurePosixPath(p).name in ("Moss.toml","Moss.lock"))
    roots=sorted(p.rsplit("/",1)[0] for p in files if p.endswith("/Moss.toml"))
    manifest_path=HERE/"package-input-manifest.jsonl"
    manifest_path.write_text("".join(json.dumps({"file":p,"sha256":hashlib.sha256((ROOT/p).read_bytes()).hexdigest()},sort_keys=True)+"\n" for p in inputs))
    rows=[]
    with OUT.open("w") as sink:
      for revision,binary in (("baseline",BASE/"moss"),("candidate",ROOT/"moss")):
       for mode in ("off","shadow"):
        stage=STAGING/f"{revision}-{mode}"
        if stage.exists():shutil.rmtree(stage)
        stage.mkdir(parents=True)
        for p in inputs:
          dest=stage/p;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/p,dest)
        env=os.environ.copy();env["MOSS"]=str(binary);env["MOSS_EST"]=mode
        env["MARGO_HOME"]=str(STAGING/f"margo-home-{revision}-{mode}")
        env["TMPDIR"]=str(ROOT/"tmp")
        env.pop("MOSS_MODULE_PATH",None);env.pop("MOSS_FAST_DEBUG_SOURCE_ROOTS",None)
        for ix,root in enumerate(roots,1):
          p=subprocess.run([str(ROOT/"margo"),"--root",str(stage/root),"build","--check","--json"],
                           cwd=stage,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
          try:payload=json.loads(p.stdout)
          except Exception:payload=None
          row={"revision":revision,"mode":mode,"root":root,"exit":p.returncode,
               "ok":payload.get("ok") if payload else None,
               "checked":payload.get("checked",[]) if payload else [],
               "skipped":payload.get("skipped",[]) if payload else [],
               "packages":payload.get("packages",[]) if payload else [],
               "error":payload.get("error") if payload else None,
               "stdout_sha256":hashlib.sha256(p.stdout.encode()).hexdigest(),
               "stderr_sha256":hashlib.sha256(p.stderr.encode()).hexdigest()}
          sink.write(json.dumps(row,sort_keys=True)+"\n");sink.flush();rows.append(row)
          if ix%25==0:print(revision,mode,ix,"/",len(roots),file=sys.stderr,flush=True)
    failed=[{k:r[k] for k in ("revision","mode","root","exit","ok","error")} for r in rows if r["ok"] is not True or r["exit"]]
    print(json.dumps({"roots":len(roots),"inputs":len(inputs),"input_manifest":str(manifest_path),
      "rows":len(rows),"failures":failed},sort_keys=True))
if __name__=="__main__":main()
