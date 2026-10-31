import concurrent.futures, json, os, pathlib, subprocess, sys
root=pathlib.Path(sys.argv[1]).resolve(); out=pathlib.Path(sys.argv[2]); out.mkdir(parents=True,exist_ok=True)
files=[]
for base in ('tests','sdk'):
 for p in (root/base).rglob('*.moss'):
  rel=p.relative_to(root)
  if 'adversarial' in rel.parts: continue
  if rel.parts[:3]==('tests','phase27','est') and 'repro' in rel.parts: continue
  files.append(str(rel))
files=sorted(set(files))
def one(item):
 mode,rel=item
 env=os.environ.copy(); env['MOSS_EST']=mode
 try:
  p=subprocess.run([str(root/'moss'),'check',str(root/rel),'--json'],cwd=root,env=env,capture_output=True,text=True,timeout=90)
  try:
   obj=json.loads(p.stdout); ds=obj.get('result',{}).get('diagnostics',[])
   if obj.get('error'): ds=[obj['error']]
   ds=sorted((d.get('code','?'),d.get('severity','?'),d.get('message','')) for d in ds)
   return [mode,rel,p.returncode,ds,p.stderr[-400:]]
  except Exception: return [mode,rel,p.returncode,['JSON_ERROR'],p.stdout[-500:],p.stderr[-300:]]
 except subprocess.TimeoutExpired: return [mode,rel,124,['TIMEOUT'],'']
items=[(m,f) for m in ('off','shadow') for f in files]
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex: rows=list(ex.map(one,items))
json.dump({'root':str(root),'count':len(files),'files':files,'results':rows},open(out/'results.json','w'),indent=2)
print('files',len(files),'runs',len(rows),'timeouts',sum(r[2]==124 for r in rows),'badjson',sum('JSON_ERROR' in r[3] for r in rows))
