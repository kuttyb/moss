#!/usr/bin/env python3
"""Census checked declarations via the semantic symbols API for every root."""
import concurrent.futures,importlib.util,json,os,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]; BASE=ROOT.parent/'cp-e-independent-d28-baseline'
OUT=ROOT/'tests/phase27/adversarial/cp-e-independent-d28/symbol-closure.jsonl'
MODES=(('baseline-off',BASE,'off'),('baseline-shadow',BASE,'shadow'),('candidate-off',ROOT,'off'),('candidate-shadow',ROOT,'shadow'))
def paths(root): return {x.decode() for x in subprocess.check_output(['git','ls-files','-z','*.moss'],cwd=root).split(b'\0') if x}
def modules(root):
 s=importlib.util.spec_from_file_location('sf',root/'sdk/advanced-data-acceptance/check_source_free.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return ':'.join(str(p/'build/debug') for _,p in m.PROVIDERS)
def norm(v,r):
 if isinstance(v,str):return v.replace(str(r),'<ROOT>')
 if isinstance(v,list):return [norm(x,r) for x in v]
 if isinstance(v,dict):return {k:norm(x,r) for k,x in v.items()}
 return v
def run(root,mode,source):
 env=os.environ.copy();env.update(MOSS_EST=mode,MOSS_MODULE_PATH=modules(root))
 try:
  p=subprocess.run([str(root/'build/compiler/moss-dev'),'symbols','--source',source,'--json'],cwd=root,env=env,capture_output=True,text=True,timeout=120)
  try:d=json.loads(p.stdout)
  except json.JSONDecodeError:d=None
  symbols=[] if not d else [norm({k:x.get(k) for k in ('entity_id','kind','qualified_name','source','enclosing_entity')},root) for x in d.get('result',{}).get('symbols',[])]
  return {'rc':p.returncode,'ok':None if not d else d.get('ok'),'symbols':symbols,'error':None if not d else norm(d.get('error'),root),'stderr':norm(p.stderr,root) if not d else None}
 except subprocess.TimeoutExpired:return {'rc':'TIMEOUT','ok':False,'symbols':[],'error':'TIMEOUT'}
def main():
 common=sorted(paths(BASE)&paths(ROOT));jobs=int(os.environ.get('CP_E_JOBS','8'));print('common',len(common),'jobs',jobs,flush=True);start=time.monotonic();rows=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
  futures={(src,n):pool.submit(run,r,m,src) for src in common for n,r,m in MODES}
  for i,src in enumerate(common,1):
   modes={n:futures[src,n].result() for n,_,_ in MODES};base=modes['baseline-off']['symbols']; cand=modes['candidate-shadow']['symbols']
   row={'source':src,'modes':modes,'symbols_equal_all_modes':all(modes[n]['symbols']==base for n,_,_ in MODES),'baseline_only':sorted(set(map(json.dumps,base))-set(map(json.dumps,cand))),'candidate_only':sorted(set(map(json.dumps,cand))-set(map(json.dumps,base)))}
   rows.append(row)
   if i%250==0:print(f'completed={i}/{len(common)} elapsed={time.monotonic()-start:.1f}s',flush=True)
 OUT.write_text(''.join(json.dumps(x,sort_keys=True)+'\n' for x in rows));print(json.dumps({'roots':len(rows),'roots_exact_all_mode':sum(r['symbols_equal_all_modes'] for r in rows),'roots_with_symbol_delta':sum(not r['symbols_equal_all_modes'] for r in rows),'baseline_only_declarations':sum(len(r['baseline_only']) for r in rows),'candidate_only_declarations':sum(len(r['candidate_only']) for r in rows),'seconds':round(time.monotonic()-start,1),'output':str(OUT.relative_to(ROOT))},indent=2))
if __name__=='__main__':main()
