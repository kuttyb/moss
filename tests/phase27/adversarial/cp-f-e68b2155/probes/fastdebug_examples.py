import concurrent.futures,hashlib,json,os,pathlib,subprocess,time
root=pathlib.Path.cwd();work=root/'tmp/phase27-3/cp-f-agent2-e68b2155';corpus=work/'corpus'; sources=[x for x in subprocess.check_output(['git','ls-tree','-r','--name-only','e68b21557e675cc4697c391a083ead5add7ad76a'],text=True).splitlines() if x.startswith('examples/') and x.count('/')==1 and x.endswith('.moss')]
bins={'phase27-2':root.parent/'cp-f-agent2-baseline-04be1a1/build/compiler/moss-dev','phase27-3':root/'build/compiler/moss-dev'}
def run(c):
 rev,mode,src=c; env=os.environ.copy();env['MOSS_EST']=mode;cmd=[str(bins[rev]),'debug',src];t=time.time()
 try:p=subprocess.run(cmd,cwd=corpus,env=env,text=True,capture_output=True,timeout=180);return {'revision':rev,'mode':mode,'source':src,'command':cmd,'status':'completed','returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr,'elapsed_seconds':round(time.time()-t,3)}
 except subprocess.TimeoutExpired as e:
  so=e.stdout.decode(errors='replace') if isinstance(e.stdout,bytes) else e.stdout or '';se=e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else e.stderr or '';return {'revision':rev,'mode':mode,'source':src,'command':cmd,'status':'timeout','timeout_seconds':180,'stdout':so,'stderr':se,'elapsed_seconds':round(time.time()-t,3)}
cases=[(r,m,s) for r in bins for m in ('off','shadow') for s in sources];rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
 for i,r in enumerate(ex.map(run,cases),1):
  rows.append(r)
  if i%10==0:print(f'{i}/{len(cases)}',flush=True)
out=work/'fastdebug_examples.jsonl';out.write_text(''.join(json.dumps(r,separators=(',',':'),sort_keys=True)+'\n' for r in rows))
ix={(r['revision'],r['mode'],r['source']):r for r in rows};diffs=[]
for src in sources:
 for mode in ('off','shadow'):
  a=ix['phase27-2',mode,src];b=ix['phase27-3',mode,src]
  sa=(a['status'],a.get('returncode'),a.get('stdout'),a.get('stderr'));sb=(b['status'],b.get('returncode'),b.get('stdout'),b.get('stderr'))
  if sa!=sb:diffs.append({'source':src,'mode':mode,'baseline_status':a['status'],'candidate_status':b['status'],'baseline_sha256':hashlib.sha256((a.get('stdout','')+a.get('stderr','')).encode()).hexdigest(),'candidate_sha256':hashlib.sha256((b.get('stdout','')+b.get('stderr','')).encode()).hexdigest()})
print(json.dumps({'sources':len(sources),'runs':len(rows),'diffs':diffs,'timeouts':[{'revision':r['revision'],'mode':r['mode'],'source':r['source']} for r in rows if r['status']=='timeout']},indent=2));print('raw:',out)
