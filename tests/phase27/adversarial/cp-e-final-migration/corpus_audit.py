#!/usr/bin/env python3
"""Independent Phase 27.3 off/shadow verdict and diagnostic parity sweep."""
import argparse, concurrent.futures, hashlib, json, os, pathlib, subprocess

p=argparse.ArgumentParser()
p.add_argument('--candidate',required=True); p.add_argument('--baseline',required=True)
p.add_argument('--root',required=True); p.add_argument('--out',required=True)
p.add_argument('--jobs',type=int,default=6); p.add_argument('--timeout',type=int,default=120)
a=p.parse_args(); root=pathlib.Path(a.root).resolve(); out=pathlib.Path(a.out).resolve()
out.mkdir(parents=True,exist_ok=True)
sources=sorted(x for x in (root/'tests').rglob('*.moss') if 'build' not in x.parts)
def run(binary,source,mode):
 env=dict(os.environ,MOSS_EST=mode)
 try:
  r=subprocess.run([str(pathlib.Path(binary).resolve()),'check',str(source),'--json'],cwd=root,env=env,text=True,capture_output=True,timeout=a.timeout)
  try: payload=json.loads(r.stdout)
  except Exception: payload={'decode_error':True,'stdout':r.stdout,'stderr':r.stderr}
  return {'exit':r.returncode,'payload':payload,'stderr':r.stderr}
 except subprocess.TimeoutExpired:
  return {'timeout':True}
def sig(x):
 if x.get('timeout'): return ('TIMEOUT',)
 p=x['payload']; result=p.get('result') or {}
 return x['exit'],p.get('ok'),p.get('error'),result.get('diagnostics')
def one(src):
 rel=src.relative_to(root).as_posix()
 return {'path':rel,'baseline_off':run(a.baseline,src,'off'),
         'candidate_off':run(a.candidate,src,'off'),
         'candidate_shadow':run(a.candidate,src,'shadow')}
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
 for i,row in enumerate(pool.map(one,sources),1):
  rows.append(row)
  if i%100==0: print(f'completed {i}/{len(sources)}',flush=True)
(out/'corpus.jsonl').write_text(''.join(json.dumps(r,sort_keys=True,separators=(',',':'))+'\n' for r in rows))
off=[r for r in rows if sig(r['baseline_off'])!=sig(r['candidate_off'])]
shadow=[r for r in rows if sig(r['candidate_off'])!=sig(r['candidate_shadow'])]
timeouts=[r['path'] for r in rows if any(x.get('timeout') for x in (r['baseline_off'],r['candidate_off'],r['candidate_shadow']))]
summary={'source_count':len(rows),'baseline_accepted':sum(r['baseline_off'].get('payload',{}).get('ok') is True for r in rows),
 'candidate_off_accepted':sum(r['candidate_off'].get('payload',{}).get('ok') is True for r in rows),
 'candidate_shadow_accepted':sum(r['candidate_shadow'].get('payload',{}).get('ok') is True for r in rows),
 'off_difference_count':len(off),'shadow_difference_count':len(shadow),'timeouts':timeouts,
 'off_differences':off,'shadow_differences':shadow}
(out/'corpus-summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ('off_differences','shadow_differences')},indent=2))
