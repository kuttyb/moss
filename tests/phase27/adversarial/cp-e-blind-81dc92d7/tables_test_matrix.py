#!/usr/bin/env python3
import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[4];base=root.parent/'cp-e-baseline';out=pathlib.Path(__file__).resolve().parent;rows=[]
for label,w in [('baseline',base),('candidate',root)]:
 for mode in ('off','shadow'):
  env=dict(os.environ,MOSS=str(w/'build/compiler/moss-opt'),MOSS_EST=mode)
  r=subprocess.run([str(w/'margo'),'test','--json'],cwd=w/'sdk/tables',env=env,text=True,capture_output=True,timeout=60)
  try:data=json.loads(r.stdout)
  except Exception:data={'stdout':r.stdout,'stderr':r.stderr,'parse_error':True}
  rows.append({'compiler':label,'mode':mode,'rc':r.returncode,'result':data,'stderr':r.stderr})
(out/'tables-test-matrix.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps([{'compiler':r['compiler'],'mode':r['mode'],'rc':r['rc'],'ok':r['result'].get('ok'),'error':r['result'].get('error')} for r in rows],indent=2))
