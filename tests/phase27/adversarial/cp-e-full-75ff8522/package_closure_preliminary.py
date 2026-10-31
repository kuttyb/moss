#!/usr/bin/env python3
"""Clean and check every tracked package root against both compiler commits."""
import hashlib, json, os, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
OUT = pathlib.Path(__file__).with_name("package-matrix.jsonl")
BASE = pathlib.Path(sys.argv[1]).resolve()

def roots():
    listed = subprocess.check_output(["git", "ls-files", "-z"],cwd=ROOT).split(b"\0")
    return sorted({x.decode().rsplit("/", 1)[0] for x in listed
                   if x and x.decode().endswith("/Moss.toml")
                   and not x.decode().startswith("tmp/")})

def call(args, env):
    p = subprocess.run(args, cwd=ROOT, env=env, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try: payload = json.loads(p.stdout)
    except Exception: payload = None
    return p, payload

def main():
    package_roots = roots()
    rows=[]
    bins={"baseline":BASE/"moss", "candidate":ROOT/"moss"}
    with OUT.open("w") as f:
      for revision, compiler in bins.items():
        for mode in ("off", "shadow"):
          env=os.environ.copy();env["MOSS"]=str(compiler);env["MOSS_EST"]=mode
          env["MARGO_HOME"]=str(ROOT/"tmp/cp-e-full-75ff8522/margo-home")
          env["TMPDIR"]=str(ROOT/"tmp")
          for ix, root in enumerate(package_roots,1):
            clean, _ = call([str(ROOT/"margo"),"--root",root,"clean"], env)
            checked, payload = call([str(ROOT/"margo"),"--root",root,"build","--check","--json"],env)
            row={"revision":revision,"mode":mode,"root":root,
                 "clean_exit":clean.returncode,"exit":checked.returncode,
                 "ok":payload.get("ok") if payload else None,
                 "checked":payload.get("checked",[]) if payload else [],
                 "skipped":payload.get("skipped",[]) if payload else [],
                 "packages":payload.get("packages",[]) if payload else [],
                 "raw_stdout_sha256":hashlib.sha256(checked.stdout.encode()).hexdigest(),
                 "raw_stderr_sha256":hashlib.sha256(checked.stderr.encode()).hexdigest(),
                 "clean_stderr":clean.stderr.strip(),
                 "error":payload.get("error") if payload else None}
            f.write(json.dumps(row,sort_keys=True)+"\n");f.flush();rows.append(row)
            if ix%25==0: print(revision,mode,ix,"/",len(package_roots),file=sys.stderr,flush=True)
    print(json.dumps({"roots":len(package_roots),"rows":len(rows),
      "failures":[{k:r[k] for k in ("revision","mode","root","exit","ok","error")}
                   for r in rows if r["ok"] is not True or r["exit"]!=0 or r["clean_exit"]!=0]},sort_keys=True))

if __name__ == "__main__": main()
