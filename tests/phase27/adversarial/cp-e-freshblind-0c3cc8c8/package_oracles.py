import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2]; rows=[]
for label,tree in [('baseline',root.parent/'cp-e-baseline-04be1a1'),('candidate',root)]:
 for name,cmd,cwd,home in [
  ('tables_build',['../../margo','build','--json'],'sdk/tables','margo-tables-build'),
  ('tables_test',['../../margo','test','--json'],'sdk/tables','margo-tables-test'),
  ('advanced_build',['../../margo','build','--json'],'sdk/advanced-data-acceptance','margo-advanced-build'),
  ('source_free_abi9_debug',['python3','sdk/advanced-data-acceptance/check_source_free.py','--abi','v9','--profile','debug'],'.','margo-source-free')]:
  env=os.environ.copy();env['MOSS']=str(tree/'moss');env['MARGO_HOME']=str(tree/'tmp/cp-e-evidence'/home)
  r=subprocess.run(cmd,cwd=tree/cwd,env=env,text=True,capture_output=True)
  rows.append({'tree':label,'oracle':name,'command':' '.join(cmd),'exit_code':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
out=root/'tmp/cp-e-evidence/package-oracles.json';out.write_text(json.dumps(rows,indent=2)+'\n')
summary=[{'tree':r['tree'],'oracle':r['oracle'],'exit_code':r['exit_code'],'ok':r['exit_code']==0,'error':r['stdout'][:500] if r['exit_code'] else None} for r in rows]
(root/'tmp/cp-e-evidence/package-oracles-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
