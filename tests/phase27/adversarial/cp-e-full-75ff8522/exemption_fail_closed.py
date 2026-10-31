#!/usr/bin/env python3
"""In-memory mutation checks for the three function/API reader exemptions."""
import importlib.util, json, pathlib
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
spec=importlib.util.spec_from_file_location("string_debt",ROOT/"tests/tooling/check_phase27_string_debt.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
entries=m.load_exemptions()
if len(entries)!=3: raise SystemExit(f"expected exactly 3 exemptions, got {len(entries)}")
results=[]
original=m.read_source
for path,fn,api in sorted(entries):
    src=original(path)
    spans=m.function_ranges(src,{fn})
    if fn not in spans: raise SystemExit(f"missing function before mutation: {fn}")
    start,end=spans[fn]
    lines=src.splitlines(keepends=True)
    lo=sum(1 for _ in src.splitlines()[:start-1]);hi=end
    body="".join(lines[lo:hi])
    def exercise(label, changed, expected):
        def read(p,commit=None): return changed if p==path else original(p,commit)
        m.read_source=read
        try: m.sites()
        except SystemExit as e: msg=str(e)
        else: msg="ACCEPTED"
        if expected not in msg: raise SystemExit(f"{path}|{fn}|{label}: expected {expected!r}, got {msg!r}")
        results.append({"path":path,"function":fn,"api":api,"mutation":label,"result":msg})
    changed_body=body.replace(api+"(","renamed_reader(",1)
    if changed_body==body: raise SystemExit(f"API call absent in {path}|{fn}")
    exercise("removed-reader-site",src.replace(body,changed_body,1),"stale expression-reader exemptions")
    dupe=body.replace(api+"(",api+"(); "+api+"(",1)
    exercise("duplicate-reader-site",src.replace(body,dupe,1),"multiple reader sites")
    decl_start=sum(len(x) for x in lines[:start-1])
    decl_line=lines[start-1]
    if fn+"(" not in decl_line: raise SystemExit(f"declaration token absent on line {start} for {fn}")
    changed=src[:decl_start]+decl_line.replace(fn+"(","renamed_exemption_function(",1)+src[decl_start+len(decl_line):]
    exercise("removed-function",changed,"missing exemption function")
m.read_source=original
(HERE/"exemption-fail-closed.json").write_text(json.dumps(
    {"exemptions":len(entries),"mutations":len(results),"results":results},
    indent=2,sort_keys=True)+"\n")
print(json.dumps({"exemptions":len(entries),"mutations":len(results),"pass":True},sort_keys=True))
