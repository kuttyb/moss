#!/usr/bin/env python3
import importlib.util,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]; BASE=ROOT.parent/'cp-e-independent-d28-baseline'; PROBES=ROOT/'tests/phase27/adversarial/cp-e-independent-d28/probes'; OUT=ROOT/'tests/phase27/adversarial/cp-e-independent-d28/probe-matrix.jsonl'
def modules(root):
 s=importlib.util.spec_from_file_location('sf',root/'sdk/advanced-data-acceptance/check_source_free.py'); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); return ':'.join(str(p/'build/debug') for _,p in m.PROVIDERS)
def norm(x,r):
 if isinstance(x,str): return x.replace(str(r),'<ROOT>')
 if isinstance(x,list): return [norm(v,r) for v in x]
 if isinstance(x,dict): return {k:norm(v,r) for k,v in x.items()}
 return x
rows=[]
for f in sorted(PROBES.glob('*.moss')):
 row={'probe':f.name,'modes':{}}
 for name,root,mode in [('baseline-off',BASE,'off'),('baseline-shadow',BASE,'shadow'),('candidate-off',ROOT,'off'),('candidate-shadow',ROOT,'shadow')]:
  env=os.environ.copy(); env.update(MOSS_EST=mode,MOSS_MODULE_PATH=modules(root))
  p=subprocess.run([str(root/'build/compiler/moss-dev'),'check',str(f.relative_to(ROOT)),'--json'],cwd=ROOT if name.startswith('candidate') else BASE,env=env,capture_output=True,text=True)
  try: d=json.loads(p.stdout)
  except json.JSONDecodeError: d=None
  if d is None: signature={'ok':'NOJSON','stderr':norm(p.stderr,root)}
  else: signature={'ok':d.get('ok'),'error':norm(d.get('error'),root),'diagnostics':norm(d.get('result',{}).get('diagnostics',[]),root)}
  row['modes'][name]={'returncode':p.returncode,'signature':signature}
 sig=[row['modes'][m]['signature'] for m in row['modes']]
 row['all_modes_equal']=all(v==sig[0] for v in sig)
 row['candidate_off_shadow_equal']=sig[2]==sig[3]
 rows.append(row)
OUT.write_text(''.join(json.dumps(x,sort_keys=True)+'\n' for x in rows))
for r in rows: print(r['probe'], 'equal=',r['all_modes_equal'],'candidate_off_shadow=',r['candidate_off_shadow_equal'],'verdicts=',[r['modes'][m]['signature'].get('ok') for m in r['modes']],'codes=',[(r['modes'][m]['signature'].get('error') or {}).get('code') for m in r['modes']])
