#!/usr/bin/env python3
"""Standalone and Margo diagnostics for compiled-only ABI-v9 module providers."""
import json,os,pathlib,shutil,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[4]
candidate=pathlib.Path(sys.argv[1]).resolve(); baseline=pathlib.Path(sys.argv[2]).resolve()
out=pathlib.Path(sys.argv[3]).resolve(); out.mkdir(parents=True,exist_ok=True)
scratch=root/'tmp/cp-e-final-migration-source-free-oracle'
scratch.mkdir(parents=True,exist_ok=True)
margo=root/'margo'
provider_source='''module Service\nexport fn answer(value: Int) -> Int:\n  return value + 1\n'''
consumer_source='''module Main\nimport Service\nfn main():\n  echo Service.answer("wrong-type")\n'''
def pkg(path,name,source):
 (path/'src').mkdir(parents=True)
 (path/'Moss.toml').write_text(f'[package]\nname = "{name}"\nversion = "0.1.0"\n[build]\nsource = "src"\n')
 (path/'src/main.moss').write_text(source)
def invoke(cmd,cwd,env):
 p=subprocess.run(cmd,cwd=cwd,env=env,text=True,capture_output=True,timeout=300)
 try:j=json.loads(p.stdout)
 except Exception:j={'decode_error':True,'stdout':p.stdout}
 return {'exit':p.returncode,'json':j,'stderr':p.stderr,'stdout':p.stdout}
def diag(row):
 j=row['json']; e=j.get('error') or {}
 if isinstance(e,str):
  start=e.find('{')
  if start>=0:
   try:
    nested,_=json.JSONDecoder().raw_decode(e[start:]); j=nested; e=j.get('error') or {}
   except json.JSONDecodeError: pass
 if isinstance(e,str): return ('UNWRAPPED_ERROR',e)
 ds=(j.get('result') or {}).get('diagnostics') or []
 if e:return (e.get('code'),e.get('line'),e.get('message'))
 return tuple((d.get('code'),d.get('line'),d.get('message')) for d in ds)
rows={}
for label,compiler in [('baseline',baseline),('candidate',candidate)]:
 work=scratch/label
 shutil.rmtree(work,ignore_errors=True); work.mkdir(parents=True)
 provider=work/'provider'; pkg(provider,'source_free_provider',provider_source)
 env=dict(os.environ,MOSS=str(compiler),MOSS_EST='off',MARGO_HOME=str(work/'margo-provider-home'))
 built=invoke([str(margo),'build'],provider,env)
 if built['exit']!=0: raise SystemExit(f'{label} provider build failed: {built}')
 artifacts=work/'artifacts'; shutil.copytree(provider/'build/debug',artifacts)
 shutil.rmtree(provider/'src')
 app=work/'app'; pkg(app,'source_free_consumer',consumer_source)
 appenv=dict(env,MOSS_MODULE_PATH=str(artifacts),MARGO_HOME=str(work/'margo-app-home'))
 standalone=invoke([str(compiler),'check','src/main.moss','--json'],app,appenv)
 package=invoke([str(margo),'build','--check','--json'],app,appenv)
 rows[label]={'provider_build':built,'provider_source_removed':not (provider/'src').exists(),
              'standalone':standalone,'package':package,
              'standalone_diagnostic':diag(standalone),'package_diagnostic':diag(package),
              'mode_equal':diag(standalone)==diag(package)}
result={'baseline':rows['baseline'],'candidate':rows['candidate'],
 'baseline_candidate_standalone_equal':rows['baseline']['standalone_diagnostic']==rows['candidate']['standalone_diagnostic'],
 'baseline_candidate_package_equal':rows['baseline']['package_diagnostic']==rows['candidate']['package_diagnostic'],
 'candidate_mode_equal':rows['candidate']['mode_equal'],
 'baseline_mode_equal':rows['baseline']['mode_equal']}
(out/'source-free-oracle.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('baseline','candidate')},indent=2))
