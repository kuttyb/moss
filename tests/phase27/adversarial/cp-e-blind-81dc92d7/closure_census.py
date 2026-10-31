#!/usr/bin/env python3
import concurrent.futures,json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[4]; out=pathlib.Path(__file__).resolve().parent
candidate=root/'build/compiler/moss-opt'; baseline=root.parent/'cp-e-baseline/build/compiler/moss-opt'
corpus=[json.loads(line) for line in (out/'corpus-raw.jsonl').read_text().splitlines()]
sources=[r['source'] for r in corpus if r['normalized']['baseline-off'].get('ok')]
modes=[('baseline-off',baseline,'off'),('baseline-shadow',baseline,'shadow'),('candidate-off',candidate,'off'),('candidate-shadow',candidate,'shadow')]
def one(args):
 source,key,compiler,mode=args; p=root/source; env=dict(os.environ,MOSS_EST=mode,MOSS_PHASE26_C1_SCOPE_TRACE='1')
 try:r=subprocess.run([str(compiler),'check',str(p),'--json'],cwd=root,env=env,text=True,capture_output=True,timeout=15)
 except subprocess.TimeoutExpired:return source,key,None
 if r.returncode:return source,key,{'rejected':r.stdout}
 lines=sorted(x.replace(str(root.parent/'cp-e-baseline'),'<WORKTREE>').replace(str(root),'<WORKTREE>') for x in r.stderr.splitlines() if x.startswith('MOSS_C1_SCOPE_'))
 return source,key,lines
jobs=[(s,k,c,m) for s in sources for k,c,m in modes]; results={}
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
 for source,key,value in ex.map(one,jobs): results.setdefault(source,{})[key]=value
mismatches=[]; accepted=0; missing=[]
for source,per in results.items():
 if all(isinstance(per.get(k),list) for k,_,_ in modes):
  accepted+=1
  sig={k:per[k] for k,_,_ in modes}
  if len({json.dumps(v,sort_keys=True) for v in sig.values()})>1:mismatches.append({'source':source,'modes':sig})
 else: missing.append({'source':source,'modes':per})
(out/'closure-raw.jsonl').write_text(''.join(json.dumps({'source':s,'modes':v},sort_keys=True)+'\n' for s,v in sorted(results.items())))
(out/'closure-summary.json').write_text(json.dumps({'baseline_accepted_sources':len(sources),'accepted_closure_compared':accepted,'rejected_or_timeouts':missing,'mismatches':mismatches},indent=2)+'\n')
print(json.dumps({'baseline_accepted_sources':len(sources),'accepted_closure_compared':accepted,'rejected_or_timeouts':len(missing),'mismatches':len(mismatches)}))
