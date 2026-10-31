import json, os, pathlib, subprocess, sys
cand=pathlib.Path(sys.argv[1]).resolve(); base=pathlib.Path(sys.argv[2]).resolve(); own=pathlib.Path(__file__).resolve().parent
examples=['examples/hi.moss','examples/collections.moss','examples/domains_and_messages.moss','examples/mini_application.moss','examples/phase2_safety.moss','examples/functional_pipeline.moss']
rows=[]
for src in examples:
 r={'source':src}
 for label,root,binary in [('baseline',base,base/'build/compiler/moss-dev'),('candidate',cand,cand/'build/compiler/moss-dev')]:
  env=os.environ.copy();env['MOSS_EST']='off'
  try:
   p=subprocess.run([str(binary),'run','--interp',src],cwd=root,env=env,capture_output=True,text=True,timeout=15)
   r[label]={'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
  except subprocess.TimeoutExpired as e:
   r[label]={'timeout_seconds':15,'stdout':e.stdout.decode(errors='replace') if isinstance(e.stdout,bytes) else e.stdout,'stderr':e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else e.stderr}
 r['equal']=r.get('baseline')==r.get('candidate');rows.append(r)
(own/'fast_debug_examples.json').write_text(json.dumps(rows,indent=2)+'\n')
print(f'{len(rows)} Fast Debug examples; {sum(r["equal"] for r in rows)} equal')
for r in rows:
 if not r['equal']: print('DIFF',r['source'],r['baseline'],r['candidate'])
