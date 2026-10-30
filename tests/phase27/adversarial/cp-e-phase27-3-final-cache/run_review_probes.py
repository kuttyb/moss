#!/usr/bin/env python3
"""Two-path CP-E probes for frozen Phase 27.3 checker candidate."""
import json, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[4]
HERE=pathlib.Path(__file__).resolve().parent
BASELINE=pathlib.Path(sys.argv[1]).resolve()
CANDIDATE=pathlib.Path(sys.argv[2]).resolve()
SOURCES=['accepted_expression_shapes.moss','accepted_closure.moss','rejected_unreachable.moss','rejected_incomplete.moss']

def run(compiler, source, mode, command='check', target=None):
 env=dict(os.environ,MOSS_EST=mode)
 args=[str(compiler),command]
 if target: args.append(target)
 if command=='check': args.append(str(HERE/source))
 elif command in ('symbols','calls','inspect'):
  args.extend(['--source',str(HERE/source)])
 args.append('--json')
 proc=subprocess.run(args,cwd=ROOT,text=True,capture_output=True,env=env,timeout=30)
 try: response=json.loads(proc.stdout)
 except Exception: raise RuntimeError(f'{args}: return={proc.returncode} stdout={proc.stdout!r} stderr={proc.stderr!r}')
 error=response.get('error') or {}
 result=response.get('result') or {}
 return {'ok':response.get('ok'),'code':error.get('code'),'line':error.get('line'),'message':error.get('message'),'result':result}

rows=[]
for source in SOURCES:
 for mode,compiler in [('baseline-off',BASELINE),('candidate-off',CANDIDATE),('candidate-shadow',CANDIDATE)]:
  actual_mode='off' if mode.endswith('off') else 'shadow'
  item=run(compiler,source,actual_mode)
  row={'source':source,'mode':mode,'check':{k:v for k,v in item.items() if k!='result'}}
  if source=='accepted_closure.moss' and item['ok']:
   symbols=run(compiler,source,actual_mode,'symbols')
   row['symbols']={'ok':symbols['ok'],'declarations':[
    {'id':s['entity_id'],'kind':s['kind'],'name':s['qualified_name']}
    for s in symbols['result'].get('symbols',[]) if s.get('kind') in ('function','main')
   ]}
   for target in ('entity-v1:main','entity-v1:function:unreachable_but_checked'):
    inspected=run(compiler,source,actual_mode,'inspect',target)
    tf=(inspected['result'].get('type_facts') or {})
    row.setdefault('inspect',[]).append({'target':target,'ok':inspected['ok'],'status':inspected['result'].get('status'),'type_origin':tf.get('origin'),'error':inspected.get('code')})
   calls=run(compiler,source,actual_mode,'calls','entity-v1:main')
   row['calls']={'ok':calls['ok'],'transitive':calls['result'].get('transitive_calls',[])}
  rows.append(row)
(HERE/'probe_results.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
for r in rows:
 print(r['source'],r['mode'],r['check']['ok'],r['check']['code'],r['check']['line'])
