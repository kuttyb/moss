import concurrent.futures,hashlib,json,os,pathlib,subprocess,tarfile,io
root=pathlib.Path.cwd(); work=root/'tmp/phase27-3/cp-f-agent2-e68b2155'; corpus=work/'reach-corpus'; corpus.mkdir(parents=True,exist_ok=True)
archive=subprocess.check_output(['git','archive','e68b21557e675cc4697c391a083ead5add7ad76a'])
with tarfile.open(fileobj=io.BytesIO(archive)) as t:t.extractall(corpus,filter='data')
sources=sorted(x for x in subprocess.check_output(['git','ls-tree','-r','--name-only','e68b21557e675cc4697c391a083ead5add7ad76a'],text=True).splitlines() if x.endswith('.moss'))
bins={'phase27-2':root.parent/'cp-f-agent2-baseline-04be1a1/build/compiler/moss-dev','phase27-3':root/'build/compiler/moss-dev'}
cases=[(rev,mode,src) for rev in bins for mode in ('off','shadow') for src in sources]
def run(case):
 rev,mode,src=case; env=os.environ.copy(); env.update({'MOSS_EST':mode,'MOSS_PHASE26_C1_SCOPE_TRACE':'1'})
 try:p=subprocess.run([str(bins[rev]),'check',src,'--json'],cwd=corpus,env=env,text=True,capture_output=True,timeout=120)
 except subprocess.TimeoutExpired:return {'rev':rev,'mode':mode,'source':src,'timeout':True}
 try:
  d=json.loads(p.stdout); err=d.get('error') or {}; verdict=(d.get('ok'),err.get('code'),err.get('message'),err.get('line'),err.get('column'))
 except Exception:verdict=('NOJSON',p.returncode)
 edges=set(); checked=set(); complete=False; prefix=str(corpus)+'/'
 for line in p.stderr.splitlines():
  if line.startswith('MOSS_C1_SCOPE_TRACE_COMPLETE|'):complete=True
  elif line.startswith('MOSS_C1_SCOPE_EDGE|'):
   _,owner,kind,target=line.split('|',3); edges.add((owner.replace(prefix,''),kind,target))
  elif line.startswith('MOSS_C1_SCOPE_ID|'):
   _,kind,source_file,identity=line.split('|',3); checked.add((kind,source_file.replace(prefix,''),identity))
 return {'rev':rev,'mode':mode,'source':src,'verdict':verdict,'rc':p.returncode,'edges':sorted(edges),'checked':sorted(checked),'trace_complete':complete,'timeout':False}
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=12) as ex:
 for i,row in enumerate(ex.map(run,cases),1):
  rows.append(row)
  if i%500==0: print(f'{i}/{len(cases)}',flush=True)
out=work/'reach_matrix.jsonl';out.write_text(''.join(json.dumps(r,separators=(',',':'),sort_keys=True)+'\n' for r in rows))
ix={(r['rev'],r['mode'],r['source']):r for r in rows}
def delta(a,b):
 return {'verdict':a.get('verdict')!=b.get('verdict'),'edges':a.get('edges')!=b.get('edges'),'checked':a.get('checked')!=b.get('checked'),'trace_complete':a.get('trace_complete')!=b.get('trace_complete'),'timeout':a.get('timeout') or b.get('timeout')}
ctrl=[];cand=[]
for src in sources:
 ctrl.append((src,delta(ix[('phase27-2','off',src)],ix[('phase27-2','shadow',src)])))
 cand.append((src,delta(ix[('phase27-3','off',src)],ix[('phase27-3','shadow',src)])))
def changed(rows):return {src:d for src,d in rows if any(d.values())}
a=changed(ctrl);b=changed(cand)
new=sorted(set(b)-set(a));gone=sorted(set(a)-set(b));both=sorted(set(a)&set(b));same=[x for x in both if a[x]==b[x]];changed_both=[x for x in both if a[x]!=b[x]]
summary={'source_count':len(sources),'control_delta_rows':len(a),'candidate_delta_rows':len(b),'inherited_exact_rows':len(same),'new_rows':new,'removed_rows':gone,'changed_existing_rows':changed_both,'control_categories':{k:sum(d[k] for d in a.values()) for k in ('verdict','edges','checked','trace_complete','timeout')},'candidate_categories':{k:sum(d[k] for d in b.values()) for k in ('verdict','edges','checked','trace_complete','timeout')},'control_failure_rows':[{'source':x,'delta':a[x]} for x in sorted(a)],'candidate_failure_rows':[{'source':x,'delta':b[x]} for x in sorted(b)]}
(work/'reach_summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:v for k,v in summary.items() if k not in ('control_failure_rows','candidate_failure_rows')},indent=2));print('raw:',out)
