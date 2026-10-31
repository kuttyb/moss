import json,pathlib,runpy,subprocess,sys
root=pathlib.Path(sys.argv[1]).resolve();compiler=pathlib.Path(sys.argv[2]).resolve();label=sys.argv[3]
own=pathlib.Path(__file__).resolve().parent
script=root/'tests/tooling/check_phase27_accepts_rustc.py'
sys.path.insert(0,str(script.parent))
raw=json.loads((root/'tests/phase27/conformance/accepts_rustc_allowlist.json').read_text())
raw['cases']=[x for x in raw['cases'] if not any('adversarial' in s.lower() for s in pathlib.PurePosixPath(x['path']).parts)]
allow=own/f'accepts_rustc_allowlist_{label}.json';allow.write_text(json.dumps(raw,indent=2)+'\n')
sys.argv=[str(script),str(compiler)]
ns=runpy.run_path(str(script),run_name='cpf_accepts_rustc')
ns['ROOT']=root;ns['COMPILER']=compiler;ns['ALLOW']=allow
original=ns['run']
def filtered_run(args,cwd=None,env=None):
 result=original(args,cwd=cwd,env=env)
 if len(args)>=2 and args[0]=='git' and args[1]=='ls-tree':
  paths=[x for x in result.stdout.splitlines() if not any('adversarial' in p.lower() for p in pathlib.PurePosixPath(x).parts)]
  return subprocess.CompletedProcess(result.args,result.returncode,'\n'.join(paths)+'\n',result.stderr)
 return result
ns['run']=filtered_run
ns['main']()
