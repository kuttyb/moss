#!/usr/bin/env python3
import json,os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[4]
bins={'baseline':root/'tmp/cp-e-post-taint/baseline/build/compiler/moss-dev','candidate':root/'build/compiler/moss-dev'}
sources=['examples/swarm/domain_torture/domain_boundaries/probe3e_store_map.moss','sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss','tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss']
out=[]
for src in sources:
 row={'source':src,'runs':{}}
 for side,binary in bins.items():
  row['runs'][side]={}
  for mode in ('off','shadow'):
   env=os.environ.copy();env['MOSS_EST']=mode
   p=subprocess.run([str(binary),'check',src,'--json'],cwd=root,env=env,text=True,capture_output=True)
   try: payload=json.loads(p.stdout);err=payload.get('error') or {}; result={'ok':payload.get('ok'),'code':err.get('code'),'message':err.get('message'),'line':err.get('line'),'returncode':p.returncode,'stderr':p.stderr}
   except Exception:result={'ok':None,'code':'NOJSON','message':None,'line':None,'returncode':p.returncode,'stderr':p.stderr}
   row['runs'][side][mode]=result
 out.append(row)
path=root/'tmp/cp-e-post-taint/diagnostics.json';path.write_text(json.dumps(out,indent=2)+'\n')
print(path)
