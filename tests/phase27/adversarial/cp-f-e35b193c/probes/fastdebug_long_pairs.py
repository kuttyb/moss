import hashlib,json,os,pathlib,subprocess,time
root=pathlib.Path.cwd();work=root/'tmp/phase27-3/cp-f-agent2-e35b193c';corpus=work/'corpus'
bins={'phase27-2':root.parent/'cp-f-agent2-baseline-e35b193c/build/compiler/moss-dev','phase27-3':root/'build/compiler/moss-dev'}
tasks=[]
for fixture,trace in (('examples/functional_pipeline.moss',True),('examples/frontend_syntax.moss',False)):
 for rev,mode in (('phase27-2','off'),('phase27-3','shadow')):
  cmd=[str(bins[rev]),'debug',fixture]+(['--trace'] if trace else []);env=os.environ.copy();env['MOSS_EST']=mode
  meta={'revision':rev,'source_sha':'04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4' if rev=='phase27-2' else 'e35b193c4f8ffca14fd820f6c938f53e1c1a0744','compiler_sha256':hashlib.sha256(bins[rev].read_bytes()).hexdigest(),'fixture':fixture,'mode':mode,'command':cmd,'timeout_seconds':600,'start':time.monotonic()}
  meta['proc']=subprocess.Popen(cmd,cwd=corpus,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE);tasks.append(meta)
remaining=set(range(len(tasks)))
while remaining:
 for i in list(remaining):
  t=tasks[i];p=t['proc'];remaining_time=600-(time.monotonic()-t['start'])
  if remaining_time<=0:
   p.kill();so,se=p.communicate();t.update({'status':'timeout','stdout':so,'stderr':se,'elapsed_seconds':round(time.monotonic()-t['start'],3)});remaining.remove(i);continue
  try:
   so,se=p.communicate(timeout=min(1.0,remaining_time));t.update({'status':'completed','returncode':p.returncode,'stdout':so,'stderr':se,'elapsed_seconds':round(time.monotonic()-t['start'],3)});remaining.remove(i)
  except subprocess.TimeoutExpired:pass
 if remaining:time.sleep(0.5)
for t in tasks:
 rev=t['revision'];fixture=pathlib.Path(t['fixture']).stem;stem=('pipeline' if fixture=='functional_pipeline' else 'frontend')
 for key in ('stdout','stderr'):
  s=t.pop(key,'');(work/f'fastdebug_{stem}_{rev}_{key}.log').write_text(s);t[key+'_bytes']=len(s.encode());t[key+'_sha256']=hashlib.sha256(s.encode()).hexdigest()
 t.pop('proc',None);t.pop('start',None)
 (work/f'fastdebug_{stem}_{rev}.json').write_text(json.dumps(t,indent=2,sort_keys=True)+'\n')
 print(stem,rev,t['status'],'elapsed',t['elapsed_seconds'],'stdout',t['stdout_bytes'],'stderr',t['stderr_bytes'],flush=True)
(work/'fastdebug_long_summary.json').write_text(json.dumps(tasks,indent=2,sort_keys=True)+'\n')
