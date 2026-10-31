import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2];out=[]
for label,workspace,compiler in [('baseline','baseline-sdk-clean',root.parent/'cp-e-baseline-04be1a1/moss'),('candidate','candidate-sdk-clean',root/'moss')]:
 work=root/'tmp/cp-e-evidence'/workspace
 env=os.environ.copy();env['MOSS']=str(compiler);env['MARGO_HOME']=str(root/'tmp/cp-e-evidence'/f'margo-{label}-clean')
 cmd=['../../../../margo','build','--check','--json']
 p=subprocess.run(cmd,cwd=work/'advanced-data-acceptance',env=env,text=True,capture_output=True)
 payload=json.loads(p.stdout);out.append({'tree':label,'command':'margo build --check --json','exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr,'ok':payload.get('ok'),'checked':payload.get('checked'),'build_order':payload.get('build_order'),'skipped':payload.get('skipped')})
(root/'tmp/cp-e-evidence/checked-declaration-closure.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps([{k:r[k] for k in ('tree','exit_code','ok','checked','build_order','skipped')} for r in out],indent=2))
