#!/usr/bin/env python3
import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[4]; base=root.parent/'cp-e-baseline'; out=pathlib.Path(__file__).resolve().parent
rows=[]
for label,worktree in [('baseline',base),('candidate',root)]:
 for mode in ('off','shadow'):
  env=dict(os.environ,MOSS=str(worktree/'build/compiler/moss-opt'),MOSS_EST=mode)
  run=subprocess.run(['python3','sdk/advanced-data-acceptance/check_source_free.py','--abi','v9','--profile','release'],cwd=worktree,env=env,text=True,capture_output=True,timeout=180)
  row={'compiler':label,'mode':mode,'rc':run.returncode,'stdout':run.stdout,'stderr':run.stderr}
  rows.append(row)
  if run.returncode:break
(out/'source-free-oracle.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps([{'compiler':r['compiler'],'mode':r['mode'],'rc':r['rc'],'stdout':r['stdout'].strip(),'stderr':r['stderr'][-1000:]} for r in rows],indent=2))
if len(rows)!=4 or any(r['rc'] for r in rows):raise SystemExit(1)
