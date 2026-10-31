#!/usr/bin/env python3
"""Compare checked declaration closures for both binaries and modes on one archive."""
import concurrent.futures, json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
BINARIES={'baseline':ROOT/'tmp/cp-e-post-taint/baseline/build/compiler/moss-dev','candidate':ROOT/'build/compiler/moss-dev'}
CORPUS=ROOT/'tmp/cp-e-post-taint/source-archive'
SOURCES=sorted(p.relative_to(CORPUS).as_posix() for p in CORPUS.rglob('*.moss') if p.is_file())
def run(binary,mode,source):
 env=os.environ.copy();env['MOSS_EST']=mode;env['MOSS_PHASE26_C1_SCOPE_TRACE']='1'
 proc=subprocess.run([str(binary),'check',source,'--json'],cwd=CORPUS,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
 try:
  j=json.loads(proc.stdout);e=j.get('error') or {};verdict=(j.get('ok'),e.get('code'),e.get('line'))
 except Exception:verdict=('NOJSON',proc.returncode,None)
 checked=set();complete=False;edges=set()
 for line in proc.stderr.splitlines():
  if line.startswith('MOSS_C1_SCOPE_TRACE_COMPLETE|'):complete=True
  elif line.startswith('MOSS_C1_SCOPE_ID|'):
   _,kind,path,identity=line.split('|',3);checked.add((kind,path.replace(str(CORPUS)+'/',''),identity))
  elif line.startswith('MOSS_C1_SCOPE_EDGE|'):
   _,owner,kind,target=line.split('|',3);edges.add((owner.replace(str(CORPUS)+'/',''),kind,target))
 return {'verdict':verdict,'checked':checked,'edges':edges,'complete':complete}
def one(source):
 return source,{binary:{mode:run(path,mode,source) for mode in ('off','shadow')} for binary,path in BINARIES.items()}
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(one,SOURCES))
def delta(a,b,key):
 return sorted(a[key]-b[key]),sorted(b[key]-a[key])
missing={};verdicts={};trace={};edges={}
for source,r in rows:
 for label,left,right in (
  ('baseline_off_vs_candidate_off',r['baseline']['off'],r['candidate']['off']),
  ('baseline_shadow_vs_candidate_shadow',r['baseline']['shadow'],r['candidate']['shadow']),
  ('baseline_off_vs_shadow',r['baseline']['off'],r['baseline']['shadow']),
  ('candidate_off_vs_shadow',r['candidate']['off'],r['candidate']['shadow'])):
  lost,added=delta(left,right,'checked')
  if lost or added:missing.setdefault(label,[]).append({'source':source,'left_only':lost,'right_only':added})
  if left['verdict']!=right['verdict']:verdicts.setdefault(label,[]).append({'source':source,'left':left['verdict'],'right':right['verdict']})
  if left['complete']!=right['complete']:trace.setdefault(label,[]).append({'source':source,'left':left['complete'],'right':right['complete']})
  elost,eadd=delta(left,right,'edges')
  if elost or eadd:edges.setdefault(label,[]).append({'source':source,'left_only':elost,'right_only':eadd})
completeness={name:{mode:sum(r[name][mode]['complete'] for _,r in rows) for mode in ('off','shadow')} for name in BINARIES}
raw=[{'source':src,'runs':{name:{mode:{'verdict':r[name][mode]['verdict'],'trace_complete':r[name][mode]['complete'],'checked':sorted(r[name][mode]['checked'])} for mode in ('off','shadow')} for name in BINARIES}} for src,r in rows]
(ROOT/'tmp/cp-e-post-taint/checked-closure-runs.json').write_text(json.dumps(raw,separators=(',',':'))+'\n')
report={'trace_complete_counts':completeness,'timeouts':0,'candidate_sha':'3aa929be9f6413bb2864049d864ce4890a4f1bdf','baseline_sha':'04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4','source_tree':'candidate SHA exported once; all runs use same absolute source tree per entry','entry_count':len(rows),'checked_declaration_deltas':missing,'verdict_deltas':verdicts,'trace_completion_deltas':trace,'edge_deltas':edges}
out=ROOT/'tmp/cp-e-post-taint/checked-closure-4way.json';out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'entry_count':len(rows),'checked_delta_counts':{k:sum(len(x['left_only'])+len(x['right_only']) for x in v) for k,v in missing.items()},'verdict_delta_counts':{k:len(v) for k,v in verdicts.items()},'trace_complete_counts':completeness},sort_keys=True))
