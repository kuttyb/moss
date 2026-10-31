import concurrent.futures,hashlib,json,os,pathlib,subprocess,time
root=pathlib.Path.cwd(); work=root/'tmp/phase27-3/cp-f-agent2-e68b2155'; corpus=work/'corpus'
accepted=[x.split('\t',1)[0] for x in (work/'rust-baseline-off.tsv').read_text().splitlines() if x.endswith(tuple(':0' for _ in []))]
accepted=[]
for x in (work/'rust-baseline-off.tsv').read_text().splitlines():
 src,sig=x.split('\t',1)
 if sig.startswith('ok:'): accepted.append(src)
compilers={'baseline':root.parent/'cp-f-agent2-baseline-04be1a1/build/compiler/moss-opt','candidate':root/'build/compiler/moss-opt'}
cases=[(rev,mode,src) for rev in compilers for mode in ('off','shadow') for src in accepted]
def run(case):
 rev,mode,src=case; env=os.environ.copy(); env['MOSS_EST']=mode; out=work/'optimized-rust'/rev/mode/(src.replace('/','_')+'.rs'); out.parent.mkdir(parents=True,exist_ok=True)
 try:p=subprocess.run([str(compilers[rev]),src,'-O','-o',str(out)],cwd=corpus,env=env,text=True,capture_output=True,timeout=120)
 except subprocess.TimeoutExpired:return {'rev':rev,'mode':mode,'source':src,'error':'timeout'}
 if p.returncode or not out.is_file():return {'rev':rev,'mode':mode,'source':src,'error':'emit','stderr':p.stderr[-2000:]}
 return {'rev':rev,'mode':mode,'source':src,'sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=12) as ex:
 for i,row in enumerate(ex.map(run,cases),1):
  rows.append(row)
  if i%500==0:print(f'{i}/{len(cases)}',flush=True)
(work/'optimized_matrix.jsonl').write_text(''.join(json.dumps(r,sort_keys=True,separators=(',',':'))+'\n' for r in rows))
idx={(r.get('rev'),r.get('mode'),r.get('source')):r for r in rows}; diffs=[]
for mode in ('off','shadow'):
 for src in accepted:
  a=idx[('baseline',mode,src)]; b=idx[('candidate',mode,src)]
  if a.get('sha256')!=b.get('sha256') or 'error' in a or 'error' in b:diffs.append({'mode':mode,'source':src,'baseline':a,'candidate':b})
print(json.dumps({'accepted':len(accepted),'runs':len(rows),'errors':sum('error' in r for r in rows),'diffs':len(diffs),'examples':diffs[:20]},indent=2))
