#!/usr/bin/env python3
"""Normalize package closure results and compare revision/mode cells."""
import collections,json,pathlib,re
HERE=pathlib.Path(__file__).resolve().parent
rows=[json.loads(x) for x in (HERE/"package-matrix-isolated.jsonl").read_text().splitlines()]
by={(r["revision"],r["mode"],r["root"]):r for r in rows}
roots=sorted({r["root"] for r in rows})
def diag(r):
    e=r.get("error")
    if not e:return []
    if isinstance(e,dict):return [e]
    m=re.search(r'(\{\s*"protocol_version"[\s\S]*)',str(e))
    if m:
        try:
            p=json.loads(m.group(1));return [p["error"]] if p.get("error") else p.get("result",{}).get("diagnostics",[])
        except Exception:pass
    s=str(e)
    if "moss: error:" in s:
        return [{"code":"INTERNAL_ERROR","message":s.split("moss: error:",1)[1].splitlines()[0].strip()}]
    if "lacks Moss.toml" in s:
        return [{"code":"MISSING_PACKAGE_MANIFEST","message":"package dependency lacks Moss.toml"}]
    if "no Moss.toml found" in s:
        return [{"code":"MISSING_PACKAGE_ROOT","message":"no Moss.toml found"}]
    if "): " in s:
        return [{"code":"MARGO_ERROR","message":s.rsplit("): ",1)[1].splitlines()[0][:240]}]
    return [{"code":"MARGO_ERROR","message":s.splitlines()[0][:240]}]
def dsig(r):
    return [{k:d.get(k) for k in ("code","severity","message","line","column")}
            for d in diag(r)]
def graph(r):
    result=[]
    prefix=f"/tmp/cp-e-full-75ff8522/package-stages/{r['revision']}-{r['mode']}"
    for x in r.get("packages",[]):
        src=x.get("source",{});path=src.get("path")
        if path and prefix in path:path=path.split(prefix,1)[1]
        result.append({"name":x.get("name"),"source_kind":src.get("kind"),"source_path":path})
    return sorted(result,key=lambda x:(str(x["name"]),str(x["source_kind"]),str(x["source_path"])))
cells={}
for revision in ("baseline","candidate"):
  for mode in ("off","shadow"):
    items=[by[(revision,mode,r)] for r in roots]
    accepted=[r for r in items if r["ok"] is True]
    failed=[r for r in items if r["ok"] is not True]
    closure=sorted({p["name"] for r in accepted for p in r.get("packages",[]) if p.get("name")})
    cells[f"{revision}/{mode}"]={"roots":len(items),"accepted":len(accepted),"rejected":len(failed),
      "accepted_roots":[r["root"] for r in accepted],"rejected_roots":[r["root"] for r in failed],
      "diagnostics":[{"root":r["root"],"diagnostics":dsig(r)} for r in failed],
      "checked_declaration_names_union":closure,"checked_declaration_name_count":len(closure)}
comparisons={}
for left,right in (("baseline/off","candidate/off"),("baseline/shadow","candidate/shadow"),
                   ("baseline/off","baseline/shadow"),("candidate/off","candidate/shadow")):
  a_rev,a_mode=left.split("/");b_rev,b_mode=right.split("/")
  verdict=[];diagnostic=[];closures=[]
  for root in roots:
    a=by[(a_rev,a_mode,root)];b=by[(b_rev,b_mode,root)]
    if (a["ok"],a["exit"])!=(b["ok"],b["exit"]):verdict.append(root)
    if dsig(a)!=dsig(b):diagnostic.append({"root":root,"left":dsig(a),"right":dsig(b)})
    if a["ok"] is True and b["ok"] is True and graph(a)!=graph(b):closures.append(root)
  comparisons[f"{left} vs {right}"]={"verdict_differences":verdict,"diagnostic_differences":diagnostic,"accepted_closure_differences":closures}
result={"rows":len(rows),"roots":len(roots),"cells":cells,"comparisons":comparisons}
(HERE/"package-closure-summary.json").write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
print(json.dumps({"rows":len(rows),"roots":len(roots),"cells":{k:{"accepted":v["accepted"],"rejected":v["rejected"],"checked_declaration_name_count":v["checked_declaration_name_count"]} for k,v in cells.items()},"comparisons":{k:{a:len(b) for a,b in v.items()} for k,v in comparisons.items()}},sort_keys=True))
