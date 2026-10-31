import concurrent.futures, pathlib, subprocess, json, os
root=pathlib.Path(__file__).resolve().parents[4]; base=root.parent/'cp-e-base/build/compiler/moss-dev'; cand=root/'build/compiler/moss-dev'
corpus=json.loads((root/'tests/phase27/adversarial/cp-e-c2f51b/corpus-summary.json').read_text())
rows=[json.loads(line) for line in (root/'tests/phase27/adversarial/cp-e-c2f51b/corpus.jsonl').read_text().splitlines()]
accepted=[r['path'] for r in rows if r['baseline_off']['ok'] is True and r['candidate_off']['ok'] is True and r['candidate_shadow']['ok'] is True]
def run(bin,mode,path):
 env=os.environ.copy(); env['MOSS_EST']=mode
 try:
  p=subprocess.run([str(bin),'symbols','--source',str(root/path),'--json'],cwd=root,env=env,text=True,capture_output=True,timeout=60)
  j=json.loads(p.stdout)
  if not j.get('ok'): return {'ok':False,'code':(j.get('error') or {}).get('code')}
  ds=[]
  for s in j.get('result',{}).get('symbols',[]):
   src=(s.get('source') or {}).get('file','')
   if src.startswith(str(root)+'/'):
    ds.append((s.get('entity_id'),s.get('kind'),s.get('qualified_name')))
  return {'ok':True,'decls':sorted(set(ds))}
 except subprocess.TimeoutExpired:return {'ok':False,'code':'TIMEOUT'}
 except Exception as e:return {'ok':False,'code':'NO_JSON:'+str(e)}
def one(path):return path,run(base,'off',path),run(cand,'off',path),run(cand,'shadow',path)
with concurrent.futures.ThreadPoolExecutor(8) as ex: result=list(ex.map(one,accepted))
mis=[{'path':p,'baseline':a,'off':b,'shadow':c} for p,a,b,c in result if a!=b or b!=c]
summary={'accepted_inputs':len(accepted),'query_failures':sum(not x[k]['ok'] for x in result for k in (1,2,3)),'symbol_closure_differences':len(mis),'declaration_rows_baseline':sum(len(x[1].get('decls',[])) for x in result),'declaration_rows_candidate_off':sum(len(x[2].get('decls',[])) for x in result),'declaration_rows_candidate_shadow':sum(len(x[3].get('decls',[])) for x in result)}
out=root/'tests/phase27/adversarial/cp-e-c2f51b/closure.jsonl'; out.write_text(''.join(json.dumps({'path':p,'baseline_off':a,'candidate_off':b,'candidate_shadow':c},sort_keys=True)+'\n' for p,a,b,c in result))
(root/'tests/phase27/adversarial/cp-e-c2f51b/closure-summary.json').write_text(json.dumps(summary|{'mismatches':mis},indent=2)+'\n')
print(json.dumps(summary,indent=2))
