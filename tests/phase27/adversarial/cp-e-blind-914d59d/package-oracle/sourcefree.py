import json,os,pathlib,subprocess,sys
root=pathlib.Path(sys.argv[1]).resolve(); app=root/'tmp/cpe-sourcefree-app'; (app/'src').mkdir(parents=True,exist_ok=True)
(app/'Moss.toml').write_text('[package]\nname = "cpe_sourcefree"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
(app/'src/main.moss').write_text('module app\nimport tables\n\nfn main():\n  echo tables.kind_int()\n')
modpaths=[str(p) for p in (root/'sdk').glob('**/build/debug') if p.is_dir()]
env=os.environ.copy(); env['MOSS_MODULE_PATH']=':'.join(modpaths); env['MARGO_HOME']=str(root/'tmp/margo-cpe-sourcefree'); env['TMPDIR']=str(root/'tmp')
steps=[]
for name,cmd in [('check',[str(root/'moss'),'check','src/main.moss','--json']),('build',[str(root/'margo'),'build','--moss-json']),('run',[str(app/'build/debug/cpe_sourcefree')])]:
 p=subprocess.run(cmd,cwd=app,env=env,capture_output=True,text=True,timeout=300); steps.append({'step':name,'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
 if p.returncode: break
json.dump(steps,open(root/'tmp/cpe-sourcefree-result.json','w'),indent=2)
print(json.dumps([{k:v for k,v in x.items() if k!='stdout'} for x in steps],indent=2))
