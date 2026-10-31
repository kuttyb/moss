#!/usr/bin/env python3
"""Compare declared source entities via Moss symbols queries over accepted corpus."""
import concurrent.futures,json,os,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[4]
candidate=pathlib.Path(sys.argv[1]).resolve(); baseline=pathlib.Path(sys.argv[2]).resolve()
corpus=pathlib.Path(sys.argv[3]); out=pathlib.Path(sys.argv[4]); out.parent.mkdir(parents=True,exist_ok=True)
rows=[json.loads(x) for x in corpus.read_text().splitlines() if x]
sources=[r['path'] for r in rows if r['baseline_off'].get('payload',{}).get('ok') is True and r['candidate_off'].get('payload',{}).get('ok') is True]
def query(binary,source):
 env=dict(os.environ,MOSS_EST='off')
 p=subprocess.run([str(binary),'symbols','--source',str(root/source),'--json'],cwd=root,env=env,text=True,capture_output=True,timeout=120)
 try:j=json.loads(p.stdout)
 except Exception:return {'exit':p.returncode,'error':'NO_JSON','stderr':p.stderr,'stdout':p.stdout}
 if not j.get('ok'): return {'exit':p.returncode,'error':(j.get('error') or {}).get('code')}
 decl=[]
 for s in j.get('result',{}).get('symbols',[]):
  src=s.get('source') or {}
  if pathlib.Path(src.get('file','')).resolve()==(root/source).resolve():
   decl.append((s.get('kind'),s.get('qualified_name'),src.get('line'),s.get('enclosing_entity')))
 return {'exit':p.returncode,'declarations':sorted(decl)}
def one(source):
 b=query(baseline,source); c=query(candidate,source)
 return {'source':source,'baseline':b,'candidate':c,'equal':b==c}
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool: results=list(pool.map(one,sources))
out.write_text(''.join(json.dumps(r,sort_keys=True,separators=(',',':'))+'\n' for r in results))
diffs=[r for r in results if not r['equal']]
summary={'accepted_source_count':len(sources),'baseline_query_failures':sum('error'in r['baseline'] for r in results),'candidate_query_failures':sum('error'in r['candidate'] for r in results),'declaration_differences':len(diffs),'differences':diffs}
out.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='differences'},indent=2))
