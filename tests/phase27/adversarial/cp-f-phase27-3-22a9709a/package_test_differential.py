import json,os,pathlib,subprocess,sys
root=pathlib.Path(sys.argv[1]).resolve(); binary=pathlib.Path(sys.argv[2]).resolve(); label=sys.argv[3]
own=pathlib.Path(__file__).resolve().parent
packages=['examples/swarm/modules/calc_interpreter/repro/natural_full','examples/swarm/modules/text_toolkit/toolkit']
rows=[]
for package in packages:
 env=os.environ.copy();env.update(MOSS=str(binary),MOSS_EST='off',MARGO_HOME=str(root/'tmp'/f'cpf-margo-{label}'))
 try:
  p=subprocess.run([str(root/'margo'),'test'],cwd=root/package,env=env,capture_output=True,text=True,timeout=600)
  rows.append({'package':package,'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
 except subprocess.TimeoutExpired as e:
  rows.append({'package':package,'timeout_seconds':600,'stdout':e.stdout.decode(errors='replace') if isinstance(e.stdout,bytes) else e.stdout or '', 'stderr':e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else e.stderr or ''})
out=own/f'package_tests_{label}.json';out.write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps([{'package':x['package'],'returncode':x.get('returncode'),'timeout':x.get('timeout_seconds'),'stdout':x.get('stdout','')[-1500:],'stderr':x.get('stderr','')[-1500:]} for x in rows],indent=2))
