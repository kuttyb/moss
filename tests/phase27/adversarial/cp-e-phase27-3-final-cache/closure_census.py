#!/usr/bin/env python3
import argparse, concurrent.futures, json, os, pathlib, subprocess
p=argparse.ArgumentParser(); p.add_argument('compiler'); p.add_argument('corpus'); p.add_argument('verdicts'); p.add_argument('out'); p.add_argument('--jobs',type=int,default=8); a=p.parse_args()
compiler=pathlib.Path(a.compiler).resolve(); corpus=pathlib.Path(a.corpus).resolve(); out=pathlib.Path(a.out); env=dict(os.environ,MOSS_EST='off')
verdicts={line.split('\t',1)[0]:line.split('\t',1)[1] for line in pathlib.Path(a.verdicts).read_text().splitlines() if line}
accepted=sorted(path for path,v in verdicts.items() if v=='ok')
def run(args):
 r=subprocess.run([str(compiler),*args,'--json'],cwd=corpus,text=True,capture_output=True,env=env,timeout=90)
 try: j=json.loads(r.stdout)
 except Exception: return {'ok':False,'error':'NO_JSON','stderr':r.stderr[:300],'returncode':r.returncode}
 return j
def census(path):
 j=run(['symbols','--source',path])
 if not j.get('ok'): return {'file':path,'symbols_ok':False,'error':(j.get('error') or {}).get('code')}
 decls=[]
 for s in j.get('result',{}).get('symbols',[]):
  source=(s.get('source') or {}).get('file','')
  try: rel=pathlib.Path(source).resolve().relative_to(corpus).as_posix()
  except Exception: continue
  decls.append({'id':s.get('entity_id'),'kind':s.get('kind'),'name':s.get('qualified_name'),'source':rel,'line':(s.get('source') or {}).get('line')})
 return {'file':path,'symbols_ok':True,'declarations':decls}
with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool: rows=list(pool.map(census,accepted))
failures=[r for r in rows if not r['symbols_ok']]
unique={ (d['source'],d['id']): d for r in rows for d in r.get('declarations',[]) if d['id'] }
summary={'source_count':len(verdicts),'accepted_sources':len(accepted),'rejected_sources':len(verdicts)-len(accepted),'symbol_query_failures':failures,'accepted_source_contexts_with_symbols':sum(r['symbols_ok'] for r in rows),'distinct_source_declarations':len(unique),'declarations_by_kind':{},'declarations':sorted(unique.values(),key=lambda d:(d['source'],d['line'] or 0,d['kind'],d['name'] or ''))}
for d in unique.values(): summary['declarations_by_kind'][d['kind']]=summary['declarations_by_kind'].get(d['kind'],0)+1
out.parent.mkdir(parents=True,exist_ok=True)
with out.open('w') as f:
 for r in rows: f.write(json.dumps(r,sort_keys=True)+'\n')
out.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='declarations'},indent=2))
if failures: raise SystemExit(1)
