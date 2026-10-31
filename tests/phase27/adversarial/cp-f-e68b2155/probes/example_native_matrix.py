import concurrent.futures,hashlib,json,os,pathlib,subprocess
root=pathlib.Path.cwd();work=root/'tmp/phase27-3/cp-f-agent2-e68b2155';corpus=work/'corpus';sources=[x for x in subprocess.check_output(['git','ls-tree','-r','--name-only','e68b21557e675cc4697c391a083ead5add7ad76a'],text=True).splitlines() if x.startswith('examples/') and x.count('/')==1 and x.endswith('.moss')]
bins={'phase27-2':root.parent/'cp-f-agent2-baseline-04be1a1/build/compiler/moss-dev','phase27-3':root/'build/compiler/moss-dev'}
def run(args,env,timeout=120):
 try:return subprocess.run(args,cwd=corpus,env=env,text=True,capture_output=True,timeout=timeout)
 except subprocess.TimeoutExpired as e:return None
# cache check validity/signatures from the complete check matrix
checkdata={}
for line in (work/'check_matrix.jsonl').read_text().splitlines():
 r=json.loads(line);checkdata[(r['rev'],r['mode'],r['source'])]=r
revmap={'phase27-2':'baseline','phase27-3':'candidate'}
cases=[(rev,mode,src,opt) for rev in bins for mode in ('off','shadow') for src in sources for opt in ('default','optimized')]
def task(c):
 rev,mode,src,opt=c;env=os.environ.copy();env['MOSS_EST']=mode;out=work/'example-native-rust'/rev/mode/(src.replace('/','_')+opt+'.rs');exe=out.with_suffix('.bin');out.parent.mkdir(parents=True,exist_ok=True)
 ck=checkdata[(revmap[rev],mode,src)]
 if ck['kind']!='ok': return {'revision':rev,'mode':mode,'source':src,'optimization':opt,'check':ck.get('data',{}).get('error')}
 compiler=bins[rev];args=[str(compiler),src]
 if opt=='optimized':args.append('-O')
 args+=['-o',str(out)];em=run(args,env)
 if em is None or em.returncode or not out.exists():return {'revision':rev,'mode':mode,'source':src,'optimization':opt,'emit_error':getattr(em,'stderr','timeout')}
 digest=hashlib.sha256(out.read_bytes()).hexdigest();crate='phase27_example_'+hashlib.sha1((rev+mode+src+opt).encode()).hexdigest()[:12];rc=run(['rustc','--edition=2021','--crate-name',crate,str(out),'-o',str(exe)],env)
 if rc is None or rc.returncode:return {'revision':rev,'mode':mode,'source':src,'optimization':opt,'rust_sha256':digest,'rustc_error':getattr(rc,'stderr','timeout')}
 execution=run([str(exe)],env,timeout=180)
 if execution is None:return {'revision':rev,'mode':mode,'source':src,'optimization':opt,'rust_sha256':digest,'native_timeout':True}
 return {'revision':rev,'mode':mode,'source':src,'optimization':opt,'rust_sha256':digest,'returncode':execution.returncode,'stdout':execution.stdout,'stderr':execution.stderr}
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for i,r in enumerate(pool.map(task,cases),1):
  rows.append(r)
  if i%20==0:print(f'{i}/{len(cases)}',flush=True)
out=work/'example_native_matrix.jsonl';out.write_text(''.join(json.dumps(r,separators=(',',':'),sort_keys=True)+'\n' for r in rows))
ix={(r.get('revision'),r.get('mode'),r.get('source'),r.get('optimization')):r for r in rows};diffs=[]
for mode in ('off','shadow'):
 for src in sources:
  for opt in ('default','optimized'):
   a=ix[('phase27-2',mode,src,opt)];b=ix[('phase27-3',mode,src,opt)]
   sig=lambda r:(r.get('returncode'),r.get('stdout'),r.get('stderr'),r.get('native_timeout'),r.get('emit_error'),r.get('rustc_error'))
   if sig(a)!=sig(b) or a.get('rust_sha256')!=b.get('rust_sha256'):diffs.append({'source':src,'mode':mode,'optimization':opt,'baseline':sig(a),'candidate':sig(b),'hash_equal':a.get('rust_sha256')==b.get('rust_sha256')})
print(json.dumps({'sources':len(sources),'runs':len(rows),'accepted':sum('rust_sha256'in r for r in rows),'native_errors':sum(any(k in r for k in ('emit_error','rustc_error','native_timeout')) for r in rows),'differential_count':len(diffs),'diffs':diffs[:15]},indent=2));print('raw:',out)
