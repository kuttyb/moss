#!/usr/bin/env python3
"""Paired package-mode check of the local dependency closure fixture."""
import json,os,pathlib,shutil,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[4]
fixture=root/'tests/phase27/adversarial/cp-e-c211da42/package'
out=root/'tests/phase27/adversarial/cp-e-c211da42'
rows={}
for label,compiler in [('baseline',pathlib.Path(sys.argv[1]).resolve()),('candidate',pathlib.Path(sys.argv[2]).resolve())]:
 work=root/'tmp'/('cp-e-package-'+label)
 shutil.rmtree(work,ignore_errors=True); work.mkdir(parents=True)
 app=work/'app'; shutil.copytree(fixture,app)
 env=dict(os.environ,MOSS=str(compiler),MOSS_EST='off',MARGO_HOME=str(work/'margo-home'))
 r=subprocess.run([str(root/'margo'),'build','--check','--json'],cwd=app/'app',env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try: payload=json.loads(r.stdout)
 except Exception: payload={'decode_error':True,'stdout':r.stdout}
 rows[label]={'exit':r.returncode,'payload':payload,'stderr':r.stderr}
(out/'package_raw.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')

def norm(row):
 p=row['payload']; p.pop('source_root',None)
 return row['exit'],p.get('ok'),p.get('error'),(p.get('result') or {}).get('diagnostics')
result={'baseline':norm(rows['baseline']),'candidate':norm(rows['candidate']),'equal':norm(rows['baseline'])==norm(rows['candidate'])}
(out/'package_summary.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
print(json.dumps(result,indent=2)); sys.exit(not result['equal'])
