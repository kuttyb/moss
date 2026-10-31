#!/usr/bin/env python3
import json,os,pathlib,subprocess,shutil
root=pathlib.Path(__file__).resolve().parents[4]; base=root.parent/'cp-e-baseline'; out=pathlib.Path(__file__).resolve().parent
rows=[]
for label,worktree in [('baseline',base),('candidate',root)]:
 for mode in ('off','shadow'):
  env=dict(os.environ,MOSS=str(worktree/'build/compiler/moss-opt'),MOSS_EST=mode,MARGO_HOME=str(worktree/'tmp'/('cp-e-open-'+label+'-'+mode)))
  package=worktree/'tmp'/('cp-e-open-probe-'+label+'-'+mode)
  if package.exists():shutil.rmtree(package)
  shutil.copytree(root/'tests/phase27/est/repro/package_open_collection',package)
  r=subprocess.run([str(worktree/'margo'),'build','--json'],cwd=package,env=env,text=True,capture_output=True,timeout=60)
  try:data=json.loads(r.stdout)
  except Exception:data={'stdout':r.stdout,'stderr':r.stderr,'parse_error':True}
  rows.append({'compiler':label,'mode':mode,'rc':r.returncode,'result':data,'stderr':r.stderr})
(out/'open-collection-package-matrix.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps([{'compiler':r['compiler'],'mode':r['mode'],'rc':r['rc'],'ok':r['result'].get('ok'),'error':r['result'].get('error')} for r in rows],indent=2))
