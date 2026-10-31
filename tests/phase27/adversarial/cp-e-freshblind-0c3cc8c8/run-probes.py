import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2]; candidate=root; baseline=root.parent/'cp-e-baseline-04be1a1'; pdir=root/'tmp/cp-e-evidence/probes'; out=[]
for f in sorted(pdir.glob('*.moss')):
 row={'probe':f.name}
 for label,tree,mode in [('baseline_off',baseline,'off'),('candidate_off',candidate,'off'),('candidate_shadow',candidate,'shadow')]:
  env=os.environ.copy();env['MOSS_EST']=mode
  r=subprocess.run([str(tree/'moss'),'check',str(tree/'tmp/cp-e-evidence/probes'/f.name),'--json'],cwd=tree,env=env,text=True,capture_output=True)
  try:d=json.loads(r.stdout)
  except Exception:d={'ok':False,'error':{'code':'NON_JSON','message':r.stderr}}
  e=d.get('error') or {}; row[label]={'ok':bool(d.get('ok')),'code':e.get('code'),'message':e.get('message'),'line':e.get('line'),'column':e.get('column'),'returncode':r.returncode}
 out.append(row)
(root/'tmp/cp-e-evidence/probes.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out))
