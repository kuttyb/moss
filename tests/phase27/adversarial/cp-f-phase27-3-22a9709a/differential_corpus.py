import concurrent.futures, hashlib, json, os, pathlib, subprocess, sys
cand=pathlib.Path(sys.argv[1]).resolve(); base=pathlib.Path(sys.argv[2]).resolve(); own=pathlib.Path(__file__).resolve().parent
paths=subprocess.check_output(['git','ls-tree','-r','--name-only','HEAD'],cwd=cand,text=True).splitlines()
paths=[p for p in paths if p.endswith('.moss') and not any('adversarial' in q.lower() for q in pathlib.PurePosixPath(p).parts)]
def as_text(value):
 return value.decode(errors='replace') if isinstance(value,bytes) else (value or '')
def run(root,binary,src,mode,args=()):
 env=os.environ.copy(); env['MOSS_EST']=mode
 try:
  p=subprocess.run([str(binary),'check',*args,src,'--json'],cwd=root,env=env,capture_output=True,text=True,timeout=120)
 except subprocess.TimeoutExpired as e:
  return {'kind':'timeout','timeout':120,'stdout':as_text(e.stdout), 'stderr':as_text(e.stderr)}
 try: j=json.loads(p.stdout)
 except Exception: return {'kind':'NOJSON','rc':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
 e=j.get('error') or {}
 return {'ok':j.get('ok'),'code':e.get('code'),'message':e.get('message'),'line':e.get('line'),'column':e.get('column'),'rc':p.returncode,'json':j}
def one(src):
 b=run(cand,base/'build/compiler/moss-dev',src,'off')
 co=run(cand,cand/'build/compiler/moss-dev',src,'off')
 cs=run(cand,cand/'build/compiler/moss-dev',src,'shadow')
 row={'source':src,'baseline':b,'candidate_off':co,'candidate_shadow':cs}
 def sig(x): return (x.get('ok'),x.get('code'),x.get('message'),x.get('line'),x.get('column'))
 row['diagnostic_equal']=(sig(b)==sig(co)==sig(cs) and not any(x.get('kind') for x in (b,co,cs)))
 if b.get('ok') and co.get('ok') and cs.get('ok'):
  for profile,args in [('default',()),('O0',('-O0',)),('optimized',('-Oshared-memory',))]:
   items=[]
   for label,root,binary,mode in [('baseline',cand,base/'build/compiler/moss-dev','off'),('candidate_off',cand,cand/'build/compiler/moss-dev','off'),('candidate_shadow',cand,cand/'build/compiler/moss-dev','shadow')]:
    out=root/'tmp/cpf/rust'/label/mode/(src.replace('/','_')+'.rs'); out.parent.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['MOSS_EST']=mode
    command=[str(binary),*args,src,'-o',str(out)]
    try:
     p=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True,timeout=120)
    except subprocess.TimeoutExpired as e:
     items.append({'kind':'timeout','timeout':120,'stderr':str(e.stderr or '')[:2000]}); continue
    if p.returncode or not out.exists(): items.append({'kind':'emit_failure','rc':p.returncode,'stderr':p.stderr[:2000]})
    else:
     raw=out.read_bytes(); normalized=raw.replace(str(out).encode(),b'<OUT>').replace(str(root).encode(),b'<ROOT>')
     items.append({'sha256_raw':hashlib.sha256(raw).hexdigest(),'sha256_normalized':hashlib.sha256(normalized).hexdigest()})
   row[profile]=items
  for profile in ('default','O0','optimized'):
   hashes=row.get(profile,[])
   raw=[x.get('sha256_raw') for x in hashes]
   normalized=[x.get('sha256_normalized') for x in hashes]
   row[profile+'_raw_equal']=len(set(raw))==1 and None not in raw
   row[profile+'_normalized_equal']=len(set(normalized))==1 and None not in normalized
   row[profile+'_emit_complete']=len(hashes)==3 and all(x.get('sha256_normalized') for x in hashes)
 return row
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for r in pool.map(one,paths): rows.append(r)
diffs=[r for r in rows if not r['diagnostic_equal'] or any(not r.get(p+'_normalized_equal',True) or not r.get(p+'_emit_complete',True) for p in ('default','O0','optimized'))]
out={'baseline_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=base,text=True).strip(),'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=cand,text=True).strip(),'candidate_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=cand,text=True).strip(),'source_count':len(paths),'difference_count':len(diffs),'differences':diffs,'benign_raw_path_difference_count':sum(1 for r in rows for p in ('default','O0','optimized') if r.get(p+'_normalized_equal') and not r.get(p+'_raw_equal')),'rows':rows,'pruned_adversarial_path_segments':True}
(own/'corpus_differential.json').write_text(json.dumps(out,indent=2)+'\n'); print(f'{len(paths)} sources; {len(diffs)} diagnostic/Rust-output differences; report {own}/corpus_differential.json')
