#!/usr/bin/env python3
import concurrent.futures, json, os, pathlib, subprocess, sys, tarfile, io, shutil
ROOT=pathlib.Path(__file__).resolve().parents[4]
BASE_GIT='04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4'
CAND_GIT=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
BASE_BIN=pathlib.Path(sys.argv[1]).resolve(); CAND_BIN=pathlib.Path(sys.argv[2]).resolve()
OUT=pathlib.Path(sys.argv[3]).resolve()
paths=subprocess.check_output(['git','ls-tree','-r','--name-only',CAND_GIT],cwd=ROOT,text=True).splitlines()
paths=sorted(p for p in paths if p.endswith('.moss') and not p.startswith('tests/phase27/adversarial/'))
base_root=ROOT/'tmp/cp-e-postfix-baseline-archive'; cand_root=ROOT/'tmp/cp-e-postfix-candidate-archive'
for rev,dest in ((BASE_GIT,base_root),(CAND_GIT,cand_root)):
  if dest.exists(): shutil.rmtree(dest)
  dest.mkdir(parents=True)
  archive=subprocess.check_output(['git','archive',rev],cwd=ROOT)
  with tarfile.open(fileobj=io.BytesIO(archive)) as tar: tar.extractall(dest,filter='data')
work=[(compiler,mode,rel) for rel in paths for compiler in ('baseline','candidate') for mode in ('off','shadow')]
def trace(item):
  compiler,mode,rel=item; root=cand_root
  binary=BASE_BIN if compiler=='baseline' else CAND_BIN
  env=os.environ.copy(); env.update(MOSS_EST=mode,MOSS_PHASE26_C1_SCOPE_TRACE='1')
  try: r=subprocess.run([str(binary),'debug',rel],cwd=root,env=env,text=True,capture_output=True,timeout=60)
  except subprocess.TimeoutExpired: return {'compiler':compiler,'mode':mode,'path':rel,'timeout':True}
  edges=set(); checked=set(); complete=False
  for line in r.stderr.splitlines():
    if line.startswith('MOSS_C1_SCOPE_TRACE_COMPLETE|'): complete=True
    elif line.startswith('MOSS_C1_SCOPE_EDGE|'):
      _,owner,kind,target=line.split('|',3); owner=owner.replace(str(root)+'/', ''); edges.add((owner,kind,target))
    elif line.startswith('MOSS_C1_SCOPE_ID|'):
      _,kind,source,identity=line.split('|',3); checked.add((kind,source.replace(str(root)+'/', ''),identity))
  return {'compiler':compiler,'mode':mode,'path':rel,'returncode':r.returncode,'complete':complete,
          'edges':sorted(edges),'checked':sorted(checked),'stderr_tail':r.stderr[-1500:] if not complete else ''}
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool: rows=list(pool.map(trace,work))
OUT.write_text('\n'.join(json.dumps(r,sort_keys=True,separators=(',',':')) for r in rows)+'\n')
idx={(r['compiler'],r['mode'],r['path']):r for r in rows}; diffs=[]
for rel in paths:
  for mode in ('off','shadow'):
    b=idx['baseline',mode,rel]; c=idx['candidate',mode,rel]
    if b.get('complete') and c.get('complete') and (b.get('checked')!=c.get('checked') or b.get('edges')!=c.get('edges')):
      bm=set(map(tuple,b.get('checked',[]))); cm=set(map(tuple,c.get('checked',[])))
      diffs.append({'mode':mode,'path':rel,'baseline_only_checked':sorted(bm-cm),'candidate_only_checked':sorted(cm-bm),
                    'edge_missing':sorted(set(map(tuple,b.get('edges',[])))-set(map(tuple,c.get('edges',[])))),
                    'edge_added':sorted(set(map(tuple,c.get('edges',[])))-set(map(tuple,b.get('edges',[]))))})
summary={'files':len(paths),'runs':len(rows),'trace_complete':sum(bool(r.get('complete')) for r in rows),
         'timeouts':sum(bool(r.get('timeout')) for r in rows),'complete_paired_cases':sum(1 for rel in paths for mode in ('off','shadow') if idx['baseline',mode,rel].get('complete') and idx['candidate',mode,rel].get('complete')),
         'same_mode_closure_or_edge_differences':len(diffs),'differences':diffs}
OUT.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='differences'},sort_keys=True))
