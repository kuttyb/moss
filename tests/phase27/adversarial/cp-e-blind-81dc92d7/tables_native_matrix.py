#!/usr/bin/env python3
import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[4]; base=root.parent/'cp-e-baseline'; out=pathlib.Path(__file__).resolve().parent
rows=[]
for label,worktree in [('baseline',base),('candidate',root)]:
 for mode in ('off','shadow'):
  env=dict(os.environ,MOSS=str(worktree/'build/compiler/moss-opt'),MOSS_EST=mode,MARGO_HOME=str(worktree/'tmp'/('cp-e-tables-'+label+'-'+mode)))
  for package in ('sdk/foundations/core','sdk/text','sdk/fmt','sdk/parse','sdk/csv','sdk/tables'):
   subprocess.run([str(worktree/'margo'),'clean'],cwd=worktree/package,env=env,text=True,capture_output=True,timeout=30,check=True)
  r=subprocess.run([str(worktree/'margo'),'build','--release','--json'],cwd=worktree/'sdk/tables',env=env,text=True,capture_output=True,timeout=180)
  try:data=json.loads(r.stdout)
  except Exception:data={'stdout':r.stdout,'stderr':r.stderr,'parse_error':True}
  rows.append({'compiler':label,'mode':mode,'rc':r.returncode,'result':data,'stderr':r.stderr})
(out/'tables-native-matrix.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps([{'compiler':r['compiler'],'mode':r['mode'],'rc':r['rc'],'ok':r['result'].get('ok'),'native_checked':r['result'].get('native_checked'),'native_skipped':r['result'].get('native_skipped')} for r in rows],indent=2))
if len(rows)!=4 or any(r['rc'] or not r['result'].get('ok') or 'moss.tables' not in r['result'].get('native_checked',[]) for r in rows):raise SystemExit(1)
