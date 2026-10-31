import json,os,pathlib,subprocess,sys
cand=pathlib.Path(sys.argv[1]).resolve();base=pathlib.Path(sys.argv[2]).resolve(); own=pathlib.Path(__file__).resolve().parent
pkg='tmp/cpf/pkg-malformed'
source='fn main():\n  echo 2 ** 3\n'
rows=[]
for label,root,binary in [('baseline',base,base/'build/compiler/moss-dev'),('candidate',cand,cand/'build/compiler/moss-dev')]:
 path=root/pkg; (path/'src').mkdir(parents=True,exist_ok=True)
 (path/'Moss.toml').write_text('[package]\nname = "cpf_malformed"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
 (path/'src/main.moss').write_text(source)
 for mode in ('off','shadow'):
  env=os.environ.copy();env.update(MOSS=str(binary),MOSS_EST=mode)
  p=subprocess.run([str(root/'margo'),'build','--check','--json'],cwd=path,env=env,capture_output=True,text=True,timeout=60)
  try:j=json.loads(p.stdout)
  except Exception:j={'raw_stdout':p.stdout,'stderr':p.stderr}
  e=j.get('error') or {}
  diag={}
  if isinstance(e,str) and '): ' in e:
   try: diag=json.loads(e.split('): ',1)[1]).get('error') or {}
   except Exception: pass
  if isinstance(e,dict): diag=e
  rows.append({'engine':label,'mode':mode,'returncode':p.returncode,'ok':j.get('ok'),'code':diag.get('code'),'message':diag.get('message'),'line':diag.get('line'),'column':diag.get('column'),'stdout':p.stdout,'stderr':p.stderr})
(own/'package_probes.json').write_text(json.dumps(rows,indent=2)+'\n')
print(f'{len(rows)} package builds; signatures={[(r["engine"],r["mode"],r["code"],r["message"]) for r in rows]}')
