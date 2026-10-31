import concurrent.futures, json, os, subprocess, sys, tarfile, io, shutil
from pathlib import Path
root=Path(__file__).resolve().parents[1]
bin=Path(sys.argv[1]).resolve(); label=sys.argv[2]
rev=sys.argv[3]
sources=[x for x in subprocess.check_output(['git','ls-tree','-r','--name-only',rev],cwd=root,text=True).splitlines() if x.endswith('.moss')]
corpus=root/'tmp'/f'reach-pair-{label}'
shutil.rmtree(corpus,ignore_errors=True); corpus.mkdir(parents=True)
with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','archive',rev],cwd=root))) as t: t.extractall(corpus,filter='data')
def trace(source,mode):
 env=os.environ.copy(); env.update(MOSS_EST=mode,MOSS_PHASE26_C1_SCOPE_TRACE='1')
 r=subprocess.run([str(bin),'check',source,'--json'],cwd=corpus,env=env,text=True,capture_output=True,timeout=120)
 try:
  d=json.loads(r.stdout); er=d.get('error') or {}; verdict=(d.get('ok'),er.get('code'),er.get('line'))
 except Exception: verdict=('NOJSON',r.returncode,None)
 prefix=str(corpus)+'/'
 edges=set(); checked=set(); complete=False
 for line in r.stderr.splitlines():
  if line.startswith('MOSS_C1_SCOPE_TRACE_COMPLETE|'): complete=True
  elif line.startswith('MOSS_C1_SCOPE_EDGE|'):
   _,owner,kind,target=line.split('|',3); edges.add((owner.replace(prefix,''),kind,target))
  elif line.startswith('MOSS_C1_SCOPE_ID|'):
   _,kind,path,identity=line.split('|',3); checked.add((kind,path.replace(prefix,''),identity))
 return {'edges':edges,'checked':checked,'complete':complete,'verdict':verdict}
def one(source):
 a=trace(source,'off'); b=trace(source,'shadow')
 em=sorted(a['edges']-b['edges']); ea=sorted(b['edges']-a['edges']); cm=sorted(a['checked']-b['checked']); ca=sorted(b['checked']-a['checked'])
 return {'source':source,'edge_missing':em,'edge_added':ea,'checked_missing':cm,'checked_added':ca,'complete':[a['complete'],b['complete']],'verdict':[a['verdict'],b['verdict']]}
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=min(8,os.cpu_count() or 4)) as ex:
 for row in ex.map(one,sources):
  if row['edge_missing'] or row['edge_added'] or row['checked_missing'] or row['checked_added'] or row['complete'][0]!=row['complete'][1] or row['verdict'][0]!=row['verdict'][1]: rows.append(row)
rows.sort(key=lambda x:x['source'])
out=root/'tmp'/f'reach-pair-{label}.json'; out.write_text(json.dumps({'rev':rev,'binary':str(bin),'sources':len(sources),'diff_rows':rows,'counts':{k:sum(len(r[k]) for r in rows) for k in ('edge_missing','edge_added','checked_missing','checked_added')}},indent=2)+'\n')
print(json.dumps({'sources':len(sources),'diff_rows':len(rows),'counts':{k:sum(len(r[k]) for r in rows) for k in ('edge_missing','edge_added','checked_missing','checked_added')},'out':str(out.relative_to(root))}))
