import concurrent.futures, json, os, pathlib, subprocess, sys, tarfile, io
root=pathlib.Path.cwd()
out=root/'tmp/phase27-3/cp-f-agent2-e68b2155/check_matrix.jsonl'
corpus=root/'tmp/phase27-3/cp-f-agent2-e68b2155/corpus'
corpus.mkdir(parents=True,exist_ok=True)
subprocess.run(['git','archive','e68b21557e675cc4697c391a083ead5add7ad76a'],stdout=subprocess.PIPE,check=True)
archive=subprocess.check_output(['git','archive','e68b21557e675cc4697c391a083ead5add7ad76a'])
with tarfile.open(fileobj=io.BytesIO(archive)) as t:t.extractall(corpus,filter='data')
sources=sorted(x for x in subprocess.check_output(['git','ls-tree','-r','--name-only','e68b21557e675cc4697c391a083ead5add7ad76a'],cwd=root,text=True).splitlines() if x.endswith('.moss'))
compilers={'baseline':root.parent/'cp-f-agent2-baseline-04be1a1/build/compiler/moss-dev','candidate':root/'build/compiler/moss-dev'}
cases=[(rev,mode,src) for rev in compilers for mode in ('off','shadow') for src in sources]
def run(case):
 rev,mode,src=case; env=os.environ.copy(); env['MOSS_EST']=mode
 try:
  p=subprocess.run([str(compilers[rev]),'check',src,'--json'],cwd=corpus,env=env,text=True,capture_output=True,timeout=120)
  try: data=json.loads(p.stdout)
  except Exception:return {'rev':rev,'mode':mode,'source':src,'kind':'NOJSON','rc':p.returncode,'stdout':p.stdout[-4000:],'stderr':p.stderr[-4000:]}
  return {'rev':rev,'mode':mode,'source':src,'kind':'ok' if data.get('ok') else 'error','data':data,'rc':p.returncode}
 except subprocess.TimeoutExpired as e:return {'rev':rev,'mode':mode,'source':src,'kind':'TIMEOUT','stdout':(e.stdout or '')[-1000:] if isinstance(e.stdout,str) else '', 'stderr':(e.stderr or '')[-1000:] if isinstance(e.stderr,str) else ''}
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
 for i,row in enumerate(ex.map(run,cases),1):
  rows.append(row)
  if i%500==0: print(f'{i}/{len(cases)}',flush=True)
# store all parsed diagnostic payloads and process status compactly
out.write_text(''.join(json.dumps(r,separators=(',',':'),sort_keys=True)+'\n' for r in rows))
idx={(r['rev'],r['mode'],r['source']):r for r in rows}
def sig(r):
 if r['kind']!='error': return r['kind']
 d=r['data']; err=d.get('error') or {}
 return ('error',err.get('code'),err.get('message'),err.get('line'),err.get('column'))
diffs=[]
for src in sources:
 for mode in ('off','shadow'):
  a=idx[('baseline',mode,src)]; b=idx[('candidate',mode,src)]
  if sig(a)!=sig(b):diffs.append({'source':src,'mode':mode,'baseline':sig(a),'candidate':sig(b)})
summary={}
for rev in compilers:
 for mode in ('off','shadow'):
  vals=[idx[(rev,mode,s)] for s in sources]
  summary[f'{rev}-{mode}']={'sources':len(vals),'accepted':sum(x['kind']=='ok' for x in vals),'rejected':sum(x['kind']=='error' for x in vals),'other':sum(x['kind'] not in ('ok','error') for x in vals)}
print(json.dumps({'summary':summary,'differential_count':len(diffs),'diffs':diffs[:100]},indent=2))
print('raw:',out)
