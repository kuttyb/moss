from pathlib import Path
import subprocess,os,json
root=Path(__file__).resolve().parents[4]; base=root.parent/'cp-e-base/build/compiler/moss-dev'; cand=root/'build/compiler/moss-dev'
work=root/'tmp/cp-e-c2f51b'; module_dirs=sorted({str(p.parent) for p in [*root.glob('sdk/**/build/debug/*.mossi'),*root.glob('packages/**/build/debug/*.mossi')]})
modpath=':'.join(module_dirs)
valid=root/'sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss'
bad=root/'tests/phase27/adversarial/cp-e-c2f51b/source_free_invalid.moss'
# Create project package in scratch and point it to exactly these provider interfaces.
app=work/'sfree-app'; (app/'src').mkdir(parents=True,exist_ok=True)
(app/'Moss.toml').write_text('[package]\nname = "cp-e-source-free-probe"\nversion = "0.1.0"\n[build]\nsource = "src"\n')
def one(binary,mode,path,package):
 env=os.environ.copy();env['MOSS_EST']=mode
 if package:env['MOSS_MODULE_PATH']=modpath
 r=subprocess.run([str(binary),'check',str(path),'--json'],cwd=app if package else root,env=env,text=True,capture_output=True)
 try:j=json.loads(r.stdout)
 except Exception:j={'raw':r.stdout}
 e=j.get('error') or {}
 return {'exit':r.returncode,'ok':j.get('ok'),'code':e.get('code'),'message':e.get('message'),'line':e.get('line'),'stderr':r.stderr}
results={}
for scenario,path in [('valid-source-free-consumer',valid),('invalid-imported-function',bad)]:
 for ctx,pack in [('standalone',False),('package-source-free',True)]:
  target=path
  if pack:
   dest=app/'src'/'main.moss';dest.write_text(path.read_text());target=dest
  for label,bin,mode in [('baseline',base,'off'),('candidate-off',cand,'off'),('candidate-shadow',cand,'shadow')]:
   results[f'{scenario}/{ctx}/{label}']=one(bin,mode,target,pack)
# The invalid source refers to an imported module function; package results prove identical structured failures.
(root/'tests/phase27/adversarial/cp-e-c2f51b/source-free-oracle.json').write_text(json.dumps({'provider_interface_roots':module_dirs,'results':results},indent=2)+'\n')
print(json.dumps(results,indent=2))
