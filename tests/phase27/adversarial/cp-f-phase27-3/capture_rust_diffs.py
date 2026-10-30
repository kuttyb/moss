#!/usr/bin/env python3
"""Re-emit and preserve exact default/-O Rust diffs for CP-F hash mismatches."""
import difflib, hashlib, json, os, pathlib, subprocess, sys, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[4]
BASELINE = pathlib.Path(sys.argv[1]).resolve()
CANDIDATE = pathlib.Path(sys.argv[2]).resolve()
ANALYSIS = json.loads((ROOT/"docs/phase27/adversarial/CP_F_PHASE27_3_DIFFERENTIAL_ANALYSIS.json").read_text())
sources = sorted(set(ANALYSIS["rust_relationships"]["baseline_vs_candidate_off"]["default_rust_hash_differences"] + ANALYSIS["rust_relationships"]["baseline_vs_candidate_off"]["optimized_rust_hash_differences"]))
OUT = ROOT/"tests/phase27/adversarial/cp-f-phase27-3/rust-diffs.jsonl"
rows=[]
with tempfile.TemporaryDirectory(prefix="cpf-rust-diffs-",dir=ROOT/"tmp/cpf-phase27-3/evidence") as td:
    work=pathlib.Path(td)
    for source in sources:
        src=ROOT/source
        for profile in ("default","optimized"):
            env=os.environ.copy(); env["MOSS_EST"]="off"
            values={}
            for label,compiler in (("baseline",BASELINE),("candidate_off",CANDIDATE)):
                target=work/f"{source.replace('/', '_')}-{profile}-{label}.rs"
                argv=[str(compiler)]+(["-O"] if profile=="optimized" else [])+[str(src),"-o",str(target)]
                p=subprocess.run(argv,cwd=ROOT,env=env,text=True,capture_output=True,timeout=120)
                data=target.read_text(encoding="utf-8",errors="replace") if target.exists() else ""
                values[label]={"command":argv,"exit":p.returncode,"stdout":p.stdout,"stderr":p.stderr,"sha256":hashlib.sha256(data.encode()).hexdigest(),"rust":data}
            left=values["baseline"]["rust"].splitlines(keepends=True)
            right=values["candidate_off"]["rust"].splitlines(keepends=True)
            diff="".join(difflib.unified_diff(left,right,fromfile="baseline.rs",tofile="candidate-off.rs"))
            row={"source":source,"profile":profile,"baseline_sha256":values["baseline"]["sha256"],"candidate_sha256":values["candidate_off"]["sha256"],"baseline_emit_exit":values["baseline"]["exit"],"candidate_emit_exit":values["candidate_off"]["exit"],"unified_diff":diff}
            rows.append(row)
OUT.parent.mkdir(parents=True,exist_ok=True)
with OUT.open("w",encoding="utf-8") as f:
    for row in rows: f.write(json.dumps(row,sort_keys=True)+"\n")
print(json.dumps({"source_count":len(sources),"profile_diff_count":len(rows),"result":str(OUT.relative_to(ROOT)),"per_profile":[{"source":r["source"],"profile":r["profile"],"baseline_sha256":r["baseline_sha256"],"candidate_sha256":r["candidate_sha256"],"diff_line_count":len(r["unified_diff"].splitlines())} for r in rows]},indent=2))
